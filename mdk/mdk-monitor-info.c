/*
 * Copyright (C) 2025 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "mdk-monitor-info.h"

MdkMonitorMode *
mdk_monitor_mode_new (int    width,
                      int    height,
                      double preferred_scale)
{
  MdkMonitorMode *monitor_mode;

  monitor_mode = g_new0 (MdkMonitorMode, 1);
  monitor_mode->width = width;
  monitor_mode->height = height;
  monitor_mode->preferred_scale = preferred_scale;

  return monitor_mode;
}

MdkMonitorMode *
mdk_monitor_mode_dup (MdkMonitorMode *monitor_mode)
{
  return g_memdup2 (monitor_mode, sizeof *monitor_mode);
}

void
mdk_monitor_mode_free (MdkMonitorMode *monitor_mode)
{
  g_free (monitor_mode);
}

MdkMonitorInfo *
mdk_monitor_info_new (GList *modes)
{
  MdkMonitorInfo *monitor_info;

  monitor_info = g_new0 (MdkMonitorInfo, 1);
  monitor_info->modes =
    g_list_copy_deep (modes, (GCopyFunc) mdk_monitor_mode_dup, NULL);

  return monitor_info;
}

void
mdk_monitor_info_free (MdkMonitorInfo *monitor_info)
{
  g_clear_list (&monitor_info->modes, (GDestroyNotify) mdk_monitor_mode_free);
}
