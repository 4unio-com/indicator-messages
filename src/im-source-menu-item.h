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

#ifndef __IM_SOURCE_MENU_ITEM_H__
#define __IM_SOURCE_MENU_ITEM_H__

#include <gtk/gtk.h>

#define IM_TYPE_SOURCE_MENU_ITEM            (im_source_menu_item_get_type ())
#define IM_SOURCE_MENU_ITEM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), IM_TYPE_SOURCE_MENU_ITEM, ImSourceMenuItem))
#define IM_SOURCE_MENU_ITEM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), IM_TYPE_SOURCE_MENU_ITEM, ImSourceMenuItemClass))
#define IS_IM_SOURCE_MENU_ITEM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), IM_TYPE_SOURCE_MENU_ITEM))
#define IS_IM_SOURCE_MENU_ITEM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), IM_TYPE_SOURCE_MENU_ITEM))
#define IM_SOURCE_MENU_ITEM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), IM_TYPE_SOURCE_MENU_ITEM, ImSourceMenuItemClass))

typedef struct _ImSourceMenuItem        ImSourceMenuItem;
typedef struct _ImSourceMenuItemClass   ImSourceMenuItemClass;
typedef struct _ImSourceMenuItemPrivate ImSourceMenuItemPrivate;

struct _ImSourceMenuItemClass
{
  GtkMenuItemClass parent_class;
};

struct _ImSourceMenuItem
{
  GtkMenuItem parent;
  ImSourceMenuItemPrivate *priv;
};

GType           im_source_menu_item_get_type           (void);

void            im_source_menu_item_set_menu_item      (ImSourceMenuItem *item,
                                                        GMenuItem     *menuitem);
void            im_source_menu_item_set_action_group   (ImSourceMenuItem *self,
                                                        GActionGroup  *action_group);

#endif
