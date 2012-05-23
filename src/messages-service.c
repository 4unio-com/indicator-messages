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

#include <string.h>
#include <locale.h>
#include <libintl.h>
#include <config.h>
#include <pango/pango-utils.h>
#include <libindicate/listener.h>
#include <libindicator/indicator-service.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <glib/gi18n.h>

#include <libdbusmenu-glib/client.h>
#include <libdbusmenu-glib/server.h>

#include "im-menu-item.h"
#include "app-menu-item.h"
#include "dbus-data.h"
#include "messages-service-dbus.h"
#include "status-items.h"

static IndicatorService * service = NULL;
static IndicateListener * listener = NULL;
static GList * serverList = NULL;

static DbusmenuMenuitem * root_menuitem = NULL;
static DbusmenuMenuitem * status_separator = NULL;
static DbusmenuMenuitem * clear_attention = NULL;
static GSettings *settings;
static GMainLoop * mainloop = NULL;

static MessageServiceDbus * dbus_interface = NULL;

#define DESKTOP_FILE_GROUP        "Messaging Menu"
#define DESKTOP_FILE_KEY_DESKTOP  "DesktopFile"

static void server_shortcut_added (AppMenuItem * appitem, DbusmenuMenuitem * mi, gpointer data);
static void server_shortcut_removed (AppMenuItem * appitem, DbusmenuMenuitem * mi, gpointer data);
static void server_count_changed (AppMenuItem * appitem, guint count, gpointer data);
static void server_name_changed (AppMenuItem * appitem, gchar * name, gpointer data);
static void im_time_changed (ImMenuItem * imitem, glong seconds, gpointer data);
static void resort_menu (DbusmenuMenuitem * menushell);
static void indicator_removed (IndicateListener * listener, IndicateListenerServer * server, IndicateListenerIndicator * indicator, gpointer data);
static gboolean build_launcher (gpointer data);
static gboolean build_launchers (gpointer data);


/*
 * Server List
 */

typedef struct _serverList_t serverList_t;
struct _serverList_t {
	IndicateListenerServer * server;
	AppMenuItem * menuitem;
	DbusmenuMenuitem * separator;
	gboolean attention;
	guint count;
	GList * imList;
};

static gint
serverList_equal (gconstpointer a, gconstpointer b)
{
	serverList_t * pa, * pb;

	pa = (serverList_t *)a;
	pb = (serverList_t *)b;

	const gchar * pan = INDICATE_LISTENER_SERVER_DBUS_NAME(pa->server);
	const gchar * pbn = INDICATE_LISTENER_SERVER_DBUS_NAME(pb->server);
	const gchar * pap = indicate_listener_server_get_dbuspath(pa->server);
	const gchar * pbp = indicate_listener_server_get_dbuspath(pb->server);

	if (g_strcmp0(pan, pbn) == 0)
	  return g_strcmp0(pap, pbp);
	else
	  return 1;
}

static gint
serverList_sort (gconstpointer a, gconstpointer b)
{
	serverList_t * pa, * pb;

	pa = (serverList_t *)a;
	pb = (serverList_t *)b;

	const gchar * pan = app_menu_item_get_name(pa->menuitem);
	const gchar * pbn = app_menu_item_get_name(pb->menuitem);

	return g_strcmp0(pan, pbn);
}

/*
 * Item List
 */

typedef struct _imList_t imList_t;
struct _imList_t {
	IndicateListenerServer * server;
	IndicateListenerIndicator * indicator;
	DbusmenuMenuitem * menuitem;
	gulong timechange_cb;
	gulong attentionchange_cb;
};

static gboolean
imList_equal (gconstpointer a, gconstpointer b)
{
	imList_t * pa, * pb;

	pa = (imList_t *)a;
	pb = (imList_t *)b;

	const gchar * pas = INDICATE_LISTENER_SERVER_DBUS_NAME(pa->server);
	const gchar * pbs = INDICATE_LISTENER_SERVER_DBUS_NAME(pb->server);

	guint pai = INDICATE_LISTENER_INDICATOR_ID(pa->indicator);
	guint pbi = INDICATE_LISTENER_INDICATOR_ID(pb->indicator);

	g_debug("\tComparing (%s %d) to (%s %d)", pas, pai, pbs, pbi);

	return !((!g_strcmp0(pas, pbs)) && (pai == pbi));
}

static gint
imList_sort (gconstpointer a, gconstpointer b)
{
	imList_t * pa, * pb;

	pa = (imList_t *)a;
	pb = (imList_t *)b;

	return (gint)(im_menu_item_get_seconds(IM_MENU_ITEM(pb->menuitem)) - im_menu_item_get_seconds(IM_MENU_ITEM(pa->menuitem)));
}


/* Goes through all the servers and sees if any of them
   want attention.  If they do, then well we'll give it
   to them.  If they don't, let's not bother the user
   any, shall we? */
static void
check_attention (void)
{
	GList * pointer;
	for (pointer = serverList; pointer != NULL; pointer = g_list_next(pointer)) {
		serverList_t * slt = (serverList_t *)pointer->data;
		if (slt->attention) {
			message_service_dbus_set_attention(dbus_interface, TRUE);
			return;
		}
	}
	message_service_dbus_set_attention(dbus_interface, FALSE);
	return;
}

/* This checks a server listing to see if it should
   have attention.  It can get attention through it's
   count or by having an indicator that is requestion
   attention. */
static void
server_attention (serverList_t * slt)
{
	/* Count, easy yes and out. */
	if (slt->count > 0) {
		slt->attention = TRUE;
		return;
	}

	/* Check to see if any of the indicators want attention */
	GList * pointer;
	for (pointer = slt->imList; pointer != NULL; pointer = g_list_next(pointer)) {
		imList_t * ilt = (imList_t *)pointer->data;
		if (im_menu_item_get_attention(IM_MENU_ITEM(ilt->menuitem))) {
			slt->attention = TRUE;
			return;
		}
	}

	/* Nope, no one */
	slt->attention = FALSE;
	return;
}

static void 
desktop_cb (IndicateListener *listener,
	    IndicateListenerServer *server,
	    const gchar *value,
	    gpointer data)
{
	DbusmenuMenuitem * menushell = DBUSMENU_MENUITEM(data);
	GList *listitem;
	serverList_t * sl_item = NULL;

	/* Check to see if we already have a launcher for this app */
	for (listitem = serverList; listitem != NULL; listitem = listitem->next) {
		serverList_t * slt = listitem->data;
		if (!g_strcmp0(app_menu_item_get_desktop(slt->menuitem), value)) {
			sl_item = slt;
			break;
		}
	}

	if (!sl_item) {
		/* Build the Menu item */
		AppMenuItem * menuitem = app_menu_item_new_with_server (listener, server);

		/* Build a possible server structure */
		sl_item = g_new0(serverList_t, 1);
		sl_item->server = server;
		sl_item->menuitem = menuitem;
		sl_item->imList = NULL;
		sl_item->attention = FALSE;
		sl_item->count = 0;

		/* Build a separator */
		sl_item->separator = dbusmenu_menuitem_new();
		dbusmenu_menuitem_property_set(sl_item->separator, DBUSMENU_MENUITEM_PROP_TYPE, DBUSMENU_CLIENT_TYPES_SEPARATOR);

		/* Connect the signals up to the menu item */
		g_signal_connect(G_OBJECT(menuitem), APP_MENU_ITEM_SIGNAL_COUNT_CHANGED, G_CALLBACK(server_count_changed), sl_item);
		g_signal_connect(G_OBJECT(menuitem), APP_MENU_ITEM_SIGNAL_NAME_CHANGED,  G_CALLBACK(server_name_changed),  menushell);
		g_signal_connect(G_OBJECT(menuitem), APP_MENU_ITEM_SIGNAL_SHORTCUT_ADDED,  G_CALLBACK(server_shortcut_added),  menushell);
		g_signal_connect(G_OBJECT(menuitem), APP_MENU_ITEM_SIGNAL_SHORTCUT_REMOVED,  G_CALLBACK(server_shortcut_removed),  menushell);

		/* Put our new menu item in, with the separator behind it.
		   resort_menu will take care of whether it should be hidden
		   or not. */
		dbusmenu_menuitem_child_append(menushell, DBUSMENU_MENUITEM(menuitem));

		/* Incase we got an indicator first */
		GList * alreadythere = g_list_find_custom(serverList, sl_item, serverList_equal);
		if (alreadythere != NULL) {
			/* Use the one we already had */
			g_free(sl_item);
			sl_item = (serverList_t *)alreadythere->data;
			sl_item->menuitem = menuitem;
			serverList = g_list_sort(serverList, serverList_sort);
		} else {
			/* Insert the new one in the list */
			serverList = g_list_insert_sorted(serverList, sl_item, serverList_sort);
		}

		dbusmenu_menuitem_child_append(menushell, DBUSMENU_MENUITEM(sl_item->separator));
	}
	else {
		app_menu_item_set_server (sl_item->menuitem, listener, server);
	}

	GList * shortcuts = app_menu_item_get_items(sl_item->menuitem);
	GList * shortcut = shortcuts;
	while (shortcut != NULL) {
		DbusmenuMenuitem * mi = DBUSMENU_MENUITEM(shortcut->data);
		g_debug("\tAdding shortcut: %s", dbusmenu_menuitem_property_get(mi, DBUSMENU_MENUITEM_PROP_LABEL));
		dbusmenu_menuitem_child_append(menushell, mi);
		shortcut = g_list_next(shortcut);
	}
	g_list_free (shortcuts);

	resort_menu(menushell);
}

/* A new server has been created on the indicate bus.
   We need to check to see if we like it.  And build
   structures for it if so. */
static void 
server_added (IndicateListener * listener, IndicateListenerServer * server, gchar * type, gpointer data)
{
	g_debug("Server Added '%s' of type '%s'.", INDICATE_LISTENER_SERVER_DBUS_NAME(server), type);
	if (type == NULL) {
		return;
	}

	if (type[0] == '\0') {
		return;
	}

	if (strncmp(type, "message", strlen("message"))) {
		g_debug("\tServer type '%s' is not a message based type.", type);
		return;
	}

	/* fetch the desktop file before creating the menu item, in case we
	 * already have a launcher for it */
	indicate_listener_server_get_desktop(listener, server, desktop_cb, data);
}

/* Server shortcut has been added */
static void
server_shortcut_added (AppMenuItem * appitem, DbusmenuMenuitem * mi, gpointer data)
{
	g_debug("Application Shortcut added: %s", mi != NULL ? dbusmenu_menuitem_property_get(mi, DBUSMENU_MENUITEM_PROP_LABEL) : "none");
	DbusmenuMenuitem * shell = DBUSMENU_MENUITEM(data);
	if (mi != NULL) {
		dbusmenu_menuitem_property_set (mi, DBUSMENU_MENUITEM_PROP_ICON_NAME, DBUSMENU_MENUITEM_ICON_NAME_BLANK);
		dbusmenu_menuitem_child_append(shell, mi);
	}
	resort_menu(shell);
	return;
}

/* Server shortcut has been removed */
static void
server_shortcut_removed (AppMenuItem * appitem, DbusmenuMenuitem * mi, gpointer data)
{
	g_debug("Application Shortcut removed: %s", mi != NULL ? dbusmenu_menuitem_property_get(mi, DBUSMENU_MENUITEM_PROP_LABEL) : "none");
	DbusmenuMenuitem * shell = DBUSMENU_MENUITEM(data);
	dbusmenu_menuitem_child_delete(shell, mi);
	return;
}

/* The name of a server has changed, we probably
   need to reorder the menu to keep it in alphabetical
   order.  This happens often after we read the destkop
   file from disk. */
static void
server_name_changed (AppMenuItem * appitem, gchar * name, gpointer data)
{
	serverList = g_list_sort(serverList, serverList_sort);
	resort_menu(DBUSMENU_MENUITEM(data));
	return;
}

/* If the count on the server changes, we need to know
   whether that should be grabbing attention or not.  If
   it is, we need to reevaluate whether the whole thing
   should be grabbing attention or not. */
static void
server_count_changed (AppMenuItem * appitem, guint count, gpointer data)
{
	serverList_t * slt = (serverList_t *)data;
	slt->count = count;

	if (count == 0 && slt->attention) {
		/* Regen based on indicators if the count isn't going to cause it. */
		server_attention(slt);
		/* If we're dropping let's see if we're the last. */
		if (!slt->attention) {
			check_attention();
		}
	}

	if (count != 0 && !slt->attention) {
		slt->attention = TRUE;
		/* Let's tell everyone about us! */
		message_service_dbus_set_attention(dbus_interface, TRUE);
	}

	return;
}

/* Respond to the IM entrie's time changing
   which results in it needing to resort the list
   and rebuild the menu to match. */
static void
im_time_changed (ImMenuItem * imitem, glong seconds, gpointer data)
{
	serverList_t * sl = (serverList_t *)data;
	sl->imList = g_list_sort(sl->imList, imList_sort);
	resort_menu(root_menuitem);
	return;
}

/* The IM entrie's request for attention has changed
   so we need to pass that up the stack. */
static void
im_attention_changed (ImMenuItem * imitem, gboolean requestit, gpointer data)
{
	serverList_t * sl = (serverList_t *)data;

	if (requestit) {
		sl->attention = TRUE;
		message_service_dbus_set_attention(dbus_interface, TRUE);
	} else {
		server_attention(sl);
		if (!sl->attention) {
			check_attention();
		}
	}

	return;
}

/* Run when a server is removed from the indicator bus.  It figures
   out if we have it somewhere, and if so then we dump it out and
   clean up all of it's entries. */
static void 
server_removed (IndicateListener * listener, IndicateListenerServer * server, gchar * type, gpointer data)
{
	/* Look for the server */
	g_debug("Removing server: %s", INDICATE_LISTENER_SERVER_DBUS_NAME(server));
	serverList_t slt = {0};
	slt.server = server;
	GList * lookup = g_list_find_custom(serverList, &slt, serverList_equal);

	/* If we don't have it, exit */
	if (lookup == NULL) {
		g_debug("\tUnable to find server: %s", INDICATE_LISTENER_SERVER_DBUS_NAME(server));
		return;
	}

	serverList_t * sltp = (serverList_t *)lookup->data;

	/* Removing indicators from this server */
	while (sltp->imList) {
		imList_t * imitem = (imList_t *)sltp->imList->data;
		indicator_removed(listener, server, imitem->indicator, data);
	}

	/* Remove from the server list */
	serverList = g_list_remove(serverList, sltp);

	/* If there is a menu item, let's get rid of it. */
	if (sltp->menuitem != NULL) {
		/* If there are shortcuts remove them */
		GList * shortcuts = app_menu_item_get_items(sltp->menuitem);
		GList * shortcut = shortcuts;
		while (shortcut != NULL) {
			g_debug("\tRemoving shortcut: %s", dbusmenu_menuitem_property_get(DBUSMENU_MENUITEM(shortcut->data), DBUSMENU_MENUITEM_PROP_LABEL));
			dbusmenu_menuitem_property_set_bool(DBUSMENU_MENUITEM(shortcut->data), DBUSMENU_MENUITEM_PROP_VISIBLE, FALSE);
			dbusmenu_menuitem_child_delete(DBUSMENU_MENUITEM(data), DBUSMENU_MENUITEM(shortcut->data));
			shortcut = g_list_next(shortcut);
		}
		g_list_free (shortcuts);

		g_debug("\tRemoving item");
		dbusmenu_menuitem_property_set_bool(DBUSMENU_MENUITEM(sltp->menuitem), DBUSMENU_MENUITEM_PROP_VISIBLE, FALSE);
		dbusmenu_menuitem_child_delete(DBUSMENU_MENUITEM(data), DBUSMENU_MENUITEM(sltp->menuitem));
		g_object_unref(G_OBJECT(sltp->menuitem));
	} else {
		g_debug("\tNo menuitem");
	}
	
	/* If there is a separator, let's get rid of it. */
	if (sltp->separator != NULL) {
		g_debug("\tRemoving separator");
		dbusmenu_menuitem_property_set_bool(DBUSMENU_MENUITEM(sltp->separator), DBUSMENU_MENUITEM_PROP_VISIBLE, FALSE);
		dbusmenu_menuitem_child_delete(DBUSMENU_MENUITEM(data), DBUSMENU_MENUITEM(sltp->separator));
		g_object_unref(G_OBJECT(sltp->separator));
	} else {
		g_debug("\tNo separator");
	}

	if (sltp->attention) {
		/* Check to see if this was the server causing the menu item to
		   be lit up. */
		check_attention();
	}

	g_free(sltp);

	return;
}

typedef struct _menushell_location menushell_location_t;
struct _menushell_location {
	const IndicateListenerServer * server;
	gint position;
	gboolean found;
};

static void
menushell_foreach_cb (DbusmenuMenuitem * data_mi, gpointer data_ms) {
	menushell_location_t * msl = (menushell_location_t *)data_ms;

	if (msl->found) return;

	if (!IS_APP_MENU_ITEM(data_mi)) {
		msl->position++;
		return;
	}

	AppMenuItem * appmenu = APP_MENU_ITEM(data_mi);
	if (!g_strcmp0(INDICATE_LISTENER_SERVER_DBUS_NAME((IndicateListenerServer*)msl->server), INDICATE_LISTENER_SERVER_DBUS_NAME(app_menu_item_get_server(appmenu)))) {
		GList *shortcuts = app_menu_item_get_items(appmenu);
		msl->found = TRUE;
		/* Return a position at the end of our shortcuts */
		msl->position += g_list_length(shortcuts);
		g_list_free (shortcuts);
		
	} else {
		msl->position++;
	}

	return;
}

/* This function takes care of putting the menu in the right order.
   It basically it rebuilds the order by looking through all the
   applications and launchers and puts them in the right place.  The
   menu functions will handle the cases where they don't move so this
   is a good way to ensure everything is right. */
static void
resort_menu (DbusmenuMenuitem * menushell)
{
	guint position = 0;
	GList * serverentry;

	g_debug("Reordering Menu:");
	
	if (DBUSMENU_IS_MENUITEM(status_separator)) {
		position = dbusmenu_menuitem_get_position(status_separator, root_menuitem) + 1;
		g_debug("\tPriming with location of status separator: %d", position);
	}

	for (serverentry = serverList; serverentry != NULL; serverentry = serverentry->next) {
		serverList_t * si = (serverList_t *)serverentry->data;
		
		/* Putting the app menu item in */
		if (si->menuitem != NULL) {
			g_debug("\tMoving app %s to position %d", INDICATE_LISTENER_SERVER_DBUS_NAME(si->server), position);
			dbusmenu_menuitem_child_reorder(DBUSMENU_MENUITEM(menushell), DBUSMENU_MENUITEM(si->menuitem), position);
			position++;

			/* Inserting the shortcuts from the launcher */
			GList * shortcuts = app_menu_item_get_items(si->menuitem);
			GList * shortcut = shortcuts;
			while (shortcut != NULL) {
				g_debug("\t\tMoving shortcut to position %d", position);
				dbusmenu_menuitem_child_reorder(DBUSMENU_MENUITEM(menushell), DBUSMENU_MENUITEM(shortcut->data), position);
				position++;
				shortcut = g_list_next(shortcut);
			}
			g_list_free (shortcuts);
		}

		/* Putting all the indicators that are related to this application
		   after it. */
		GList * imentry;
		for (imentry = si->imList; imentry != NULL; imentry = imentry->next) {
			imList_t * imi = (imList_t *)imentry->data;

			if (imi->menuitem != NULL) {
				g_debug("\tMoving indicator on %s id %d to position %d", INDICATE_LISTENER_SERVER_DBUS_NAME(imi->server), INDICATE_LISTENER_INDICATOR_ID(imi->indicator), position);

				if (si->menuitem == NULL || !dbusmenu_menuitem_property_get_bool(DBUSMENU_MENUITEM(si->menuitem), DBUSMENU_MENUITEM_PROP_VISIBLE)) {
					dbusmenu_menuitem_property_set_bool(imi->menuitem, DBUSMENU_MENUITEM_PROP_VISIBLE, FALSE);
				} else {
					dbusmenu_menuitem_property_set_bool(imi->menuitem, DBUSMENU_MENUITEM_PROP_VISIBLE, TRUE);
				}

				dbusmenu_menuitem_child_reorder(DBUSMENU_MENUITEM(menushell), DBUSMENU_MENUITEM(imi->menuitem), position);
				position++;
			}
		}

		/* Lastly putting the separator in */
		if (si->separator != NULL) {
			g_debug("\tMoving app %s separator to position %d", INDICATE_LISTENER_SERVER_DBUS_NAME(si->server), position);

			if (si->menuitem == NULL || !dbusmenu_menuitem_property_get_bool(DBUSMENU_MENUITEM(si->menuitem), DBUSMENU_MENUITEM_PROP_VISIBLE)) {
				dbusmenu_menuitem_property_set_bool(si->separator, DBUSMENU_MENUITEM_PROP_VISIBLE, FALSE);
				/* Note, this isn't the last if we can't see it */
			} else {
				dbusmenu_menuitem_property_set_bool(si->separator, DBUSMENU_MENUITEM_PROP_VISIBLE, TRUE);
			}

			dbusmenu_menuitem_child_reorder(DBUSMENU_MENUITEM(menushell), DBUSMENU_MENUITEM(si->separator), position);
			position++;
		}
	}

	if (clear_attention != NULL) {
		dbusmenu_menuitem_child_reorder(DBUSMENU_MENUITEM(menushell), clear_attention, position);
		position++; /* Not needed, but reduce bugs on code tacked on here, compiler will remove */
	}

	return;
}

/* Responding to a new indicator showing up on the bus.  We
   need to create a menuitem for it and start populating the
   internal structures to track it. */
static void
indicator_added (IndicateListener * listener, IndicateListenerServer * server, IndicateListenerIndicator * indicator, gpointer data)
{
	DbusmenuMenuitem * menushell = DBUSMENU_MENUITEM(data);
	if (menushell == NULL) {
		g_error("Data in callback is not a menushell");
		return;
	}

	imList_t * listItem = g_new0(imList_t, 1);
	listItem->server = server;
	listItem->indicator = indicator;

	/* Building the IM Menu Item which is a subclass
	   of DBus Menuitem */
	ImMenuItem * menuitem = im_menu_item_new(listener, server, indicator);
	listItem->menuitem = DBUSMENU_MENUITEM(menuitem);

	/* Looking for a server entry to attach this indicator
	   to.  If we can't find one then we have to build one
	   and attach the indicator to it. */
	serverList_t sl_item_local = {0};
	serverList_t * sl_item = NULL;
	sl_item_local.server = server;
	GList * serverentry = g_list_find_custom(serverList, &sl_item_local, serverList_equal);

	if (serverentry == NULL) {
		/* This sucks, we got an indicator before the server.  I guess
		   that's the joy of being asynchronous */
		sl_item = g_new0(serverList_t, 1);
		sl_item->server = server;
		sl_item->menuitem = NULL;
		sl_item->imList = NULL;
		sl_item->attention = FALSE;
		sl_item->count = 0;
		sl_item->separator = NULL;

		serverList = g_list_insert_sorted(serverList, sl_item, serverList_sort);
	} else {
		sl_item = (serverList_t *)serverentry->data;
	}

	/* Added a this entry into the IM list */
	sl_item->imList = g_list_insert_sorted(sl_item->imList, listItem, imList_sort);
	listItem->timechange_cb = g_signal_connect(G_OBJECT(menuitem), IM_MENU_ITEM_SIGNAL_TIME_CHANGED, G_CALLBACK(im_time_changed), sl_item);
	listItem->attentionchange_cb = g_signal_connect(G_OBJECT(menuitem), IM_MENU_ITEM_SIGNAL_ATTENTION_CHANGED, G_CALLBACK(im_attention_changed), sl_item);

	/* Check the length of the list.  If we've got more inidactors
	   than we allow.  Well.  Someone's gotta pay.  Sorry.  I didn't
	   want to do this, but you did it to yourself. */
	if (g_list_length(sl_item->imList) > MAX_NUMBER_OF_INDICATORS) {
		GList * indicatoritem;
		gint count;
		for (indicatoritem = sl_item->imList, count = 0; indicatoritem != NULL; indicatoritem = g_list_next(indicatoritem), count++) {
			imList_t * im = (imList_t *)indicatoritem->data;
			im_menu_item_show(IM_MENU_ITEM(im->menuitem), count < MAX_NUMBER_OF_INDICATORS);
		}
	}

	/* Placing the item into the shell.  Look to see if
	   we can find our server and slip in there.  Otherwise
	   we'll just append. */
	menushell_location_t msl;
	msl.found = FALSE;
	msl.position = 0;
	msl.server = server;

	dbusmenu_menuitem_foreach(DBUSMENU_MENUITEM(menushell), menushell_foreach_cb, &msl);
	if (msl.found) {
		dbusmenu_menuitem_child_add_position(menushell, DBUSMENU_MENUITEM(menuitem), msl.position);
	} else {
		g_warning("Unable to find server menu item");
		dbusmenu_menuitem_child_append(menushell, DBUSMENU_MENUITEM(menuitem));
		resort_menu (root_menuitem);
	}

	return;
}

/* Process and indicator getting removed from the system.  We
   first need to ensure that it's one of ours and figure out
   where we put it.  When we find all that out we can go through
   the process of removing the effect it had on the system. */
static void
indicator_removed (IndicateListener * listener, IndicateListenerServer * server, IndicateListenerIndicator * indicator, gpointer data)
{
	g_debug("Removing %s %d", INDICATE_LISTENER_SERVER_DBUS_NAME(server), INDICATE_LISTENER_INDICATOR_ID(indicator));

	gboolean removed = FALSE;

	/* Find the server that was related to this item */
	serverList_t sl_item_local = {0};
	serverList_t * sl_item = NULL;
	sl_item_local.server = server;
	GList * serverentry = g_list_find_custom(serverList, &sl_item_local, serverList_equal);
	if (serverentry == NULL) {
		/* We didn't care about that server */
		return;
	}
	sl_item = (serverList_t *)serverentry->data;

	/* Look in the IM Hash Table */
	imList_t listData = {0};
	listData.server = server;
	listData.indicator = indicator;

	GList * listItem = g_list_find_custom(sl_item->imList, &listData, imList_equal);
	DbusmenuMenuitem * menuitem = NULL;
	imList_t * ilt = NULL;
	if (listItem != NULL) {
		ilt = (imList_t *)listItem->data;
		menuitem = ilt->menuitem;
	}

	/* If we found a menu item and an imList_t item then
	   we can go ahead and remove it.  Otherwise we can 
	   skip this and exit. */
	if (!removed && menuitem != NULL) {
		sl_item->imList = g_list_remove(sl_item->imList, ilt);
		g_signal_handler_disconnect(menuitem, ilt->timechange_cb);
		g_signal_handler_disconnect(menuitem, ilt->attentionchange_cb);
		g_free(ilt);

		if (im_menu_item_get_attention(IM_MENU_ITEM(menuitem)) && im_menu_item_shown(IM_MENU_ITEM(menuitem))) {
			/* If the removed indicator menu item was asking for
			   attention we need to see if this server should still
			   be asking for attention. */
			server_attention(sl_item);
			/* If the server is no longer asking for attention then
			   we need to check if the whole system should be. */
			if (!sl_item->attention) {
				check_attention();
			}
		}

		if (im_menu_item_shown(IM_MENU_ITEM(menuitem)) && g_list_length(sl_item->imList) >= MAX_NUMBER_OF_INDICATORS) {
			/* In this case we need to show a different indicator
			   becasue a shown one has left.  But we're going to be
			   easy and set all the values. */
			GList * indicatoritem;
			gint count;
			for (indicatoritem = sl_item->imList, count = 0; indicatoritem != NULL; indicatoritem = g_list_next(indicatoritem), count++) {
				imList_t * im = (imList_t *)indicatoritem->data;
				im_menu_item_show(IM_MENU_ITEM(im->menuitem), count < MAX_NUMBER_OF_INDICATORS);
			}
		}

		/* Hide the item immediately, and then remove it
		   which might take a little longer. */
		dbusmenu_menuitem_property_set_bool(menuitem, DBUSMENU_MENUITEM_PROP_VISIBLE, FALSE);
		dbusmenu_menuitem_child_delete(DBUSMENU_MENUITEM(data), menuitem);
		removed = TRUE;
	}

	if (!removed) {
		g_warning("We were asked to remove %s %d but we didn't.", INDICATE_LISTENER_SERVER_DBUS_NAME(server), INDICATE_LISTENER_INDICATOR_ID(indicator));
	}

	return;
}

/* This function turns a specific file into a menu
   item and registers it appropriately with everyone */
static gboolean
build_launcher (gpointer data)
{
	gchar *desktop_id = data;
	GDesktopAppInfo *appinfo;
	GList *listitem;

	appinfo = g_desktop_app_info_new (desktop_id);

	/* Check to see if we already have a launcher */
	for (listitem = serverList; listitem != NULL; listitem = listitem->next) {
		serverList_t * slt = listitem->data;
		if (!g_strcmp0(app_menu_item_get_desktop(slt->menuitem),
			       g_desktop_app_info_get_filename (appinfo))) {
			break;
		}
	}

	if (listitem == NULL) {
		/* If not */
		/* Build the item */
		serverList_t * sl_item = g_new0(serverList_t, 1);
		sl_item->menuitem = app_menu_item_new(appinfo);

		/* Build a separator */
		sl_item->separator = dbusmenu_menuitem_new();
		dbusmenu_menuitem_property_set(sl_item->separator, DBUSMENU_MENUITEM_PROP_TYPE, DBUSMENU_CLIENT_TYPES_SEPARATOR);

		/* Add it to the list */
		serverList = g_list_insert_sorted (serverList, sl_item, serverList_sort);

		/* Add it to the menu */
		dbusmenu_menuitem_property_set(DBUSMENU_MENUITEM(sl_item->menuitem), DBUSMENU_MENUITEM_PROP_TYPE, APPLICATION_MENUITEM_TYPE);
		dbusmenu_menuitem_child_append(root_menuitem, DBUSMENU_MENUITEM(sl_item->menuitem));
		GList * shortcuts = app_menu_item_get_items(sl_item->menuitem);
		GList * shortcut = shortcuts;
		while (shortcut != NULL) {
			dbusmenu_menuitem_child_append(root_menuitem, DBUSMENU_MENUITEM(shortcut->data));
			shortcut = g_list_next(shortcut);
		}
		g_list_free (shortcuts);
		dbusmenu_menuitem_child_append(root_menuitem, DBUSMENU_MENUITEM(sl_item->separator));

		resort_menu(root_menuitem);
	}

	g_object_unref (appinfo);
	g_free (desktop_id);
	return FALSE;
}

/* This function goes through all the launchers that we're
   supposed to be grabbing and decides to show turn them
   into menu items or not.  It doens't do the work, but it
   makes the decision. */
static gboolean
build_launchers (gpointer data)
{
	gchar **applications = g_settings_get_strv (settings, "applications");
	gchar **app;

	g_return_val_if_fail (applications != NULL, FALSE);

	for (app = applications; *app; app++)
	{
		g_idle_add(build_launcher, g_strdup (*app));
	}

	serverList = g_list_sort(serverList, serverList_sort);

	g_strfreev (applications);
	return FALSE;
}

static void 
service_shutdown (IndicatorService * service, gpointer user_data)
{
	g_warning("Shutting down service!");
	g_main_loop_quit(mainloop);
	return;
}

/* Respond to changing status by updating the icon that
   is on the panel */
static void
status_update_callback (void)
{
	return;
}

/* The clear attention item has been clicked on, what to do? */
static void
clear_attention_activate (DbusmenuMenuitem * mi, guint timestamp, MessageServiceDbus * dbus)
{
	message_service_dbus_set_attention(dbus, FALSE);
	return;
}

/* Handle an update of the active state to ensure that we're
   only enabled when we could do something. */
static void
clear_attention_handler (MessageServiceDbus * msd, gboolean attention, DbusmenuMenuitem * clearitem)
{
	dbusmenu_menuitem_property_set_bool(clearitem, DBUSMENU_MENUITEM_PROP_ENABLED, attention);
	return;
}

/* Oh, if you don't know what main() is for
   we really shouldn't be talking. */
int
main (int argc, char ** argv)
{
	/* Glib init */
	g_type_init();

	/* Create the Indicator Service interface */
	service = indicator_service_new_version(INDICATOR_MESSAGES_DBUS_NAME, 1);
	g_signal_connect(service, INDICATOR_SERVICE_SIGNAL_SHUTDOWN, G_CALLBACK(service_shutdown), NULL);

	/* Setting up i18n and gettext.  Apparently, we need
	   all of these. */
	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	textdomain (GETTEXT_PACKAGE);

	/* Bring up the service DBus interface */
	dbus_interface = message_service_dbus_new();

	/* Build the base menu */
	root_menuitem = dbusmenu_menuitem_new();
	DbusmenuServer * server = dbusmenu_server_new(INDICATOR_MESSAGES_DBUS_OBJECT);
	dbusmenu_server_set_root(server, root_menuitem);

	/* Add status items */
	GList * statusitems = status_items_build(&status_update_callback);
	while (statusitems != NULL) {
		dbusmenu_menuitem_child_append(root_menuitem, DBUSMENU_MENUITEM(statusitems->data));
		statusitems = g_list_next(statusitems);
	}
	status_separator = dbusmenu_menuitem_new();
	dbusmenu_menuitem_property_set(status_separator, DBUSMENU_MENUITEM_PROP_TYPE, DBUSMENU_CLIENT_TYPES_SEPARATOR);
	dbusmenu_menuitem_child_append(root_menuitem, status_separator);

	/* Add in the clear attention item */
	clear_attention = dbusmenu_menuitem_new();
	dbusmenu_menuitem_property_set(clear_attention, DBUSMENU_MENUITEM_PROP_LABEL, _("Clear"));
	dbusmenu_menuitem_child_append(root_menuitem, clear_attention);
	g_signal_connect(G_OBJECT(dbus_interface), MESSAGE_SERVICE_DBUS_SIGNAL_ATTENTION_CHANGED, G_CALLBACK(clear_attention_handler), clear_attention);
	g_signal_connect(G_OBJECT(clear_attention), DBUSMENU_MENUITEM_SIGNAL_ITEM_ACTIVATED, G_CALLBACK(clear_attention_activate), dbus_interface);

	/* Start up the libindicate listener */
	listener = indicate_listener_ref_default();
	serverList = NULL;

	g_signal_connect(listener, INDICATE_LISTENER_SIGNAL_INDICATOR_ADDED, G_CALLBACK(indicator_added), root_menuitem);
	g_signal_connect(listener, INDICATE_LISTENER_SIGNAL_INDICATOR_REMOVED, G_CALLBACK(indicator_removed), root_menuitem);
	g_signal_connect(listener, INDICATE_LISTENER_SIGNAL_SERVER_ADDED, G_CALLBACK(server_added), root_menuitem);
	g_signal_connect(listener, INDICATE_LISTENER_SIGNAL_SERVER_REMOVED, G_CALLBACK(server_removed), root_menuitem);

	settings = g_settings_new ("com.canonical.indicator.messages");

	g_idle_add(build_launchers, NULL);

	/* Let's run a mainloop */
	mainloop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(mainloop);

	/* Clean up */
	status_items_cleanup();
	g_object_unref (settings);

	return 0;
}
