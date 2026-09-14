/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gio/gio.h>
#include <stdint.h>

#include "backends/native/meta-kms-types.h"

#define META_TYPE_KMS_CAPTURE_GRANT (meta_kms_capture_grant_get_type ())
G_DECLARE_FINAL_TYPE (MetaKmsCaptureGrant, meta_kms_capture_grant,
                      META, KMS_CAPTURE_GRANT, GObject)

MetaKmsCaptureGrant * meta_kms_capture_grant_new (MetaKmsDevice  *device,
                                                   uint32_t        crtc_id,
                                                   uint32_t        connector_id,
                                                   GError        **error);

int meta_kms_capture_grant_steal_capture_fd (MetaKmsCaptureGrant *grant);
int meta_kms_capture_grant_get_control_fd (MetaKmsCaptureGrant *grant);

/*
 * Revoke before releasing the issuing file. Failure means the file hold still
 * needs releasing; capture authority is revoked even when that release fails.
 * Callers may retry. Revocation does not wait for admitted output writes.
 */
gboolean meta_kms_capture_grant_revoke (MetaKmsCaptureGrant  *grant,
                                       GError              **error);
