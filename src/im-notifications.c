/*
 * Copyright 2014 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3, as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranties of
 * MERCHANTABILITY, SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *     Lars Uebernickel <lars.uebernickel@canonical.com>
 */

#include "im-notifications.h"

static gchar *interface_xml = 
  "<node>"
  "  <interface name='com.canonical.Notifications'>"
  "    <method name='AddNotification'>"
  "      <arg type='s' name='application_id' direction='in'/>"
  "      <arg type='s' name='notification_id' direction='in'/>"
  "      <arg type='a{sv}' name='notification' direction='in'/>"
  "    </method>"
  "    <method name='RemoveNotification'>"
  "      <arg type='s' name='application_id' direction='in'/>"
  "      <arg type='s' name='notification_id' direction='in'/>"
  "    </method>"
  "    <signal name='NotificationActivated'>"
  "      <arg type='s' name='application_id' />"
  "      <arg type='s' name='notification_id' />"
  "      <arg type='s' name='action_name' />"
  "      <arg type='av' name='parameter' />"
  "    </signal>"
  "  </interface>"
  "</node>";

typedef GObjectClass ImNotificationsClass;

struct _ImNotifications
{
  GObject parent;

  ImApplicationList *app_list;

  GDBusConnection *connection;
  guint name_owner_id;
  guint object_id;
};

G_DEFINE_TYPE (ImNotifications, im_notifications, G_TYPE_OBJECT);

static void
im_notifications_method_call (GDBusConnection       *connection,
                              const gchar           *sender,
                              const gchar           *object_path,
                              const gchar           *interface_name,
                              const gchar           *method_name,
                              GVariant              *parameters,
                              GDBusMethodInvocation *invocation,
                              gpointer               user_data)
{
  ImNotifications *notifications = user_data;

  if (g_str_equal (method_name, "AddNotification"))
    {
      if (g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(ssa{sv})")))
        {
          const gchar *application_id;
          const gchar *notification_id;
          GVariant *notification;

          g_variant_get (parameters, "(&s&s@a{sv})", &application_id, &notification_id, &notification);
          im_application_list_add_message (notifications->app_list, application_id, notification_id, notification);
          g_dbus_method_invocation_return_value (invocation, NULL);

          g_variant_unref (notification);
        }
    }
  else if (g_str_equal (method_name, "RemoveNotification"))
    {
      if (g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(ss)")))
        {
          const gchar *application_id;
          const gchar *notification_id;

          g_variant_get (parameters, "(&s&s)", &application_id, &notification_id);
          im_application_list_remove_message (notifications->app_list, application_id, notification_id);

          g_dbus_method_invocation_return_value (invocation, NULL);
        }
    }
}

static guint
register_object_from_xml (GDBusConnection             *connection,
                          const gchar                 *object_path,
                          const gchar                 *interface_xml,
                          const gchar                 *interface_name,
                          const GDBusInterfaceVTable  *vtable,
                          gpointer                     user_data,
                          GDestroyNotify               user_data_free_func,
                          GError                     **error)
{
  GDBusNodeInfo *node_info;
  guint id = 0;

  node_info = g_dbus_node_info_new_for_xml (interface_xml, error);
  if (node_info)
    {
      GDBusInterfaceInfo *interface_info;

      interface_info = g_dbus_node_info_lookup_interface (node_info, interface_name);
      if (interface_info)
        {
          id = g_dbus_connection_register_object (connection, object_path, interface_info, vtable,
                                                  user_data, user_data_free_func, error);
        }
      else
        {
          g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_INTERFACE,
                       "%s: 'interface_xml' doesn't contain an interface named '%s'", G_STRFUNC, interface_name);
        }

      g_dbus_node_info_unref (node_info);
    }

  return id;
}

static void
im_notifications_bus_acquired (GDBusConnection *connection,
                               const gchar     *name,
                               gpointer         user_data)
{
  ImNotifications *notifications = user_data;
  const GDBusInterfaceVTable vtable = { im_notifications_method_call };
  GError *error = NULL;

  g_clear_object (&notifications->connection);
  notifications->connection = g_object_ref (connection);
  notifications->object_id = register_object_from_xml (connection, "/com/canonical/Notifications",
                                                       interface_xml, "com.canonical.Notifications",
                                                       &vtable, notifications, NULL, &error);
  if (notifications->object_id == 0)
    {
      g_warning ("ImNotifications: unable to export object: %s", error->message);
      g_error_free (error);
    }
}

static void
im_notifications_init (ImNotifications *notifications)
{
  notifications->name_owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                                 "com.canonical.Notifications",
                                                 G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT,
                                                 im_notifications_bus_acquired, NULL, NULL,
                                                 notifications, NULL);
}

static void
im_notifications_action_activated (ImApplicationList *applist,
                                   const gchar       *application_id,
                                   const gchar       *notification_id,
                                   const gchar       *action,
                                   GVariant          *parameter,
                                   gpointer           user_data)
{
  ImNotifications *notifications = user_data;
  GError *error = NULL;

  if (notifications->connection == NULL)
    return;

  g_dbus_connection_emit_signal (notifications->connection, NULL, "/com/canonical/Notifications",
                                 "com.canonical.Notifications", "NotificationActivated",
                                 g_variant_new ("(sss@av)", application_id,
                                                            notification_id,
                                                            action ? action : "",
                                                            g_variant_new_array (G_VARIANT_TYPE ("v"),
                                                                                 &parameter,
                                                                                 parameter != NULL)),
                                 &error);
}

static void
im_notifications_dispose (GObject *object)
{
  ImNotifications *notifications = IM_NOTIFICATIONS (object);

  if (notifications->app_list)
    {
      g_signal_handlers_disconnect_by_func (notifications->app_list, im_notifications_action_activated, notifications);
      g_clear_object (&notifications->app_list);
    }

  if (notifications->object_id)
    {
      g_dbus_connection_unregister_object (notifications->connection, notifications->object_id);
      notifications->object_id = 0;
    }

  if (notifications->name_owner_id)
    {
      g_bus_unown_name (notifications->name_owner_id);
      notifications->name_owner_id = 0;
    }

  g_clear_object (&notifications->connection);

  G_OBJECT_CLASS (im_notifications_parent_class)->dispose (object);
}

static void
im_notifications_class_init (ImNotificationsClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->dispose = im_notifications_dispose;
}

ImNotifications *
im_notifications_new (ImApplicationList *app_list)
{
  ImNotifications *notifications;

  g_return_val_if_fail (IM_IS_APPLICATION_LIST (app_list), NULL);

  notifications = g_object_new (IM_TYPE_NOTIFICATIONS, NULL);
  notifications->app_list = g_object_ref (app_list);
  g_signal_connect (app_list, "message-activated",
                    G_CALLBACK (im_notifications_action_activated), notifications);

  return notifications;
}
