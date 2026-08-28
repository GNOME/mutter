/*
 * Copyright (C) 2026 Red Hat
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

#include "config.h"

#include "backends/native/meta-cast-kms-grant-policy.h"

#include "backends/native/meta-kms-cast-uapi.h"

#define DISPLAY_V1_RIGHTS \
  (DRM_CASTKMS_GRANT_CAPTURE_PIXELS | \
   DRM_CASTKMS_GRANT_MANAGE_ATTACHMENT | \
   DRM_CASTKMS_GRANT_UPDATE_EDID | \
   DRM_CASTKMS_GRANT_READ_CURSOR)
#define DISPLAY_CEC_V1_RIGHTS \
  (DISPLAY_V1_RIGHTS | DRM_CASTKMS_GRANT_MANAGE_CEC)
#define DISPLAY_CEC_AUDIO_V1_RIGHTS \
  (DISPLAY_CEC_V1_RIGHTS | DRM_CASTKMS_GRANT_CAPTURE_AUDIO)

#define AUDIO_TAP_UAPI_MINOR 12

gboolean
meta_cast_kms_grant_profile_get_rights (uint16_t   profile,
                                        uint32_t  *out_rights)
{
  g_return_val_if_fail (out_rights != NULL, FALSE);

  switch ((MetaCastKmsGrantProfile) profile)
    {
    case META_CAST_KMS_GRANT_PROFILE_DISPLAY_V1:
      *out_rights = DISPLAY_V1_RIGHTS;
      return TRUE;
    case META_CAST_KMS_GRANT_PROFILE_DISPLAY_CEC_V1:
      *out_rights = DISPLAY_CEC_V1_RIGHTS;
      return TRUE;
    case META_CAST_KMS_GRANT_PROFILE_DISPLAY_CEC_AUDIO_V1:
      *out_rights = DISPLAY_CEC_AUDIO_V1_RIGHTS;
      return TRUE;
    }

  return FALSE;
}

gboolean
meta_cast_kms_grant_check_caps (const struct drm_castkms_capture_query_caps *caps,
                                uint32_t                                     rights)
{
  uint32_t minimum_minor = DRM_CASTKMS_CAPTURE_UAPI_MINOR;

  /* The grant/control-fd capability bits alone do not establish audio UAPI
   * support. Audio availability for an attachment is checked when opening
   * its tap; the capture query has no audio-availability capability bit.
   */
  if (rights & DRM_CASTKMS_GRANT_CAPTURE_AUDIO)
    minimum_minor = AUDIO_TAP_UAPI_MINOR;

  return caps->uapi_major == DRM_CASTKMS_CAPTURE_UAPI_MAJOR &&
         caps->uapi_minor >= minimum_minor &&
         caps->uapi_major <= UINT16_MAX &&
         caps->uapi_minor <= UINT16_MAX &&
         (caps->flags & DRM_CASTKMS_CAPTURE_CAP_GRANT_FD) &&
         (caps->flags & DRM_CASTKMS_CAPTURE_CAP_GRANT_CONTROL_FD) &&
         caps->reserved == 0;
}
