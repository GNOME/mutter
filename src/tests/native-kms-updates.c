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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#include "config.h"

#include <gbm.h>

#include "backends/native/meta-backend-native-private.h"
#include "backends/native/meta-device-pool.h"
#include "backends/native/meta-drm-buffer-dumb.h"
#include "backends/native/meta-kms-connector.h"
#include "backends/native/meta-kms-crtc.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-mode.h"
#include "backends/native/meta-kms-update-private.h"
#include "backends/native/meta-kms.h"
#include "meta-test/meta-context-test.h"
#include "meta/meta-backend.h"

static MetaContext *test_context;

static MetaKmsDevice *
get_test_kms_device (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);
  GList *devices;

  devices = meta_kms_get_devices (kms);
  g_assert_cmpuint (g_list_length (devices), ==, 1);
  return META_KMS_DEVICE (devices->data);
}

static MetaKmsCrtc *
get_test_crtc (MetaKmsDevice *device)
{
  GList *crtcs;

  crtcs = meta_kms_device_get_crtcs (device);
  g_assert_cmpuint (g_list_length (crtcs), ==, 1);

  return META_KMS_CRTC (crtcs->data);
}

static void
meta_test_kms_update_sanity (void)
{
  MetaKmsDevice *device;
  MetaKmsCrtc *crtc;
  MetaKmsUpdate *update;

  device = get_test_kms_device ();
  crtc = get_test_crtc (device);

  update = meta_kms_update_new (device);
  g_assert (meta_kms_update_get_device (update) == device);
  g_assert_false (meta_kms_update_is_locked (update));
  g_assert_false (meta_kms_update_is_power_save (update));
  g_assert_null (meta_kms_update_get_primary_plane_assignment (update, crtc));
  g_assert_null (meta_kms_update_get_plane_assignments (update));
  g_assert_null (meta_kms_update_get_mode_sets (update));
  g_assert_null (meta_kms_update_get_page_flip_listeners (update));
  g_assert_null (meta_kms_update_get_connector_updates (update));
  g_assert_null (meta_kms_update_get_crtc_gammas (update));
  meta_kms_update_free (update);
}

static void
meta_test_kms_update_fixed16 (void)
{
  MetaFixed16Rectangle rect16;

  g_assert_cmpint (meta_fixed_16_from_int (12345), ==, 809041920);
  g_assert_cmpint (meta_fixed_16_to_int (809041920), ==, 12345);
  g_assert_cmpint (meta_fixed_16_from_int (-12345), ==, -809041920);
  g_assert_cmpint (meta_fixed_16_to_int (-809041920), ==, -12345);

  rect16 = META_FIXED_16_RECTANGLE_INIT_INT (100, 200, 300, 400);
  g_assert_cmpint (rect16.x, ==, 6553600);
  g_assert_cmpint (rect16.y, ==, 13107200);
  g_assert_cmpint (rect16.width, ==, 19660800);
  g_assert_cmpint (rect16.height, ==, 26214400);
}

static void
init_tests (void)
{
  g_test_add_func ("/backends/native/kms/update/sanity",
                   meta_test_kms_update_sanity);
  g_test_add_func ("/backends/native/kms/update/fixed16",
                   meta_test_kms_update_fixed16);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;
  g_autoptr (GError) error = NULL;

  context = test_context =
    meta_create_test_context (META_CONTEXT_TEST_TYPE_VKMS,
                              META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert (meta_context_configure (context, &argc, &argv, NULL));

  init_tests ();

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_CAN_SKIP);
}
