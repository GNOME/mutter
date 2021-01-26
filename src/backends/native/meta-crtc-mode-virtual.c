/*
 * Copyright (C) 2021 Red Hat
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
 */

#include "config.h"

#include "backends/native/meta-crtc-mode-virtual.h"

#include "backends/meta-virtual-monitor.h"

struct _MetaCrtcModeVirtual
{
  MetaCrtcMode parent;
};

#define META_CRTC_MODE_VIRTUAL_ID_BIT (((uint64_t) 1) << 63)

G_DEFINE_TYPE (MetaCrtcModeVirtual, meta_crtc_mode_virtual,
               META_TYPE_CRTC_MODE)

MetaCrtcModeVirtual *
meta_crtc_mode_virtual_new (uint64_t                      id,
                            const MetaVirtualMonitorInfo *info)
{
  g_autoptr (MetaCrtcModeInfo) crtc_mode_info = NULL;
  g_autofree char *crtc_mode_name = NULL;
  MetaCrtcModeVirtual *mode_virtual;

  crtc_mode_info = meta_crtc_mode_info_new ();
  crtc_mode_info->width = info->width;
  crtc_mode_info->height = info->height;
  crtc_mode_info->refresh_rate = info->refresh_rate;

  crtc_mode_name = g_strdup_printf ("%dx%d@%f",
                                    info->width,
                                    info->height,
                                    info->refresh_rate);
  mode_virtual = g_object_new (META_TYPE_CRTC_MODE_VIRTUAL,
                               "id", META_CRTC_MODE_VIRTUAL_ID_BIT | id,
                               "name", crtc_mode_name,
                               "info", crtc_mode_info,
                               NULL);

  return mode_virtual;
}

static void
meta_crtc_mode_virtual_init (MetaCrtcModeVirtual *mode_virtual)
{
}

static void
meta_crtc_mode_virtual_class_init (MetaCrtcModeVirtualClass *klass)
{
}
