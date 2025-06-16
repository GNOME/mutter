/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat
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

#define META_TYPE_LOGICAL_MONITOR (meta_logical_monitor_get_type ())
META_EXPORT
G_DECLARE_FINAL_TYPE (MetaLogicalMonitor,
                      meta_logical_monitor,
                      META, LOGICAL_MONITOR,
                      GObject)

META_EXPORT
GList * meta_logical_monitor_get_monitors (MetaLogicalMonitor *logical_monitor);

META_EXPORT
int meta_logical_monitor_get_number (MetaLogicalMonitor *logical_monitor);
