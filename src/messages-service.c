/*
An indicator to show information that is in messaging applications
that the user is using.

Copyright 2012 Canonical Ltd.

Authors:
    Ted Gould <ted@canonical.com>
    Lars Uebernickel <lars.uebernickel@canonical.com>

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

#include <config.h>
#include <locale.h>
#include <libindicator/indicator-service.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <glib/gi18n.h>

#include "app-section.h"
#include "dbus-data.h"
#include "gactionmuxer.h"
#include "gsettingsstrv.h"
#include "gmenuutils.h"
#include "indicator-messages-service.h"

#define NUM_STATUSES 5

static GHashTable *applications;

static IndicatorMessagesService *messages_service;
static GSimpleActionGroup *actions;
static GActionMuxer *action_muxer;
static GMenu *toplevel_menu;
static GMenu *menu;
static GMenuModel *chat_section;
static GSettings *settings;
static gboolean draws_attention;
static const gchar *global_status[6]; /* max 5: available, away, busy, invisible, offline */

static gchar *
indicator_messages_get_icon_name ()
{
	GString *name;
	GIcon *icon;
	gchar *iconstr;

	name = g_string_new ("indicator-messages");

	if (global_status[0] != NULL)
	{
		if (global_status[1] != NULL)
			g_string_append (name, "-mixed");
		else
			g_string_append_printf (name, "-%s", global_status[0]);
	}

	if (draws_attention)
		g_string_append (name, "-new");

	icon = g_themed_icon_new (name->str);
	g_themed_icon_append_name (G_THEMED_ICON (icon),
				   draws_attention ? "indicator-messages-new"
						   : "indicator-messages");

	iconstr = g_icon_to_string (icon);

	g_object_unref (icon);
	g_string_free (name, TRUE);

	return iconstr;
}

static void
indicator_messages_update_icon ()
{
	GSimpleAction *messages;
	gchar *icon;

	messages = G_SIMPLE_ACTION (g_simple_action_group_lookup (actions, "messages"));
	g_return_if_fail (messages != NULL);

	icon = indicator_messages_get_icon_name ();
	g_simple_action_set_state (messages, g_variant_new_string (icon));

	g_free (icon);
}

static gchar *
g_app_info_get_simple_id (GAppInfo *appinfo)
{
	const gchar *id;

	id = g_app_info_get_id (appinfo);
	if (!id)
		return NULL;

	if (g_str_has_suffix (id, ".desktop"))
		return g_strndup (id, strlen (id) - 8);
	else
		return g_strdup (id);
}

static void
actions_changed (GObject *object,
		 GParamSpec *pspec,
		 gpointer user_data)
{
	AppSection *section = APP_SECTION (object);
	gchar *id;
	GActionGroup *actions;

	id = g_app_info_get_simple_id (app_section_get_app_info (section));
	actions = app_section_get_actions (section);

	g_action_muxer_insert (action_muxer, id, actions);
	g_free (id);
}


static gboolean
app_section_draws_attention (gpointer key,
			     gpointer value,
			     gpointer user_data)
{
	AppSection *section = value;
	return app_section_get_draws_attention (section);
}

static void
draws_attention_changed (GObject *object,
			 GParamSpec *pspec,
			 gpointer user_data)
{
	GSimpleAction *clear;

	clear = G_SIMPLE_ACTION (g_simple_action_group_lookup (actions, "clear"));
	g_return_if_fail (clear != NULL);

	draws_attention = g_hash_table_find (applications, app_section_draws_attention, NULL) != NULL;

	g_simple_action_set_enabled (clear, draws_attention);

	indicator_messages_update_icon ();
}

static gboolean
app_section_uses_chat (gpointer key,
		       gpointer value,
		       gpointer user_data)
{
	AppSection *section = value;
	return app_section_get_uses_chat_status (section);
}

static void
update_chat_section ()
{
	gboolean show_chat;
	GMenuModel *first_section;

	show_chat = g_hash_table_find (applications, app_section_uses_chat, NULL) != NULL;

	first_section = g_menu_model_get_item_link (G_MENU_MODEL (menu), 0, G_MENU_LINK_SECTION);
	if (first_section == chat_section) {
		if (!show_chat)
			g_menu_remove (menu, 0);
	}
	else {
		if (show_chat)
			g_menu_insert_section (menu, 0, NULL, chat_section);
	}

	if (first_section != NULL)
		g_object_unref (first_section);

	indicator_messages_update_icon ();
}

static void
uses_chat_status_changed (GObject *object,
			  GParamSpec *pspec,
			  gpointer user_data)
{
	update_chat_section ();
}

static gboolean
strv_contains (const gchar **strv,
	       const gchar  *needle)
{
	const gchar **it;

	it = strv;
	while (*it != NULL && !g_str_equal (*it, needle))
		it++;

	return *it != NULL;
}

static void
update_chat_status ()
{
	GHashTableIter iter;
	AppSection *section;
	int pos;
	GAction *status;

	for (pos = 0; pos < G_N_ELEMENTS (global_status); pos++)
		global_status[pos] = NULL;

	pos = 0;
	g_hash_table_iter_init (&iter, applications);
	while (g_hash_table_iter_next (&iter, NULL, (gpointer) &section) &&
	       pos < G_N_ELEMENTS (global_status))
	{
		const gchar *status_str = NULL;

		status_str = app_section_get_status (section);
		if (status_str != NULL && !strv_contains (global_status, status_str))
			global_status[pos++] = status_str;
	}

	if (pos == 0)
		global_status[0] = "offline";

	status = g_simple_action_group_lookup (actions, "status");
	g_return_if_fail (status != NULL);

	g_simple_action_set_state (G_SIMPLE_ACTION (status), g_variant_new_strv (global_status, -1));

	indicator_messages_update_icon ();
}

static void
chat_status_changed (GObject    *object,
		     GParamSpec *pspec,
		     gpointer    user_data)
{
	update_chat_status ();
}

static void
remove_section (AppSection  *section,
		const gchar *id)
{
	int pos = g_menu_find_section (menu, app_section_get_menu (section));
	if (pos >= 0)
		g_menu_remove (menu, pos);
	g_action_muxer_remove (action_muxer, id);

	g_signal_handlers_disconnect_by_func (section, actions_changed, NULL);
	g_signal_handlers_disconnect_by_func (section, draws_attention_changed, NULL);
	g_signal_handlers_disconnect_by_func (section, uses_chat_status_changed, NULL);
	g_signal_handlers_disconnect_by_func (section, chat_status_changed, NULL);
	g_signal_handlers_disconnect_by_func (section, remove_section, NULL);

	g_hash_table_remove (applications, id);

	if (g_hash_table_size (applications) == 0 &&
	    g_menu_model_get_n_items (G_MENU_MODEL (toplevel_menu)) == 1) {
		g_menu_remove (toplevel_menu, 0);
	}

	update_chat_status ();
	update_chat_section ();
}

static AppSection *
add_application (const gchar *desktop_id)
{
	GDesktopAppInfo *appinfo;
	gchar *id;
	AppSection *section;

	appinfo = g_desktop_app_info_new (desktop_id);
	if (!appinfo) {
		g_warning ("could not add '%s', there's no desktop file with that id", desktop_id);
		return NULL;
	}

	id = g_app_info_get_simple_id (G_APP_INFO (appinfo));
	section = g_hash_table_lookup (applications, id);

	if (!section) {
		GMenuItem *menuitem;

		section = app_section_new(appinfo);
		g_hash_table_insert (applications, g_strdup (id), section);

		g_action_muxer_insert (action_muxer, id, app_section_get_actions (section));
		g_signal_connect (section, "notify::actions",
				  G_CALLBACK (actions_changed), NULL);
		g_signal_connect (section, "notify::draws-attention",
				  G_CALLBACK (draws_attention_changed), NULL);
		g_signal_connect (section, "notify::uses-chat-status",
				  G_CALLBACK (uses_chat_status_changed), NULL);
		g_signal_connect (section, "notify::chat-status",
				  G_CALLBACK (chat_status_changed), NULL);
		g_signal_connect_data (section, "destroy",
				       G_CALLBACK (remove_section),
				       g_strdup (id),
				       (GClosureNotify) g_free,
				       0);

		/* TODO insert it at the right position (alphabetically by application name) */
		menuitem = g_menu_item_new_section (NULL, app_section_get_menu (section));
		g_menu_item_set_attribute (menuitem, "action-namespace", "s", id);
		g_menu_insert_item (menu, g_menu_model_get_n_items (G_MENU_MODEL (menu)) -1, menuitem);
		g_object_unref (menuitem);
	}

	if (g_menu_model_get_n_items (G_MENU_MODEL (toplevel_menu)) == 0) {
		GMenuItem *header;

		header = g_menu_item_new (NULL, "messages");
		g_menu_item_set_submenu (header, G_MENU_MODEL (menu));
		g_menu_item_set_attribute (header, "x-canonical-accessible-description", "s", _("Messages"));
		g_menu_append_item (toplevel_menu, header);

		g_object_unref (header);
	}

	g_free (id);
	g_object_unref (appinfo);
	return section;
}

static void
remove_application (const char *desktop_id)
{
	GDesktopAppInfo *appinfo;
	gchar *id;
	AppSection *section;

	appinfo = g_desktop_app_info_new (desktop_id);
	if (!appinfo) {
		g_warning ("could not remove '%s', there's no desktop file with that id", desktop_id);
		return;
	}

	id = g_app_info_get_simple_id (G_APP_INFO (appinfo));

	section = g_hash_table_lookup (applications, id);
	if (section) {
		remove_section (section, id);
	}
	else {
		g_warning ("could not remove '%s', it's not registered", desktop_id);
	}
	
	g_free (id);
	g_object_unref (appinfo);
}

/* This function turns a specific desktop id into a menu
   item and registers it appropriately with everyone */
static gboolean
build_launcher (gpointer data)
{
	gchar *desktop_id = data;

	add_application (desktop_id);

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

	g_strfreev (applications);
	return FALSE;
}

static void 
service_shutdown (IndicatorService * service, gpointer user_data)
{
	GMainLoop *mainloop = user_data;

	g_warning("Shutting down service!");
	g_main_loop_quit(mainloop);
}

static void
app_section_remove_attention (gpointer key,
			      gpointer value,
			      gpointer user_data)
{
	AppSection *section = value;
	app_section_clear_draws_attention (section);
}

static void
clear_action_activate (GSimpleAction *simple,
		       GVariant *param,
		       gpointer user_data)
{
	g_hash_table_foreach (applications, app_section_remove_attention, NULL);
}

static void
status_action_activate (GSimpleAction *action,
			GVariant *parameter,
			gpointer user_data)
{
	const gchar *status;

	status = g_variant_get_string (parameter, NULL);

	indicator_messages_service_emit_status_changed (messages_service, status);
}

static void
register_application (IndicatorMessagesService *service,
		      GDBusMethodInvocation *invocation,
		      const gchar *desktop_id,
		      const gchar *menu_path,
		      gpointer user_data)
{
	AppSection *section;
	GDBusConnection *bus;
	const gchar *sender;

	section = add_application (desktop_id);
	if (!section)
		return;

	bus = g_dbus_interface_skeleton_get_connection (G_DBUS_INTERFACE_SKELETON (service));
	sender = g_dbus_method_invocation_get_sender (invocation);

	app_section_set_object_path (section, bus, sender, menu_path);
	g_settings_strv_append_unique (settings, "applications", desktop_id);

	indicator_messages_service_complete_register_application (service, invocation);
}

static void
unregister_application (IndicatorMessagesService *service,
			GDBusMethodInvocation *invocation,
			const gchar *desktop_id,
			gpointer user_data)
{
	remove_application (desktop_id);
	g_settings_strv_remove (settings, "applications", desktop_id);

	indicator_messages_service_complete_unregister_application (service, invocation);
}

static void
application_stopped_running (IndicatorMessagesService *service,
			     GDBusMethodInvocation    *invocation,
			     const gchar              *desktop_id,
			     gpointer                  user_data)
{
	GDesktopAppInfo *appinfo;
	gchar *id;
	AppSection *section;

	indicator_messages_service_complete_application_stopped_running (service, invocation);

	if (!(appinfo = g_desktop_app_info_new (desktop_id)))
		return;

	id = g_app_info_get_simple_id (G_APP_INFO (appinfo));
	section = g_hash_table_lookup (applications, id);
	app_section_unset_object_path (section);

	g_free (id);
	g_object_unref (appinfo);
}

static void
set_status (IndicatorMessagesService *service,
	    GDBusMethodInvocation *invocation,
	    const gchar *desktop_id,
	    const gchar *status_str,
	    gpointer user_data)
{
	GDesktopAppInfo *appinfo;
	gchar *id;
	AppSection *section;

	g_return_if_fail (g_str_equal (status_str, "available") ||
			  g_str_equal (status_str, "away")||
			  g_str_equal (status_str, "busy") ||
			  g_str_equal (status_str, "invisible") ||
			  g_str_equal (status_str, "offline"));

	appinfo = g_desktop_app_info_new (desktop_id);
	if (!appinfo) {
		g_warning ("could not set status for '%s', there's no desktop file with that id", desktop_id);
		return;
	}

	id = g_app_info_get_simple_id (G_APP_INFO (appinfo));
	section = g_hash_table_lookup (applications, id);
	if (section != NULL)
		app_section_set_status (section, status_str);

	indicator_messages_service_complete_set_status (service, invocation);

	g_free (id);
	g_object_unref (appinfo);
}

static GSimpleActionGroup *
create_action_group (void)
{
	GSimpleActionGroup *actions;
	GSimpleAction *messages;
	GSimpleAction *clear;
	GSimpleAction *status;
	const gchar *default_status[] = { "offline", NULL };
	gchar *icon;

	actions = g_simple_action_group_new ();

	/* state of the messages action is its icon name */
	icon = indicator_messages_get_icon_name ();
	messages = g_simple_action_new_stateful ("messages", G_VARIANT_TYPE ("s"),
						 g_variant_new_string (icon));

	status = g_simple_action_new_stateful ("status", G_VARIANT_TYPE ("s"),
					       g_variant_new_strv (default_status, -1));
	g_signal_connect (status, "activate", G_CALLBACK (status_action_activate), NULL);

	clear = g_simple_action_new ("clear", NULL);
	g_simple_action_set_enabled (clear, FALSE);
	g_signal_connect (clear, "activate", G_CALLBACK (clear_action_activate), NULL);

	g_simple_action_group_insert (actions, G_ACTION (messages));
	g_simple_action_group_insert (actions, G_ACTION (status));
	g_simple_action_group_insert (actions, G_ACTION (clear));

	g_free (icon);
	return actions;
}

static GMenuModel *
create_status_section (void)
{
	GMenu *menu;
	GMenuItem *item;
	struct status_item {
		gchar *label;
		gchar *action;
		gchar *icon_name;
	} status_items[] = {
		{ _("Available"), "status::available", "user-available" },
		{ _("Away"),      "status::away",      "user-away" },
		{ _("Busy"),      "status::busy",      "user-busy" },
		{ _("Invisible"), "status::invisible", "user-invisible" },
		{ _("Offline"),   "status::offline",   "user-offline" }
	};
	int i;

	menu = g_menu_new ();

	item = g_menu_item_new (NULL, NULL);
	g_menu_item_set_attribute (item, "x-canonical-type", "s", "IdoMenuItem");

	for (i = 0; i < G_N_ELEMENTS (status_items); i++) {
		g_menu_item_set_label (item, status_items[i].label);
		g_menu_item_set_detailed_action (item, status_items[i].action);
		g_menu_item_set_attribute (item, "x-canonical-icon", "s", status_items[i].icon_name);
		g_menu_append_item (menu, item);
	}

	g_object_unref (item);
	return G_MENU_MODEL (menu);
}

static void
got_bus (GObject *object,
	 GAsyncResult * res,
	 gpointer user_data)
{
	GDBusConnection *bus;
	GError *error = NULL;

	bus = g_bus_get_finish (res, &error);
	if (!bus) {
		g_warning ("unable to connect to the session bus: %s", error->message);
		g_error_free (error);
		return;
	}

	g_dbus_connection_export_action_group (bus, INDICATOR_MESSAGES_DBUS_OBJECT,
                                               G_ACTION_GROUP (action_muxer), &error);
	if (error) {
		g_warning ("unable to export action group on dbus: %s", error->message);
		g_error_free (error);
		return;
	}

	g_dbus_connection_export_menu_model (bus, INDICATOR_MESSAGES_DBUS_OBJECT,
					     G_MENU_MODEL (toplevel_menu), &error);
	if (error) {
		g_warning ("unable to export menu on dbus: %s", error->message);
		g_error_free (error);
		return;
	}

	g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (messages_service),
					  bus, INDICATOR_MESSAGES_DBUS_SERVICE_OBJECT,
					  &error);
	if (error) {
		g_warning ("unable to export messages service on dbus: %s", error->message);
		g_error_free (error);
		return;
	}

	g_object_unref (bus);
}

int
main (int argc, char ** argv)
{
	GMainLoop * mainloop;
	IndicatorService * service;

	mainloop = g_main_loop_new (NULL, FALSE);

	/* Create the Indicator Service interface */
	service = indicator_service_new_version(INDICATOR_MESSAGES_DBUS_NAME, 1);
	g_signal_connect(service, INDICATOR_SERVICE_SIGNAL_SHUTDOWN, G_CALLBACK(service_shutdown), mainloop);

	/* Setting up i18n and gettext.  Apparently, we need
	   all of these. */
	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	textdomain (GETTEXT_PACKAGE);

	/* Bring up the service DBus interface */
	messages_service = indicator_messages_service_skeleton_new ();

	g_bus_get (G_BUS_TYPE_SESSION, NULL, got_bus, NULL);

	actions = create_action_group ();

	action_muxer = g_action_muxer_new ();
	g_action_muxer_insert (action_muxer, NULL, G_ACTION_GROUP (actions));

	g_signal_connect (messages_service, "handle-register-application",
			  G_CALLBACK (register_application), NULL);
	g_signal_connect (messages_service, "handle-unregister-application",
			  G_CALLBACK (unregister_application), NULL);
	g_signal_connect (messages_service, "handle-application-stopped-running",
			  G_CALLBACK (application_stopped_running), NULL);
	g_signal_connect (messages_service, "handle-set-status",
			  G_CALLBACK (set_status), NULL);

	menu = g_menu_new ();
	chat_section = create_status_section ();
	g_menu_append (menu, _("Clear"), "clear");

	toplevel_menu = g_menu_new ();

	settings = g_settings_new ("com.canonical.indicator.messages");

	applications = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

	g_idle_add(build_launchers, NULL);

	g_main_loop_run(mainloop);

	/* Clean up */
	g_object_unref (messages_service);
	g_object_unref (chat_section);
	g_object_unref (settings);
	g_hash_table_unref (applications);
	return 0;
}
