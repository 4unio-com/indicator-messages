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
#include <gtk/gtk.h>
#include <gio/gdesktopappinfo.h>
#include "app-menu-item.h"

enum {
	COUNT_CHANGED,
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
	guint unreadcount;

	GtkWidget * name;
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
static void indicator_added_cb (IndicateListener * listener, IndicateListenerServer * server, IndicateListenerIndicator * indicator, gchar * type, gpointer data);
static void indicator_removed_cb (IndicateListener * listener, IndicateListenerServer * server, IndicateListenerIndicator * indicator, gchar * type, gpointer data);



G_DEFINE_TYPE (AppMenuItem, app_menu_item, GTK_TYPE_MENU_ITEM);

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

	return;
}

static void
app_menu_item_init (AppMenuItem *self)
{
	g_debug("Building new IM Menu Item");
	AppMenuItemPrivate * priv = APP_MENU_ITEM_GET_PRIVATE(self);

	priv->listener = NULL;
	priv->server = NULL;
	priv->name = NULL;
	priv->type = NULL;
	priv->appinfo = NULL;
	priv->unreadcount = 0;


	return;
}

static void
app_menu_item_dispose (GObject *object)
{
	G_OBJECT_CLASS (app_menu_item_parent_class)->dispose (object);
}

static void
app_menu_item_finalize (GObject *object)
{
	G_OBJECT_CLASS (app_menu_item_parent_class)->finalize (object);
}

AppMenuItem *
app_menu_item_new (IndicateListener * listener, IndicateListenerServer * server)
{
	AppMenuItem * self = g_object_new(APP_MENU_ITEM_TYPE, NULL);

	AppMenuItemPrivate * priv = APP_MENU_ITEM_GET_PRIVATE(self);

	priv->listener = listener;
	priv->server = server;

	g_signal_connect(G_OBJECT(listener), INDICATE_LISTENER_SIGNAL_INDICATOR_ADDED, G_CALLBACK(indicator_added_cb), self);
	g_signal_connect(G_OBJECT(listener), INDICATE_LISTENER_SIGNAL_INDICATOR_REMOVED, G_CALLBACK(indicator_added_cb), self);

	priv->name = gtk_label_new(INDICATE_LISTENER_SERVER_DBUS_NAME(server));
	gtk_misc_set_alignment(GTK_MISC(priv->name), 0.0, 0.5);
	gtk_widget_show(GTK_WIDGET(priv->name));

	gtk_container_add(GTK_CONTAINER(self), GTK_WIDGET(priv->name));

	indicate_listener_server_get_type(listener, server, type_cb, self);
	indicate_listener_server_get_desktop(listener, server, desktop_cb, self);

	g_signal_connect(G_OBJECT(self), "activate", G_CALLBACK(activate_cb), NULL);

	return self;
}

static void 
type_cb (IndicateListener * listener, IndicateListenerServer * server, gchar * value, gpointer data)
{
	AppMenuItem * self = APP_MENU_ITEM(data);
	AppMenuItemPrivate * priv = APP_MENU_ITEM_GET_PRIVATE(self);

	if (priv->type != NULL) {
		g_free(priv->type);
	}
	
	priv->type = g_strdup(value);

	return;
}

static void 
desktop_cb (IndicateListener * listener, IndicateListenerServer * server, gchar * value, gpointer data)
{
	AppMenuItem * self = APP_MENU_ITEM(data);
	AppMenuItemPrivate * priv = APP_MENU_ITEM_GET_PRIVATE(self);

	if (priv->appinfo != NULL) {
		g_object_unref(G_OBJECT(priv->appinfo));
	}
	
	priv->appinfo = G_APP_INFO(g_desktop_app_info_new_from_filename(value));
	g_return_if_fail(priv->appinfo != NULL);

	gtk_label_set_text(GTK_LABEL(priv->name), g_app_info_get_name(priv->appinfo));

	return;
}

static void
activate_cb (AppMenuItem * self, gpointer data)
{
	AppMenuItemPrivate * priv = APP_MENU_ITEM_GET_PRIVATE(self);

	indicate_listener_display(priv->listener, priv->server, NULL);

	return;
}

static void 
indicator_added_cb (IndicateListener * listener, IndicateListenerServer * server, IndicateListenerIndicator * indicator, gchar * type, gpointer data)
{
	AppMenuItemPrivate * priv = APP_MENU_ITEM_GET_PRIVATE(data);

	if (g_strcmp0(INDICATE_LISTENER_SERVER_DBUS_NAME(server), INDICATE_LISTENER_SERVER_DBUS_NAME(priv->server))) {
		/* Not us */
		return;
	}

	priv->unreadcount++;

	g_signal_emit(G_OBJECT(data), signals[COUNT_CHANGED], 0, TRUE);

	return;
}

static void
indicator_removed_cb (IndicateListener * listener, IndicateListenerServer * server, IndicateListenerIndicator * indicator, gchar * type, gpointer data)
{
	AppMenuItemPrivate * priv = APP_MENU_ITEM_GET_PRIVATE(data);

	if (g_strcmp0(INDICATE_LISTENER_SERVER_DBUS_NAME(server), INDICATE_LISTENER_SERVER_DBUS_NAME(priv->server))) {
		/* Not us */
		return;
	}

	priv->unreadcount--;

	g_signal_emit(G_OBJECT(data), signals[COUNT_CHANGED], 0, TRUE);

	return;
}

guint
app_menu_item_get_count (AppMenuItem * appitem)
{
	AppMenuItemPrivate * priv = APP_MENU_ITEM_GET_PRIVATE(appitem);

	return priv->unreadcount;
}

