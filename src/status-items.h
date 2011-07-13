/*
Code to build and maintain the status adjustment menuitems.

Copyright 2011 Canonical Ltd.

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

#ifndef __STATUS_ITEMS_H__
#define __STATUS_ITEMS_H__

#include <glib.h>

G_BEGIN_DECLS

typedef void (*StatusUpdateFunc) (void);

GList * status_items_build (StatusUpdateFunc update_func);
const gchar * status_current_panel_icon (gboolean alert);
void status_items_cleanup (void);

G_END_DECLS

#endif /* __STATUS_ITEMS_H__ */

