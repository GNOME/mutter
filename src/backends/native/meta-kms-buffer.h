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

#ifndef META_KMS_BUFFER_H
#define META_KMS_BUFFER_H

#include <gbm.h>
#include <glib-object.h>

#include "backends/native/meta-gpu-kms.h"

#define META_TYPE_KMS_BUFFER (meta_kms_buffer_get_type ())
G_DECLARE_FINAL_TYPE (MetaKmsBuffer,
                      meta_kms_buffer,
                      META,
                      KMS_BUFFER,
                      GObject)

MetaKmsBuffer *
meta_kms_buffer_new_from_gbm (MetaGpuKms          *gpu_kms,
                              struct gbm_surface  *gbm_surface,
                              gboolean             use_modifiers,
                              GError             **error);

MetaKmsBuffer *
meta_kms_buffer_new_from_dumb (uint32_t dumb_fb_id);

uint32_t meta_kms_buffer_get_fb_id (const MetaKmsBuffer *kms_buffer);

struct gbm_bo *meta_kms_buffer_get_bo (const MetaKmsBuffer *kms_buffer);

#endif /* META_KMS_BUFFER_H */
