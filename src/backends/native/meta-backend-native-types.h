/*
 * Copyright (C) 2019 Red Hat
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

typedef struct _MetaBackendNative MetaBackendNative;
typedef struct _MetaSeatNative MetaSeatNative;
typedef struct _MetaSeatImpl MetaSeatImpl;
typedef struct _MetaKeymapNative MetaKeymapNative;
typedef struct _MetaRendererNative MetaRendererNative;
typedef struct _MetaGpuKms MetaGpuKms;
typedef struct _MetaCrtcVirtual MetaCrtcVirtual;
typedef struct _MetaCrtcModeVirtual MetaCrtcModeVirtual;
typedef struct _MetaDevicePool MetaDevicePool;
typedef struct _MetaDeviceFile MetaDeviceFile;
typedef struct _MetaDrmBuffer MetaDrmBuffer;
typedef struct _MetaDrmLeaseManager MetaDrmLeaseManager;
typedef struct _MetaRenderDevice MetaRenderDevice;

typedef enum _MetaSeatNativeFlag
{
  META_SEAT_NATIVE_FLAG_NONE = 0,
  META_SEAT_NATIVE_FLAG_NO_LIBINPUT = 1 << 0,
} MetaSeatNativeFlag;

typedef enum _MetaBackendNativeMode
{
  META_BACKEND_NATIVE_MODE_DEFAULT = 0,
  META_BACKEND_NATIVE_MODE_HEADLESS,
  META_BACKEND_NATIVE_MODE_TEST_VKMS,
  META_BACKEND_NATIVE_MODE_TEST_HEADLESS,
} MetaBackendNativeMode;
