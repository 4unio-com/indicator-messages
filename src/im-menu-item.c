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
#include <libindicate-gtk/indicator.h>
#include <libindicate-gtk/listener.h>
#include "im-menu-item.h"

enum {
	TIME_CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

typedef struct _ImMenuItemPrivate ImMenuItemPrivate;

struct _ImMenuItemPrivate
{
	IndicateListener *           listener;
	IndicateListenerServer *      server;
	IndicateListenerIndicator *  indicator;

	glong seconds;
	gboolean show_time;
	gulong indicator_changed;

	guint time_update_min;
};

#define IM_MENU_ITEM_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), IM_MENU_ITEM_TYPE, ImMenuItemPrivate))

/* Prototypes */
static void im_menu_item_class_init (ImMenuItemClass *klass);
static void im_menu_item_init       (ImMenuItem *self);
static void im_menu_item_dispose    (GObject *object);
static void im_menu_item_finalize   (GObject *object);
static void sender_cb               (IndicateListener * listener,
                                     IndicateListenerServer * server,
                                     IndicateListenerIndicator * indicator,
                                     gchar * property,
                                     gchar * propertydata,
                                     gpointer data);
static void time_cb                 (IndicateListener * listener,
                                     IndicateListenerServer * server,
                                     IndicateListenerIndicator * indicator,
                                     gchar * property,
                                     GTimeVal * propertydata,
                                     gpointer data);
static void icon_cb                 (IndicateListener * listener,
                                     IndicateListenerServer * server,
                                     IndicateListenerIndicator * indicator,
                                     gchar * property,
                                     GdkPixbuf * propertydata,
                                     gpointer data);
static void activate_cb             (ImMenuItem * self,
                                     gpointer data);
static void indicator_modified_cb   (IndicateListener * listener,
                                     IndicateListenerServer * server,
                                     IndicateListenerIndicator * indicator,
                                     gchar * type,
                                     gchar * property,
                                     ImMenuItem * self);

G_DEFINE_TYPE (ImMenuItem, im_menu_item, DBUSMENU_TYPE_MENUITEM);

static void
im_menu_item_class_init (ImMenuItemClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (ImMenuItemPrivate));

	object_class->dispose = im_menu_item_dispose;
	object_class->finalize = im_menu_item_finalize;

	signals[TIME_CHANGED] =  g_signal_new(IM_MENU_ITEM_SIGNAL_TIME_CHANGED,
	                                      G_TYPE_FROM_CLASS(klass),
	                                      G_SIGNAL_RUN_LAST,
	                                      G_STRUCT_OFFSET (ImMenuItemClass, time_changed),
	                                      NULL, NULL,
	                                      g_cclosure_marshal_VOID__LONG,
	                                      G_TYPE_NONE, 1, G_TYPE_LONG);

	return;
}

static void
im_menu_item_init (ImMenuItem *self)
{
	g_debug("Building new IM Menu Item");
	ImMenuItemPrivate * priv = IM_MENU_ITEM_GET_PRIVATE(self);

	priv->listener = NULL;
	priv->server = NULL;
	priv->indicator = NULL;

	priv->seconds = 0;

	return;
}

static void
im_menu_item_dispose (GObject *object)
{
	G_OBJECT_CLASS (im_menu_item_parent_class)->dispose (object);

	ImMenuItem * self = IM_MENU_ITEM(object);
	ImMenuItemPrivate * priv = IM_MENU_ITEM_GET_PRIVATE(self);

	if (priv->time_update_min != 0) {
		g_source_remove(priv->time_update_min);
	}

	g_signal_handler_disconnect(priv->listener, priv->indicator_changed);
	priv->indicator_changed = 0;

	return;
}

static void
im_menu_item_finalize (GObject *object)
{
	G_OBJECT_CLASS (im_menu_item_parent_class)->finalize (object);
}

static void
icon_cb (IndicateListener * listener, IndicateListenerServer * server, IndicateListenerIndicator * indicator, gchar * property, GdkPixbuf * propertydata, gpointer data)
{
	/* dbusmenu_menuitem_property_set(DBUSMENU_MENUITEM(self), "icon", propertydata); */

	return;
}

static void
update_time (ImMenuItem * self)
{
	ImMenuItemPrivate * priv = IM_MENU_ITEM_GET_PRIVATE(self);

	if (!priv->show_time) {
		dbusmenu_menuitem_property_set(DBUSMENU_MENUITEM(self), "right-column", "");
		return;
	}
	
	gchar * timestring = NULL;

	GTimeVal current_time;
	g_get_current_time(&current_time);

	guint elapsed_seconds = current_time.tv_sec - priv->seconds;
	guint elapsed_minutes = elapsed_seconds / 60;

	if (elapsed_seconds % 60 > 55) {
		/* We're using fuzzy timers, so we need fuzzy comparisons */
		elapsed_minutes += 1;
	}

	if (elapsed_minutes < 60) {
		timestring = g_strdup_printf(ngettext("%d m", "%d m", elapsed_minutes), elapsed_minutes);
	} else {
		guint elapsed_hours = elapsed_minutes / 60;

		if (elapsed_minutes % 60 > 55) {
			/* We're using fuzzy timers, so we need fuzzy comparisons */
			elapsed_hours += 1;
		}

		timestring = g_strdup_printf(ngettext("%d h", "%d h", elapsed_hours), elapsed_hours);
	}

	if (timestring != NULL) {
		dbusmenu_menuitem_property_set(DBUSMENU_MENUITEM(self), "right-column", "");
		g_free(timestring);
	}

	return;
}

static gboolean
time_update_cb (gpointer data)
{
	ImMenuItem * self = IM_MENU_ITEM(data);

	update_time(self);

	return TRUE;
}

static void
time_cb (IndicateListener * listener, IndicateListenerServer * server, IndicateListenerIndicator * indicator, gchar * property, GTimeVal * propertydata, gpointer data)
{
	g_debug("Got Time info");
	ImMenuItem * self = IM_MENU_ITEM(data);
	if (self == NULL) {
		g_error("Menu Item callback called without a menu item");
		return;
	}

	if (property == NULL || g_strcmp0(property, "time")) {
		g_warning("Time callback called without being sent the time.");
		return;
	}

	ImMenuItemPrivate * priv = IM_MENU_ITEM_GET_PRIVATE(self);

	priv->seconds = propertydata->tv_sec;

	update_time(self);

	if (priv->time_update_min == 0) {
		priv->time_update_min = g_timeout_add_seconds(60, time_update_cb, self);
	}

	g_signal_emit(G_OBJECT(self), signals[TIME_CHANGED], 0, priv->seconds, TRUE);

	return;
}

static void
sender_cb (IndicateListener * listener, IndicateListenerServer * server, IndicateListenerIndicator * indicator, gchar * property, gchar * propertydata, gpointer data)
{
	g_debug("Got Sender Information");
	ImMenuItem * self = IM_MENU_ITEM(data);
	if (self == NULL) {
		g_error("Menu Item callback called without a menu item");
		return;
	}

	if (property == NULL || g_strcmp0(property, "sender")) {
		g_warning("Sender callback called without being sent the sender.  We got '%s' with value '%s'.", property, propertydata);
		return;
	}

	dbusmenu_menuitem_property_set(DBUSMENU_MENUITEM(self), DBUSMENU_MENUITEM_PROP_LABEL, propertydata);

	return;
}

static void
activate_cb (ImMenuItem * self, gpointer data)
{
	ImMenuItemPrivate * priv = IM_MENU_ITEM_GET_PRIVATE(self);

	indicate_listener_display(priv->listener, priv->server, priv->indicator);
}

void
indicator_modified_cb (IndicateListener * listener, IndicateListenerServer * server, IndicateListenerIndicator * indicator, gchar * type, gchar * property, ImMenuItem * self)
{
	ImMenuItemPrivate * priv = IM_MENU_ITEM_GET_PRIVATE(self);

	/* Not meant for us */
	if (INDICATE_LISTENER_INDICATOR_ID(indicator) != INDICATE_LISTENER_INDICATOR_ID(priv->indicator)) return;
	if (server != priv->server) return;

	if (!g_strcmp0(property, "sender")) {
		indicate_listener_get_property(listener, server, indicator, "sender", sender_cb, self);	
	} else if (!g_strcmp0(property, "time")) {
		indicate_listener_get_property_time(listener, server, indicator, "time",   time_cb, self);	
	} else if (!g_strcmp0(property, "icon")) {
		indicate_listener_get_property_icon(listener, server, indicator, "icon",   icon_cb, self);	
	}
	
	return;
}

ImMenuItem *
im_menu_item_new (IndicateListener * listener, IndicateListenerServer * server, IndicateListenerIndicator * indicator, gboolean show_time)
{
	ImMenuItem * self = g_object_new(IM_MENU_ITEM_TYPE, NULL);

	ImMenuItemPrivate * priv = IM_MENU_ITEM_GET_PRIVATE(self);

	priv->listener = listener;
	priv->server = server;
	priv->indicator = indicator;
	priv->show_time = show_time;
	priv->time_update_min = 0;

	indicate_listener_get_property(listener, server, indicator, "sender", sender_cb, self);	
	indicate_listener_get_property_time(listener, server, indicator, "time",   time_cb, self);	
	indicate_listener_get_property_icon(listener, server, indicator, "icon",   icon_cb, self);	

	g_signal_connect(G_OBJECT(self), DBUSMENU_MENUITEM_SIGNAL_ITEM_ACTIVATED, G_CALLBACK(activate_cb), NULL);
	priv->indicator_changed = g_signal_connect(G_OBJECT(listener), INDICATE_LISTENER_SIGNAL_INDICATOR_MODIFIED, G_CALLBACK(indicator_modified_cb), self);

	return self;
}

glong
im_menu_item_get_seconds (ImMenuItem * menuitem)
{
	ImMenuItemPrivate * priv = IM_MENU_ITEM_GET_PRIVATE(menuitem);
	return priv->seconds;
}
