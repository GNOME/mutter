/*
 * Copyright (C) 2021 Red Hat Inc.
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
 *
 */

#include "config.h"

#include "tests/meta-kms-test-utils.h"

#include <drm_fourcc.h>

#include "backends/native/meta-device-pool.h"
#include "backends/native/meta-drm-buffer-dumb.h"
#include "backends/native/meta-backend-native-private.h"
#include "backends/native/meta-kms-connector.h"
#include "backends/native/meta-kms-crtc.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-mode.h"
#include "backends/native/meta-kms-plane.h"
#include "backends/native/meta-kms-types.h"
#include "backends/native/meta-kms-update.h"
#include "backends/native/meta-kms.h"

MetaKmsDevice *
meta_get_test_kms_device (MetaContext *context)
{
  MetaBackend *backend = meta_context_get_backend (context);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);
  GList *devices;

  devices = meta_kms_get_devices (kms);
  g_assert_cmpuint (g_list_length (devices), ==, 1);
  return META_KMS_DEVICE (devices->data);
}

MetaKmsCrtc *
meta_get_test_kms_crtc (MetaKmsDevice *device)
{
  GList *crtcs;

  crtcs = meta_kms_device_get_crtcs (device);
  g_assert_cmpuint (g_list_length (crtcs), ==, 1);

  return META_KMS_CRTC (crtcs->data);
}

MetaKmsConnector *
meta_get_test_kms_connector (MetaKmsDevice *device)
{
  GList *connectors;

  connectors = meta_kms_device_get_connectors (device);
  g_assert_cmpuint (g_list_length (connectors), ==, 1);

  return META_KMS_CONNECTOR (connectors->data);
}

static MetaKmsPlane *
get_plane_with_type_for (MetaKmsDevice    *device,
                         MetaKmsCrtc      *crtc,
                         MetaKmsPlaneType  type)
{
  GList *l;

  for (l = meta_kms_device_get_planes (device); l; l = l->next)
    {
      MetaKmsPlane *plane = l->data;

      if (meta_kms_plane_get_plane_type (plane) != type)
        continue;

      if (meta_kms_plane_is_usable_with (plane, crtc))
        return plane;
    }

  return NULL;
}

MetaKmsPlane *
meta_get_primary_test_plane_for (MetaKmsDevice *device,
                                 MetaKmsCrtc   *crtc)
{
  return get_plane_with_type_for (device, crtc, META_KMS_PLANE_TYPE_PRIMARY);
}

MetaKmsPlane *
meta_get_cursor_test_plane_for (MetaKmsDevice *device,
                                MetaKmsCrtc   *crtc)
{
  return get_plane_with_type_for (device, crtc, META_KMS_PLANE_TYPE_CURSOR);
}

static MetaDeviceFile *
open_device_file_for (MetaKmsDevice *device)
{
  MetaKms *kms = meta_kms_device_get_kms (device);
  MetaBackend *backend = meta_kms_get_backend (kms);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaDevicePool *device_pool =
    meta_backend_native_get_device_pool (backend_native);
  const char *device_path;
  MetaDeviceFile *device_file;

  device_path = meta_kms_device_get_path (device);
  device_file = meta_device_pool_open (device_pool, device_path,
                                       META_DEVICE_FILE_FLAG_TAKE_CONTROL,
                                       NULL);
  g_assert_nonnull (device_file);
  return device_file;
}

MetaDrmBuffer *
meta_create_test_dumb_buffer (MetaKmsDevice *device,
                              int            width,
                              int            height)
{
  g_autoptr (MetaDeviceFile) device_file = NULL;
  MetaDrmBufferDumb *dumb_buffer;

  device_file = open_device_file_for (device);
  dumb_buffer = meta_drm_buffer_dumb_new (device_file,
                                          width, height,
                                          DRM_FORMAT_XRGB8888,
                                          NULL);
  g_assert_nonnull (dumb_buffer);

  return META_DRM_BUFFER (dumb_buffer);
}

MetaDrmBuffer *
meta_create_test_mode_dumb_buffer (MetaKmsDevice *device,
                                   MetaKmsMode   *mode)
{
  return meta_create_test_dumb_buffer (device,
                                       meta_kms_mode_get_width (mode),
                                       meta_kms_mode_get_height (mode));
}

MetaFixed16Rectangle
meta_get_mode_fixed_rect_16 (MetaKmsMode *mode)
{
  return META_FIXED_16_RECTANGLE_INIT_INT (0, 0,
                                           meta_kms_mode_get_width (mode),
                                           meta_kms_mode_get_height (mode));
}

MtkRectangle
meta_get_mode_rect (MetaKmsMode *mode)
{
  return MTK_RECTANGLE_INIT (0, 0,
                             meta_kms_mode_get_width (mode),
                             meta_kms_mode_get_height (mode));
}
