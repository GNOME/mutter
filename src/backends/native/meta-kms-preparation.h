/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gio/gio.h>
#include <stdint.h>

typedef struct _MetaKmsPreparation MetaKmsPreparation;

MetaKmsPreparation * meta_kms_preparation_create (int              drm_fd,
                                                  const uint32_t  *crtc_ids,
                                                  unsigned int     n_crtcs,
                                                  GError         **error);
void meta_kms_preparation_free (MetaKmsPreparation *preparation);
int meta_kms_preparation_get_fd (MetaKmsPreparation *preparation);
const GArray * meta_kms_preparation_get_crtc_ids (MetaKmsPreparation *preparation);
gboolean meta_kms_preparation_is_pending (MetaKmsPreparation *preparation);
gboolean meta_kms_preparation_wait (MetaKmsPreparation  *preparation,
                                    GError             **error);
