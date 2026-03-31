/*
 * Copyright (C) 2018-2026 Red Hat Inc.
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

#include "backends/meta-stream-source-window.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-cursor-tracker-private.h"
#include "backends/meta-screen-cast-window.h"
#include "backends/meta-stream-window.h"
#include "compositor/meta-window-actor-private.h"

struct _MetaStreamSourceWindow
{
  MetaStreamSource parent;

  MetaScreenCastWindow *screen_cast_window;

  gulong screen_cast_window_damaged_handler_id;
  gulong screen_cast_window_destroyed_handler_id;
  gulong position_invalidated_handler_id;
  gulong cursor_changed_handler_id;
  gulong prepare_frame_handler_id;

  MetaStreamRecordFlag queue_record_flags;
  GSource *queue_record_source;

  gboolean cursor_bitmap_invalid;

  struct {
    gboolean set;
    int x;
    int y;
  } last_cursor_matadata;
};

G_DEFINE_TYPE (MetaStreamSourceWindow, meta_stream_source_window,
               META_TYPE_STREAM_SOURCE)

static MetaBackend *
get_backend (MetaStreamSourceWindow *source_window)
{
  MetaStreamSource *source = META_STREAM_SOURCE (source_window);
  MetaStream *stream = meta_stream_source_get_stream (source);

  return meta_stream_get_backend (stream);
}

static ClutterStage *
get_stage (MetaStreamSourceWindow *source_window)
{
  return CLUTTER_STAGE (meta_backend_get_stage (get_backend (source_window)));
}

static MetaStreamWindow *
get_window_stream (MetaStreamSourceWindow *source_window)
{
  MetaStreamSource *source;
  MetaStream *stream;

  source = META_STREAM_SOURCE (source_window);
  stream = meta_stream_source_get_stream (source);

  return META_STREAM_WINDOW (stream);
}

static MetaWindow *
get_window (MetaStreamSourceWindow *source_window)
{
  MetaStreamWindow *stream_window;

  stream_window = get_window_stream (source_window);

  return meta_stream_window_get_window (stream_window);
}

static int
get_stream_width (MetaStreamSourceWindow *source_window)
{
  MetaStreamWindow *stream_window;

  stream_window = get_window_stream (source_window);

  return meta_stream_window_get_width (stream_window);
}

static int
get_stream_height (MetaStreamSourceWindow *source_window)
{
  MetaStreamWindow *stream_window;

  stream_window = get_window_stream (source_window);

  return meta_stream_window_get_height (stream_window);
}

static void
maybe_draw_cursor_sprite (MetaStreamSourceWindow *source_window,
                          uint8_t                 *data,
                          MtkRectangle            *stream_rect)
{
  MetaStreamSource *source = META_STREAM_SOURCE (source_window);
  MetaBackend *backend = get_backend (source_window);
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);
  MetaCursorTracker *cursor_tracker =
    meta_backend_get_cursor_tracker (backend);
  ClutterCursor *cursor;
  CoglTexture *cursor_texture;
  MetaScreenCastWindow *screen_cast_window;
  graphene_point_t cursor_position;
  graphene_point_t relative_cursor_position;
  cairo_surface_t *cursor_surface;
  uint8_t *cursor_surface_data;
  g_autoptr (GError) error = NULL;
  cairo_surface_t *stream_surface;
  int width, height;
  int texture_width, texture_height;
  float scale, view_scale, cursor_scale;
  MtkMonitorTransform cursor_transform;
  const graphene_rect_t *src_rect;
  graphene_matrix_t matrix;
  int hotspot_x, hotspot_y;
  cairo_t *cr;

  cursor = meta_cursor_renderer_get_cursor (cursor_renderer);
  if (!cursor)
    return;

  cursor_texture = clutter_cursor_get_texture (cursor, &hotspot_x, &hotspot_y);
  if (!cursor_texture)
    return;

  screen_cast_window = source_window->screen_cast_window;
  meta_cursor_tracker_get_pointer (cursor_tracker, &cursor_position, NULL);
  if (!meta_screen_cast_window_transform_cursor_position (screen_cast_window,
                                                          cursor,
                                                          &cursor_position,
                                                          &relative_cursor_position,
                                                          &view_scale))
    return;

  cursor_scale = clutter_cursor_get_texture_scale (cursor);
  scale = cursor_scale * view_scale;
  cursor_transform = clutter_cursor_get_texture_transform (cursor);
  src_rect = clutter_cursor_get_viewport_src_rect (cursor);

  texture_width = cogl_texture_get_width (cursor_texture);
  texture_height = cogl_texture_get_height (cursor_texture);

  if (clutter_cursor_get_viewport_dst_size (cursor,
                                            &width,
                                            &height))
    {
      width = (int) ceilf (width * view_scale);
      height = (int) ceilf (height * view_scale);
    }
  else if (src_rect)
    {
      width = (int) ceilf (src_rect->size.width * view_scale);
      height = (int) ceilf (src_rect->size.height * view_scale);
    }
  else
    {
      if (mtk_monitor_transform_is_rotated (cursor_transform))
        {
          width = (int) ceilf (texture_height * scale);
          height = (int) ceilf (texture_width * scale);
        }
      else
        {
          width = (int) ceilf (texture_width * scale);
          height = (int) ceilf (texture_height * scale);
        }
    }

  graphene_matrix_init_identity (&matrix);
  mtk_compute_viewport_matrix (&matrix,
                               texture_width,
                               texture_height,
                               cursor_scale,
                               cursor_transform,
                               src_rect);

  cursor_surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                               width, height);

  cursor_surface_data = cairo_image_surface_get_data (cursor_surface);
  if (!meta_stream_source_draw_cursor_into (source,
                                            cursor_texture,
                                            width,
                                            height,
                                            &matrix,
                                            cursor_surface_data,
                                            &error))
    {
      g_warning ("Failed to draw cursor: %s", error->message);
      cairo_surface_destroy (cursor_surface);
      return;
    }

  stream_surface =
    cairo_image_surface_create_for_data (data, CAIRO_FORMAT_ARGB32,
                                         stream_rect->width,
                                         stream_rect->height,
                                         stream_rect->width * 4);

  cr = cairo_create (stream_surface);
  cairo_surface_mark_dirty (cursor_surface);
  cairo_surface_flush (cursor_surface);
  cairo_set_source_surface (cr, cursor_surface,
                            relative_cursor_position.x - hotspot_x * scale,
                            relative_cursor_position.y - hotspot_y * scale);
  cairo_paint (cr);
  cairo_destroy (cr);
  cairo_surface_destroy (stream_surface);
  cairo_surface_destroy (cursor_surface);
}

static void
maybe_blit_cursor_sprite (MetaStreamSourceWindow *source_window,
                          CoglFramebuffer        *framebuffer,
                          MtkRectangle           *stream_rect)
{
  MetaBackend *backend = get_backend (source_window);
  ClutterBackend *clutter_backend =
    meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_backend);
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);
  MetaCursorTracker *cursor_tracker =
    meta_backend_get_cursor_tracker (backend);
  MetaScreenCastWindow *screen_cast_window;
  ClutterCursor *cursor;
  graphene_point_t relative_cursor_position;
  graphene_point_t cursor_position;
  CoglTexture *cursor_texture;
  CoglPipeline *pipeline;
  int width, height;
  float scale, view_scale, cursor_scale;
  MtkMonitorTransform cursor_transform;
  const graphene_rect_t *src_rect;
  graphene_matrix_t matrix;
  int hotspot_x, hotspot_y;
  float x, y;

  cursor = meta_cursor_renderer_get_cursor (cursor_renderer);
  if (!cursor)
    return;

  cursor_texture = clutter_cursor_get_texture (cursor, &hotspot_x, &hotspot_y);
  if (!cursor_texture)
    return;

  screen_cast_window = source_window->screen_cast_window;
  meta_cursor_tracker_get_pointer (cursor_tracker, &cursor_position, NULL);
  if (!meta_screen_cast_window_transform_cursor_position (screen_cast_window,
                                                          cursor,
                                                          &cursor_position,
                                                          &relative_cursor_position,
                                                          &view_scale))
    return;

  cursor_scale = clutter_cursor_get_texture_scale (cursor);
  scale = cursor_scale * view_scale;
  cursor_transform = clutter_cursor_get_texture_transform (cursor);
  src_rect = clutter_cursor_get_viewport_src_rect (cursor);

  x = (relative_cursor_position.x - hotspot_x) * scale;
  y = (relative_cursor_position.y - hotspot_y) * scale;
  width = cogl_texture_get_width (cursor_texture);
  height = cogl_texture_get_height (cursor_texture);

  pipeline = cogl_pipeline_new (cogl_context);
  cogl_pipeline_set_layer_texture (pipeline, 0, cursor_texture);
  cogl_pipeline_set_layer_filters (pipeline, 0,
                                   COGL_PIPELINE_FILTER_LINEAR,
                                   COGL_PIPELINE_FILTER_LINEAR);

  graphene_matrix_init_identity (&matrix);
  mtk_compute_viewport_matrix (&matrix,
                               width,
                               height,
                               cursor_scale,
                               cursor_transform,
                               src_rect);
  cogl_pipeline_set_layer_matrix (pipeline, 0, &matrix);

  cogl_framebuffer_draw_rectangle (framebuffer,
                                   pipeline,
                                   x, y,
                                   x + width, y + height);

  g_object_unref (pipeline);
}

static gboolean
capture_into (MetaStreamSourceWindow *source_window,
              int                     width,
              int                     height,
              int                     stride,
              uint8_t                *data)
{
  MetaStreamSource *source = META_STREAM_SOURCE (source_window);
  MtkRectangle stream_rect;
  MetaStream *stream;

  stream_rect = (MtkRectangle) {
    .width = width,
    .height = height,
  };

  meta_screen_cast_window_capture_into (source_window->screen_cast_window,
                                        &stream_rect, data);

  stream = meta_stream_source_get_stream (source);
  switch (meta_stream_get_cursor_mode (stream))
    {
    case META_STREAM_CURSOR_MODE_EMBEDDED:
      maybe_draw_cursor_sprite (source_window, data, &stream_rect);
      break;
    case META_STREAM_CURSOR_MODE_METADATA:
    case META_STREAM_CURSOR_MODE_HIDDEN:
      break;
    }

  return TRUE;
}

static gboolean
meta_stream_source_window_get_specs (MetaStreamSource *source,
                                     int              *width,
                                     int              *height,
                                     float            *frame_rate)
{
  MetaStreamSourceWindow *source_window =
    META_STREAM_SOURCE_WINDOW (source);

  if (width)
    *width = get_stream_width (source_window);
  if (height)
    *height = get_stream_height (source_window);
  if (frame_rate)
    *frame_rate = 60.0f;

  return TRUE;
}

static gboolean
meta_stream_source_window_get_videocrop (MetaStreamSource *source,
                                         MtkRectangle     *crop_rect)
{
  MetaStreamSourceWindow *source_window =
    META_STREAM_SOURCE_WINDOW (source);
  MtkRectangle stream_rect;

  meta_screen_cast_window_get_buffer_bounds (source_window->screen_cast_window,
                                             crop_rect);

  stream_rect.x = 0;
  stream_rect.y = 0;
  stream_rect.width = get_stream_width (source_window);
  stream_rect.height = get_stream_height (source_window);

  mtk_rectangle_intersect (crop_rect, &stream_rect, crop_rect);

  return TRUE;
}

static void
unqueue_record (MetaStreamSourceWindow *source_window)
{
  source_window->queue_record_flags = -1;
  g_source_set_ready_time (source_window->queue_record_source, -1);
}

static void
meta_stream_source_window_stop (MetaStreamSourceWindow *source_window)

{
  MetaBackend *backend = get_backend (source_window);
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);
  ClutterStage *stage = get_stage (source_window);

  if (!source_window->screen_cast_window)
    return;

  g_clear_signal_handler (&source_window->screen_cast_window_damaged_handler_id,
                          source_window->screen_cast_window);
  g_clear_signal_handler (&source_window->screen_cast_window_destroyed_handler_id,
                          source_window->screen_cast_window);
  g_clear_signal_handler (&source_window->position_invalidated_handler_id,
                          cursor_tracker);
  g_clear_signal_handler (&source_window->cursor_changed_handler_id,
                          cursor_tracker);
  g_clear_signal_handler (&source_window->prepare_frame_handler_id,
                          stage);
  unqueue_record (source_window);
}

static void
record_frame (MetaStreamSourceWindow *source_window,
              MetaStreamRecordFlag    flags)
{
  MetaStreamSource *source = META_STREAM_SOURCE (source_window);
  MetaStreamPaintPhase paint_phase;

  paint_phase = META_STREAM_PAINT_PHASE_DETACHED;
  meta_stream_source_maybe_record_frame (source, flags, paint_phase, NULL);
}

static gboolean
record_frame_cb (gpointer user_data)
{
  MetaStreamSourceWindow *source_window =
    META_STREAM_SOURCE_WINDOW (user_data);
  MetaStreamRecordFlag flags;

  g_source_set_ready_time (source_window->queue_record_source, -1);

  flags = source_window->queue_record_flags;
  source_window->queue_record_flags = -1;

  g_return_val_if_fail (flags != -1, G_SOURCE_CONTINUE);

  record_frame (source_window, flags);

  return G_SOURCE_CONTINUE;
}

static void
queue_record_with_flags (MetaStreamSourceWindow *source_window,
                         MetaStreamRecordFlag    flags)
{
  MetaStreamSource *source = META_STREAM_SOURCE (source_window);
  float frame_rate;
  int64_t frame_interval_us;

  source_window->queue_record_flags = flags;

  if (g_source_get_ready_time (source_window->queue_record_source) >= 0)
    return;

  meta_stream_source_window_get_specs (source, NULL, NULL, &frame_rate);
  frame_interval_us = (int64_t) (0.5 + G_USEC_PER_SEC / frame_rate);

  g_source_set_ready_time (source_window->queue_record_source,
                           g_get_monotonic_time () + frame_interval_us);
}

static void
queue_record (MetaStreamSourceWindow *source_window)
{
  queue_record_with_flags (source_window, META_STREAM_RECORD_FLAG_NONE);
}

static void
queue_record_cursor (MetaStreamSourceWindow *source_window)
{
  MetaStreamRecordFlag flags;

  if (source_window->queue_record_flags == -1)
    flags = META_STREAM_RECORD_FLAG_CURSOR_ONLY;
  else
    flags = source_window->queue_record_flags;

  queue_record_with_flags (source_window, flags);
}

static void
queue_record_now (MetaStreamSourceWindow *source_window)
{
  if (g_source_get_ready_time (source_window->queue_record_source) == 0)
    return;

  source_window->queue_record_flags = META_STREAM_RECORD_FLAG_NONE;
  g_source_set_ready_time (source_window->queue_record_source, 0);
}

static void
queue_record_cursor_now (MetaStreamSourceWindow *source_window)
{
  if (source_window->queue_record_flags == -1)
    source_window->queue_record_flags = META_STREAM_RECORD_FLAG_CURSOR_ONLY;

  g_source_set_ready_time (source_window->queue_record_source, 0);
}

static void
screen_cast_window_damaged (MetaWindowActor        *actor,
                            MetaStreamSourceWindow *source_window)
{
  queue_record_now (source_window);
}

static void
screen_cast_window_destroyed (MetaWindowActor        *actor,
                              MetaStreamSourceWindow *source_window)
{
  meta_stream_source_window_stop (source_window);
  source_window->screen_cast_window = NULL;
}

static void
sync_cursor_state (MetaStreamSourceWindow *source_window)
{
  queue_record_cursor_now (source_window);
}

static void
pointer_position_invalidated (MetaCursorTracker      *cursor_tracker,
                              MetaStreamSourceWindow *source_window)
{
  ClutterStage *stage = get_stage (source_window);

  clutter_stage_schedule_update (stage);
}

static void
cursor_changed (MetaCursorTracker      *cursor_tracker,
                MetaStreamSourceWindow *source_window)
{
  source_window->cursor_bitmap_invalid = TRUE;
  sync_cursor_state (source_window);
}

static void
on_prepare_frame (ClutterStage           *stage,
                  ClutterStageView       *stage_view,
                  ClutterFrame           *frame,
                  MetaStreamSourceWindow *source_window)
{
  sync_cursor_state (source_window);
}

static void
meta_stream_source_window_enable (MetaStreamSource *source)
{
  MetaStreamSourceWindow *source_window =
    META_STREAM_SOURCE_WINDOW (source);
  MetaBackend *backend = get_backend (source_window);
  ClutterStage *stage = get_stage (source_window);
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);
  MetaWindowActor *window_actor;
  MetaStream *stream;

  window_actor = meta_window_actor_from_window (get_window (source_window));
  if (!window_actor)
    return;

  source_window->screen_cast_window = META_SCREEN_CAST_WINDOW (window_actor);

  source_window->screen_cast_window_damaged_handler_id =
    g_signal_connect (source_window->screen_cast_window,
                      "damaged",
                      G_CALLBACK (screen_cast_window_damaged),
                      source_window);

  source_window->screen_cast_window_destroyed_handler_id =
    g_signal_connect (source_window->screen_cast_window,
                      "destroy",
                      G_CALLBACK (screen_cast_window_destroyed),
                      source_window);

  stream = meta_stream_source_get_stream (source);
  switch (meta_stream_get_cursor_mode (stream))
    {
    case META_STREAM_CURSOR_MODE_METADATA:
    case META_STREAM_CURSOR_MODE_EMBEDDED:
      source_window->position_invalidated_handler_id =
        g_signal_connect_after (cursor_tracker, "position-invalidated",
                                G_CALLBACK (pointer_position_invalidated),
                                source_window);
      source_window->cursor_changed_handler_id =
        g_signal_connect_after (cursor_tracker, "cursor-changed",
                                G_CALLBACK (cursor_changed),
                                source_window);
      source_window->prepare_frame_handler_id =
        g_signal_connect_after (stage, "prepare_frame",
                                G_CALLBACK (on_prepare_frame),
                                source_window);
      break;
    case META_STREAM_CURSOR_MODE_HIDDEN:
      break;
    }

  queue_record_now (source_window);
}

static void
meta_stream_source_window_disable (MetaStreamSource *source)
{
  MetaStreamSourceWindow *source_window =
    META_STREAM_SOURCE_WINDOW (source);

  meta_stream_source_window_stop (source_window);
}

static gboolean
meta_stream_source_window_record_to_buffer (MetaStreamSource      *source,
                                            MetaStreamRecordFlag   flags,
                                            MetaStreamPaintPhase   paint_phase,
                                            int                    width,
                                            int                    height,
                                            int                    stride,
                                            uint8_t               *data,
                                            MtkRegion             *damage,
                                            GError               **error)
{
  MetaStreamSourceWindow *source_window =
    META_STREAM_SOURCE_WINDOW (source);

  unqueue_record (source_window);

  capture_into (source_window, width, height, stride, data);

  return TRUE;
}

static gboolean
meta_stream_source_window_record_to_framebuffer (MetaStreamSource      *source,
                                                 MetaStreamPaintPhase   paint_phase,
                                                 CoglFramebuffer       *framebuffer,
                                                 MtkRegion             *damage,
                                                 GError               **error)
{
  MetaStreamSourceWindow *source_window =
    META_STREAM_SOURCE_WINDOW (source);
  MetaStream *stream;
  MtkRectangle stream_rect;

  unqueue_record (source_window);

  stream_rect.x = 0;
  stream_rect.y = 0;
  stream_rect.width = cogl_framebuffer_get_width (framebuffer);
  stream_rect.height = cogl_framebuffer_get_height (framebuffer);

  if (!meta_screen_cast_window_blit_to_framebuffer (source_window->screen_cast_window,
                                                    &stream_rect,
                                                    framebuffer))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to blit window content to framebuffer");
      return FALSE;
    }

  stream = meta_stream_source_get_stream (source);
  switch (meta_stream_get_cursor_mode (stream))
    {
    case META_STREAM_CURSOR_MODE_EMBEDDED:
      maybe_blit_cursor_sprite (source_window, framebuffer, &stream_rect);
      break;
    case META_STREAM_CURSOR_MODE_METADATA:
    case META_STREAM_CURSOR_MODE_HIDDEN:
      break;
    }

  cogl_framebuffer_flush (framebuffer);

  return TRUE;
}

static void
meta_stream_source_window_stream_queue_follow_up (MetaStreamSource     *source,
                                                  MetaStreamRecordFlag  flags)
{
  MetaStreamSourceWindow *source_window =
    META_STREAM_SOURCE_WINDOW (source);

  if (flags & META_STREAM_RECORD_FLAG_CURSOR_ONLY)
    queue_record_cursor (source_window);
  else
    queue_record (source_window);
}

static gboolean
meta_stream_source_window_is_cursor_metadata_valid (MetaStreamSource *source)
{
  MetaStreamSourceWindow *source_window =
    META_STREAM_SOURCE_WINDOW (source);
  MetaScreenCastWindow *screen_cast_window = source_window->screen_cast_window;
  MetaBackend *backend = get_backend (source_window);
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);
  MetaCursorTracker *cursor_tracker =
    meta_backend_get_cursor_tracker (backend);
  ClutterCursor *cursor;
  graphene_point_t cursor_position;
  graphene_point_t relative_cursor_position;

  cursor = meta_cursor_renderer_get_cursor (cursor_renderer);
  meta_cursor_tracker_get_pointer (cursor_tracker, &cursor_position, NULL);

  if (meta_cursor_tracker_get_pointer_visible (cursor_tracker) &&
      meta_screen_cast_window_transform_cursor_position (screen_cast_window,
                                                         cursor,
                                                         &cursor_position,
                                                         &relative_cursor_position,
                                                         NULL))
    {
      int x, y;

      if (!source_window->last_cursor_matadata.set)
        return FALSE;

      if (source_window->cursor_bitmap_invalid)
        return FALSE;

      x = (int) roundf (relative_cursor_position.x);
      y = (int) roundf (relative_cursor_position.y);

      return (source_window->last_cursor_matadata.x == x &&
              source_window->last_cursor_matadata.y == y);
    }
  else
    {
      return !source_window->last_cursor_matadata.set;
    }
}

static void
meta_stream_source_window_set_cursor_metadata (MetaStreamSource       *source,
                                               struct spa_meta_cursor *spa_meta_cursor)
{
  MetaStreamSourceWindow *source_window =
    META_STREAM_SOURCE_WINDOW (source);
  MetaBackend *backend = get_backend (source_window);
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);
  MetaCursorTracker *cursor_tracker =
    meta_backend_get_cursor_tracker (backend);
  MetaScreenCastWindow *screen_cast_window = source_window->screen_cast_window;
  ClutterCursor *cursor;
  graphene_point_t cursor_position;
  float view_scale;
  graphene_point_t relative_cursor_position;
  int x, y;

  cursor = meta_cursor_renderer_get_cursor (cursor_renderer);
  meta_cursor_tracker_get_pointer (cursor_tracker, &cursor_position, NULL);

  if (!meta_cursor_tracker_get_pointer_visible (cursor_tracker) ||
      !meta_screen_cast_window_transform_cursor_position (screen_cast_window,
                                                          cursor,
                                                          &cursor_position,
                                                          &relative_cursor_position,
                                                          &view_scale))
    {
      source_window->last_cursor_matadata.set = FALSE;
      meta_stream_source_unset_cursor_metadata (source,
                                                         spa_meta_cursor);
      return;
    }

  x = (int) roundf (relative_cursor_position.x);
  y = (int) roundf (relative_cursor_position.y);

  source_window->last_cursor_matadata.set = TRUE;
  source_window->last_cursor_matadata.x = x;
  source_window->last_cursor_matadata.y = y;

  if (source_window->cursor_bitmap_invalid)
    {
      if (cursor)
        {
          meta_stream_source_set_cursor_sprite_metadata (source,
                                                         spa_meta_cursor,
                                                         cursor,
                                                         x, y,
                                                         view_scale);
        }
      else
        {
          meta_stream_source_set_empty_cursor_sprite_metadata (source,
                                                               spa_meta_cursor,
                                                               x, y);
        }
      source_window->cursor_bitmap_invalid = FALSE;
    }
  else
    {
      meta_stream_source_set_cursor_position_metadata (source,
                                                       spa_meta_cursor,
                                                       x, y);
    }
}

static const MetaStreamFormat *
meta_stream_source_window_get_formats (MetaStreamSource *source)
{
  static MetaStreamFormat formats[] = {
    {
      .format = COGL_PIXEL_FORMAT_BGRA_8888_PRE,
    },
    {
      .format = COGL_PIXEL_FORMAT_BGRX_8888,
    },
    {},
  };

  return formats;
}

static void
meta_stream_source_window_finalize (GObject *object)
{
  MetaStreamSourceWindow *source_window =
    META_STREAM_SOURCE_WINDOW (object);

  g_clear_pointer (&source_window->queue_record_source, g_source_destroy);

  G_OBJECT_CLASS (meta_stream_source_window_parent_class)->finalize (object);
}

static void
meta_stream_source_window_class_init (MetaStreamSourceWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaStreamSourceClass *source_class =
    META_STREAM_SOURCE_CLASS (klass);

  object_class->finalize = meta_stream_source_window_finalize;

  source_class->get_specs = meta_stream_source_window_get_specs;
  source_class->enable = meta_stream_source_window_enable;
  source_class->disable = meta_stream_source_window_disable;
  source_class->record_to_buffer =
    meta_stream_source_window_record_to_buffer;
  source_class->record_to_framebuffer =
    meta_stream_source_window_record_to_framebuffer;
  source_class->queue_follow_up =
    meta_stream_source_window_stream_queue_follow_up;
  source_class->get_videocrop = meta_stream_source_window_get_videocrop;
  source_class->is_cursor_metadata_valid =
    meta_stream_source_window_is_cursor_metadata_valid;
  source_class->set_cursor_metadata = meta_stream_source_window_set_cursor_metadata;
  source_class->get_formats =
    meta_stream_source_window_get_formats;
}

static gboolean
source_dispatch (GSource     *source,
                 GSourceFunc  callback,
                 gpointer     user_data)
{
  g_source_set_ready_time (source, -1);

  return callback (user_data);
}

static GSourceFuncs source_funcs =
{
  .dispatch = source_dispatch,
};

static void
meta_stream_source_window_init (MetaStreamSourceWindow *source_window)
{
  source_window->cursor_bitmap_invalid = TRUE;

  source_window->queue_record_flags = -1;
  source_window->queue_record_source = g_source_new (&source_funcs,
                                                  sizeof (GSource));
  g_source_set_callback (source_window->queue_record_source,
                         record_frame_cb, source_window, NULL);
  g_source_set_ready_time (source_window->queue_record_source, -1);
  g_source_attach (source_window->queue_record_source, NULL);
  g_source_unref (source_window->queue_record_source);
}

MetaStreamSourceWindow *
meta_stream_source_window_new (MetaStreamWindow  *stream_window,
                               GError           **error)
{
  return g_initable_new (META_TYPE_STREAM_SOURCE_WINDOW, NULL, error,
                         "stream", stream_window,
                         NULL);
}
