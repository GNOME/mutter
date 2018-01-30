/*
 * Copyright © 2018 Canonical Ltd.
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
 * Author: Daniel van Vugt <daniel.van.vugt@canonical.com>
 */

#ifndef META_KMS_FRAMEBUFFER_H
#define META_KMS_FRAMEBUFFER_H

#include <gbm.h>
#include <glib-object.h>

#include "config.h"
#include "backends/native/meta-gpu-kms.h"

#define META_TYPE_KMS_FRAMEBUFFER (meta_kms_framebuffer_get_type ())
G_DECLARE_FINAL_TYPE (MetaKmsFramebuffer, meta_kms_framebuffer, META, KMS_FRAMEBUFFER, GObject)

MetaKmsFramebuffer*
meta_kms_framebuffer_new_from_gbm (MetaGpuKms         *gpu_kms,
                                   struct gbm_surface *gbm_surface,
                                   gboolean            use_modifiers);

MetaKmsFramebuffer*
meta_kms_framebuffer_new_from_dumb (MetaGpuKms *gpu_kms,
                                    uint32_t    dumb_fb_id);

uint32_t meta_kms_framebuffer_get_fb_id (const MetaKmsFramebuffer *kms_fb);

struct gbm_bo *meta_kms_framebuffer_get_bo (const MetaKmsFramebuffer *kms_fb);

void meta_kms_framebuffer_release_buffer (MetaKmsFramebuffer *kms_fb);

#endif /* META_KMS_FRAMEBUFFER_H */
