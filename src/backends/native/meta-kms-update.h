/*
 * Copyright (C) 2018 Red Hat
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

#ifndef META_KMS_UPDATE_H
#define META_KMS_UPDATE_H

#include <glib-object.h>
#include <glib.h>
#include <stdint.h>
#include <xf86drmMode.h>

#include "backends/meta-monitor-transform.h"
#include "backends/native/meta-kms-types.h"

struct _MetaKmsPageFlipFeedback
{
  void (* flipped) (MetaKmsCrtc  *crtc,
                    unsigned int  sequence,
                    unsigned int  tv_sec,
                    unsigned int  tv_usec,
                    gpointer      user_data);

  void (* mode_set_fallback) (MetaKmsCrtc *crtc,
                              gpointer     user_data);

  void (* discarded) (MetaKmsCrtc  *crtc,
                      gpointer      user_data,
                      const GError *error);
};

typedef int (* MetaKmsCustomPageFlipFunc) (gpointer custom_page_flip_data,
                                           gpointer user_data);

MetaKmsUpdate * meta_kms_update_new (void);

void meta_kms_update_free (MetaKmsUpdate *update);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaKmsUpdate, meta_kms_update_free)

void meta_kms_update_mode_set (MetaKmsUpdate   *update,
                               MetaKmsCrtc     *crtc,
                               GList           *connectors,
                               drmModeModeInfo *drm_mode);

MetaKmsPlaneAssignment * meta_kms_update_assign_plane (MetaKmsUpdate        *update,
                                                       MetaKmsCrtc          *crtc,
                                                       MetaKmsPlane         *plane,
                                                       uint32_t              fb_id,
                                                       MetaFixed16Rectangle  src_rect,
                                                       MetaFixed16Rectangle  dst_rect);

void meta_kms_update_page_flip (MetaKmsUpdate                 *update,
                                MetaKmsCrtc                   *crtc,
                                const MetaKmsPageFlipFeedback *feedback,
                                gpointer                       user_data);

void meta_kms_update_custom_page_flip (MetaKmsUpdate                 *update,
                                       MetaKmsCrtc                   *crtc,
                                       const MetaKmsPageFlipFeedback *feedback,
                                       gpointer                       user_data,
                                       MetaKmsCustomPageFlipFunc      custom_page_flip_func,
                                       gpointer                       custom_page_flip_user_data);

static inline MetaFixed16
meta_fixed_16_from_int (int16_t d)
{
  return d * 65536;
}

static inline int16_t
meta_fixed_16_to_int (MetaFixed16 fixed)
{
  return fixed / 65536;
}

#endif /* META_KMS_UPDATE_H */
