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

#ifndef META_VIRTUAL_MONITOR_NATIVE_H
#define META_VIRTUAL_MONITOR_NATIVE_H

#include <stdint.h>

#include "backends/meta-virtual-monitor.h"
#include "backends/meta-backend-types.h"

#define META_TYPE_VIRTUAL_MONITOR_NATIVE (meta_virtual_monitor_native_get_type ())
G_DECLARE_FINAL_TYPE (MetaVirtualMonitorNative, meta_virtual_monitor_native,
                      META, VIRTUAL_MONITOR_NATIVE,
                      MetaVirtualMonitor)

uint64_t meta_virtual_monitor_native_get_id (MetaVirtualMonitorNative *virtual_monitor_native);

MetaCrtcMode * meta_virtual_monitor_native_get_crtc_mode (MetaVirtualMonitorNative *virtual_monitor_native);

MetaCrtc * meta_virtual_monitor_native_get_crtc (MetaVirtualMonitorNative *virtual_monitor_native);

MetaOutput * meta_virtual_monitor_native_get_output (MetaVirtualMonitorNative *virtual_monitor_native);

MetaVirtualMonitorNative * meta_virtual_monitor_native_new (uint64_t                      id,
                                                            const MetaVirtualMonitorInfo *info);

#endif /* META_VIRTUAL_MONITOR_NATIVE_H */
