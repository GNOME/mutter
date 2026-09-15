/* SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include <gio/gio.h>
#include <stdint.h>

#include "backends/native/meta-kms-types.h"

typedef struct _MetaKmsRendererControl MetaKmsRendererControl;

MetaKmsRendererControl * meta_kms_renderer_control_new (MetaKmsDevice  *device,
                                                         uint32_t        crtc_id,
                                                         uint32_t        connector_id,
                                                         GError        **error);

void meta_kms_renderer_control_free (MetaKmsRendererControl *control);

int meta_kms_renderer_control_steal_fd (MetaKmsRendererControl *control);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaKmsRendererControl,
                               meta_kms_renderer_control_free)
