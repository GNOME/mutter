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

#include <drm_fourcc.h>
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>

#include "backends/native/meta-drm-preparation.h"
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
#include "tests/drm-mock/drm-mock.h"
#include "tests/drm-mock/drm-mock-preparation.h"
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

  g_assert_true (meta_kms_device_get_kms (device) == kms);
  g_assert_cmpstr (meta_kms_device_get_driver_name (device), ==, "vkms");
  g_assert_true (meta_kms_device_uses_monotonic_clock (device));

  connectors = meta_kms_device_get_connectors (device);
  g_assert_cmpuint (g_list_length (connectors), ==, 1);
  connector = META_KMS_CONNECTOR (connectors->data);
  g_assert_true (meta_kms_connector_get_device (connector) == device);
  g_assert_nonnull (meta_kms_connector_get_preferred_mode (connector));

  crtcs = meta_kms_device_get_crtcs (device);
  g_assert_cmpuint (g_list_length (crtcs), ==, 1);
  crtc = META_KMS_CRTC (crtcs->data);
  g_assert_true (meta_kms_crtc_get_device (crtc) == device);

  planes = meta_kms_device_get_planes (device);
  g_assert_cmpuint (g_list_length (planes), ==, 2);
  primary_plane = meta_get_primary_test_plane_for (device, crtc);
  g_assert_nonnull (primary_plane);
  cursor_plane = meta_get_cursor_test_plane_for (device, crtc);
  g_assert_nonnull (cursor_plane);
  g_assert_true (cursor_plane != primary_plane);
  g_assert_nonnull (g_list_find (planes, primary_plane));
  g_assert_nonnull (g_list_find (planes, cursor_plane));
  g_assert_true (meta_kms_plane_get_device (primary_plane) == device);
  g_assert_true (meta_kms_plane_get_device (cursor_plane) == device);
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
meta_test_kms_device_castkms_display (void)
{
  MetaBackendNative *backend =
    META_BACKEND_NATIVE (meta_context_get_backend (test_context));
  MetaKms *kms = meta_backend_native_get_kms (backend);
  gboolean found_castkms = FALSE;

  for (GList *l = meta_kms_get_devices (kms); l; l = l->next)
    {
      MetaKmsDevice *device = l->data;

      if (!g_str_equal (meta_kms_device_get_driver_name (device), "castkms"))
        continue;

      for (GList *c = meta_kms_device_get_connectors (device); c; c = c->next)
        {
          const MetaKmsConnectorState *state =
            meta_kms_connector_get_current_state (c->data);

          if (!state)
            continue;

          found_castkms = TRUE;
          for (unsigned int frame = 0; frame < 4; frame++)
            {
              drmModeCrtc *crtc;
              drmModeFB2 *fb;
              int fd;

              meta_wait_for_presented (test_context);
              state = meta_kms_connector_get_current_state (c->data);
              g_assert_cmpuint (state->current_crtc_id, !=, 0);
              fd = open (meta_kms_device_get_path (device), O_RDONLY | O_CLOEXEC);
              g_assert_cmpint (fd, >=, 0);
              crtc = drmModeGetCrtc (fd, state->current_crtc_id);
              g_assert_nonnull (crtc);
              g_assert_true (crtc->mode_valid);
              g_assert_cmpuint (crtc->buffer_id, !=, 0);
              fb = drmModeGetFB2 (fd, crtc->buffer_id);
              g_assert_nonnull (fb);
              g_assert_cmpuint (fb->pixel_format, ==, DRM_FORMAT_XRGB8888);
              g_assert_cmpuint (fb->modifier, ==, DRM_FORMAT_MOD_LINEAR);
              g_assert_cmpuint (fb->width, ==, crtc->width);
              g_assert_cmpuint (fb->height, ==, crtc->height);
              g_assert_cmpuint (fb->pitches[0] % 4, ==, 0);
              drmModeFreeFB2 (fb);
              drmModeFreeCrtc (crtc);
              close (fd);
            }
        }
    }
  if (!found_castkms)
    g_test_skip ("No CastKMS output");
}

static void
assert_crtc_state_equals (const MetaKmsCrtcState *crtc_state1,
                          const MetaKmsCrtcState *crtc_state2)
{
  g_assert_cmpint (crtc_state1->is_active, ==, crtc_state2->is_active);
  g_assert_true (mtk_rectangle_equal (&crtc_state1->rect, &crtc_state2->rect));
  g_assert_cmpint (crtc_state1->is_drm_mode_valid,
                   ==,
                   crtc_state2->is_drm_mode_valid);
  if (crtc_state1->is_drm_mode_valid)
    {
      g_assert_cmpstr (crtc_state1->drm_mode.name,
                       ==,
                       crtc_state2->drm_mode.name);
    }

  g_assert_true (crtc_state1->vrr.enabled == crtc_state2->vrr.enabled);
  g_assert_cmpint (crtc_state1->constraints.supported,
                   ==,
                   crtc_state2->constraints.supported);
  g_assert_cmpuint (crtc_state1->constraints.id,
                    ==,
                    crtc_state2->constraints.id);

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

  new_state.vrr.enabled = crtc_state->vrr.enabled;

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
  g_assert_true (mtk_rectangle_equal (&crtc_state.rect, &mode_rect));

  g_assert_nonnull (meta_kms_connector_get_current_state (connector));
  connector_state =
    copy_connector_state (meta_kms_connector_get_current_state (connector));
  g_assert_cmpuint (connector_state.current_crtc_id,
                    ==,
                    meta_kms_crtc_get_id (crtc));

  meta_kms_update_states_sync (meta_kms_device_get_kms (device));
  assert_crtc_state_equals (&crtc_state,
                            meta_kms_crtc_get_current_state (crtc));
  assert_connector_state_equals (&connector_state,
                                 meta_kms_connector_get_current_state (connector));

  release_crtc_state (&crtc_state);
  release_connector_state (&connector_state);
}

typedef struct
{
  int expected_error;
  unsigned int feedback_count;
  unsigned int destroy_count;
} UpdateResultData;

static void
update_result_feedback (const MetaKmsFeedback *feedback,
                          gpointer               user_data)
{
  UpdateResultData *result = user_data;

  result->feedback_count++;
  g_assert_cmpuint (result->destroy_count, ==, 0);
  g_assert_cmpuint (result->feedback_count, ==, 1);
  if (result->expected_error == ECANCELED)
    {
      g_assert_false (meta_kms_feedback_did_pass (feedback));
      g_assert_error (meta_kms_feedback_get_error (feedback), META_KMS_ERROR,
                      META_KMS_ERROR_DISCARDED);
    }
  else if (result->expected_error)
    {
      g_assert_false (meta_kms_feedback_did_pass (feedback));
      g_assert_error (meta_kms_feedback_get_error (feedback), G_IO_ERROR,
                      g_io_error_from_errno (result->expected_error));
    }
  else
    {
      g_assert_true (meta_kms_feedback_did_pass (feedback));
    }
}

static void
update_result_destroy (gpointer user_data)
{
  UpdateResultData *result = user_data;

  result->destroy_count++;
  g_assert_cmpuint (result->feedback_count, ==, 1);
  g_assert_cmpuint (result->destroy_count, ==, 1);
}

static const MetaKmsResultListenerVtable update_result_listener_vtable = {
  .feedback = update_result_feedback,
};

static void
meta_test_kms_device_rejected_mode_set (void)
{
  MetaKmsDevice *device = meta_get_test_kms_device (test_context);
  MetaKmsCrtc *crtc = meta_get_test_kms_crtc (device);
  MetaKmsConnector *connector = meta_get_test_kms_connector (device);
  MetaKmsCrtcState crtc_state;
  MetaKmsConnectorState connector_state;
  const int errors[] = { EINVAL, EBUSY };
  size_t i;

  if (META_IS_KMS_IMPL_DEVICE_SIMPLE (meta_kms_device_get_impl_device (device)))
    {
      g_test_skip ("Atomic rejection requires an atomic KMS device");
      return;
    }

  meta_test_kms_device_mode_set ();
  crtc_state = copy_crtc_state (meta_kms_crtc_get_current_state (crtc));
  connector_state =
    copy_connector_state (meta_kms_connector_get_current_state (connector));

  for (i = 0; i < G_N_ELEMENTS (errors); i++)
    {
      MetaKmsUpdate *update = meta_kms_update_new (device);
      g_autoptr (MetaKmsFeedback) feedback = NULL;
      UpdateResultData result = { .expected_error = errors[i] };

      meta_kms_update_mode_set (update, crtc, NULL, NULL);
      meta_kms_update_add_result_listener (update,
                                           &update_result_listener_vtable,
                                           NULL,
                                           &result,
                                           update_result_destroy);
      drm_mock_queue_error (DRM_MOCK_CALL_ATOMIC_COMMIT, errors[i]);
      feedback = meta_kms_device_process_update_sync (device, update,
                                                      META_KMS_UPDATE_FLAG_MODE_SET);
      g_assert_false (meta_kms_feedback_did_pass (feedback));
      g_assert_error (meta_kms_feedback_get_error (feedback), G_IO_ERROR,
                      g_io_error_from_errno (errors[i]));
      assert_crtc_state_equals (&crtc_state,
                                meta_kms_crtc_get_current_state (crtc));
      assert_connector_state_equals (&connector_state,
                                     meta_kms_connector_get_current_state (connector));

      meta_kms_update_states_sync (meta_kms_device_get_kms (device));
      assert_crtc_state_equals (&crtc_state,
                                meta_kms_crtc_get_current_state (crtc));
      assert_connector_state_equals (&connector_state,
                                     meta_kms_connector_get_current_state (connector));

      while (!result.destroy_count)
        g_main_context_iteration (NULL, TRUE);
      g_assert_cmpuint (result.feedback_count, ==, 1);
      g_assert_cmpuint (result.destroy_count, ==, 1);
    }

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

static gpointer
change_inhibition_in_impl (MetaThreadImpl  *thread_impl,
                           gpointer         user_data,
                           GError         **error)
{
  MetaKmsUpdate *update = user_data;
  MetaKmsImplDevice *impl_device =
    meta_kms_device_get_impl_device (meta_kms_update_get_device (update));

  meta_kms_impl_device_set_updates_inhibited (impl_device, META_KMS_INHIBIT_ALL);
  meta_kms_impl_device_handle_update (impl_device, update,
                                      META_KMS_UPDATE_FLAG_NONE);
  meta_kms_impl_device_set_updates_inhibited (impl_device,
                                              META_KMS_INHIBIT_NON_TEST_ONLY);
  meta_kms_impl_device_set_updates_inhibited (impl_device, META_KMS_INHIBIT_NONE);

  return NULL;
}

static void
meta_test_kms_device_inhibition_transition (void)
{
  MetaKmsDevice *device = meta_get_test_kms_device (test_context);
  MetaKmsCrtc *crtc = meta_get_test_kms_crtc (device);
  MetaKmsConnector *connector = meta_get_test_kms_connector (device);
  MetaKmsMode *mode = meta_kms_connector_get_preferred_mode (connector);
  g_autoptr (MetaDrmBuffer) buffer = NULL;
  MetaKmsUpdate *update;
  UpdateResultData result = { 0 };

  meta_test_kms_device_mode_set ();
  buffer = meta_create_test_mode_dumb_buffer (device, mode);
  update = meta_kms_update_new (device);
  meta_kms_update_assign_plane (update, crtc,
                                meta_get_primary_test_plane_for (device, crtc),
                                buffer,
                                meta_get_mode_fixed_rect_16 (mode),
                                meta_get_mode_rect (mode),
                                META_KMS_ASSIGN_PLANE_FLAG_NONE);
  meta_kms_update_add_result_listener (update,
                                       &update_result_listener_vtable,
                                       NULL, &result,
                                       update_result_destroy);
  g_clear_object (&buffer);
  meta_thread_run_impl_task_sync (META_THREAD (meta_kms_device_get_kms (device)),
                                  change_inhibition_in_impl, update, NULL);
  while (!result.destroy_count)
    g_main_context_iteration (NULL, TRUE);
  g_assert_cmpuint (result.feedback_count, ==, 1);
  g_assert_cmpuint (result.destroy_count, ==, 1);
}

typedef enum
{
  BUSY_UPDATE_TRANSIENT,
  BUSY_UPDATE_PERSISTENT,
  BUSY_UPDATE_CANCEL,
  BUSY_UPDATE_SUCCESSOR,
  BUSY_UPDATE_MODE_SET,
  BUSY_UPDATE_INHIBIT,
  BUSY_UPDATE_TERMINAL_ERROR,
} BusyUpdateScenario;

typedef struct
{
  BusyUpdateScenario scenario;
  MetaKmsCrtc *crtc;
  MetaKmsUpdate *first;
  MetaKmsUpdate *successor;
  GSource *action_source;
} BusyUpdates;

static gboolean
finish_busy_action (gpointer user_data)
{
  BusyUpdates *updates = user_data;
  MetaKmsUpdate *update;
  MetaKmsCrtc *crtc = updates->crtc;
  MetaKmsDevice *device = meta_kms_crtc_get_device (crtc);
  MetaKmsImplDevice *impl_device =
    meta_kms_device_get_impl_device (device);

  if (drm_mock_count_errors (DRM_MOCK_CALL_ATOMIC_COMMIT) == 1000)
    {
      g_source_set_ready_time (updates->action_source,
                               g_get_monotonic_time () + 1000);
      return G_SOURCE_CONTINUE;
    }

  g_assert_cmpuint (drm_mock_clear_errors (DRM_MOCK_CALL_ATOMIC_COMMIT), >, 0);
  if (updates->successor)
    {
      drm_mock_queue_error (DRM_MOCK_CALL_ATOMIC_COMMIT, EBUSY);
      drm_mock_queue_error (DRM_MOCK_CALL_ATOMIC_COMMIT, EBUSY);
      meta_kms_impl_device_handle_update (impl_device, updates->successor,
                                          META_KMS_UPDATE_FLAG_NONE);
    }
  else
    {
      if (updates->scenario == BUSY_UPDATE_MODE_SET)
        {
          g_autoptr (MetaKmsFeedback) feedback = NULL;

          update = meta_kms_update_new (device);
          meta_kms_update_mode_set (update, crtc, NULL, NULL);
          feedback = meta_kms_impl_device_process_update (impl_device, update,
                                                          META_KMS_UPDATE_FLAG_MODE_SET);
          g_assert_true (meta_kms_feedback_did_pass (feedback));
        }
      else if (updates->scenario == BUSY_UPDATE_INHIBIT)
        {
          meta_kms_impl_device_set_updates_inhibited (impl_device,
                                                      META_KMS_INHIBIT_ALL);
          meta_kms_impl_device_set_updates_inhibited (impl_device,
                                                      META_KMS_INHIBIT_NONE);
        }
      else
        {
          meta_kms_impl_device_discard_pending_page_flips (impl_device);
        }
    }
  return G_SOURCE_REMOVE;
}

static gpointer
queue_busy_updates_in_impl (MetaThreadImpl  *thread_impl,
                            gpointer         user_data,
                            GError         **error)
{
  BusyUpdates *updates = user_data;
  MetaKmsImplDevice *impl_device =
    meta_kms_device_get_impl_device (meta_kms_crtc_get_device (updates->crtc));

  meta_kms_impl_device_handle_update (impl_device, updates->first,
                                      META_KMS_UPDATE_FLAG_NONE);
  updates->action_source =
    meta_thread_impl_add_source (thread_impl, finish_busy_action, updates, NULL);
  g_source_set_priority (updates->action_source, G_PRIORITY_HIGH);
  g_source_unref (updates->action_source);
  return NULL;
}

static void
meta_test_kms_device_busy_update (gconstpointer user_data)
{
  BusyUpdateScenario scenario = GPOINTER_TO_INT (user_data);
  gboolean persistent = scenario == BUSY_UPDATE_PERSISTENT;
  gboolean cancel = scenario == BUSY_UPDATE_CANCEL ||
                    scenario == BUSY_UPDATE_MODE_SET ||
                    scenario == BUSY_UPDATE_INHIBIT;
  gboolean successor = scenario == BUSY_UPDATE_SUCCESSOR;
  MetaKmsDevice *device = meta_get_test_kms_device (test_context);
  MetaKmsCrtc *crtc = meta_get_test_kms_crtc (device);
  MetaKmsConnector *connector = meta_get_test_kms_connector (device);
  MetaKmsMode *mode = meta_kms_connector_get_preferred_mode (connector);
  g_autoptr (MetaDrmBuffer) buffer = NULL;
  g_autoptr (MetaKmsFeedback) feedback = NULL;
  UpdateResultData result = {
    .expected_error = cancel ? ECANCELED : persistent ? EBUSY :
                      scenario == BUSY_UPDATE_TERMINAL_ERROR ? EINVAL : 0,
  };
  UpdateResultData successor_result = { 0 };
  BusyUpdates updates = { .scenario = scenario, .crtc = crtc };
  MetaKmsUpdate *update;
  unsigned int i, remaining;
  int64_t started_us;

  if (META_IS_KMS_IMPL_DEVICE_SIMPLE (meta_kms_device_get_impl_device (device)))
    {
      g_test_skip ("Busy retry requires an atomic KMS device");
      return;
    }

  meta_test_kms_device_mode_set ();
  buffer = meta_create_test_mode_dumb_buffer (device, mode);
  update = meta_kms_update_new (device);
  meta_kms_update_assign_plane (update, crtc,
                                meta_get_primary_test_plane_for (device, crtc),
                                buffer,
                                meta_get_mode_fixed_rect_16 (mode),
                                meta_get_mode_rect (mode),
                                META_KMS_ASSIGN_PLANE_FLAG_NONE);
  meta_kms_update_add_result_listener (update,
                                       &update_result_listener_vtable,
                                       NULL, &result,
                                       update_result_destroy);

  updates.first = update;
  if (successor)
    {
      updates.successor = meta_kms_update_new (device);
      meta_kms_update_assign_plane (updates.successor, crtc,
                                    meta_get_primary_test_plane_for (device, crtc),
                                    buffer,
                                    meta_get_mode_fixed_rect_16 (mode),
                                    meta_get_mode_rect (mode),
                                    META_KMS_ASSIGN_PLANE_FLAG_NONE);
      meta_kms_update_add_result_listener (updates.successor,
                                           &update_result_listener_vtable,
                                           NULL, &successor_result,
                                           update_result_destroy);
    }

  for (i = 0; i < (persistent || cancel || successor ? 1000 : 3); i++)
    drm_mock_queue_error (DRM_MOCK_CALL_ATOMIC_COMMIT, EBUSY);
  if (scenario == BUSY_UPDATE_TERMINAL_ERROR)
    drm_mock_queue_error (DRM_MOCK_CALL_ATOMIC_COMMIT, EINVAL);

  started_us = g_get_monotonic_time ();
  if (cancel || successor)
    meta_thread_run_impl_task_sync (META_THREAD (meta_kms_device_get_kms (device)),
                                    queue_busy_updates_in_impl, &updates, NULL);
  else
    meta_kms_device_post_update (device, update, META_KMS_UPDATE_FLAG_NONE);
  g_clear_object (&buffer);
  while (!result.destroy_count || (successor && !successor_result.destroy_count))
    g_main_context_iteration (NULL, TRUE);

  remaining = drm_mock_clear_errors (DRM_MOCK_CALL_ATOMIC_COMMIT);
  if (persistent)
    {
      g_assert_cmpuint (remaining, >, 0);
      g_assert_cmpuint (remaining, <, 999);
      g_assert_cmpint (g_get_monotonic_time () - started_us, >=, 100 * 1000);
    }
  else
    {
      g_assert_cmpuint (remaining, ==, 0);
    }
  g_assert_cmpuint (result.feedback_count, ==, 1);
  g_assert_cmpuint (result.destroy_count, ==, 1);

  update = meta_kms_update_new (device);
  meta_kms_update_mode_set (update, crtc, NULL, NULL);
  feedback = meta_kms_device_process_update_sync (device, update,
                                                  META_KMS_UPDATE_FLAG_MODE_SET);
  g_assert_true (meta_kms_feedback_did_pass (feedback));
}

typedef enum
{
  PREPARATION_RELEASE,
  PREPARATION_CANCEL,
  PREPARATION_HANGUP,
} PreparationScenario;

typedef struct
{
  PreparationScenario scenario;
  MetaKmsImplDevice *impl_device;
  MetaKmsUpdate *update;
  int action_done;
} PreparationUpdate;

static gboolean
finish_preparation_action (gpointer user_data)
{
  PreparationUpdate *pending = user_data;

  g_assert_true (drm_mock_preparation_was_issued ());
  g_assert_false (drm_mock_preparation_was_closed ());
  switch (pending->scenario)
    {
    case PREPARATION_RELEASE:
      drm_mock_preparation_release ();
      break;
    case PREPARATION_CANCEL:
      meta_kms_impl_device_discard_pending_page_flips (pending->impl_device);
      g_assert_true (drm_mock_preparation_was_closed ());
      break;
    case PREPARATION_HANGUP:
      drm_mock_preparation_hangup ();
      break;
    }
  g_atomic_int_set (&pending->action_done, TRUE);
  g_main_context_wakeup (g_main_context_default ());
  return G_SOURCE_REMOVE;
}

static gpointer
queue_preparation_update (MetaThreadImpl  *thread_impl,
                           gpointer         user_data,
                           GError         **error)
{
  PreparationUpdate *pending = user_data;
  g_autoptr (GSource) source = NULL;

  drm_mock_preparation_delay_next ();
  meta_kms_impl_device_handle_update (pending->impl_device, pending->update,
                                      META_KMS_UPDATE_FLAG_NONE);
  source = meta_thread_impl_add_source (thread_impl, finish_preparation_action,
                                        pending, NULL);
  g_source_set_ready_time (source, g_get_monotonic_time () + 150 * 1000);
  return NULL;
}

static gpointer
clear_preparation_mock (MetaThreadImpl  *thread_impl,
                        gpointer         user_data,
                        GError         **error)
{
  drm_mock_preparation_clear ();
  return NULL;
}

static void
meta_test_kms_device_preparation (gconstpointer user_data)
{
  PreparationScenario scenario = GPOINTER_TO_INT (user_data);
  MetaKmsDevice *device = meta_get_test_kms_device (test_context);
  MetaKmsImplDevice *impl_device = meta_kms_device_get_impl_device (device);
  MetaKmsCrtc *crtc = meta_get_test_kms_crtc (device);
  MetaKmsConnector *connector = meta_get_test_kms_connector (device);
  MetaKmsMode *mode = meta_kms_connector_get_preferred_mode (connector);
  g_autoptr (MetaDrmBuffer) buffer = NULL;
  UpdateResultData result = {
    .expected_error = scenario == PREPARATION_CANCEL ? ECANCELED :
                      scenario == PREPARATION_HANGUP ? ENOTTY : 0,
  };
  PreparationUpdate pending = { .scenario = scenario, .impl_device = impl_device };
  uint64_t capability = 0;
  int fd;
  int64_t started_us;

  fd = open (meta_kms_device_get_path (device), O_RDWR | O_CLOEXEC);
  g_assert_cmpint (fd, >=, 0);
  drmGetCap (fd, DRM_CAP_ATOMIC_PREPARATION, &capability);
  close (fd);
  if (!capability || META_IS_KMS_IMPL_DEVICE_SIMPLE (impl_device))
    {
      g_test_skip ("Requires atomic preparation on the virtual device");
      return;
    }

  meta_test_kms_device_mode_set ();
  buffer = meta_create_test_mode_dumb_buffer (device, mode);
  pending.update = meta_kms_update_new (device);
  meta_kms_update_assign_plane (pending.update, crtc,
                                meta_get_primary_test_plane_for (device, crtc), buffer,
                                meta_get_mode_fixed_rect_16 (mode), meta_get_mode_rect (mode),
                                META_KMS_ASSIGN_PLANE_FLAG_NONE);
  meta_kms_update_add_result_listener (pending.update, &update_result_listener_vtable,
                                       NULL, &result, update_result_destroy);
  started_us = g_get_monotonic_time ();
  meta_thread_run_impl_task_sync (META_THREAD (meta_kms_device_get_kms (device)),
                                  queue_preparation_update, &pending, NULL);
  g_clear_object (&buffer);
  while (!result.destroy_count || !g_atomic_int_get (&pending.action_done))
    g_main_context_iteration (NULL, TRUE);
  g_assert_cmpint (g_get_monotonic_time () - started_us, >=, 150 * 1000);
  g_assert_cmpuint (result.feedback_count, ==, 1);
  g_assert_cmpuint (result.destroy_count, ==, 1);
  meta_thread_run_impl_task_sync (META_THREAD (meta_kms_device_get_kms (device)),
                                  clear_preparation_mock, NULL, NULL);
}

static void
init_tests (void)
{
  g_test_add_func ("/backends/native/kms/device/castkms-display",
                   meta_test_kms_device_castkms_display);
  g_test_add_func ("/backends/native/kms/device/sanity",
                   meta_test_kms_device_sanity);
  g_test_add_func ("/backends/native/kms/device/mode-set",
                   meta_test_kms_device_mode_set);
  g_test_add_func ("/backends/native/kms/device/rejected-mode-set",
                   meta_test_kms_device_rejected_mode_set);
  g_test_add_func ("/backends/native/kms/device/power-save",
                   meta_test_kms_device_power_save);
  g_test_add_func ("/backends/native/kms/device/discard-disabled",
                   meta_test_kms_device_discard_disabled);
  g_test_add_func ("/backends/native/kms/device/empty-update",
                   meta_test_kms_device_empty_update);
  g_test_add_func ("/backends/native/kms/device/inhibition-transition",
                   meta_test_kms_device_inhibition_transition);
  g_test_add_data_func ("/backends/native/kms/device/busy-update/transient",
                        GINT_TO_POINTER (BUSY_UPDATE_TRANSIENT), meta_test_kms_device_busy_update);
  g_test_add_data_func ("/backends/native/kms/device/busy-update/persistent",
                        GINT_TO_POINTER (BUSY_UPDATE_PERSISTENT), meta_test_kms_device_busy_update);
  g_test_add_data_func ("/backends/native/kms/device/busy-update/cancel",
                        GINT_TO_POINTER (BUSY_UPDATE_CANCEL), meta_test_kms_device_busy_update);
  g_test_add_data_func ("/backends/native/kms/device/busy-update/successor",
                        GINT_TO_POINTER (BUSY_UPDATE_SUCCESSOR), meta_test_kms_device_busy_update);
  g_test_add_data_func ("/backends/native/kms/device/busy-update/mode-set",
                        GINT_TO_POINTER (BUSY_UPDATE_MODE_SET), meta_test_kms_device_busy_update);
  g_test_add_data_func ("/backends/native/kms/device/busy-update/inhibit",
                        GINT_TO_POINTER (BUSY_UPDATE_INHIBIT), meta_test_kms_device_busy_update);
  g_test_add_data_func ("/backends/native/kms/device/busy-update/terminal-error",
                        GINT_TO_POINTER (BUSY_UPDATE_TERMINAL_ERROR), meta_test_kms_device_busy_update);
  g_test_add_data_func ("/backends/native/kms/device/preparation/release",
                        GINT_TO_POINTER (PREPARATION_RELEASE), meta_test_kms_device_preparation);
  g_test_add_data_func ("/backends/native/kms/device/preparation/cancel",
                        GINT_TO_POINTER (PREPARATION_CANCEL), meta_test_kms_device_preparation);
  g_test_add_data_func ("/backends/native/kms/device/preparation/hangup",
                        GINT_TO_POINTER (PREPARATION_HANGUP), meta_test_kms_device_preparation);
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
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  init_tests ();

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_CAN_SKIP);
}
