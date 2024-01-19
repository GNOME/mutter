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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "backends/meta-output.h"
#include "backends/native/meta-gpu-kms.h"
#include "backends/native/meta-kms-types.h"
#include "backends/native/meta-output-native.h"

#define META_TYPE_OUTPUT_KMS (meta_output_kms_get_type ())
G_DECLARE_FINAL_TYPE (MetaOutputKms, meta_output_kms,
                      META, OUTPUT_KMS,
                      MetaOutputNative)

gboolean meta_output_kms_is_privacy_screen_invalid (MetaOutputKms *output_kms);

gboolean meta_output_kms_can_clone (MetaOutputKms *output_kms,
                                    MetaOutputKms *other_output_kms);

MetaKmsConnector * meta_output_kms_get_kms_connector (MetaOutputKms *output_kms);

uint32_t meta_output_kms_get_connector_id (MetaOutputKms *output_kms);

MetaOutputKms * meta_output_kms_new (MetaGpuKms        *gpu_kms,
                                     MetaKmsConnector  *kms_connector,
                                     MetaOutput        *old_output,
                                     GError           **error);
