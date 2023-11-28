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

#include "backends/native/meta-backend-native-private.h"
#include "backends/native/meta-device-pool.h"
#include "backends/native/meta-input-thread.h"
#include "backends/native/meta-kms-connector.h"
#include "backends/native/meta-kms-crtc.h"
#include "backends/native/meta-kms-cursor-manager.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-impl-device-simple.h"
#include "backends/native/meta-kms-mode-private.h"
#include "backends/native/meta-kms-plane.h"
#include "backends/native/meta-kms-private.h"
#include "backends/native/meta-kms-update.h"
#include "backends/native/meta-seat-native.h"
#include "backends/native/meta-thread-impl.h"
#include "meta-test/meta-context-test.h"
#include "tests/meta-kms-test-utils.h"
#include "tests/meta-test-utils.h"

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
  primary_plane = meta_get_primary_test_plane_for (device, crtc);
  g_assert_nonnull (primary_plane);
  cursor_plane = meta_get_cursor_test_plane_for (device, crtc);
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
  g_assert (mtk_rectangle_equal (&crtc_state1->rect, &crtc_state2->rect));
  g_assert_cmpint (crtc_state1->is_drm_mode_valid,
                   ==,
                   crtc_state2->is_drm_mode_valid);
  if (crtc_state1->is_drm_mode_valid)
    {
      g_assert_cmpstr (crtc_state1->drm_mode.name,
                       ==,
                       crtc_state2->drm_mode.name);
    }

  g_assert_true (meta_gamma_lut_equal (crtc_state1->gamma.value,
                                       crtc_state2->gamma.value));
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
  if (crtc_state->gamma.value)
    new_state.gamma.value = meta_gamma_lut_copy (crtc_state->gamma.value);
  else
    new_state.gamma.value = NULL;

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
release_crtc_state (MetaKmsCrtcState *crtc_state)
{
  g_clear_pointer (&crtc_state->gamma.value, meta_gamma_lut_free);
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
  MtkRectangle mode_rect;
  MetaKmsFeedback *feedback;

  device = meta_get_test_kms_device (test_context);
  crtc = meta_get_test_kms_crtc (device);
  connector = meta_get_test_kms_connector (device);
  mode = meta_kms_connector_get_preferred_mode (connector);

  update = meta_kms_update_new (device);

  meta_kms_update_mode_set (update, crtc,
                            g_list_append (NULL, connector),
                            mode);

  primary_buffer = meta_create_test_mode_dumb_buffer (device, mode);

  primary_plane = meta_get_primary_test_plane_for (device, crtc);
  meta_kms_update_assign_plane (update,
                                crtc,
                                primary_plane,
                                primary_buffer,
                                meta_get_mode_fixed_rect_16 (mode),
                                meta_get_mode_rect (mode),
                                META_KMS_ASSIGN_PLANE_FLAG_NONE);
  feedback = meta_kms_device_process_update_sync (device, update,
                                                  META_KMS_UPDATE_FLAG_MODE_SET);
  meta_kms_feedback_unref (feedback);

  g_assert_nonnull (meta_kms_crtc_get_current_state (crtc));
  crtc_state = copy_crtc_state (meta_kms_crtc_get_current_state (crtc));
  g_assert_true (crtc_state.is_active);
  g_assert_true (crtc_state.is_drm_mode_valid);
  mode_rect = meta_get_mode_rect (mode);
  g_assert (mtk_rectangle_equal (&crtc_state.rect, &mode_rect));

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
  MetaKmsFeedback *feedback;
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
  primary_plane = meta_get_primary_test_plane_for (device, crtc);
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
  feedback = meta_kms_device_process_update_sync (device, update,
                                                  META_KMS_UPDATE_FLAG_MODE_SET);
  meta_kms_feedback_unref (feedback);

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
  feedback = meta_kms_device_process_update_sync (device, update,
                                                  META_KMS_UPDATE_FLAG_MODE_SET);
  meta_kms_feedback_unref (feedback);

  g_assert_true (meta_kms_crtc_is_active (crtc));
  connector_state = meta_kms_connector_get_current_state (connector);
  g_assert_nonnull (connector_state);
  g_assert_cmpuint (connector_state->current_crtc_id,
                    ==,
                    meta_kms_crtc_get_id (crtc));
}

static void
done_update_result_feedback (const MetaKmsFeedback *feedback,
                             gpointer               user_data)
{
  gboolean *done = user_data;

  *done = TRUE;
}

static const MetaKmsResultListenerVtable done_result_listener_vtable = {
  .feedback = done_update_result_feedback,
};

static gboolean
fake_position_changed_in_input_impl (GTask *task)
{
  MetaKmsCursorManager *cursor_manager = g_task_get_task_data (task);

  meta_kms_cursor_manager_position_changed_in_input_impl (cursor_manager,
                                                          &GRAPHENE_POINT_INIT (50, 50));

  g_task_return_boolean (task, TRUE);
  return G_SOURCE_REMOVE;
}

static void
fake_position_changed (ClutterSeat          *seat,
                       MetaKmsCursorManager *cursor_manager)
{
  MetaSeatNative *seat_native;
  g_autoptr (GTask) task = NULL;

  seat_native = META_SEAT_NATIVE (seat);
  task = g_task_new (seat_native, NULL, NULL, NULL);
  g_task_set_task_data (task, cursor_manager, NULL);

  meta_seat_impl_run_input_task (seat_native->impl, task,
                                 (GSourceFunc) fake_position_changed_in_input_impl);
}

static void
meta_test_kms_device_discard_disabled (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);
  MetaKmsCursorManager *cursor_manager = meta_kms_get_cursor_manager (kms);
  g_autoptr (GArray) layout_array = NULL;
  MetaKmsCrtcLayout layout;
  g_autoptr (GArray) empty_array = NULL;
  ClutterSeat *seat;
  MetaKmsDevice *device;
  MetaDevicePool *device_pool;
  MetaDeviceFile *device_file;
  MetaKmsUpdate *update;
  MetaKmsCrtc *crtc;
  MetaKmsConnector *connector;
  MetaKmsMode *mode;
  MetaKmsPlane *primary_plane;
  MetaKmsPlane *cursor_plane;
  uint64_t cursor_width, cursor_height;
  g_autoptr (MetaDrmBuffer) primary_buffer = NULL;
  g_autoptr (MetaDrmBuffer) cursor_buffer = NULL;
  MetaKmsFeedback *feedback;
  drmModePlane *drm_plane;
  gboolean done = FALSE;
  GError *error = NULL;

  device = meta_get_test_kms_device (test_context);

  if (META_IS_KMS_IMPL_DEVICE_SIMPLE (meta_kms_device_get_impl_device (device)))
    {
      g_test_skip ("Legacy KMS cursor API doesn't get reflected in DRM planes");
      return;
    }

  crtc = meta_get_test_kms_crtc (device);
  connector = meta_get_test_kms_connector (device);
  mode = meta_kms_connector_get_preferred_mode (connector);
  primary_plane = meta_get_primary_test_plane_for (device, crtc);
  cursor_plane = meta_get_cursor_test_plane_for (device, crtc);

  device_pool = meta_backend_native_get_device_pool (backend_native);
  device_file = meta_device_pool_open (device_pool,
                                       meta_kms_device_get_path (device),
                                       META_DEVICE_FILE_FLAG_TAKE_CONTROL,
                                       &error);
  if (!device_file)
    g_error ("Failed to open KMS device: %s", error->message);

  primary_buffer = meta_create_test_mode_dumb_buffer (device, mode);

  g_assert_true (meta_kms_device_get_cursor_size (device,
                                                  &cursor_width,
                                                  &cursor_height));
  cursor_buffer = meta_create_test_dumb_buffer (device,
                                                cursor_width,
                                                cursor_height);

  /*
   * Setup base state: cursor + primary active
   */

  layout_array = g_array_new (FALSE, TRUE, sizeof (MetaKmsCrtcLayout));
  layout = (MetaKmsCrtcLayout) {
    .crtc = crtc,
    .layout = {
      .size = {
        .width = meta_kms_mode_get_width (mode),
        .height = meta_kms_mode_get_height (mode),
      },
    },
    .scale = 1.0,
  };
  g_array_append_val (layout_array, layout);
  meta_kms_cursor_manager_update_crtc_layout (cursor_manager, layout_array);

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
  meta_kms_update_assign_plane (update,
                                crtc,
                                cursor_plane,
                                cursor_buffer,
                                META_FIXED_16_RECTANGLE_INIT_INT (0, 0,
                                                                  cursor_width,
                                                                  cursor_width),
                                MTK_RECTANGLE_INIT (10, 10,
                                                    cursor_width,
                                                    cursor_width),
                                META_KMS_ASSIGN_PLANE_FLAG_NONE);

  feedback = meta_kms_device_process_update_sync (device, update,
                                                  META_KMS_UPDATE_FLAG_MODE_SET);
  meta_kms_feedback_unref (feedback);

  g_assert_true (meta_kms_crtc_is_active (crtc));

  drm_plane = drmModeGetPlane (meta_device_file_get_fd (device_file),
                               meta_kms_plane_get_id (cursor_plane));
  g_assert_nonnull (drm_plane);
  g_assert_cmpuint (drm_plane->crtc_id, !=, 0);
  g_assert_cmpuint (drm_plane->fb_id, !=, 0);
  drmModeFreePlane (drm_plane);

  /*
   * Invalidate the cursor so the cursor manager will attempt to assign
   * the cursor plane the next update.
   */

  seat = meta_backend_get_default_seat (backend);
  meta_kms_device_await_flush (device, crtc);
  fake_position_changed (seat, cursor_manager);
  meta_flush_input (test_context);

  /*
   * Disable the CRTC before the cursor update is processed.
   */

  empty_array = g_array_new (FALSE, TRUE, sizeof (MetaKmsCrtcLayout));
  meta_kms_cursor_manager_update_crtc_layout (cursor_manager, empty_array);
  update = meta_kms_update_new (device);
  meta_kms_update_mode_set (update, crtc, NULL, NULL);
  meta_kms_update_add_result_listener (update,
                                       &done_result_listener_vtable,
                                       NULL,
                                       &done,
                                       NULL);
  feedback = meta_kms_device_process_update_sync (device, update,
                                                  META_KMS_UPDATE_FLAG_MODE_SET);
  meta_kms_feedback_unref (feedback);

  while (!done)
    g_main_context_iteration (NULL, TRUE);

  drm_plane = drmModeGetPlane (meta_device_file_get_fd (device_file),
                               meta_kms_plane_get_id (cursor_plane));
  g_assert_nonnull (drm_plane);
  g_assert_cmpuint (drm_plane->crtc_id, ==, 0);
  g_assert_cmpuint (drm_plane->fb_id, ==, 0);
  drmModeFreePlane (drm_plane);

  meta_device_file_release (device_file);
}

static gpointer
schedule_process_in_impl (MetaThreadImpl  *thread_impl,
                          gpointer         user_data,
                          GError         **error)
{
  MetaKmsCrtc *crtc = META_KMS_CRTC (user_data);
  MetaKmsDevice *device = meta_kms_crtc_get_device (crtc);
  MetaKmsImplDevice *impl_device = meta_kms_device_get_impl_device (device);

  meta_kms_impl_device_schedule_process (impl_device, crtc);

  return NULL;
}

static gboolean
quit_loop (gpointer user_data)
{
  GMainLoop *loop = user_data;

  g_main_loop_quit (loop);

  return G_SOURCE_REMOVE;
}

static gpointer
quit_loop_timeout_in_impl (MetaThreadImpl  *thread_impl,
                           gpointer         user_data,
                           GError         **error)
{
  GMainLoop *loop = user_data;
  g_autoptr (GSource) timeout_source = NULL;

  timeout_source = meta_thread_impl_add_source (thread_impl,
                                                quit_loop,
                                                loop,
                                                NULL);
  g_source_set_ready_time (timeout_source,
                           g_get_monotonic_time () + s2us (2));

  return NULL;
}

static void
meta_test_kms_device_empty_update (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);
  MetaKmsCursorManager *cursor_manager = meta_kms_get_cursor_manager (kms);
  g_autoptr (GArray) layout_array = NULL;
  MetaKmsCrtcLayout layout;
  MetaKmsDevice *device;
  MetaKmsUpdate *update;
  MetaKmsCrtc *crtc;
  MetaKmsConnector *connector;
  MetaKmsMode *mode;
  MetaKmsPlane *primary_plane;
  g_autoptr (MetaDrmBuffer) primary_buffer = NULL;
  g_autoptr (MetaDrmBuffer) cursor_buffer = NULL;
  MetaKmsFeedback *feedback;

  device = meta_get_test_kms_device (test_context);
  crtc = meta_get_test_kms_crtc (device);
  connector = meta_get_test_kms_connector (device);
  mode = meta_kms_connector_get_preferred_mode (connector);
  primary_plane = meta_get_primary_test_plane_for (device, crtc);
  primary_buffer = meta_create_test_mode_dumb_buffer (device, mode);

  /*
   * Setup base state, mode + primary plane.
   */

  layout_array = g_array_new (FALSE, TRUE, sizeof (MetaKmsCrtcLayout));
  layout = (MetaKmsCrtcLayout) {
    .crtc = crtc,
    .layout = {
      .size = {
        .width = meta_kms_mode_get_width (mode),
        .height = meta_kms_mode_get_height (mode),
      },
    },
    .scale = 1.0,
  };
  g_array_append_val (layout_array, layout);
  meta_kms_cursor_manager_update_crtc_layout (cursor_manager, layout_array);

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

  feedback = meta_kms_device_process_update_sync (device, update,
                                                  META_KMS_UPDATE_FLAG_MODE_SET);
  meta_kms_feedback_unref (feedback);

  meta_thread_post_impl_task (META_THREAD (kms),
                              schedule_process_in_impl,
                              crtc, NULL,
                              NULL, NULL);
  g_autoptr (GMainLoop) loop = NULL;
  loop = g_main_loop_new (NULL, FALSE);
  meta_thread_post_impl_task (META_THREAD (kms),
                              quit_loop_timeout_in_impl,
                              loop, NULL,
                              NULL, NULL);

  g_main_loop_run (loop);
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
  g_test_add_func ("/backends/native/kms/device/discard-disabled",
                   meta_test_kms_device_discard_disabled);
  g_test_add_func ("/backends/native/kms/device/empty-update",
                   meta_test_kms_device_empty_update);
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
