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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#include "config.h"

#include "backends/native/meta-virtual-monitor-native.h"

#include "backends/native/meta-crtc-mode-virtual.h"
#include "backends/native/meta-crtc-virtual.h"
#include "backends/native/meta-output-virtual.h"

struct _MetaVirtualMonitorNative
{
  MetaVirtualMonitor parent;

  uint64_t id;
};

#define VIRTUAL_OUTPUT_ID_BIT (((uint64_t) 1) << 63)

G_DEFINE_TYPE (MetaVirtualMonitorNative, meta_virtual_monitor_native,
               META_TYPE_VIRTUAL_MONITOR)

uint64_t
meta_virtual_monitor_native_get_id (MetaVirtualMonitorNative *virtual_monitor_native)
{
  return virtual_monitor_native->id;
}

MetaVirtualMonitorNative *
meta_virtual_monitor_native_new (uint64_t                      id,
                                 const MetaVirtualMonitorInfo *info)
{
  MetaVirtualMonitorNative *virtual_monitor_native;
  MetaCrtcVirtual *crtc_virtual;
  MetaCrtcModeVirtual *crtc_mode_virtual;
  MetaOutputVirtual *output_virtual;

  crtc_virtual = meta_crtc_virtual_new (id);
  crtc_mode_virtual = meta_crtc_mode_virtual_new (id, info);
  output_virtual = meta_output_virtual_new (id, info,
                                            crtc_virtual,
                                            crtc_mode_virtual);

  virtual_monitor_native = g_object_new (META_TYPE_VIRTUAL_MONITOR_NATIVE,
                                         "crtc", crtc_virtual,
                                         "crtc-mode", crtc_mode_virtual,
                                         "output", output_virtual,
                                         NULL);
  virtual_monitor_native->id = id;

  return virtual_monitor_native;
}

static void
meta_virtual_monitor_native_init (MetaVirtualMonitorNative *virtual_monitor_native)
{
}

static void
meta_virtual_monitor_native_class_init (MetaVirtualMonitorNativeClass *klass)
{
}
