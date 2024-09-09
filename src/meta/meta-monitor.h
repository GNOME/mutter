/*
 * Copyright (C) 2025 Red Hat
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

#define META_TYPE_MONITOR (meta_monitor_get_type ())
META_EXPORT
G_DECLARE_DERIVABLE_TYPE (MetaMonitor, meta_monitor, META, MONITOR, GObject)

META_EXPORT
gboolean meta_monitor_is_active (MetaMonitor *monitor);

META_EXPORT
const char * meta_monitor_get_display_name (MetaMonitor *monitor);

META_EXPORT
const char * meta_monitor_get_connector (MetaMonitor *monitor);

META_EXPORT
const char * meta_monitor_get_vendor (MetaMonitor *monitor);

META_EXPORT
const char * meta_monitor_get_product (MetaMonitor *monitor);

META_EXPORT
const char * meta_monitor_get_serial (MetaMonitor *monitor);

META_EXPORT
gboolean meta_monitor_is_primary (MetaMonitor *monitor);

META_EXPORT
gboolean meta_monitor_is_builtin (MetaMonitor *monitor);

META_EXPORT
gboolean meta_monitor_is_virtual (MetaMonitor *monitor);

META_EXPORT
MetaBacklight * meta_monitor_get_backlight (MetaMonitor *monitor);
