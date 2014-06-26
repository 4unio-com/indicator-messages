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

#ifndef __IM_NOTIFICATIONS_H__
#define __IM_NOTIFICATIONS_H__

#include <gio/gio.h>
#include "im-application-list.h"

#define IM_TYPE_NOTIFICATIONS  (im_notifications_get_type ())
#define IM_NOTIFICATIONS(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), IM_TYPE_NOTIFICATIONS, ImNotifications))
#define IM_IS_NOTIFICATIONS(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), IM_TYPE_NOTIFICATIONS))

typedef struct _ImNotifications ImNotifications;

GType                   im_notifications_get_type                       (void);

ImNotifications *       im_notifications_new                            (ImApplicationList *app_list);

void                    im_notifications_export                         (GDBusConnection *connection,
                                                                         const gchar     *object_path,
                                                                         GError           *error);

#endif
