/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright 2013 Red Hat, Inc.
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <glib-object.h>

#include "meta/types.h"

#define META_TYPE_IDLE_MONITOR (meta_idle_monitor_get_type ())
META_EXPORT
G_DECLARE_FINAL_TYPE (MetaIdleMonitor, meta_idle_monitor,
                      META, IDLE_MONITOR,
                      GObject)

typedef void (*MetaIdleMonitorWatchFunc) (MetaIdleMonitor *monitor,
                                          guint            watch_id,
                                          gpointer         user_data);

META_EXPORT
guint         meta_idle_monitor_add_idle_watch        (MetaIdleMonitor          *monitor,
						       guint64                   interval_msec,
						       MetaIdleMonitorWatchFunc  callback,
						       gpointer                  user_data,
						       GDestroyNotify            notify);

META_EXPORT
guint         meta_idle_monitor_add_user_active_watch (MetaIdleMonitor          *monitor,
						       MetaIdleMonitorWatchFunc  callback,
						       gpointer                  user_data,
						       GDestroyNotify            notify);

META_EXPORT
void          meta_idle_monitor_remove_watch          (MetaIdleMonitor          *monitor,
						       guint                     id);

META_EXPORT
gint64        meta_idle_monitor_get_idletime          (MetaIdleMonitor          *monitor);
