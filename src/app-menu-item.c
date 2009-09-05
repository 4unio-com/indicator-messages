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
#include "app-menu-item.h"
#include "dbus-data.h"

enum {
	COUNT_CHANGED,
	NAME_CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

typedef struct _AppMenuItemPrivate AppMenuItemPrivate;

struct _AppMenuItemPrivate
{
	IndicateListener *            listener;
	IndicateListenerServer *      server;
	
	gchar * type;
	GAppInfo * appinfo;
	gchar * desktop;
	guint unreadcount;
	gboolean count_on_label;
};

#define APP_MENU_ITEM_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), APP_MENU_ITEM_TYPE, AppMenuItemPrivate))

/* Prototypes */
static void app_menu_item_class_init (AppMenuItemClass *klass);
static void app_menu_item_init       (AppMenuItem *self);
static void app_menu_item_dispose    (GObject *object);
static void app_menu_item_finalize   (GObject *object);
static void activate_cb (AppMenuItem * self, gpointer data);
static void type_cb (IndicateListener * listener, IndicateListenerServer * server, gchar * value, gpointer data);
static void desktop_cb (IndicateListener * listener, IndicateListenerServer * server, gchar * value, gpointer data);
static void update_label (AppMenuItem * self);



G_DEFINE_TYPE (AppMenuItem, app_menu_item, DBUSMENU_TYPE_MENUITEM);

static void
app_menu_item_class_init (AppMenuItemClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (AppMenuItemPrivate));

	object_class->dispose = app_menu_item_dispose;
	object_class->finalize = app_menu_item_finalize;

	signals[COUNT_CHANGED] = g_signal_new(APP_MENU_ITEM_SIGNAL_COUNT_CHANGED,
	                                      G_TYPE_FROM_CLASS(klass),
	                                      G_SIGNAL_RUN_LAST,
	                                      G_STRUCT_OFFSET (AppMenuItemClass, count_changed),
	                                      NULL, NULL,
	                                      g_cclosure_marshal_VOID__UINT,
	                                      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals[NAME_CHANGED] =  g_signal_new(APP_MENU_ITEM_SIGNAL_NAME_CHANGED,
	                                      G_TYPE_FROM_CLASS(klass),
	                                      G_SIGNAL_RUN_LAST,
	                                      G_STRUCT_OFFSET (AppMenuItemClass, name_changed),
	                                      NULL, NULL,
	                                      g_cclosure_marshal_VOID__STRING,
	                                      G_TYPE_NONE, 1, G_TYPE_STRING);

	return;
}

static void
app_menu_item_init (AppMenuItem *self)
{
	g_debug("Building new IM Menu Item");
	AppMenuItemPrivate * priv = APP_MENU_ITEM_GET_PRIVATE(self);

	priv->listener = NULL;
	priv->server = NULL;
	priv->type = NULL;
	priv->appinfo = NULL;
	priv->desktop = NULL;
	priv->unreadcount = 0;
	priv->count_on_label = FALSE;


	return;
}

static void
app_menu_item_dispose (GObject *object)
{
	AppMenuItem * self = APP_MENU_ITEM(object);
	AppMenuItemPrivate * priv = APP_MENU_ITEM_GET_PRIVATE(self);

	g_object_unref(priv->listener);

	G_OBJECT_CLASS (app_menu_item_parent_class)->dispose (object);
}

static void
app_menu_item_finalize (GObject *object)
{
	AppMenuItem * self = APP_MENU_ITEM(object);
	AppMenuItemPrivate * priv = APP_MENU_ITEM_GET_PRIVATE(self);

	if (priv->type != NULL) {
		g_free(priv->type);
	}

	if (priv->desktop != NULL) {
		g_free(priv->desktop);
	}

	if (priv->appinfo != NULL) {
		g_object_unref(priv->appinfo);
	}

	G_OBJECT_CLASS (app_menu_item_parent_class)->finalize (object);

	return;
}

AppMenuItem *
app_menu_item_new (IndicateListener * listener, IndicateListenerServer * server)
{
	AppMenuItem * self = g_object_new(APP_MENU_ITEM_TYPE, NULL);

	AppMenuItemPrivate * priv = APP_MENU_ITEM_GET_PRIVATE(self);

	priv->listener = listener;
	g_object_ref(G_OBJECT(listener));
	priv->server = server;
	/* Can not ref as not real GObject */

	indicate_listener_server_get_type(listener, server, type_cb, self);
	indicate_listener_server_get_desktop(listener, server, desktop_cb, self);

	g_signal_connect(G_OBJECT(self), DBUSMENU_MENUITEM_SIGNAL_ITEM_ACTIVATED, G_CALLBACK(activate_cb), NULL);

	indicate_listener_server_show_interest(listener, server, INDICATE_INTEREST_SERVER_DISPLAY);
	indicate_listener_server_show_interest(listener, server, INDICATE_INTEREST_SERVER_SIGNAL);
	indicate_listener_set_server_max_indicators(listener, server, MAX_NUMBER_OF_INDICATORS);

	return self;
}

static void 
type_cb (IndicateListener * listener, IndicateListenerServer * server, gchar * value, gpointer data)
{
	AppMenuItem * self = APP_MENU_ITEM(data);
	AppMenuItemPrivate * priv = APP_MENU_ITEM_GET_PRIVATE(self);

	if (priv->type != NULL) {
		g_free(priv->type);
		priv->type = NULL;
	}
	
	if (value == NULL) {
		g_warning("Type value is NULL, that shouldn't really happen");
		return;
	}

	priv->type = g_strdup(value);

	if (!(!g_strcmp0(priv->type, "message.instant") || !g_strcmp0(priv->type, "message.micro") || !g_strcmp0(priv->type, "message.im"))) {
		/* For IM and Microblogging we want the individual items, not a count */
		priv->count_on_label = TRUE;
		update_label(self);

		indicate_listener_server_show_interest(listener, server, INDICATE_INTEREST_INDICATOR_COUNT);
	} else {
		indicate_listener_server_show_interest(listener, server, INDICATE_INTEREST_INDICATOR_DISPLAY);
		indicate_listener_server_show_interest(listener, server, INDICATE_INTEREST_INDICATOR_SIGNAL);
	}

	return;
}

static void
update_label (AppMenuItem * self)
{
	AppMenuItemPrivate * priv = APP_MENU_ITEM_GET_PRIVATE(self);

	if (priv->count_on_label && !priv->unreadcount < 1) {
		/* TRANSLATORS: This is the name of the program and the number of indicators.  So it
		                would read something like "Mail Client (5)" */
		gchar * label = g_strdup_printf(_("%s (%d)"), app_menu_item_get_name(self), priv->unreadcount);
		dbusmenu_menuitem_property_set(DBUSMENU_MENUITEM(self), DBUSMENU_MENUITEM_PROP_LABEL, label);
		g_free(label);
	} else {
		dbusmenu_menuitem_property_set(DBUSMENU_MENUITEM(self), DBUSMENU_MENUITEM_PROP_LABEL, app_menu_item_get_name(self));
	}

	return;
}

static void 
desktop_cb (IndicateListener * listener, IndicateListenerServer * server, gchar * value, gpointer data)
{
	AppMenuItem * self = APP_MENU_ITEM(data);
	AppMenuItemPrivate * priv = APP_MENU_ITEM_GET_PRIVATE(self);

	if (priv->appinfo != NULL) {
		g_object_unref(G_OBJECT(priv->appinfo));
		priv->appinfo = NULL;
	}

	if (priv->desktop != NULL) {
		g_free(priv->desktop);
		priv->desktop = NULL;
	}

	if (value == NULL || value[0] == '\0') {
		return;
	}

	priv->appinfo = G_APP_INFO(g_desktop_app_info_new_from_filename(value));
	g_return_if_fail(priv->appinfo != NULL);

	priv->desktop = g_strdup(value);

	update_label(self);
	g_signal_emit(G_OBJECT(self), signals[NAME_CHANGED], 0, app_menu_item_get_name(self), TRUE);

	return;
}

static void
activate_cb (AppMenuItem * self, gpointer data)
{
	AppMenuItemPrivate * priv = APP_MENU_ITEM_GET_PRIVATE(self);

	indicate_listener_display(priv->listener, priv->server, NULL);

	return;
}

guint
app_menu_item_get_count (AppMenuItem * appitem)
{
	g_return_val_if_fail(IS_APP_MENU_ITEM(appitem), 0);
	AppMenuItemPrivate * priv = APP_MENU_ITEM_GET_PRIVATE(appitem);

	return priv->unreadcount;
}

IndicateListenerServer *
app_menu_item_get_server (AppMenuItem * appitem) {
	g_return_val_if_fail(IS_APP_MENU_ITEM(appitem), NULL);
	AppMenuItemPrivate * priv = APP_MENU_ITEM_GET_PRIVATE(appitem);

	return priv->server;
}

const gchar *
app_menu_item_get_name (AppMenuItem * appitem)
{
	g_return_val_if_fail(IS_APP_MENU_ITEM(appitem), NULL);
	AppMenuItemPrivate * priv = APP_MENU_ITEM_GET_PRIVATE(appitem);

	if (priv->appinfo == NULL) {
		return INDICATE_LISTENER_SERVER_DBUS_NAME(priv->server);
	} else {
		return g_app_info_get_name(priv->appinfo);
	}
}

const gchar *
app_menu_item_get_desktop (AppMenuItem * appitem)
{
	g_return_val_if_fail(IS_APP_MENU_ITEM(appitem), NULL);
	AppMenuItemPrivate * priv = APP_MENU_ITEM_GET_PRIVATE(appitem);
	return priv->desktop;
}
