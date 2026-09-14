/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gio/gio.h>
#include <stdint.h>

typedef struct _MetaKmsCaptureFiles MetaKmsCaptureFiles;

/*
 * The caller retains the issuing DRM file: its final close revokes the grant.
 * Issuance does not require an active image offer. Only the capture descriptor
 * may be sent to the consumer; the control descriptor remains with the broker.
 */
MetaKmsCaptureFiles * meta_kms_capture_files_create (int       drm_fd,
                                                      uint32_t  crtc_id,
                                                      uint32_t  connector_id,
                                                      GError  **error);

/* Closing control revokes authority, but does not wait for admitted writes. */
void meta_kms_capture_files_free (MetaKmsCaptureFiles *files);

int meta_kms_capture_files_steal_capture_fd (MetaKmsCaptureFiles *files);

/* Borrowed; POLLHUP reports revocation, not consumer close or frame completion. */
int meta_kms_capture_files_get_control_fd (MetaKmsCaptureFiles *files);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaKmsCaptureFiles, meta_kms_capture_files_free)
