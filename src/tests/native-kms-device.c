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

#include "backends/native/meta-backend-native-private.h"
#include "backends/native/meta-kms-connector.h"
#include "backends/native/meta-kms-crtc.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-plane.h"
#include "backends/native/meta-kms.h"
#include "meta-test/meta-context-test.h"

static MetaContext *test_context;

static void
meta_test_kms_device_sanity (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);
  GList *devices;
  MetaKmsDevice *device;
  GList *connectors;
  MetaKmsConnector *connector;
  GList *crtcs;
  MetaKmsCrtc *crtc;
  GList *planes;
  MetaKmsPlane *primary_plane;
  MetaKmsPlane *cursor_plane;

  devices = meta_kms_get_devices (kms);
  g_assert_cmpuint (g_list_length (devices), ==, 1);
  device = META_KMS_DEVICE (devices->data);

  g_assert (meta_kms_device_get_kms (device) == kms);
  g_assert_cmpstr (meta_kms_device_get_driver_name (device), ==, "vkms");
  g_assert_true (meta_kms_device_uses_monotonic_clock (device));

  connectors = meta_kms_device_get_connectors (device);
  g_assert_cmpuint (g_list_length (connectors), ==, 1);
  connector = META_KMS_CONNECTOR (connectors->data);
  g_assert (meta_kms_connector_get_device (connector) == device);

  crtcs = meta_kms_device_get_crtcs (device);
  g_assert_cmpuint (g_list_length (crtcs), ==, 1);
  crtc = META_KMS_CRTC (crtcs->data);
  g_assert (meta_kms_crtc_get_device (crtc) == device);

  planes = meta_kms_device_get_planes (device);
  g_assert_cmpuint (g_list_length (planes), ==, 2);
  primary_plane = meta_kms_device_get_primary_plane_for (device, crtc);
  g_assert_nonnull (primary_plane);
  cursor_plane = meta_kms_device_get_cursor_plane_for (device, crtc);
  g_assert_nonnull (cursor_plane);
  g_assert (cursor_plane != primary_plane);
  g_assert_nonnull (g_list_find (planes, primary_plane));
  g_assert_nonnull (g_list_find (planes, cursor_plane));
  g_assert (meta_kms_plane_get_device (primary_plane) == device);
  g_assert (meta_kms_plane_get_device (cursor_plane) == device);
  g_assert_true (meta_kms_plane_is_usable_with (primary_plane, crtc));
  g_assert_true (meta_kms_plane_is_usable_with (cursor_plane, crtc));
  g_assert_cmpint (meta_kms_plane_get_plane_type (primary_plane),
                   ==,
                   META_KMS_PLANE_TYPE_PRIMARY);
  g_assert_cmpint (meta_kms_plane_get_plane_type (cursor_plane),
                   ==,
                   META_KMS_PLANE_TYPE_CURSOR);
}

static void
init_tests (void)
{
  g_test_add_func ("/backends/native/kms/device/sanity",
                   meta_test_kms_device_sanity);
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
