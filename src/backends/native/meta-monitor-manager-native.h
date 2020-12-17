/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2003 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
 * Copyright (C) 2013 Red Hat Inc.
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

#ifndef META_MONITOR_MANAGER_NATIVE_H
#define META_MONITOR_MANAGER_NATIVE_H

#include <xf86drm.h>
#include <xf86drmMode.h>

#include "backends/meta-monitor-manager-private.h"
#include "backends/native/meta-crtc-kms.h"
#include "backends/native/meta-kms-crtc.h"

typedef struct _MetaGpuKms MetaGpuKms;

#define META_TYPE_MONITOR_MANAGER_NATIVE (meta_monitor_manager_native_get_type ())
G_DECLARE_FINAL_TYPE (MetaMonitorManagerNative, meta_monitor_manager_native,
                      META, MONITOR_MANAGER_NATIVE,
                      MetaMonitorManager)

void meta_monitor_manager_native_pause (MetaMonitorManagerNative *manager_native);

void meta_monitor_manager_native_resume (MetaMonitorManagerNative *manager_native);

uint64_t meta_power_save_to_dpms_state (MetaPowerSave power_save);

MetaKmsCrtcGamma * meta_monitor_manager_native_get_cached_crtc_gamma (MetaMonitorManagerNative *manager_native,
                                                                      MetaCrtcKms              *crtc_kms);

#endif /* META_MONITOR_MANAGER_NATIVE_H */
