/*
 * Copyright (C) 2017-2020 Red Hat
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
#include <stdint.h>

#include "core/util-private.h"

/* Same as KMS mode flags and X11 randr flags */
typedef enum _MetaCrtcModeFlag
{
  META_CRTC_MODE_FLAG_NONE = 0,
  META_CRTC_MODE_FLAG_PHSYNC = (1 << 0),
  META_CRTC_MODE_FLAG_NHSYNC = (1 << 1),
  META_CRTC_MODE_FLAG_PVSYNC = (1 << 2),
  META_CRTC_MODE_FLAG_NVSYNC = (1 << 3),
  META_CRTC_MODE_FLAG_INTERLACE = (1 << 4),
  META_CRTC_MODE_FLAG_DBLSCAN = (1 << 5),
  META_CRTC_MODE_FLAG_CSYNC = (1 << 6),
  META_CRTC_MODE_FLAG_PCSYNC = (1 << 7),
  META_CRTC_MODE_FLAG_NCSYNC = (1 << 8),
  META_CRTC_MODE_FLAG_HSKEW = (1 << 9),
  META_CRTC_MODE_FLAG_BCAST = (1 << 10),
  META_CRTC_MODE_FLAG_PIXMUX = (1 << 11),
  META_CRTC_MODE_FLAG_DBLCLK = (1 << 12),
  META_CRTC_MODE_FLAG_CLKDIV2 = (1 << 13),

  META_CRTC_MODE_FLAG_MASK = 0x3fff
} MetaCrtcModeFlag;

typedef enum _MetaCrtcRefreshRateMode
{
  META_CRTC_REFRESH_RATE_MODE_FIXED,
  META_CRTC_REFRESH_RATE_MODE_VARIABLE,
} MetaCrtcRefreshRateMode;

typedef struct _MetaCrtcModeInfo
{
  grefcount ref_count;

  int width;
  int height;
  float refresh_rate;
  MetaCrtcRefreshRateMode refresh_rate_mode;
  int64_t vblank_duration_us;
  uint32_t pixel_clock_khz;
  MetaCrtcModeFlag flags;
} MetaCrtcModeInfo;

#define META_TYPE_CRTC_MODE (meta_crtc_mode_get_type ())
META_EXPORT_TEST
G_DECLARE_DERIVABLE_TYPE (MetaCrtcMode, meta_crtc_mode,
                          META, CRTC_MODE,
                          GObject)

struct _MetaCrtcModeClass
{
  GObjectClass parent_class;
};

#define META_TYPE_CRTC_MODE_INFO (meta_crtc_mode_info_get_type ())
GType meta_crtc_mode_info_get_type (void);

META_EXPORT_TEST
MetaCrtcModeInfo * meta_crtc_mode_info_new (void);

META_EXPORT_TEST
MetaCrtcModeInfo * meta_crtc_mode_info_ref (MetaCrtcModeInfo *crtc_mode_info);

META_EXPORT_TEST
void meta_crtc_mode_info_unref (MetaCrtcModeInfo *crtc_mode_info);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaCrtcModeInfo, meta_crtc_mode_info_unref)

uint64_t meta_crtc_mode_get_id (MetaCrtcMode *crtc_mode);

const char * meta_crtc_mode_get_name (MetaCrtcMode *crtc_mode);

META_EXPORT_TEST
const MetaCrtcModeInfo * meta_crtc_mode_get_info (MetaCrtcMode *crtc_mode);
