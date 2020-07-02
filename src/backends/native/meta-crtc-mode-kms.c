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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "config.h"

#include "backends/native/meta-crtc-mode-kms.h"

#include "backends/native/meta-kms-mode.h"
#include "backends/native/meta-kms-utils.h"

struct _MetaCrtcModeKms
{
  MetaCrtcMode parent;

  MetaKmsMode *kms_mode;
};

G_DEFINE_TYPE (MetaCrtcModeKms, meta_crtc_mode_kms,
               META_TYPE_CRTC_MODE)

MetaKmsMode *
meta_crtc_mode_kms_get_kms_mode (MetaCrtcModeKms *mode_kms)
{
  return mode_kms->kms_mode;
}

MetaCrtcModeKms *
meta_crtc_mode_kms_new (MetaKmsMode *kms_mode,
                        uint64_t     id)
{
  const drmModeModeInfo *drm_mode = meta_kms_mode_get_drm_mode (kms_mode);
  g_autoptr (MetaCrtcModeInfo) crtc_mode_info = NULL;
  g_autofree char *crtc_mode_name = NULL;
  MetaCrtcModeKms *mode_kms;

  crtc_mode_info = meta_crtc_mode_info_new ();
  crtc_mode_info->width = drm_mode->hdisplay;
  crtc_mode_info->height = drm_mode->vdisplay;
  crtc_mode_info->flags = drm_mode->flags;
  crtc_mode_info->refresh_rate =
    meta_calculate_drm_mode_refresh_rate (drm_mode);

  crtc_mode_name = g_strndup (drm_mode->name, DRM_DISPLAY_MODE_LEN);
  mode_kms = g_object_new (META_TYPE_CRTC_MODE_KMS,
                           "id", id,
                           "name", crtc_mode_name,
                           "info", crtc_mode_info,
                           NULL);

  mode_kms->kms_mode = kms_mode;

  return mode_kms;
}

static void
meta_crtc_mode_kms_init (MetaCrtcModeKms *mode_kms)
{
}

static void
meta_crtc_mode_kms_class_init (MetaCrtcModeKmsClass *klass)
{
}
