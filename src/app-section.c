/*
An indicator to show information that is in messaging applications
that the user is using.

Copyright 2009 Canonical Ltd.

Authors:
    Ted Gould <ted@canonical.com>

This program is free software: you can redistribute it and/or modify it 
under the terms of the GNU General Public License version 3, as published 
by the Free Software Foundation.

This program is distributed in the hope that it will be useful, but 
WITHOUT ANY WARRANTY; without even the implied warranties of 
MERCHANTABILITY, SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR 
PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along 
with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib/gi18n.h>
#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>
#include <libindicator/indicator-desktop-shortcuts.h>
#include "app-section.h"
#include "dbus-data.h"
#include "gmenuutils.h"

struct _AppSectionPrivate
{
	GDesktopAppInfo * appinfo;
	guint unreadcount;

	IndicatorDesktopShortcuts * ids;

	GMenu *menu;
	GSimpleActionGroup *static_shortcuts;

	GMenuModel *remote_menu;
	GActionGroup *actions;

	guint name_watch_id;
};

enum {
	PROP_0,
	PROP_APPINFO,
	PROP_ACTIONS,
	NUM_PROPERTIES
};

static GParamSpec *properties[NUM_PROPERTIES];

/* Prototypes */
static void app_section_class_init   (AppSectionClass *klass);
static void app_section_init         (AppSection *self);
static void app_section_get_property (GObject    *object,
				      guint       property_id,
				      GValue     *value,
				      GParamSpec *pspec);
static void app_section_set_property (GObject      *object,
				      guint         property_id,
				      const GValue *value,
				      GParamSpec   *pspec);
static void app_section_dispose      (GObject *object);
static void activate_cb              (GSimpleAction *action,
				      GVariant *param,
				      gpointer userdata);
static void app_section_set_app_info (AppSection *self,
				      GDesktopAppInfo *appinfo);

/* GObject Boilerplate */
G_DEFINE_TYPE (AppSection, app_section, G_TYPE_OBJECT);

static void
app_section_class_init (AppSectionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (AppSectionPrivate));

	object_class->get_property = app_section_get_property;
	object_class->set_property = app_section_set_property;
	object_class->dispose = app_section_dispose;

	properties[PROP_APPINFO] = g_param_spec_object ("app-info",
							"AppInfo",
							"The GAppInfo for the app that this menu represents",
							G_TYPE_APP_INFO,
							G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

	properties[PROP_ACTIONS] = g_param_spec_object ("actions",
							"Actions",
							"The actions exported by this application",
							G_TYPE_ACTION_GROUP,
							G_PARAM_READABLE);

	g_object_class_install_properties (object_class, NUM_PROPERTIES, properties);
}

static void
app_section_init (AppSection *self)
{
	AppSectionPrivate *priv;

	self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
						  APP_SECTION_TYPE,
						  AppSectionPrivate);
	priv = self->priv;

	priv->appinfo = NULL;
	priv->unreadcount = 0;

	priv->menu = g_menu_new ();
	priv->static_shortcuts = g_simple_action_group_new ();

	return;
}

static void
app_section_get_property (GObject    *object,
			  guint       property_id,
			  GValue     *value,
			  GParamSpec *pspec)
{
	AppSection *self = APP_SECTION (object);

	switch (property_id)
	{
	case PROP_APPINFO:
		g_value_set_object (value, app_section_get_app_info (self));
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
app_section_set_property (GObject      *object,
			  guint         property_id,
			  const GValue *value,
			  GParamSpec   *pspec)
{
	AppSection *self = APP_SECTION (object);

	switch (property_id)
	{
	case PROP_APPINFO:
		app_section_set_app_info (self, g_value_get_object (value));
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}
static void
app_section_dispose (GObject *object)
{
	AppSection * self = APP_SECTION(object);
	AppSectionPrivate * priv = self->priv;

	g_clear_object (&priv->menu);
	g_clear_object (&priv->static_shortcuts);

	if (priv->name_watch_id) {
		g_bus_unwatch_name (priv->name_watch_id);
		priv->name_watch_id = 0;
	}

	g_clear_object (&priv->actions);
	g_clear_object (&priv->remote_menu);

	if (priv->ids != NULL) {
		g_object_unref(priv->ids);
		priv->ids = NULL;
	}

	if (priv->appinfo != NULL) {
		g_object_unref(priv->appinfo);
		priv->appinfo = NULL;
	}

	G_OBJECT_CLASS (app_section_parent_class)->dispose (object);
}

/* Respond to one of the shortcuts getting clicked on. */
static void
nick_activate_cb (GSimpleAction *action,
		  GVariant *param,
		  gpointer userdata)
{
	const gchar * nick = g_action_get_name (G_ACTION (action));
	AppSection * mi = APP_SECTION (userdata);
	AppSectionPrivate * priv = mi->priv;

	g_return_if_fail(priv->ids != NULL);

	if (!indicator_desktop_shortcuts_nick_exec(priv->ids, nick)) {
		g_warning("Unable to execute nick '%s' for desktop file '%s'",
			  nick, g_desktop_app_info_get_filename (priv->appinfo));
	}
}

static void
app_section_set_app_info (AppSection *self,
			  GDesktopAppInfo *appinfo)
{
	AppSectionPrivate *priv = self->priv;
	GSimpleAction *launch;

	g_return_if_fail (priv->appinfo == NULL);

	if (appinfo == NULL) {
		g_warning ("appinfo must not be NULL");
		return;
	}

	priv->appinfo = g_object_ref (appinfo);

	launch = g_simple_action_new ("launch", NULL);
	g_signal_connect (launch, "activate", G_CALLBACK (activate_cb), self);
	g_simple_action_group_insert (priv->static_shortcuts, G_ACTION (launch));

	g_menu_append_with_icon (priv->menu,
				 g_app_info_get_name (G_APP_INFO (priv->appinfo)),
				 g_app_info_get_icon (G_APP_INFO (priv->appinfo)),
				 "launch");

	/* Start to build static shortcuts */
	priv->ids = indicator_desktop_shortcuts_new(g_desktop_app_info_get_filename (priv->appinfo), "Messaging Menu");
	const gchar ** nicks = indicator_desktop_shortcuts_get_nicks(priv->ids);
	gint i;
	for (i = 0; nicks[i] != NULL; i++) {
		gchar *name;
		GSimpleAction *action;
		GMenuItem *item;

		name = indicator_desktop_shortcuts_nick_get_name(priv->ids, nicks[i]);

		action = g_simple_action_new (nicks[i], NULL);
		g_signal_connect(action, "activate", G_CALLBACK (nick_activate_cb), self);
		g_simple_action_group_insert (priv->static_shortcuts, G_ACTION (action));

		item = g_menu_item_new (name, nicks[i]);
		g_menu_append_item (priv->menu, item);

		g_object_unref (item);
		g_free(name);
	}

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_APPINFO]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIONS]);

	g_object_unref (launch);
}

AppSection *
app_section_new (GDesktopAppInfo *appinfo)
{
	return g_object_new (APP_SECTION_TYPE,
			     "app-info", appinfo,
			     NULL);
}

static void
activate_cb (GSimpleAction *action,
	     GVariant *param,
	     gpointer userdata)
{
	AppSection * mi = APP_SECTION (userdata);
	AppSectionPrivate * priv = mi->priv;
	GError *error = NULL;

	if (!g_app_info_launch (G_APP_INFO (priv->appinfo), NULL, NULL, &error)) {
		g_warning("Unable to execute application for desktop file '%s'",
			  g_desktop_app_info_get_filename (priv->appinfo));
	}
}

guint
app_section_get_count (AppSection * self)
{
	g_return_val_if_fail(IS_APP_SECTION(self), 0);
	AppSectionPrivate * priv = self->priv;

	return priv->unreadcount;
}

const gchar *
app_section_get_name (AppSection * self)
{
	g_return_val_if_fail(IS_APP_SECTION(self), NULL);
	AppSectionPrivate * priv = self->priv;

	if (priv->appinfo) {
		return g_app_info_get_name(G_APP_INFO(priv->appinfo));
	}
	return NULL;
}

const gchar *
app_section_get_desktop (AppSection * self)
{
	g_return_val_if_fail(IS_APP_SECTION(self), NULL);
	AppSectionPrivate * priv = self->priv;
	if (priv->appinfo)
		return g_desktop_app_info_get_filename (priv->appinfo);
	else
		return NULL;
}

GActionGroup *
app_section_get_actions (AppSection *self)
{
	AppSectionPrivate * priv = self->priv;
	return priv->actions ? priv->actions : G_ACTION_GROUP (priv->static_shortcuts);
}

GMenuModel *
app_section_get_menu (AppSection *self)
{
	AppSectionPrivate * priv = self->priv;
	return G_MENU_MODEL (priv->menu);
}

GAppInfo *
app_section_get_app_info (AppSection *self)
{
	AppSectionPrivate * priv = self->priv;
	return G_APP_INFO (priv->appinfo);
}

static void
application_vanished (GDBusConnection *bus,
		      const gchar *name,
		      gpointer user_data)
{
	AppSection *self = user_data;

	app_section_unset_object_path (self);
}

/*
 * app_section_set_object_path:
 * @self: an #AppSection
 * @bus: a #GDBusConnection
 * @bus_name: the bus name of the application
 * @object_path: the object path on which the app exports its actions and menus
 *
 * Sets the D-Bus object path exported by an instance of the application
 * associated with @self.  Actions and menus exported on that path will be
 * shown in the section.
 */
void
app_section_set_object_path (AppSection *self,
			     GDBusConnection *bus,
			     const gchar *bus_name,
			     const gchar *object_path)
{
	AppSectionPrivate *priv = self->priv;

	g_object_freeze_notify (G_OBJECT (self));
	app_section_unset_object_path (self);

	priv->actions = G_ACTION_GROUP (g_dbus_action_group_get (bus, bus_name, object_path));
	priv->remote_menu = G_MENU_MODEL (g_dbus_menu_model_get (bus, bus_name, object_path));

	g_menu_append_section (priv->menu, NULL, priv->remote_menu);

	priv->name_watch_id = g_bus_watch_name_on_connection (bus, bus_name, 0,
							      NULL, application_vanished,
							      self, NULL);

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIONS]);
	g_object_thaw_notify (G_OBJECT (self));
}

/*
 * app_section_unset_object_path:
 * @self: an #AppSection
 *
 * Unsets the object path set with app_section_set_object_path().  The section
 * will return to only showing application name and static shortcuts in the
 * menu.
 */
void
app_section_unset_object_path (AppSection *self)
{
	AppSectionPrivate *priv = self->priv;

	if (priv->name_watch_id) {
		g_bus_unwatch_name (priv->name_watch_id);
		priv->name_watch_id = 0;
	}
	g_clear_object (&priv->actions);

	if (priv->remote_menu) {
		/* the last menu item points is linked to the app's menumodel */
		gint n_items = g_menu_model_get_n_items (G_MENU_MODEL (priv->menu));
		g_menu_remove (priv->menu, n_items -1);
		g_clear_object (&priv->remote_menu);
	}

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIONS]);
}

