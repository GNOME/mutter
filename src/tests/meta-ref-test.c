/*
 * Copyright (C) 2021 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * To update or initialize reference images for tests, set the
 * MUTTER_REF_TEST_UPDATE environment variable.
 *
 * The MUTTER_REF_TEST_UPDATE is interpreted as a comma separated list of
 * regular expressions. If the test path matches any of the regular
 * expressions, the test reference image will be updated, unless the
 * existing reference image is pixel identical to the newly created one.
 *
 * If MUTTER_REF_TEST_ENSURE_ONLY is set to "1", in combination with
 * MUTTER_REF_TEST_UPDATE being set, only reference images that doesn't already
 * exist are updated.
 *
 * Updating test reference images also requires using a software OpenGL
 * renderer, which can be achieved using MESA_LOADER_DRIVER_OVERRIDE=swrast
 *
 * For example, for the test case '/path/to/test/case', run the test
 * inside
 *
 * ```
 * env MESA_LOADER_DRIVER_OVERRIDE=swrast MUTTER_REF_TEST_UPDATE='/path/to/test/case`
 * ```
 *
 */

#include "config.h"

#include "tests/meta-ref-test.h"

#include <cairo.h>
#include <glib.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-crtc.h"
#include "backends/meta-gpu.h"
#include "backends/meta-stage-private.h"
#include "clutter/clutter/clutter-stage-view-private.h"
#include "meta/compositor.h"
#include "tests/meta-ref-test-utils.h"

static void
capture_view_into (ClutterStageView *view,
                   CoglContext      *context,
                   MtkRectangle     *rect,
                   uint8_t          *buffer,
                   int               stride)
{
  CoglFramebuffer *framebuffer;
  CoglBitmap *bitmap;
  MtkRectangle view_layout;
  float view_scale;
  float texture_width;
  float texture_height;
  int x, y;

  framebuffer = clutter_stage_view_get_framebuffer (view);

  view_scale = clutter_stage_view_get_scale (view);
  texture_width = roundf (rect->width * view_scale);
  texture_height = roundf (rect->height * view_scale);

  bitmap = cogl_bitmap_new_for_data (context,
                                     (int) texture_width,
                                     (int) texture_height,
                                     COGL_PIXEL_FORMAT_CAIRO_ARGB32_COMPAT,
                                     stride,
                                     buffer);

  clutter_stage_view_get_layout (view, &view_layout);

  x = (int) roundf ((rect->x - view_layout.x) * view_scale);
  y = (int) roundf ((rect->y - view_layout.y) * view_scale);
  cogl_framebuffer_read_pixels_into_bitmap (framebuffer,
                                            x, y,
                                            COGL_READ_PIXELS_COLOR_BUFFER,
                                            bitmap);

  g_object_unref (bitmap);
}

typedef struct
{
  MetaStageWatch *watch;
  GMainLoop *loop;

  cairo_surface_t *out_image;
} CaptureViewData;

static void
on_after_paint (MetaStage        *stage,
                ClutterStageView *view,
                const MtkRegion  *redraw_clip,
                ClutterFrame     *frame,
                gpointer          user_data)
{
  CaptureViewData *data = user_data;
  MtkRectangle rect;
  float view_scale;
  int texture_width, texture_height;
  cairo_surface_t *image;
  uint8_t *buffer;
  int stride;
  ClutterContext *context =
    clutter_actor_get_context (CLUTTER_ACTOR (stage));
  ClutterBackend *backend = clutter_context_get_backend (context);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (backend);

  meta_stage_remove_watch (stage, data->watch);
  data->watch = NULL;

  clutter_stage_view_get_layout (view, &rect);
  view_scale = clutter_stage_view_get_scale (view);
  texture_width = (int) roundf (rect.width * view_scale);
  texture_height = (int) roundf (rect.height * view_scale);
  image = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                      texture_width, texture_height);
  cairo_surface_set_device_scale (image, view_scale, view_scale);

  buffer = cairo_image_surface_get_data (image);
  stride = cairo_image_surface_get_stride (image);

  capture_view_into (view, cogl_context, &rect, buffer, stride);

  data->out_image = image;

  cairo_surface_mark_dirty (data->out_image);

  g_main_loop_quit (data->loop);
}

static cairo_surface_t *
capture_view (ClutterStageView *stage_view,
              gboolean          queue_damage)
{
  MetaRendererView *view = META_RENDERER_VIEW (stage_view);
  MetaCrtc *crtc = meta_renderer_view_get_crtc (view);
  MetaBackend *backend = meta_crtc_get_backend (crtc);
  MetaStage *stage = META_STAGE (meta_backend_get_stage (backend));
  MetaContext *context = meta_backend_get_context (backend);
  MetaDisplay *display = meta_context_get_display (context);
  MetaCompositor *compositor = meta_display_get_compositor (display);
  CaptureViewData data = { 0 };

  meta_compositor_disable_unredirect (compositor);
  meta_backend_inhibit_hw_cursor (backend);

  data.loop = g_main_loop_new (NULL, FALSE);
  data.watch = meta_stage_watch_view (stage, stage_view,
                                      META_STAGE_WATCH_AFTER_PAINT,
                                      on_after_paint,
                                      &data);
  if (queue_damage)
    clutter_stage_view_add_redraw_clip (stage_view, NULL);
  clutter_stage_view_schedule_update (stage_view);

  g_main_loop_run (data.loop);
  g_main_loop_unref (data.loop);

  g_assert_null (data.watch);
  g_assert_nonnull (data.out_image);

  meta_backend_uninhibit_hw_cursor (backend);
  meta_compositor_enable_unredirect (compositor);

  return data.out_image;
}

static cairo_surface_t *
view_adaptor_capture (gpointer adaptor_data)
{
  ClutterStageView *view = CLUTTER_STAGE_VIEW (adaptor_data);

  return capture_view (view, TRUE);
}

static cairo_surface_t *
view_adaptor_capture_undamaged (gpointer adaptor_data)
{
  ClutterStageView *view = CLUTTER_STAGE_VIEW (adaptor_data);

  return capture_view (view, FALSE);
}

static void
assert_software_rendered (ClutterStageView *stage_view)
{
  MetaRendererView *view = META_RENDERER_VIEW (stage_view);
  MetaCrtc *crtc = meta_renderer_view_get_crtc (view);
  MetaBackend *backend = meta_crtc_get_backend (crtc);

  g_assert_false (meta_backend_is_rendering_hardware_accelerated (backend));
}

void
meta_ref_test_verify_view (ClutterStageView *view,
                           const char       *test_name_unescaped,
                           int               test_seq_no,
                           MetaReftestFlag   flags)
{
  if (flags & META_REFTEST_FLAG_UPDATE_REF)
    assert_software_rendered (view);

  meta_ref_test_verify (view_adaptor_capture,
                        view,
                        test_name_unescaped,
                        test_seq_no,
                        flags);
}

void
meta_ref_test_verify_view_undamaged (ClutterStageView *view,
                                     const char       *test_name_unescaped,
                                     int               test_seq_no,
                                     MetaReftestFlag   flags)
{
  if (flags & META_REFTEST_FLAG_UPDATE_REF)
    assert_software_rendered (view);

  meta_ref_test_verify (view_adaptor_capture_undamaged,
                        view,
                        test_name_unescaped,
                        test_seq_no,
                        flags);
}

MetaReftestFlag
meta_ref_test_determine_ref_test_flag (void)
{
  gboolean ensure_only;
  const char *update_tests;
  char **update_test_rules;
  int n_update_test_rules;
  MetaReftestFlag flags;
  int i;

  ensure_only = g_strcmp0 (getenv ("MUTTER_REF_TEST_ENSURE_ONLY"), "1") == 0;

  update_tests = g_getenv ("MUTTER_REF_TEST_UPDATE");
  if (!update_tests)
    return META_REFTEST_FLAG_NONE;

  if (strcmp (update_tests, "all") == 0)
    {
      return ensure_only ? META_REFTEST_FLAG_ENSURE_REF
                         : META_REFTEST_FLAG_UPDATE_REF;
    }

  update_test_rules = g_strsplit (update_tests, ",", -1);
  n_update_test_rules = g_strv_length (update_test_rules);
  g_assert_cmpint (n_update_test_rules, >, 0);

  flags = META_REFTEST_FLAG_NONE;
  for (i = 0; i < n_update_test_rules; i++)
    {
      char *rule = update_test_rules[i];

      if (g_regex_match_simple (rule, g_test_get_path (), 0, 0))
        {
          flags |= ensure_only ? META_REFTEST_FLAG_ENSURE_REF
                               : META_REFTEST_FLAG_UPDATE_REF;
          break;
        }
    }

  g_strfreev (update_test_rules);

  return flags;
}
