/*
 * Copyright 2012 Canonical Ltd.
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

#include "im-application-list.h"

#include "indicator-messages-application.h"
#include "gactionmuxer.h"

#include <gio/gdesktopappinfo.h>
#include <string.h>

typedef GObjectClass ImApplicationListClass;

struct _ImApplicationList
{
  GObject parent;

  GHashTable *applications;
  GActionMuxer *muxer;
};

G_DEFINE_TYPE (ImApplicationList, im_application_list, G_TYPE_OBJECT);

enum
{
  SOURCE_ADDED,
  SOURCE_CHANGED,
  SOURCE_REMOVED,
  MESSAGE_ADDED,
  MESSAGE_REMOVED,
  APP_ADDED,
  APP_STOPPED,
  REMOVE_ALL,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

typedef struct
{
  ImApplicationList *list;
  GDesktopAppInfo *info;
  gchar *id;
  IndicatorMessagesApplication *proxy;
  GActionMuxer *muxer;
  GSimpleActionGroup *actions;
  GSimpleActionGroup *source_actions;
  GSimpleActionGroup *message_actions;
  GActionMuxer *message_sub_actions;
  GCancellable *cancellable;
} Application;

static void
application_free (gpointer data)
{
  Application *app = data;

  if (!app)
    return;

  g_object_unref (app->info);
  g_free (app->id);

  if (app->cancellable)
    {
      g_cancellable_cancel (app->cancellable);
      g_clear_object (&app->cancellable);
    }

  if (app->proxy)
    g_object_unref (app->proxy);

  if (app->muxer)
    {
      g_object_unref (app->muxer);
      g_object_unref (app->source_actions);
      g_object_unref (app->message_actions);
      g_object_unref (app->message_sub_actions);
    }

  g_slice_free (Application, app);
}

static void
im_application_list_source_removed (Application *app,
                                    const gchar *id)
{
  g_simple_action_group_remove (app->source_actions, id);

  g_signal_emit (app->list, signals[SOURCE_REMOVED], 0, app->id, id);
}

static void
im_application_list_source_activated (GSimpleAction *action,
                                      GVariant      *parameter,
                                      gpointer       user_data)
{
  Application *app = user_data;
  const gchar *source_id;

  source_id = g_action_get_name (G_ACTION (action));

  if (g_variant_get_boolean (parameter))
    {
      indicator_messages_application_call_activate_source (app->proxy,
                                                           source_id,
                                                           app->cancellable,
                                                           NULL, NULL);
    }
  else
    {
      const gchar *sources[] = { source_id, NULL };
      const gchar *messages[] = { NULL };
      indicator_messages_application_call_dismiss (app->proxy, sources, messages,
                                                   app->cancellable, NULL, NULL);
    }

  im_application_list_source_removed (app, source_id);
}

static guint
g_action_group_get_n_actions (GActionGroup *group)
{
  guint len;
  gchar **actions;

  actions = g_action_group_list_actions (group);
  len = g_strv_length (actions);

  g_strfreev (actions);
  return len;
}

static gboolean
application_draws_attention (gpointer key,
                             gpointer value,
                             gpointer user_data)
{
  Application *app = value;

  return (g_action_group_get_n_actions (G_ACTION_GROUP (app->source_actions)) +
          g_action_group_get_n_actions (G_ACTION_GROUP (app->message_actions))) > 0;
}

static void
im_application_list_update_draws_attention (ImApplicationList *list)
{
  const gchar *icon_name;
  GVariant *state;
  GActionGroup *main_actions;

  if (g_hash_table_find (list->applications, application_draws_attention, NULL))
    icon_name = "indicator-messages-new";
  else
    icon_name = "indicator-messages";

  main_actions = g_action_muxer_get_group (list->muxer, NULL);
  state = g_variant_new ("(sssb)", "", icon_name, "Messages", TRUE);
  g_action_group_change_action_state (main_actions, "messages", state);
}

static void
im_application_list_message_removed (Application *app,
                                     const gchar *id)
{
  g_simple_action_group_remove (app->message_actions, id);
  g_action_muxer_remove (app->message_sub_actions, id);

  im_application_list_update_draws_attention (app->list);

  g_signal_emit (app->list, signals[MESSAGE_REMOVED], 0, app->id, id);
}

static void
im_application_list_message_activated (GSimpleAction *action,
                                       GVariant      *parameter,
                                       gpointer       user_data)
{
  Application *app = user_data;
  const gchar *message_id;

  message_id = g_action_get_name (G_ACTION (action));

  if (g_variant_get_boolean (parameter))
    {
      indicator_messages_application_call_activate_message (app->proxy,
                                                            message_id,
                                                            "",
                                                            g_variant_new_array (G_VARIANT_TYPE_VARIANT, NULL, 0),
                                                            app->cancellable,
                                                            NULL, NULL);
    }
  else
    {
      const gchar *sources[] = { NULL };
      const gchar *messages[] = { message_id, NULL };
      indicator_messages_application_call_dismiss (app->proxy, sources, messages,
                                                   app->cancellable, NULL, NULL);
    }

  im_application_list_message_removed (app, message_id);
}

static void
im_application_list_sub_message_activated (GSimpleAction *action,
                                           GVariant      *parameter,
                                           gpointer       user_data)
{
  Application *app = user_data;
  const gchar *message_id;
  const gchar *action_id;
  GVariantBuilder builder;

  message_id = g_object_get_data (G_OBJECT (action), "message");
  action_id = g_action_get_name (G_ACTION (action));

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("av"));
  if (parameter)
    g_variant_builder_add (&builder, "v", parameter);

  indicator_messages_application_call_activate_message (app->proxy,
                                                        message_id,
                                                        action_id,
                                                        g_variant_builder_end (&builder),
                                                        app->cancellable,
                                                        NULL, NULL);

  im_application_list_message_removed (app, message_id);
}


static void
im_application_list_remove_all (GSimpleAction *action,
                                GVariant      *parameter,
                                gpointer       user_data)
{
  ImApplicationList *list = user_data;
  GHashTableIter iter;
  Application *app;

  g_signal_emit (list, signals[REMOVE_ALL], 0);

  g_hash_table_iter_init (&iter, list->applications);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &app))
    {
      gchar **source_actions;
      gchar **message_actions;
      gchar **it;

      source_actions = g_action_group_list_actions (G_ACTION_GROUP (app->source_actions));
      for (it = source_actions; *it; it++)
        im_application_list_source_removed (app, *it);

      message_actions = g_action_group_list_actions (G_ACTION_GROUP (app->message_actions));
      for (it = message_actions; *it; it++)
        im_application_list_message_removed (app, *it);

      indicator_messages_application_call_dismiss (app->proxy, 
                                                   (const gchar * const *) source_actions,
                                                   (const gchar * const *) message_actions,
                                                   app->cancellable, NULL, NULL);

      g_strfreev (source_actions);
      g_strfreev (message_actions);
    }
}

static void
im_application_list_dispose (GObject *object)
{
  ImApplicationList *list = IM_APPLICATION_LIST (object);

  g_clear_pointer (&list->applications, g_hash_table_unref);
  g_clear_object (&list->muxer);

  G_OBJECT_CLASS (im_application_list_parent_class)->dispose (object);
}

static void
im_application_list_finalize (GObject *object)
{
  G_OBJECT_CLASS (im_application_list_parent_class)->finalize (object);
}

static void
im_application_list_class_init (ImApplicationListClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = im_application_list_dispose;
  object_class->finalize = im_application_list_finalize;

  signals[SOURCE_ADDED] = g_signal_new ("source-added",
                                        IM_TYPE_APPLICATION_LIST,
                                        G_SIGNAL_RUN_FIRST,
                                        0,
                                        NULL, NULL,
                                        g_cclosure_marshal_generic,
                                        G_TYPE_NONE,
                                        4,
                                        G_TYPE_STRING,
                                        G_TYPE_STRING,
                                        G_TYPE_STRING,
                                        G_TYPE_STRING);

  signals[SOURCE_CHANGED] = g_signal_new ("source-changed",
                                          IM_TYPE_APPLICATION_LIST,
                                          G_SIGNAL_RUN_FIRST,
                                          0,
                                          NULL, NULL,
                                          g_cclosure_marshal_generic,
                                          G_TYPE_NONE,
                                          4,
                                          G_TYPE_STRING,
                                          G_TYPE_STRING,
                                          G_TYPE_STRING,
                                          G_TYPE_STRING);

  signals[SOURCE_REMOVED] = g_signal_new ("source-removed",
                                          IM_TYPE_APPLICATION_LIST,
                                          G_SIGNAL_RUN_FIRST,
                                          0,
                                          NULL, NULL,
                                          g_cclosure_marshal_generic,
                                          G_TYPE_NONE,
                                          2,
                                          G_TYPE_STRING,
                                          G_TYPE_STRING);

  signals[MESSAGE_ADDED] = g_signal_new ("message-added",
                                         IM_TYPE_APPLICATION_LIST,
                                         G_SIGNAL_RUN_FIRST,
                                         0,
                                         NULL, NULL,
                                         g_cclosure_marshal_generic,
                                         G_TYPE_NONE,
                                         10,
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_VARIANT,
                                         G_TYPE_INT64,
                                         G_TYPE_BOOLEAN);

  signals[MESSAGE_REMOVED] = g_signal_new ("message-removed",
                                           IM_TYPE_APPLICATION_LIST,
                                           G_SIGNAL_RUN_FIRST,
                                           0,
                                           NULL, NULL,
                                           g_cclosure_marshal_generic,
                                           G_TYPE_NONE,
                                           2,
                                           G_TYPE_STRING,
                                           G_TYPE_STRING);

  signals[APP_ADDED] = g_signal_new ("app-added",
                                     IM_TYPE_APPLICATION_LIST,
                                     G_SIGNAL_RUN_FIRST,
                                     0,
                                     NULL, NULL,
                                     g_cclosure_marshal_generic,
                                     G_TYPE_NONE,
                                     2,
                                     G_TYPE_STRING,
                                     G_TYPE_DESKTOP_APP_INFO);

  signals[APP_STOPPED] = g_signal_new ("app-stopped",
                                       IM_TYPE_APPLICATION_LIST,
                                       G_SIGNAL_RUN_FIRST,
                                       0,
                                       NULL, NULL,
                                       g_cclosure_marshal_VOID__OBJECT,
                                       G_TYPE_NONE,
                                       1,
                                       G_TYPE_STRING);

  signals[REMOVE_ALL] = g_signal_new ("remove-all",
                                      IM_TYPE_APPLICATION_LIST,
                                      G_SIGNAL_RUN_FIRST,
                                      0,
                                      NULL, NULL,
                                      g_cclosure_marshal_VOID__VOID,
                                      G_TYPE_NONE,
                                      0);
}

static void
im_application_list_init (ImApplicationList *list)
{
  const GActionEntry action_entries[] = {
    { "messages", NULL, NULL, "('', 'indicator-messages', 'Messages', true)", NULL },
    { "remove-all", im_application_list_remove_all }
  };

  GSimpleActionGroup *actions;

  list->applications = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, application_free);

  actions = g_simple_action_group_new ();
  g_simple_action_group_add_entries (actions, action_entries, G_N_ELEMENTS (action_entries), list);

  list->muxer = g_action_muxer_new ();
  g_action_muxer_insert (list->muxer, NULL, G_ACTION_GROUP (actions));

  g_object_unref (actions);
}

ImApplicationList *
im_application_list_new (void)
{
  return g_object_new (IM_TYPE_APPLICATION_LIST, NULL);
}

static gchar *
im_application_list_canonical_id (const gchar *id)
{
  gchar *str;
  gchar *p;
  int len;

  len = strlen (id);
  if (g_str_has_suffix (id, ".desktop"))
    len -= 8;

  str = g_strndup (id, len);

  for (p = str; *p; p++)
    {
      if (*p == '.')
        *p = '_';
    }

  return str;
}

static Application *
im_application_list_lookup (ImApplicationList *list,
                            const gchar       *desktop_id)
{
  gchar *id;
  Application *app;

  id = im_application_list_canonical_id (desktop_id);
  app = g_hash_table_lookup (list->applications, id);

  g_free (id);
  return app;
}

void
im_application_list_activate_launch (GSimpleAction *action,
                                     GVariant      *parameter,
                                     gpointer       user_data)
{
  Application *app = user_data;
  GError *error = NULL;

  if (!g_app_info_launch (G_APP_INFO (app->info), NULL, NULL, &error))
    {
      g_warning ("unable to launch application: %s", error->message);
      g_error_free (error);
    }
}

void
im_application_list_activate_app_action (GSimpleAction *action,
                                         GVariant      *parameter,
                                         gpointer       user_data)
{
  Application *app = user_data;

  g_desktop_app_info_launch_action (app->info, g_action_get_name (G_ACTION (action)), NULL);
}

void
im_application_list_add (ImApplicationList  *list,
                         const gchar        *desktop_id)
{
  GDesktopAppInfo *info;
  Application *app;
  const gchar *id;
  GSimpleActionGroup *actions;
  GSimpleAction *launch_action;

  g_return_if_fail (IM_IS_APPLICATION_LIST (list));
  g_return_if_fail (desktop_id != NULL);

  if (im_application_list_lookup (list, desktop_id))
    return;

  info = g_desktop_app_info_new (desktop_id);
  if (!info)
    {
      g_warning ("an application with id '%s' is not installed", desktop_id);
      return;
    }

  id = g_app_info_get_id (G_APP_INFO (info));
  g_return_if_fail (id != NULL);

  app = g_slice_new0 (Application);
  app->info = info;
  app->id = im_application_list_canonical_id (id);
  app->list = list;
  app->muxer = g_action_muxer_new ();
  app->source_actions = g_simple_action_group_new ();
  app->message_actions = g_simple_action_group_new ();
  app->message_sub_actions = g_action_muxer_new ();

  actions = g_simple_action_group_new ();

  launch_action = g_simple_action_new_stateful ("launch", NULL, g_variant_new_boolean (FALSE));
  g_signal_connect (launch_action, "activate", G_CALLBACK (im_application_list_activate_launch), app);
  g_action_map_add_action (G_ACTION_MAP (actions), G_ACTION (launch_action));

  {
    const gchar *const *app_actions;

    for (app_actions = g_desktop_app_info_list_actions (app->info); *app_actions; app_actions++)
      {
        GSimpleAction *action;

        action = g_simple_action_new (*app_actions, NULL);
        g_signal_connect (action, "activate", G_CALLBACK (im_application_list_activate_app_action), app);
        g_action_map_add_action (G_ACTION_MAP (actions), G_ACTION (action));

        g_object_unref (action);
      }
  }

  g_action_muxer_insert (app->muxer, NULL, G_ACTION_GROUP (actions));
  g_action_muxer_insert (app->muxer, "src", G_ACTION_GROUP (app->source_actions));
  g_action_muxer_insert (app->muxer, "msg", G_ACTION_GROUP (app->message_actions));
  g_action_muxer_insert (app->muxer, "msg-actions", G_ACTION_GROUP (app->message_sub_actions));

  g_hash_table_insert (list->applications, (gpointer) app->id, app);
  g_action_muxer_insert (list->muxer, app->id, G_ACTION_GROUP (app->muxer));

  g_signal_emit (app->list, signals[APP_ADDED], 0, app->id, app->info);

  g_object_unref (launch_action);
  g_object_unref (actions);
}

void
im_application_list_remove (ImApplicationList *list,
                            const gchar       *id)
{
  Application *app;

  g_return_if_fail (IM_IS_APPLICATION_LIST (list));

  app = im_application_list_lookup (list, id);
  if (app)
    {
      if (app->proxy || app->cancellable)
        g_signal_emit (app->list, signals[APP_STOPPED], 0, app->id);

      g_hash_table_remove (list->applications, id);
      g_action_muxer_remove (list->muxer, id);
    }
}

static void
im_application_list_source_added (Application *app,
                                  guint        position,
                                  GVariant    *source)
{
  const gchar *id;
  const gchar *label;
  const gchar *iconstr;
  guint32 count;
  gint64 time;
  const gchar *string;
  gboolean draws_attention;
  GVariant *state;
  GSimpleAction *action;

  g_variant_get (source, "(&s&s&sux&sb)",
                 &id, &label, &iconstr, &count, &time, &string, &draws_attention);

  state = g_variant_new ("(uxsb)", count, time, string, draws_attention);
  action = g_simple_action_new_stateful (id, G_VARIANT_TYPE_BOOLEAN, state);
  g_signal_connect (action, "activate", G_CALLBACK (im_application_list_source_activated), app);

  g_simple_action_group_insert (app->source_actions, G_ACTION (action));

  g_signal_emit (app->list, signals[SOURCE_ADDED], 0, app->id, id, label, iconstr);

  g_object_unref (action);
}

static void
im_application_list_source_changed (Application *app,
                                    GVariant    *source)
{
  const gchar *id;
  const gchar *label;
  const gchar *iconstr;
  guint32 count;
  gint64 time;
  const gchar *string;
  gboolean draws_attention;

  g_variant_get (source, "(&s&s&sux&sb)",
                 &id, &label, &iconstr, &count, &time, &string, &draws_attention);

  g_action_group_change_action_state (G_ACTION_GROUP (app->source_actions), id,
                                      g_variant_new ("(uxsb)", count, time, string, draws_attention));

  g_signal_emit (app->list, signals[SOURCE_CHANGED], 0, app->id, id, label, iconstr);
}

static void
im_application_list_sources_listed (GObject      *source_object,
                                    GAsyncResult *result,
                                    gpointer      user_data)
{
  Application *app = user_data;
  GVariant *sources;
  GError *error = NULL;

  if (indicator_messages_application_call_list_sources_finish (app->proxy, &sources, result, &error))
    {
      GVariantIter iter;
      GVariant *source;
      guint i = 0;

      g_variant_iter_init (&iter, sources);
      while ((source = g_variant_iter_next_value (&iter)))
        {
          im_application_list_source_added (app, i++, source);
          g_variant_unref (source);
        }

      g_variant_unref (sources);
    }
  else
    {
      g_warning ("could not fetch the list of sources: %s", error->message);
      g_error_free (error);
    }
}

static gchar *
get_symbolic_app_icon_string (GIcon *icon)
{
  const gchar * const *names;
  gchar *symbolic_name;
  GIcon *symbolic_icon;
  gchar *str;

  if (!G_IS_THEMED_ICON (icon))
    return NULL;

  names = g_themed_icon_get_names (G_THEMED_ICON (icon));
  if (!names || !names[0])
    return NULL;

  symbolic_icon = g_themed_icon_new_from_names ((gchar **) names, -1);

  symbolic_name = g_strconcat (names[0], "-symbolic", NULL);
  g_themed_icon_prepend_name (G_THEMED_ICON (symbolic_icon), symbolic_name);

  str = g_icon_to_string (symbolic_icon);

  g_free (symbolic_name);
  g_object_unref (symbolic_icon);
  return str;
}

static void
im_application_list_message_added (Application *app,
                                   GVariant    *message)
{
  const gchar *id;
  const gchar *iconstr;
  const gchar *title;
  const gchar *subtitle;
  const gchar *body;
  gint64 time;
  GVariantIter *action_iter;
  gboolean draws_attention;
  GSimpleAction *action;
  GIcon *app_icon;
  gchar *app_iconstr = NULL;
  GVariant *actions = NULL;

  g_variant_get (message, "(&s&s&s&s&sxaa{sv}b)",
                 &id, &iconstr, &title, &subtitle, &body, &time, &action_iter, &draws_attention);

  app_icon = g_app_info_get_icon (G_APP_INFO (app->info));
  if (app_icon)
    app_iconstr = get_symbolic_app_icon_string (app_icon);

  action = g_simple_action_new (id, G_VARIANT_TYPE_BOOLEAN);
  g_signal_connect (action, "activate", G_CALLBACK (im_application_list_message_activated), app);
  g_simple_action_group_insert (app->message_actions, G_ACTION (action));

  {
    GVariant *entry;
    GSimpleActionGroup *action_group;
    GVariantBuilder actions_builder;

    g_variant_builder_init (&actions_builder, G_VARIANT_TYPE ("aa{sv}"));
    action_group = g_simple_action_group_new ();

    while ((entry = g_variant_iter_next_value (action_iter)))
      {
        const gchar *name;
        GSimpleAction *action;
        GVariant *label;
        const gchar *type = NULL;
        GVariant *hint;
        GVariantBuilder dict_builder;
        gchar *prefixed_name;

        if (!g_variant_lookup (entry, "name", "&s", &name))
          {
            g_warning ("action dictionary for message '%s' is missing 'name' key", id);
            continue;
          }

        label = g_variant_lookup_value (entry, "label", G_VARIANT_TYPE_STRING);
        g_variant_lookup (entry, "parameter-type", "&g", &type);
        hint = g_variant_lookup_value (entry, "parameter-hint", NULL);

        action = g_simple_action_new (name, type ? G_VARIANT_TYPE (type) : NULL);
        g_object_set_data_full (G_OBJECT (action), "message", g_strdup (id), g_free);
        g_signal_connect (action, "activate", G_CALLBACK (im_application_list_sub_message_activated), app);
        g_simple_action_group_insert (action_group, G_ACTION (action));

        g_variant_builder_init (&dict_builder, G_VARIANT_TYPE ("a{sv}"));

        prefixed_name = g_strjoin (".", "indicator", app->id, "msg-actions", id, name, NULL);
        g_variant_builder_add (&dict_builder, "{sv}", "name", g_variant_new_string (prefixed_name));

        if (label)
          {
            g_variant_builder_add (&dict_builder, "{sv}", "label", label);
            g_variant_unref (label);
          }

        if (type)
          g_variant_builder_add (&dict_builder, "{sv}", "parameter-type", g_variant_new_string (type));

        if (hint)
          {
            g_variant_builder_add (&dict_builder, "{sv}", "parameter-hint", hint);
            g_variant_unref (hint);
          }

        g_variant_builder_add (&actions_builder, "a{sv}", &dict_builder);

        g_object_unref (action);
        g_variant_unref (entry);
        g_free (prefixed_name);
      }

    g_action_muxer_insert (app->message_sub_actions, id, G_ACTION_GROUP (action_group));
    actions = g_variant_builder_end (&actions_builder);

    g_object_unref (action_group);
  }

  im_application_list_update_draws_attention (app->list);

  g_signal_emit (app->list, signals[MESSAGE_ADDED], 0,
                 app->id, app_iconstr, id, iconstr, title,
                 subtitle, body, actions, time, draws_attention);

  g_variant_iter_free (action_iter);
  g_free (app_iconstr);
  g_object_unref (action);
}

static void
im_application_list_messages_listed (GObject      *source_object,
                                     GAsyncResult *result,
                                     gpointer      user_data)
{
  Application *app = user_data;
  GVariant *messages;
  GError *error = NULL;

  if (indicator_messages_application_call_list_messages_finish (app->proxy, &messages, result, &error))
    {
      GVariantIter iter;
      GVariant *message;

      g_variant_iter_init (&iter, messages);
      while ((message = g_variant_iter_next_value (&iter)))
        {
          im_application_list_message_added (app, message);
          g_variant_unref (message);
        }

      g_variant_unref (messages);
    }
  else
    {
      g_warning ("could not fetch the list of messages: %s", error->message);
      g_error_free (error);
    }
}

static void
im_application_list_unset_remote (Application *app)
{
  gboolean was_running;

  was_running = app->proxy || app->cancellable;

  if (app->cancellable)
    {
      g_cancellable_cancel (app->cancellable);
      g_clear_object (&app->cancellable);
    }
  g_clear_object (&app->proxy);

  /* clear actions by creating a new action group and overriding it in
   * the muxer */
  g_object_unref (app->source_actions);
  g_object_unref (app->message_actions);
  g_object_unref (app->message_sub_actions);
  app->source_actions = g_simple_action_group_new ();
  app->message_actions = g_simple_action_group_new ();
  app->message_sub_actions = g_action_muxer_new ();
  g_action_muxer_insert (app->muxer, "src", G_ACTION_GROUP (app->source_actions));
  g_action_muxer_insert (app->muxer, "msg", G_ACTION_GROUP (app->message_actions));
  g_action_muxer_insert (app->muxer, "msg-actions", G_ACTION_GROUP (app->message_sub_actions));

  im_application_list_update_draws_attention (app->list);

  if (was_running)
    g_signal_emit (app->list, signals[APP_STOPPED], 0, app->id);
}

static void
im_application_list_app_vanished (GDBusConnection *connection,
                                  const gchar     *name,
                                  gpointer         user_data)
{
  Application *app = user_data;

  im_application_list_unset_remote (app);
}

static void
im_application_list_proxy_created (GObject      *source_object,
                                   GAsyncResult *result,
                                   gpointer      user_data)
{
  Application *app = user_data;
  GError *error = NULL;

  app->proxy = indicator_messages_application_proxy_new_finish (result, &error);
  if (!app->proxy)
    {
      if (error->code != G_IO_ERROR_CANCELLED)
        g_warning ("could not create application proxy: %s", error->message);
      g_error_free (error);
      return;
    }

  indicator_messages_application_call_list_sources (app->proxy, app->cancellable,
                                                    im_application_list_sources_listed, app);
  indicator_messages_application_call_list_messages (app->proxy, app->cancellable,
                                                     im_application_list_messages_listed, app);

  g_signal_connect_swapped (app->proxy, "source-added", G_CALLBACK (im_application_list_source_added), app);
  g_signal_connect_swapped (app->proxy, "source-changed", G_CALLBACK (im_application_list_source_changed), app);
  g_signal_connect_swapped (app->proxy, "source-removed", G_CALLBACK (im_application_list_source_removed), app);
  g_signal_connect_swapped (app->proxy, "message-added", G_CALLBACK (im_application_list_message_added), app);
  g_signal_connect_swapped (app->proxy, "message-removed", G_CALLBACK (im_application_list_message_removed), app);

  g_bus_watch_name_on_connection (g_dbus_proxy_get_connection (G_DBUS_PROXY (app->proxy)),
                                  g_dbus_proxy_get_name (G_DBUS_PROXY (app->proxy)),
                                  G_BUS_NAME_WATCHER_FLAGS_NONE,
                                  NULL, im_application_list_app_vanished,
                                  app, NULL);
}

void
im_application_list_set_remote (ImApplicationList *list,
                                const gchar       *id,
                                GDBusConnection   *connection,
                                const gchar       *unique_bus_name,
                                const gchar       *object_path)
{
  Application *app;

  g_return_if_fail (IM_IS_APPLICATION_LIST (list));

  app = im_application_list_lookup (list, id);
  if (!app)
    {
      g_warning ("'%s' is not a registered application", id);
      return;
    }

  if (app->cancellable)
    {
      gchar *name_owner = NULL;

      if (app->proxy)
        name_owner = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (app->proxy));
      g_warning ("replacing '%s' at %s with %s", id, name_owner, unique_bus_name);

      im_application_list_unset_remote (app);

      g_free (name_owner);
    }

  app->cancellable = g_cancellable_new ();
  indicator_messages_application_proxy_new (connection, G_DBUS_PROXY_FLAGS_NONE,
                                            unique_bus_name, object_path, app->cancellable,
                                            im_application_list_proxy_created, app);
}

GActionGroup *
im_application_list_get_action_group (ImApplicationList *list)
{
  g_return_val_if_fail (IM_IS_APPLICATION_LIST (list), NULL);

  return G_ACTION_GROUP (list->muxer);
}

GList *
im_application_list_get_applications (ImApplicationList *list)
{
  g_return_val_if_fail (IM_IS_APPLICATION_LIST (list), NULL);

  return g_hash_table_get_keys (list->applications);
}

GDesktopAppInfo *
im_application_list_get_application (ImApplicationList *list,
                                     const gchar       *id)
{
  Application *app;

  g_return_val_if_fail (IM_IS_APPLICATION_LIST (list), NULL);

  app = g_hash_table_lookup (list->applications, id);
  return app ? app->info : NULL;
}
