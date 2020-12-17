/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2017 Red Hat
 * Copyright (C) 2018 DisplayLink (UK) Ltd.
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

#ifndef META_OUTPUT_KMS_H
#define META_OUTPUT_KMS_H

#include "backends/meta-output.h"
#include "backends/native/meta-gpu-kms.h"
#include "backends/native/meta-kms-types.h"
#include "backends/native/meta-output-native.h"

#define META_TYPE_OUTPUT_KMS (meta_output_kms_get_type ())
G_DECLARE_FINAL_TYPE (MetaOutputKms, meta_output_kms,
                      META, OUTPUT_KMS,
                      MetaOutputNative)

void meta_output_kms_set_power_save_mode (MetaOutputKms *output_kms,
                                          uint64_t       dpms_state,
                                          MetaKmsUpdate *kms_update);

void meta_output_kms_set_underscan (MetaOutputKms *output_kms,
                                    MetaKmsUpdate *kms_update);

gboolean meta_output_kms_can_clone (MetaOutputKms *output_kms,
                                    MetaOutputKms *other_output_kms);

MetaKmsConnector * meta_output_kms_get_kms_connector (MetaOutputKms *output_kms);

uint32_t meta_output_kms_get_connector_id (MetaOutputKms *output_kms);

MetaOutputKms * meta_output_kms_new (MetaGpuKms        *gpu_kms,
                                     MetaKmsConnector  *kms_connector,
                                     MetaOutput        *old_output,
                                     GError           **error);

#endif /* META_OUTPUT_KMS_H */
