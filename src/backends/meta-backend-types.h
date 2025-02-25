/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2003 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
 * Copyright (C) 2013-2018 Red Hat Inc.
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

#include <stdint.h>
#include <stddef.h>

typedef struct _MetaBackend MetaBackend;

typedef struct _MetaColorDevice MetaColorDevice;
typedef struct _MetaColorManager MetaColorManager;
typedef struct _MetaColorProfile MetaColorProfile;
typedef struct _MetaColorStore MetaColorStore;

typedef enum _MetaColorMode MetaColorMode;

typedef struct _MetaMonitorManager MetaMonitorManager;

typedef struct _MetaMonitorConfigManager MetaMonitorConfigManager;
typedef struct _MetaMonitorConfigStore MetaMonitorConfigStore;
typedef struct _MetaMonitorsConfig MetaMonitorsConfig;

typedef enum _MetaMonitorsConfigFlag MetaMonitorsConfigFlag;

typedef struct _MetaMonitor MetaMonitor;
typedef struct _MetaMonitorNormal MetaMonitorNormal;
typedef struct _MetaMonitorTiled MetaMonitorTiled;
typedef struct _MetaMonitorSpec MetaMonitorSpec;
typedef struct _MetaLogicalMonitor MetaLogicalMonitor;

typedef struct _MetaMonitorMode MetaMonitorMode;

typedef struct _MetaGpu MetaGpu;

typedef struct _MetaCrtc MetaCrtc;
typedef struct _MetaOutput MetaOutput;
typedef struct _MetaCrtcMode MetaCrtcMode;
typedef struct _MetaCrtcAssignment MetaCrtcAssignment;
typedef struct _MetaOutputAssignment MetaOutputAssignment;

typedef struct _MetaTileInfo MetaTileInfo;

typedef struct _MetaRenderer MetaRenderer;
typedef struct _MetaRendererView MetaRendererView;

typedef struct _MetaRemoteDesktop MetaRemoteDesktop;
typedef struct _MetaRemoteDesktopSession MetaRemoteDesktopSession;
typedef struct _MetaScreenCast MetaScreenCast;
typedef struct _MetaScreenCastSession MetaScreenCastSession;
typedef struct _MetaScreenCastStream MetaScreenCastStream;

typedef struct _MetaVirtualMonitor MetaVirtualMonitor;
typedef struct _MetaVirtualMonitorInfo MetaVirtualMonitorInfo;
typedef struct _MetaVirtualModeInfo MetaVirtualModeInfo;

typedef struct _MetaBarrier MetaBarrier;
typedef struct _MetaBarrierImpl MetaBarrierImpl;

typedef struct _MetaIdleManager MetaIdleManager;

typedef struct _MetaDbusSession MetaDbusSession;
typedef struct _MetaDbusSessionManager MetaDbusSessionManager;
typedef struct _MetaDbusSessionWatcher MetaDbusSessionWatcher;

#ifdef HAVE_REMOTE_DESKTOP
typedef struct _MetaRemoteDesktop MetaRemoteDesktop;
#endif

typedef struct _MetaGammaLut
{
  uint16_t *red;
  uint16_t *green;
  uint16_t *blue;
  size_t size;
} MetaGammaLut;

typedef struct _MetaInputCapture MetaInputCapture;
typedef struct _MetaInputCaptureSession MetaInputCaptureSession;

typedef struct _MetaEis MetaEis;
typedef struct _MetaEisClient MetaEisClient;

typedef struct _MetaLauncher MetaLauncher;
typedef struct _MetaUdev MetaUdev;
