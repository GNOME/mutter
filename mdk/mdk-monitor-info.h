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

#pragma once

#include <glib.h>

typedef struct _MdkMonitorMode
{
  int width;
  int height;
  double preferred_scale;
} MdkMonitorMode;

typedef struct _MdkMonitorInfo
{
  GList *modes;
} MdkMonitorInfo;

MdkMonitorMode * mdk_monitor_mode_new (int    width,
                                       int    height,
                                       double preferred_scale);

MdkMonitorMode * mdk_monitor_mode_dup (MdkMonitorMode *monitor_mode);

void mdk_monitor_mode_free (MdkMonitorMode *monitor_mode);

MdkMonitorInfo * mdk_monitor_info_new (GList *modes);

void mdk_monitor_info_free (MdkMonitorInfo *monitor_info);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MdkMonitorMode, mdk_monitor_mode_free)

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MdkMonitorInfo, mdk_monitor_info_free)
