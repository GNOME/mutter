/*
 * Copyright (C) 2018 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#ifndef META_WINDOW_LIST_H
#define META_WINDOW_LIST_H

#include <glib-object.h>

#include "backends/meta-dbus-session-watcher.h"
#include "meta-dbus-window-list.h"

typedef struct _MetaWindowListSession MetaWindowListSession;

#define META_TYPE_WINDOW_LIST (meta_window_list_get_type ())
G_DECLARE_FINAL_TYPE (MetaWindowList, meta_window_list,
                      META, WINDOW_LIST,
                      MetaDBusWindowListSkeleton)

MetaWindowListSession * meta_window_list_get_session (MetaWindowList *window_list,
                                                      const char     *session_id);

GDBusConnection * meta_window_list_get_connection (MetaWindowList *window_list);

MetaWindowList * meta_window_list_new (MetaDbusSessionWatcher *session_watcher);

#endif /* META_WINDOW_LIST_H */
