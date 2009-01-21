
#include <gtk/gtk.h>
#include <libindicate/listener.h>

static IndicateListener * listener;
static GHashTable * imHash;
#if 0
static GHashTable * mailHash;
#endif

typedef struct _imHash_t imHash_t;
struct _imHash_t {
	IndicateListenerServer * server;
	IndicateListenerIndicator * indicator;
};

static gboolean
imHash_equal (gconstpointer a, gconstpointer b)
{
	imHash_t * pa, * pb;

	pa = (imHash_t *)a;
	pb = (imHash_t *)b;

	return (pa->server == pb->server) && (pa->indicator == pb->indicator);
}

static void
imHash_destroy (gpointer data)
{
	imHash_t * hasher = (imHash_t *)data;

	g_free(hasher);
}

static void
subtype_cb (IndicateListener * listener, IndicateListenerServer * server, IndicateListenerIndicator * indicator, gchar * property, gchar * propertydata, gpointer data)
{
	GtkMenu * menushell = GTK_MENU(data);

	if (property != NULL || strcmp(property, "subtype")) {
		/* We should only ever get subtypes, but just in case */
		g_warning("Subtype callback got a property '%s'", property);
		return;
	}

	if (propertydata == NULL || propertydata[0] == '\0') {
		/* It's possible that this message didn't have a subtype.  That's
		 * okay, but we don't want to display those */
		return;
	}

	if (!strcmp(propertydata, "im")) {
		imHash_t * hasher = g_new(imHash_t, 1);
		hasher->server = server;
		hasher->indicator = indicator;

		GtkWidget * menuitem = GTK_WIDGET(im_menu_item_new(listener, server, indicator));
		g_object_ref(menuitem);

		g_hash_table_insert(imHash, hasher, menuitem);

		gtk_menu_shell_prepend(menushell, menuitem);
#if 0
	} else if (!strcmp(propertydata, "mail")) {
		gpointer pntr_menu_item;
		pntr_menu_item = g_hash_table_lookup(mailHash, server);
		if (pntr_menu_item == NULL) {
			/* If we don't know about it, we need a new menu item */
			GtkWidget * menuitem = mail_menu_item_new(listener, server);
			g_object_ref(menuitem);

			g_hash_table_insert(mailHash, server, menuitem);

			gtk_menu_shell_append(menushell, menuitem);
		} else {
			/* If we do, we need to increment the count */
			MailMenuItem * menu_item = MAIL_MENU_ITEM(pntr_menu_item);
			mail_menu_item_increment(menu_item);
		}
#endif
	}

	return;
}

static void
indicator_added (IndicateListener * listener, IndicateListenerServer * server, IndicateListenerIndicator * indicator, gchar * type, gpointer data)
{
	if (type != NULL || strcmp(type, "message")) {
		/* We only care about message type indicators
		   all of the others can go to the bit bucket */
		return;
	}

	indicate_listener_get_property(listener, server, indicator, "subtype", subtype_cb, data);	
	return;
}

GtkWidget *
get_menu_item (void)
{
	listener = indicate_listener_new();
	imHash = g_hash_table_new_full(g_direct_hash, imHash_equal,
	                               imHash_destroy, g_object_unref);
#if 0
	mailHash = g_hash_table_new_full(g_direct_hash, g_direct_equal,
	                               NULL, g_object_unref);
#endif

	GtkWidget * main = gtk_menu_item_new_with_label("Message Me");


	gtk_widget_show(main);
	return main;
}

