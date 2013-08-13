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

#include "im-desktop-menu.h"

typedef ImMenuClass ImDesktopMenuClass;

struct _ImDesktopMenu
{
  ImMenu parent;

  GHashTable *source_sections;
};

G_DEFINE_TYPE (ImDesktopMenu, im_desktop_menu, IM_TYPE_MENU);

static void
im_desktop_menu_app_added (ImApplicationList *applist,
                           const gchar       *app_id,
                           GDesktopAppInfo   *app_info,
                           gpointer           user_data)
{
  ImDesktopMenu *menu = user_data;
  GMenu *section;
  GMenu *app_section;
  GMenu *source_section;
  gchar *namespace;

  app_section = g_menu_new ();

  /* application launcher */
  {
    GMenuItem *item;
    GVariant *icon;

    item = g_menu_item_new (g_app_info_get_name (G_APP_INFO (app_info)), "launch");
    g_menu_item_set_attribute (item, "x-canonical-type", "s", "com.canonical.application");

    icon = g_icon_serialize (g_app_info_get_icon (G_APP_INFO (app_info)));
    if (icon)
      {
        g_menu_item_set_attribute_value (item, "x-canonical-icon", icon);
        g_variant_unref (icon);
      }

    g_menu_append_item (app_section, item);

    g_object_unref (item);
  }

  /* TODO application actions */

  source_section = g_menu_new ();

  section = g_menu_new ();
  g_menu_append_section (section, NULL, G_MENU_MODEL (app_section));
  g_menu_append_section (section, NULL, G_MENU_MODEL (source_section));

  namespace = g_strconcat ("indicator.", app_id, NULL);
  im_menu_insert_section (IM_MENU (menu), -1, namespace, G_MENU_MODEL (section));
  g_hash_table_insert (menu->source_sections, g_strdup (app_id), source_section);

  g_free (namespace);
  g_object_unref (section);
  g_object_unref (app_section);
}

static void
im_desktop_menu_remove_all (ImApplicationList *applist,
                            gpointer           user_data)
{
  ImDesktopMenu *menu = user_data;
  GHashTableIter it;
  GMenu *section;

  g_hash_table_iter_init (&it, menu->source_sections);
  while (g_hash_table_iter_next (&it, NULL, (gpointer *) &section))
    {
      while (g_menu_model_get_n_items (G_MENU_MODEL (section)) > 0)
        g_menu_remove (section, 0);
    }
}

static void
im_desktop_menu_constructed (GObject *object)
{
  ImDesktopMenu *menu = IM_DESKTOP_MENU (object);
  ImApplicationList *applist;

  /* TODO: chat section */

  {
    GMenu *clear_section;

    clear_section = g_menu_new ();
    g_menu_append (clear_section, "Clear", "indicator.remove-all");
    im_menu_append_section (IM_MENU (menu), G_MENU_MODEL (clear_section));

    g_object_unref (clear_section);
  }

  applist = im_menu_get_application_list (IM_MENU (menu));

  {
    GList *apps;
    GList *it;

    apps = im_application_list_get_applications (applist);
    for (it = apps; it != NULL; it = it->next)
      {
        const gchar *id = it->data;
        im_desktop_menu_app_added (applist, id, im_application_list_get_application (applist, id), menu);
      }

    g_list_free (apps);
  }


  g_signal_connect (applist, "app-added", G_CALLBACK (im_desktop_menu_app_added), menu);
  g_signal_connect (applist, "remove-all", G_CALLBACK (im_desktop_menu_remove_all), menu);

  G_OBJECT_CLASS (im_desktop_menu_parent_class)->constructed (object);
}

static void
im_desktop_menu_finalize (GObject *object)
{
  ImDesktopMenu *menu = IM_DESKTOP_MENU (object);

  g_hash_table_unref (menu->source_sections);

  G_OBJECT_CLASS (im_desktop_menu_parent_class)->finalize (object);
}

static void
im_desktop_menu_class_init (ImDesktopMenuClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = im_desktop_menu_constructed;
  object_class->finalize = im_desktop_menu_finalize;
}

static void
im_desktop_menu_init (ImDesktopMenu *menu)
{
  menu->source_sections = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
}

ImDesktopMenu *
im_desktop_menu_new (ImApplicationList  *applist)
{
  g_return_val_if_fail (IM_IS_APPLICATION_LIST (applist), NULL);

  return g_object_new (IM_TYPE_DESKTOP_MENU,
                       "application-list", applist,
                       NULL);
}
