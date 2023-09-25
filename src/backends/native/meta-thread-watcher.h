/*
 * Copyright (C) 2023 Red Hat
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the watcheried warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <glib-object.h>

#define META_TYPE_THREAD_WATCHER (meta_thread_watcher_get_type ())
G_DECLARE_FINAL_TYPE (MetaThreadWatcher, meta_thread_watcher,
                      META, THREAD_WATCHER, GObject)

MetaThreadWatcher *meta_thread_watcher_new (void);
void meta_thread_watcher_attach (MetaThreadWatcher *self,
                                 GMainContext      *context);
void meta_thread_watcher_detach (MetaThreadWatcher *self);
gboolean meta_thread_watcher_start (MetaThreadWatcher  *watcher,
                                    int                 interval_us,
                                    GError            **error);
gboolean meta_thread_watcher_is_started (MetaThreadWatcher *watcher);
gboolean meta_thread_watcher_reset (MetaThreadWatcher  *watcher,
                                    GError            **error);
void meta_thread_watcher_stop (MetaThreadWatcher *watcher);
