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

#ifndef META_CRTC_MODE_KMS_H
#define META_CRTC_MODE_KMS_H

#include <xf86drm.h>
#include <xf86drmMode.h>

#include "backends/meta-crtc-mode.h"

#define META_TYPE_CRTC_MODE_KMS (meta_crtc_mode_kms_get_type ())
G_DECLARE_FINAL_TYPE (MetaCrtcModeKms, meta_crtc_mode_kms,
                      META, CRTC_MODE_KMS,
                      MetaCrtcMode)

const drmModeModeInfo * meta_crtc_mode_kms_get_drm_mode (MetaCrtcModeKms *crtc_mode_kms);

MetaCrtcModeKms * meta_crtc_mode_kms_new (const drmModeModeInfo *drm_mode,
                                          uint64_t               id);

#endif /* META_CRTC_MODE_KMS_H */
