
#include <string.h>
#include <gtk/gtk.h>
#include <libindicate/listener.h>

#include "im-menu-item.h"
#include "app-menu-item.h"

static IndicateListener * listener;
static GList * imList;
static GHashTable * serverHash;

typedef struct _imList_t imList_t;
struct _imList_t {
	IndicateListenerServer * server;
	IndicateListenerIndicator * indicator;
	GtkWidget * menuitem;
};

static gboolean
imList_equal (gconstpointer a, gconstpointer b)
{
	imList_t * pa, * pb;

	pa = (imList_t *)a;
	pb = (imList_t *)b;

	gchar * pas = INDICATE_LISTENER_SERVER_DBUS_NAME(pa->server);
	gchar * pbs = INDICATE_LISTENER_SERVER_DBUS_NAME(pb->server);

	guint pai = INDICATE_LISTENER_INDICATOR_ID(pa->indicator);
	guint pbi = INDICATE_LISTENER_INDICATOR_ID(pb->indicator);

	g_debug("\tComparing (%s %d) to (%s %d)", pas, pai, pbs, pbi);

	return !((!strcmp(pas, pbs)) && (pai == pbi));
}


void 
server_added (IndicateListener * listener, IndicateListenerServer * server, gchar * type, gpointer data)
{
	g_debug("Server Added");
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

	GtkMenuShell * menushell = GTK_MENU_SHELL(data);
	if (menushell == NULL) {
		g_error("\tData in callback is not a menushell");
		return;
	}

	gchar * servername = g_strdup(INDICATE_LISTENER_SERVER_DBUS_NAME(server));
	AppMenuItem * menuitem = app_menu_item_new(listener, server);

	g_hash_table_insert(serverHash, servername, menuitem);
	gtk_menu_shell_prepend(menushell, GTK_WIDGET(menuitem));

	return;
}

void 
server_removed (IndicateListener * listener, IndicateListenerServer * server, gchar * type, gpointer data)
{
	g_debug("Removing server: %s", INDICATE_LISTENER_SERVER_DBUS_NAME(server));
	gpointer lookup = g_hash_table_lookup(serverHash, INDICATE_LISTENER_SERVER_DBUS_NAME(server));

	if (lookup == NULL) {
		g_debug("\tUnable to find server: %s", INDICATE_LISTENER_SERVER_DBUS_NAME(server));
		return;
	}

	g_hash_table_remove(serverHash, INDICATE_LISTENER_SERVER_DBUS_NAME(server));

	AppMenuItem * menuitem = APP_MENU_ITEM(lookup);
	g_return_if_fail(menuitem != NULL);

	gtk_widget_hide(GTK_WIDGET(menuitem));
	gtk_container_remove(GTK_CONTAINER(data), GTK_WIDGET(menuitem));

	return;
}

static void
subtype_cb (IndicateListener * listener, IndicateListenerServer * server, IndicateListenerIndicator * indicator, gchar * property, gchar * propertydata, gpointer data)
{
	GtkMenuShell * menushell = GTK_MENU_SHELL(data);
	if (menushell == NULL) {
		g_error("Data in callback is not a menushell");
		return;
	}

	if (property == NULL || strcmp(property, "subtype")) {
		/* We should only ever get subtypes, but just in case */
		g_warning("Subtype callback got a property '%s'", property);
		return;
	}

	if (propertydata == NULL || propertydata[0] == '\0') {
		/* It's possible that this message didn't have a subtype.  That's
		 * okay, but we don't want to display those */
		g_debug("No subtype");
		return;
	}

	g_debug("Message subtype: %s", propertydata);

	if (!strcmp(propertydata, "im")) {
		imList_t * listItem = g_new(imList_t, 1);
		listItem->server = server;
		listItem->indicator = indicator;

		g_debug("Building IM Item");
		ImMenuItem * menuitem = im_menu_item_new(listener, server, indicator);
		g_object_ref(G_OBJECT(menuitem));
		listItem->menuitem = GTK_WIDGET(menuitem);

		g_debug("Adding to IM Hash");
		imList = g_list_append(imList, listItem);

		g_debug("Placing in Shell");
		gtk_menu_shell_prepend(menushell, GTK_WIDGET(menuitem));
	}

	return;
}

static void
indicator_added (IndicateListener * listener, IndicateListenerServer * server, IndicateListenerIndicator * indicator, gchar * type, gpointer data)
{
	if (type == NULL || strcmp(type, "message")) {
		/* We only care about message type indicators
		   all of the others can go to the bit bucket */
		g_debug("Ignoreing indicator of type '%s'", type);
		return;
	}
	g_debug("Got a message");

	indicate_listener_get_property(listener, server, indicator, "subtype", subtype_cb, data);	
	return;
}

static void
indicator_removed (IndicateListener * listener, IndicateListenerServer * server, IndicateListenerIndicator * indicator, gchar * type, gpointer data)
{
	g_debug("Removing %s %d", (gchar*)server, (guint)indicator);
	if (type == NULL || strcmp(type, "message")) {
		/* We only care about message type indicators
		   all of the others can go to the bit bucket */
		g_debug("Ignoreing indicator of type '%s'", type);
		return;
	}

	gboolean removed = FALSE;

	/* Look in the IM Hash Table */
	imList_t listData;
	listData.server = server;
	listData.indicator = indicator;

	GList * listItem = g_list_find_custom(imList, &listData, imList_equal);
	GtkWidget * menuitem = NULL;
	if (listItem != NULL) {
		menuitem = ((imList_t *)listItem->data)->menuitem;
	}

	if (!removed && menuitem != NULL) {
		g_object_ref(menuitem);
		imList = g_list_remove(imList, listItem->data);

		gtk_widget_hide(menuitem);
		gtk_container_remove(GTK_CONTAINER(data), menuitem);

		g_object_unref(menuitem);
		removed = TRUE;
	}

	if (!removed) {
		g_warning("We were asked to remove %s %d but we didn't.", (gchar*)server, (guint)indicator);
	}

	return;
}

GtkWidget *
get_menu_item (void)
{
	listener = indicate_listener_new();
	imList = NULL;

	serverHash = g_hash_table_new_full(g_str_hash, g_str_equal,
	                                   g_free, NULL);

	GtkWidget * mainmenu = gtk_menu_item_new();

	GtkWidget * image = gtk_image_new_from_icon_name("indicator-messages", GTK_ICON_SIZE_MENU);
	gtk_widget_show(image);
	gtk_container_add(GTK_CONTAINER(mainmenu), image);

	GtkWidget * submenu = gtk_menu_new();
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(mainmenu), submenu);
	gtk_widget_show(submenu);
	gtk_widget_show(mainmenu);

	g_signal_connect(listener, INDICATE_LISTENER_SIGNAL_INDICATOR_ADDED, G_CALLBACK(indicator_added), submenu);
	g_signal_connect(listener, INDICATE_LISTENER_SIGNAL_INDICATOR_REMOVED, G_CALLBACK(indicator_removed), submenu);
	g_signal_connect(listener, INDICATE_LISTENER_SIGNAL_SERVER_ADDED, G_CALLBACK(server_added), submenu);
	g_signal_connect(listener, INDICATE_LISTENER_SIGNAL_SERVER_REMOVED, G_CALLBACK(server_removed), submenu);

	return mainmenu;
}

