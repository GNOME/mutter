/*
 * Copyright (C) 2026 Red Hat Inc.
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
#include "backends/native/meta-crtc-kms.h"
#include "backends/native/meta-device-pool.h"
#include "backends/native/meta-drm-buffer.h"
#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-impl-device-atomic.h"
#include "backends/native/meta-kms.h"
#include "backends/native/meta-onscreen-native.h"
#include "meta-test/meta-context-test.h"
#include "tests/drm-mock/drm-mock.h"
#include "tests/meta-monitor-test-utils.h"
#include "tests/meta-kms-test-utils.h"
#include "tests/meta-test-utils.h"
#include "tests/meta-wayland-test-driver.h"
#include "tests/meta-wayland-test-utils.h"
#include "wayland/meta-window-wayland.h"

typedef struct
{
  GMainLoop *loop;

  struct {
    uint32_t fb_id;
    uint32_t crtc_fb_id;
    uint32_t last_fb_id;
  } scanout;

  struct {
    gboolean presented;
    gboolean before_paint;
  } fail_twice;
} KmsRenderingTest;

static MetaContext *test_context;

static gboolean
is_atomic_mode_setting (MetaKmsDevice *kms_device)
{
  MetaKmsImplDevice *kms_impl_device;

  kms_impl_device = meta_kms_device_get_impl_device (kms_device);

  return META_IS_KMS_IMPL_DEVICE_ATOMIC (kms_impl_device);
}

static void
on_scanout_before_update (ClutterStage     *stage,
                          ClutterStageView *stage_view,
                          ClutterFrame     *frame,
                          KmsRenderingTest *test)
{
  test->scanout.fb_id = 0;
  test->scanout.crtc_fb_id = 0;

  test->fail_twice.presented = FALSE;
}

static void
on_scanout_before_paint (ClutterStage     *stage,
                         ClutterStageView *stage_view,
                         ClutterFrame     *frame,
                         KmsRenderingTest *test)
{
  CoglScanout *scanout;
  CoglScanoutBuffer *scanout_buffer;
  MetaDrmBuffer *buffer;

  scanout = clutter_stage_view_peek_scanout (stage_view);
  if (!scanout)
    return;

  scanout_buffer = cogl_scanout_get_buffer (scanout);
  g_assert_true (META_IS_DRM_BUFFER (scanout_buffer));
  buffer = META_DRM_BUFFER (scanout_buffer);
  test->scanout.fb_id = meta_drm_buffer_get_fb_id (buffer);
  g_assert_cmpuint (test->scanout.fb_id, >, 0);
  test->scanout.last_fb_id = test->scanout.fb_id;

  test->fail_twice.before_paint = TRUE;
}

static void
on_scanout_presented (ClutterStage     *stage,
                      ClutterStageView *stage_view,
                      ClutterFrameInfo *frame_info,
                      KmsRenderingTest *test)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaDevicePool *device_pool;
  CoglFramebuffer *fb;
  MetaCrtc *crtc;
  MetaKmsCrtc *kms_crtc;
  MetaKmsDevice *kms_device;
  MetaDeviceFile *device_file;
  GError *error = NULL;
  drmModeCrtc *drm_crtc;

  device_pool = meta_backend_native_get_device_pool (backend_native);

  fb = clutter_stage_view_get_onscreen (stage_view);
  crtc = meta_onscreen_native_get_crtc (META_ONSCREEN_NATIVE (fb));
  kms_crtc = meta_crtc_kms_get_kms_crtc (META_CRTC_KMS (crtc));
  kms_device = meta_kms_crtc_get_device (kms_crtc);

  device_file = meta_device_pool_open (device_pool,
                                       meta_kms_device_get_path (kms_device),
                                       META_DEVICE_FILE_FLAG_TAKE_CONTROL,
                                       &error);
  if (!device_file)
    g_error ("Failed to open KMS device: %s", error->message);

  drm_crtc = drmModeGetCrtc (meta_device_file_get_fd (device_file),
                             meta_kms_crtc_get_id (kms_crtc));
  g_assert_nonnull (drm_crtc);
  test->scanout.crtc_fb_id = drm_crtc->buffer_id;
  drmModeFreeCrtc (drm_crtc);

  meta_device_file_release (device_file);

  test->fail_twice.presented = TRUE;
}

/*
 * Test that a attaching the same scanout capable buffer with damage twice in a
 * row, where the the commits succeeds the TEST_ONLY commit, but not the actual
 * commits, is handled correctly.
 */
static void
meta_test_kms_render_client_scanout_fail_twice (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaWaylandCompositor *wayland_compositor =
    meta_context_get_wayland_compositor (test_context);
  MetaDisplay *display = meta_context_get_display (test_context);
  MetaCompositor *compositor = meta_display_get_compositor (display);
  ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
  MetaKms *kms = meta_backend_native_get_kms (META_BACKEND_NATIVE (backend));
  MetaKmsDevice *kms_device = meta_kms_get_devices (kms)->data;
  KmsRenderingTest test;
  MetaWaylandTestClient *wayland_test_client;
  g_autoptr (MetaWaylandTestDriver) test_driver = NULL;
  GList *views;
  ClutterStageView *view;
  gulong before_update_handler_id;
  gulong before_paint_handler_id;
  gulong presented_handler_id;
  MetaWindow *window;

  if (!is_atomic_mode_setting (kms_device))
    {
      g_test_skip ("Test only implemented with atomic mode setting");
      return;
    }

  test_driver = meta_wayland_test_driver_new (wayland_compositor);
  meta_wayland_test_driver_set_property (test_driver,
                                         "gpu-path",
                                         meta_kms_device_get_path (kms_device));

  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "dma-buf-scanout",
                                            "reuse-buffer",
                                            NULL);
  g_assert_nonnull (wayland_test_client);

  test = (KmsRenderingTest) {
    .loop = g_main_loop_new (NULL, FALSE),
  };

  views = clutter_stage_peek_stage_views (stage);
  g_assert_cmpuint (g_list_length (views), ==, 1);
  view = CLUTTER_STAGE_VIEW (views->data);

  clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));

  before_update_handler_id =
    g_signal_connect (stage, "before-update",
                      G_CALLBACK (on_scanout_before_update), &test);
  before_paint_handler_id =
    g_signal_connect (stage, "before-paint",
                      G_CALLBACK (on_scanout_before_paint), &test);
  presented_handler_id =
    g_signal_connect (stage, "presented",
                      G_CALLBACK (on_scanout_presented), &test);

  window = meta_wait_for_client_window (test_context,
                                        "dma-buf-scanout-test");

  /* Wait for effects to settle, and test client buffer to be scanned out. */
  meta_wait_for_window_shown (window);
  meta_wait_for_effects (window);
  while (meta_window_wayland_get_pending_serial (META_WINDOW_WAYLAND (window),
                                                 NULL) ||
         clutter_stage_view_has_redraw_clip (view) ||
         !test.fail_twice.presented ||
         test.scanout.last_fb_id == 0 ||
         test.scanout.last_fb_id != test.scanout.crtc_fb_id)
    g_main_context_iteration (NULL, TRUE);

  g_assert_cmpuint (test.scanout.last_fb_id, !=, 0);
  g_assert_cmpuint (test.scanout.last_fb_id, ==, test.scanout.crtc_fb_id);
  g_assert_true (test.fail_twice.presented);

  meta_compositor_disable_unredirect (compositor);
  meta_wait_for_presented (test_context);
  meta_compositor_enable_unredirect (compositor);

  /* Trigger artificial delays effectively triggering triple buffering. */
  meta_inhibit_kms_updates (kms_device, META_KMS_INHIBIT_NON_TEST_ONLY);

  g_debug ("Fail a first time");
  meta_wayland_test_driver_emit_sync_event (test_driver, 1);
  test.fail_twice.before_paint = FALSE;
  while (!test.fail_twice.before_paint)
    g_main_context_iteration (NULL, TRUE);
  g_assert_cmpuint (test.scanout.fb_id, !=, 0);

  g_debug ("Fail a second time");
  meta_wayland_test_driver_emit_sync_event (test_driver, 1);
  test.fail_twice.before_paint = FALSE;
  while (!test.fail_twice.before_paint)
    g_main_context_iteration (NULL, TRUE);
  g_assert_cmpuint (test.scanout.fb_id, !=, 0);

  /* Fail two consequitive direct scanout commits. */
  drm_mock_queue_error (DRM_MOCK_CALL_ATOMIC_COMMIT, EINVAL);
  drm_mock_queue_error (DRM_MOCK_CALL_ATOMIC_COMMIT, EINVAL);

  /* Expect the first scanout to fail. */
  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "*Direct scanout page flip failed*");

  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "*Direct scanout page flip failed*");

  meta_inhibit_kms_updates (kms_device, META_KMS_INHIBIT_NONE);

  /* Make sure the wl_buffer is destroyed. */
  meta_wayland_test_driver_emit_sync_event (test_driver, 0);
  meta_wayland_test_client_finish (wayland_test_client);

  /* Wait for presentation event of a follow up composition, to ensure
   * all our scanout results have been processed.
   */
  meta_wait_for_presented (test_context);

  /* Fake a hot-plug to destroy the onscreen and trigger all weak refs. */
  meta_fake_hotplug (test_context);
  meta_wait_for_presented (test_context);

  g_test_assert_expected_messages ();

  g_signal_handler_disconnect (stage, before_update_handler_id);
  g_signal_handler_disconnect (stage, before_paint_handler_id);
  g_signal_handler_disconnect (stage, presented_handler_id);

  g_main_loop_unref (test.loop);
}

static void
init_tests (void)
{
  g_test_add_func ("/backends/native/kms/render/client-scanout-fail-twice",
                   meta_test_kms_render_client_scanout_fail_twice);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;
  g_autoptr (GError) error = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_VKMS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  test_context = context;

  init_tests ();

  test_context = context;

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_CAN_SKIP);
}
