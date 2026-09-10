/* SPDX-License-Identifier: MIT */
#pragma once

#include "drm-mock.h"

/* All operations run in the KMS implementation context. */
DRM_MOCK_EXPORT void drm_mock_preparation_delay_next (void);
DRM_MOCK_EXPORT gboolean drm_mock_preparation_was_issued (void);
DRM_MOCK_EXPORT gboolean drm_mock_preparation_was_closed (void);
DRM_MOCK_EXPORT void drm_mock_preparation_release (void);
DRM_MOCK_EXPORT void drm_mock_preparation_hangup (void);
DRM_MOCK_EXPORT void drm_mock_preparation_clear (void);
