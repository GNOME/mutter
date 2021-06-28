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
#include "backends/native/meta-kms-mode-private.h"
#include "backends/native/meta-kms-plane.h"
#include "backends/native/meta-kms-private.h"
#include "backends/native/meta-kms-update.h"
#include "meta-test/meta-context-test.h"
#include "tests/meta-kms-test-utils.h"

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
  g_assert_nonnull (meta_kms_connector_get_preferred_mode (connector));

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
assert_crtc_state_equals (const MetaKmsCrtcState *crtc_state1,
                          const MetaKmsCrtcState *crtc_state2)
{
  g_assert_cmpint (crtc_state1->is_active, ==, crtc_state2->is_active);
  g_assert (meta_rectangle_equal (&crtc_state1->rect, &crtc_state2->rect));
  g_assert_cmpint (crtc_state1->is_drm_mode_valid,
                   ==,
                   crtc_state2->is_drm_mode_valid);
  if (crtc_state1->is_drm_mode_valid)
    {
      g_assert_cmpstr (crtc_state1->drm_mode.name,
                       ==,
                       crtc_state2->drm_mode.name);
    }

  g_assert_cmpint (crtc_state1->gamma.size, ==, crtc_state1->gamma.size);
  g_assert_cmpmem (crtc_state1->gamma.red,
                   crtc_state1->gamma.size * sizeof (uint16_t),
                   crtc_state2->gamma.red,
                   crtc_state2->gamma.size * sizeof (uint16_t));
  g_assert_cmpmem (crtc_state1->gamma.green,
                   crtc_state1->gamma.size * sizeof (uint16_t),
                   crtc_state2->gamma.green,
                   crtc_state2->gamma.size * sizeof (uint16_t));
  g_assert_cmpmem (crtc_state1->gamma.blue,
                   crtc_state1->gamma.size * sizeof (uint16_t),
                   crtc_state2->gamma.blue,
                   crtc_state2->gamma.size * sizeof (uint16_t));
}

static int
compare_modes (gconstpointer a,
               gconstpointer b)
{
  MetaKmsMode *mode_a = (MetaKmsMode *) a;
  MetaKmsMode *mode_b = (MetaKmsMode *) b;

  return g_strcmp0 (meta_kms_mode_get_name (mode_a),
                    meta_kms_mode_get_name (mode_b));
}

static void
assert_list_equals_unsorted (GList        *list1,
                             GList        *list2,
                             GCompareFunc  compare)
{
  list1 = g_list_copy (list1);
  list2 = g_list_copy (list2);

  while (list1)
    {
      GList *l;

      l = g_list_find_custom (list2, list1->data, compare);
      g_assert_nonnull (l);
      list2 = g_list_delete_link (list2, l);
      list1 = g_list_delete_link (list1, list1);
    }

  g_assert_null (list2);
}

static void
assert_connector_state_equals (const MetaKmsConnectorState *connector_state1,
                               const MetaKmsConnectorState *connector_state2)
{
  g_assert_cmpuint (connector_state1->current_crtc_id,
                    ==,
                    connector_state2->current_crtc_id);
  g_assert_cmpuint (connector_state1->common_possible_crtcs,
                    ==,
                    connector_state2->common_possible_crtcs);
  g_assert_cmpuint (connector_state1->common_possible_clones,
                    ==,
                    connector_state2->common_possible_clones);
  g_assert_cmpuint (connector_state1->encoder_device_idxs,
                    ==,
                    connector_state2->encoder_device_idxs);
  g_assert_cmpuint (g_list_length (connector_state1->modes),
                    ==,
                    g_list_length (connector_state2->modes));

  assert_list_equals_unsorted (connector_state1->modes,
                               connector_state2->modes,
                               compare_modes);

  if (connector_state1->edid_data || connector_state2->edid_data)
    {
      g_assert_cmpint (g_bytes_compare (connector_state1->edid_data,
                                        connector_state2->edid_data),
                       ==,
                       0);
    }

  g_assert_cmpint (connector_state1->has_scaling,
                   ==,
                   connector_state2->has_scaling);
  g_assert_cmpint (connector_state1->non_desktop,
                   ==,
                   connector_state2->non_desktop);

  g_assert_cmpint (connector_state1->subpixel_order,
                   ==,
                   connector_state2->subpixel_order);
  g_assert_cmpint (connector_state1->suggested_x,
                   ==,
                   connector_state2->suggested_x);
  g_assert_cmpint (connector_state1->hotplug_mode_update,
                   ==,
                   connector_state2->hotplug_mode_update);
  g_assert_cmpint (connector_state1->panel_orientation_transform,
                   ==,
                   connector_state2->panel_orientation_transform);
}

static MetaKmsCrtcState
copy_crtc_state (const MetaKmsCrtcState *crtc_state)
{
  MetaKmsCrtcState new_state;

  g_assert_nonnull (crtc_state);

  new_state = *crtc_state;
  new_state.gamma.red = g_memdup2 (new_state.gamma.red,
                                   new_state.gamma.size * sizeof (uint16_t));
  new_state.gamma.green = g_memdup2 (new_state.gamma.green,
                                     new_state.gamma.size * sizeof (uint16_t));
  new_state.gamma.blue = g_memdup2 (new_state.gamma.blue,
                                    new_state.gamma.size * sizeof (uint16_t));

  return new_state;
}

static MetaKmsConnectorState
copy_connector_state (const MetaKmsConnectorState *connector_state)
{
  MetaKmsConnectorState new_state;

  g_assert_nonnull (connector_state);

  new_state = *connector_state;
  new_state.modes = g_list_copy_deep (new_state.modes,
                                      (GCopyFunc) meta_kms_mode_clone,
                                      NULL);
  if (new_state.edid_data)
    {
      new_state.edid_data =
        g_bytes_new_from_bytes (new_state.edid_data,
                                0,
                                g_bytes_get_size (new_state.edid_data));
    }

  return new_state;
}

static void
release_crtc_state (const MetaKmsCrtcState *crtc_state)
{
  g_free (crtc_state->gamma.red);
  g_free (crtc_state->gamma.green);
  g_free (crtc_state->gamma.blue);
}

static void
release_connector_state (const MetaKmsConnectorState *connector_state)
{
  g_list_free_full (connector_state->modes,
                    (GDestroyNotify) meta_kms_mode_free);
  g_bytes_unref (connector_state->edid_data);
}

static void
meta_test_kms_device_mode_set (void)
{
  MetaKmsDevice *device;
  MetaKmsUpdate *update;
  MetaKmsCrtc *crtc;
  MetaKmsConnector *connector;
  MetaKmsMode *mode;
  MetaKmsPlane *primary_plane;
  g_autoptr (MetaDrmBuffer) primary_buffer = NULL;
  MetaKmsCrtcState crtc_state;
  MetaKmsConnectorState connector_state;
  MetaRectangle mode_rect;

  device = meta_get_test_kms_device (test_context);
  crtc = meta_get_test_kms_crtc (device);
  connector = meta_get_test_kms_connector (device);
  mode = meta_kms_connector_get_preferred_mode (connector);

  update = meta_kms_update_new (device);

  meta_kms_update_mode_set (update, crtc,
                            g_list_append (NULL, connector),
                            mode);

  primary_buffer = meta_create_test_mode_dumb_buffer (device, mode);

  primary_plane = meta_kms_device_get_primary_plane_for (device, crtc);
  meta_kms_update_assign_plane (update,
                                crtc,
                                primary_plane,
                                primary_buffer,
                                meta_get_mode_fixed_rect_16 (mode),
                                meta_get_mode_rect (mode),
                                META_KMS_ASSIGN_PLANE_FLAG_NONE);
  meta_kms_device_process_update_sync (device, update,
                                       META_KMS_UPDATE_FLAG_NONE);
  meta_kms_update_free (update);

  g_assert_nonnull (meta_kms_crtc_get_current_state (crtc));
  crtc_state = copy_crtc_state (meta_kms_crtc_get_current_state (crtc));
  g_assert_true (crtc_state.is_active);
  g_assert_true (crtc_state.is_drm_mode_valid);
  mode_rect = meta_get_mode_rect (mode);
  g_assert (meta_rectangle_equal (&crtc_state.rect, &mode_rect));

  g_assert_nonnull (meta_kms_connector_get_current_state (connector));
  connector_state =
    copy_connector_state (meta_kms_connector_get_current_state (connector));
  g_assert_cmpuint (connector_state.current_crtc_id,
                    ==,
                    meta_kms_crtc_get_id (crtc));

  meta_kms_update_states_sync (meta_kms_device_get_kms (device), NULL);
  assert_crtc_state_equals (&crtc_state,
                            meta_kms_crtc_get_current_state (crtc));
  assert_connector_state_equals (&connector_state,
                                 meta_kms_connector_get_current_state (connector));

  release_crtc_state (&crtc_state);
  release_connector_state (&connector_state);
}

static void
meta_test_kms_device_power_save (void)
{
  MetaKmsDevice *device;
  MetaKmsUpdate *update;
  MetaKmsCrtc *crtc;
  MetaKmsConnector *connector;
  MetaKmsMode *mode;
  MetaKmsPlane *primary_plane;
  g_autoptr (MetaDrmBuffer) primary_buffer = NULL;
  const MetaKmsCrtcState *crtc_state;
  const MetaKmsConnectorState *connector_state;

  device = meta_get_test_kms_device (test_context);
  crtc = meta_get_test_kms_crtc (device);
  connector = meta_get_test_kms_connector (device);
  mode = meta_kms_connector_get_preferred_mode (connector);
  primary_plane = meta_kms_device_get_primary_plane_for (device, crtc);
  primary_buffer = meta_create_test_mode_dumb_buffer (device, mode);

  /*
   * Set mode and assign primary plane.
   */

  update = meta_kms_update_new (device);
  meta_kms_update_mode_set (update, crtc,
                            g_list_append (NULL, connector),
                            mode);
  meta_kms_update_assign_plane (update,
                                crtc,
                                primary_plane,
                                primary_buffer,
                                meta_get_mode_fixed_rect_16 (mode),
                                meta_get_mode_rect (mode),
                                META_KMS_ASSIGN_PLANE_FLAG_NONE);
  meta_kms_device_process_update_sync (device, update,
                                       META_KMS_UPDATE_FLAG_NONE);
  meta_kms_update_free (update);

  g_assert_true (meta_kms_crtc_is_active (crtc));

  /*
   * Enable power saving mode.
   */

  meta_kms_device_disable (device);

  g_assert_false (meta_kms_crtc_is_active (crtc));
  crtc_state = meta_kms_crtc_get_current_state (crtc);
  g_assert_nonnull (crtc_state);
  g_assert_false (crtc_state->is_active);
  g_assert_false (crtc_state->is_drm_mode_valid);

  connector_state = meta_kms_connector_get_current_state (connector);
  g_assert_nonnull (connector_state);
  g_assert_cmpuint (connector_state->current_crtc_id, ==, 0);

  /*
   * Disable power saving mode by mode setting again.
   */

  update = meta_kms_update_new (device);
  meta_kms_update_mode_set (update, crtc,
                            g_list_append (NULL, connector),
                            mode);
  meta_kms_update_assign_plane (update,
                                crtc,
                                primary_plane,
                                primary_buffer,
                                meta_get_mode_fixed_rect_16 (mode),
                                meta_get_mode_rect (mode),
                                META_KMS_ASSIGN_PLANE_FLAG_NONE);
  meta_kms_device_process_update_sync (device, update,
                                       META_KMS_UPDATE_FLAG_NONE);
  meta_kms_update_free (update);

  g_assert_true (meta_kms_crtc_is_active (crtc));
  connector_state = meta_kms_connector_get_current_state (connector);
  g_assert_nonnull (connector_state);
  g_assert_cmpuint (connector_state->current_crtc_id,
                    ==,
                    meta_kms_crtc_get_id (crtc));
}

static void
init_tests (void)
{
  g_test_add_func ("/backends/native/kms/device/sanity",
                   meta_test_kms_device_sanity);
  g_test_add_func ("/backends/native/kms/device/mode-set",
                   meta_test_kms_device_mode_set);
  g_test_add_func ("/backends/native/kms/device/power-save",
                   meta_test_kms_device_power_save);
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
