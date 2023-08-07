/*
 * Copyright (C) 2021 Red Hat Inc.
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
 *
 */

#pragma once

#include <glib-object.h>

#include "backends/meta-backend-types.h"
#include "core/util-private.h"

struct _MetaVirtualModeInfo
{
  int width;
  int height;
  float refresh_rate;
};

struct _MetaVirtualMonitorInfo
{
  MetaVirtualModeInfo mode_info;

  char *vendor;
  char *product;
  char *serial;
};

#define META_TYPE_VIRTUAL_MONITOR (meta_virtual_monitor_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaVirtualMonitor, meta_virtual_monitor,
                          META, VIRTUAL_MONITOR,
                          GObject)

struct _MetaVirtualMonitorClass
{
  GObjectClass parent_class;

  void (* set_mode) (MetaVirtualMonitor *virtual_monitor,
                     int                 width,
                     int                 height,
                     float               refresh_rate);
};

META_EXPORT_TEST
MetaVirtualMonitorInfo * meta_virtual_monitor_info_new (int         width,
                                                        int         height,
                                                        float       refresh_rate,
                                                        const char *vendor,
                                                        const char *product,
                                                        const char *serial);

META_EXPORT_TEST
void meta_virtual_monitor_info_free (MetaVirtualMonitorInfo *info);

MetaCrtc * meta_virtual_monitor_get_crtc (MetaVirtualMonitor *virtual_monitor);

META_EXPORT_TEST
MetaCrtcMode * meta_virtual_monitor_get_crtc_mode (MetaVirtualMonitor *virtual_monitor);

META_EXPORT_TEST
MetaOutput * meta_virtual_monitor_get_output (MetaVirtualMonitor *virtual_monitor);

META_EXPORT_TEST
void meta_virtual_monitor_set_mode (MetaVirtualMonitor *virtual_monitor,
                                    int                 width,
                                    int                 height,
                                    float               refresh_rate);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaVirtualMonitorInfo,
                               meta_virtual_monitor_info_free)
