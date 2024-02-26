/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2015-2017 Red Hat Inc.
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

#include "backends/meta-screen-cast-stream-src.h"

#include <errno.h>
#include <fcntl.h>
#include <pipewire/pipewire.h>
#include <spa/param/props.h>
#include <spa/param/format-utils.h>
#include <spa/param/video/format-utils.h>
#include <spa/pod/dynamic.h>
#include <spa/utils/result.h>
#include <stdint.h>
#include <sys/mman.h>

#ifdef HAVE_NATIVE_BACKEND
#include <drm_fourcc.h>
#endif

#include "backends/meta-screen-cast-session.h"
#include "backends/meta-screen-cast-stream.h"
#include "core/meta-fraction.h"

#define PRIVATE_OWNER_FROM_FIELD(TypeName, field_ptr, field_name) \
        (TypeName *)((guint8 *)(field_ptr) - G_PRIVATE_OFFSET (TypeName, field_name))

#define CURSOR_META_SIZE(width, height) \
        (sizeof (struct spa_meta_cursor) + \
         sizeof (struct spa_meta_bitmap) + width * height * 4)

#define NUM_DAMAGED_RECTS 32
#define DEFAULT_SIZE SPA_RECTANGLE (1280, 720)
#define MIN_SIZE SPA_RECTANGLE (1, 1)
#define MAX_SIZE SPA_RECTANGLE (16384, 16386)

#define DEFAULT_FRAME_RATE SPA_FRACTION (60, 1)
#define MIN_FRAME_RATE SPA_FRACTION (1, 1)
#define MAX_FRAME_RATE SPA_FRACTION (1000, 1)

#define DEFAULT_COGL_PIXEL_FORMAT COGL_PIXEL_FORMAT_BGRX_8888

enum
{
  PROP_0,

  PROP_STREAM,
};

enum
{
  READY,
  CLOSED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

typedef struct _MetaPipeWireSource
{
  GSource source;

  MetaScreenCastStreamSrc *src;
  struct pw_loop *pipewire_loop;
} MetaPipeWireSource;

typedef struct _MetaScreenCastStreamSrcPrivate
{
  MetaScreenCastStream *stream;

  struct pw_context *pipewire_context;
  struct pw_core *pipewire_core;
  GSource *pipewire_source;
  struct spa_hook pipewire_core_listener;

  gboolean is_enabled;
  gboolean emit_closed_after_dispatch;

  struct pw_stream *pipewire_stream;
  struct spa_hook pipewire_stream_listener;
  uint32_t node_id;

  struct spa_video_info_raw video_format;

  int64_t last_frame_timestamp_us;
  guint follow_up_frame_source_id;

  int buffer_count;
  gboolean needs_follow_up_with_buffers;

  gboolean uses_dma_bufs;
  GHashTable *dmabuf_handles;

  MtkRegion *redraw_clip;

  GHashTable *modifiers;
} MetaScreenCastStreamSrcPrivate;

static const struct {
  CoglPixelFormat cogl_format;
  enum spa_video_format spa_video_format;
} supported_formats[] = {
  { COGL_PIXEL_FORMAT_BGRX_8888, SPA_VIDEO_FORMAT_BGRx },
  { COGL_PIXEL_FORMAT_BGRA_8888_PRE, SPA_VIDEO_FORMAT_BGRA },
};

static gboolean
spa_video_format_from_cogl_pixel_format (CoglPixelFormat        cogl_format,
                                         enum spa_video_format *out_spa_format)
{
  size_t i;

  for (i = 0; i < G_N_ELEMENTS (supported_formats); i++)
    {
      if (supported_formats[i].cogl_format == cogl_format)
        {
          if (out_spa_format)
            *out_spa_format = supported_formats[i].spa_video_format;
          return TRUE;
        }
    }

  return FALSE;
}

static gboolean
cogl_pixel_format_from_spa_video_format (enum spa_video_format  spa_format,
                                         CoglPixelFormat       *out_cogl_format)
{
  size_t i;

  for (i = 0; i < G_N_ELEMENTS (supported_formats); i++)
    {
      if (supported_formats[i].spa_video_format == spa_format)
        {
          if (out_cogl_format)
            *out_cogl_format = supported_formats[i].cogl_format;
          return TRUE;
        }
    }

  return FALSE;
}

static struct spa_pod *
push_format_object (enum spa_video_format   format,
                    uint64_t               *modifiers,
                    int                     n_modifiers,
                    gboolean                fixate_modifier,
                    ...)
{
  struct spa_pod_dynamic_builder pod_builder;
  struct spa_pod_frame pod_frame;
  va_list args;

  spa_pod_dynamic_builder_init (&pod_builder, NULL, 0, 1024);

  spa_pod_builder_push_object (&pod_builder.b,
                               &pod_frame,
                               SPA_TYPE_OBJECT_Format,
                               SPA_PARAM_EnumFormat);
  spa_pod_builder_add (&pod_builder.b,
                       SPA_FORMAT_mediaType,
                       SPA_POD_Id (SPA_MEDIA_TYPE_video),
                       0);
  spa_pod_builder_add (&pod_builder.b,
                       SPA_FORMAT_mediaSubtype,
                       SPA_POD_Id (SPA_MEDIA_SUBTYPE_raw),
                       0);
  spa_pod_builder_add (&pod_builder.b,
                       SPA_FORMAT_VIDEO_format,
                       SPA_POD_Id (format),
                       0);
  if (n_modifiers > 0)
    {
      if (fixate_modifier)
        {
          spa_pod_builder_prop (&pod_builder.b,
                                SPA_FORMAT_VIDEO_modifier,
                                SPA_POD_PROP_FLAG_MANDATORY);
          spa_pod_builder_long (&pod_builder.b, modifiers[0]);
        }
      else
        {
          struct spa_pod_frame pod_frame_mods;
          int i;

          spa_pod_builder_prop (&pod_builder.b,
                                SPA_FORMAT_VIDEO_modifier,
                                (SPA_POD_PROP_FLAG_MANDATORY |
                                 SPA_POD_PROP_FLAG_DONT_FIXATE));
          spa_pod_builder_push_choice (&pod_builder.b,
                                       &pod_frame_mods,
                                       SPA_CHOICE_Enum,
                                       0);
          spa_pod_builder_long (&pod_builder.b, modifiers[0]);
          for (i = 0; i < n_modifiers; i++)
            spa_pod_builder_long (&pod_builder.b, modifiers[i]);
          spa_pod_builder_pop (&pod_builder.b, &pod_frame_mods);
        }
    }

  va_start (args, fixate_modifier);
  spa_pod_builder_addv (&pod_builder.b, args);
  va_end (args);
  return spa_pod_builder_pop (&pod_builder.b, &pod_frame);
}

static void
meta_screen_cast_stream_src_init_initable_iface (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaScreenCastStreamSrc,
                         meta_screen_cast_stream_src,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                meta_screen_cast_stream_src_init_initable_iface)
                         G_ADD_PRIVATE (MetaScreenCastStreamSrc))

static gboolean
meta_screen_cast_stream_src_get_specs (MetaScreenCastStreamSrc *src,
                                       int                     *width,
                                       int                     *height,
                                       float                   *frame_rate)
{
  MetaScreenCastStreamSrcClass *klass =
    META_SCREEN_CAST_STREAM_SRC_GET_CLASS (src);

  return klass->get_specs (src, width, height, frame_rate);
}

static gboolean
meta_screen_cast_stream_src_get_videocrop (MetaScreenCastStreamSrc *src,
                                           MtkRectangle            *crop_rect)
{
  MetaScreenCastStreamSrcClass *klass =
    META_SCREEN_CAST_STREAM_SRC_GET_CLASS (src);

  if (klass->get_videocrop)
    return klass->get_videocrop (src, crop_rect);

  return FALSE;
}

static gboolean
meta_screen_cast_stream_src_record_to_buffer (MetaScreenCastStreamSrc   *src,
                                              MetaScreenCastPaintPhase   paint_phase,
                                              int                        width,
                                              int                        height,
                                              int                        stride,
                                              uint8_t                   *data,
                                              GError                   **error)
{
  MetaScreenCastStreamSrcClass *klass =
    META_SCREEN_CAST_STREAM_SRC_GET_CLASS (src);

  return klass->record_to_buffer (src, paint_phase,
                                  width, height, stride, data, error);
}

static gboolean
meta_screen_cast_stream_src_record_to_framebuffer (MetaScreenCastStreamSrc   *src,
                                                   MetaScreenCastPaintPhase   paint_phase,
                                                   CoglFramebuffer           *framebuffer,
                                                   GError                   **error)
{
  MetaScreenCastStreamSrcClass *klass =
    META_SCREEN_CAST_STREAM_SRC_GET_CLASS (src);

  return klass->record_to_framebuffer (src, paint_phase, framebuffer, error);
}

static void
meta_screen_cast_stream_src_record_follow_up (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastStreamSrcClass *klass =
    META_SCREEN_CAST_STREAM_SRC_GET_CLASS (src);

  klass->record_follow_up (src);
}

static void
meta_screen_cast_stream_src_set_cursor_metadata (MetaScreenCastStreamSrc *src,
                                                 struct spa_meta_cursor  *spa_meta_cursor)
{
  MetaScreenCastStreamSrcClass *klass =
    META_SCREEN_CAST_STREAM_SRC_GET_CLASS (src);

  if (klass->set_cursor_metadata)
    klass->set_cursor_metadata (src, spa_meta_cursor);
}

static gboolean
draw_cursor_sprite_via_offscreen (MetaScreenCastStreamSrc  *src,
                                  CoglTexture              *cursor_texture,
                                  int                       bitmap_width,
                                  int                       bitmap_height,
                                  MetaMonitorTransform      transform,
                                  uint8_t                  *bitmap_data,
                                  GError                  **error)
{
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);
  MetaScreenCastSession *session = meta_screen_cast_stream_get_session (stream);
  MetaScreenCast *screen_cast =
    meta_screen_cast_session_get_screen_cast (session);
  MetaBackend *backend = meta_screen_cast_get_backend (screen_cast);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_backend);
  CoglTexture *bitmap_texture;
  CoglOffscreen *offscreen;
  CoglFramebuffer *fb;
  CoglPipeline *pipeline;
  CoglColor clear_color;
  graphene_matrix_t matrix;

  bitmap_texture = cogl_texture_2d_new_with_size (cogl_context,
                                                  bitmap_width, bitmap_height);
  cogl_primitive_texture_set_auto_mipmap (bitmap_texture, FALSE);
  if (!cogl_texture_allocate (bitmap_texture, error))
    {
      g_object_unref (bitmap_texture);
      return FALSE;
    }

  offscreen = cogl_offscreen_new_with_texture (bitmap_texture);
  fb = COGL_FRAMEBUFFER (offscreen);
  g_object_unref (bitmap_texture);
  if (!cogl_framebuffer_allocate (fb, error))
    {
      g_object_unref (fb);
      return FALSE;
    }

  pipeline = cogl_pipeline_new (cogl_context);
  cogl_pipeline_set_layer_texture (pipeline, 0, cursor_texture);
  cogl_pipeline_set_layer_filters (pipeline, 0,
                                   COGL_PIPELINE_FILTER_LINEAR,
                                   COGL_PIPELINE_FILTER_LINEAR);

  graphene_matrix_init_identity (&matrix);
  meta_monitor_transform_transform_matrix (transform,
                                           &matrix);
  cogl_pipeline_set_layer_matrix (pipeline, 0, &matrix);

  cogl_color_init_from_4f (&clear_color, 0.0, 0.0, 0.0, 0.0);
  cogl_framebuffer_clear (fb, COGL_BUFFER_BIT_COLOR, &clear_color);
  cogl_framebuffer_draw_rectangle (fb, pipeline,
                                   -1, 1, 1, -1);
  g_object_unref (pipeline);

  cogl_framebuffer_read_pixels (fb,
                                0, 0,
                                bitmap_width, bitmap_height,
                                COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                                bitmap_data);
  g_object_unref (fb);

  return TRUE;
}

gboolean
meta_screen_cast_stream_src_draw_cursor_into (MetaScreenCastStreamSrc  *src,
                                              CoglTexture              *cursor_texture,
                                              float                     scale,
                                              MetaMonitorTransform      transform,
                                              uint8_t                  *data,
                                              GError                  **error)
{
  int texture_width, texture_height;
  int width, height;

  texture_width = cogl_texture_get_width (cursor_texture);
  texture_height = cogl_texture_get_height (cursor_texture);
  width = texture_width * scale;
  height = texture_height * scale;

  if (texture_width == width &&
      texture_height == height &&
      transform == META_MONITOR_TRANSFORM_NORMAL)
    {
      cogl_texture_get_data (cursor_texture,
                             COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                             texture_width * 4,
                             data);
    }
  else
    {
      if (!draw_cursor_sprite_via_offscreen (src,
                                             cursor_texture,
                                             width,
                                             height,
                                             transform,
                                             data,
                                             error))
        return FALSE;
    }

  return TRUE;
}

void
meta_screen_cast_stream_src_unset_cursor_metadata (MetaScreenCastStreamSrc *src,
                                                   struct spa_meta_cursor  *spa_meta_cursor)
{
  spa_meta_cursor->id = 0;
}

void
meta_screen_cast_stream_src_set_cursor_position_metadata (MetaScreenCastStreamSrc *src,
                                                          struct spa_meta_cursor  *spa_meta_cursor,
                                                          int                      x,
                                                          int                      y)
{
  spa_meta_cursor->id = 1;
  spa_meta_cursor->position.x = x;
  spa_meta_cursor->position.y = y;
  spa_meta_cursor->hotspot.x = 0;
  spa_meta_cursor->hotspot.y = 0;
  spa_meta_cursor->bitmap_offset = 0;
}

void
meta_screen_cast_stream_src_set_empty_cursor_sprite_metadata (MetaScreenCastStreamSrc *src,
                                                              struct spa_meta_cursor  *spa_meta_cursor,
                                                              int                      x,
                                                              int                      y)
{
  struct spa_meta_bitmap *spa_meta_bitmap;

  spa_meta_cursor->id = 1;
  spa_meta_cursor->position.x = x;
  spa_meta_cursor->position.y = y;

  spa_meta_cursor->bitmap_offset = sizeof (struct spa_meta_cursor);

  spa_meta_bitmap = SPA_MEMBER (spa_meta_cursor,
                                spa_meta_cursor->bitmap_offset,
                                struct spa_meta_bitmap);
  spa_meta_bitmap->format = SPA_VIDEO_FORMAT_RGBA;
  spa_meta_bitmap->offset = sizeof (struct spa_meta_bitmap);

  spa_meta_cursor->hotspot.x = 0;
  spa_meta_cursor->hotspot.y = 0;

  *spa_meta_bitmap = (struct spa_meta_bitmap) { 0 };
}

void
meta_screen_cast_stream_src_set_cursor_sprite_metadata (MetaScreenCastStreamSrc *src,
                                                        struct spa_meta_cursor  *spa_meta_cursor,
                                                        MetaCursorSprite        *cursor_sprite,
                                                        int                      x,
                                                        int                      y,
                                                        float                    scale,
                                                        MetaMonitorTransform     transform)
{
  CoglTexture *cursor_texture;
  struct spa_meta_bitmap *spa_meta_bitmap;
  int hotspot_x, hotspot_y;
  int texture_width, texture_height;
  int bitmap_width, bitmap_height;
  uint8_t *bitmap_data;
  GError *error = NULL;

  cursor_texture = meta_cursor_sprite_get_cogl_texture (cursor_sprite);
  if (!cursor_texture)
    {
      meta_screen_cast_stream_src_set_empty_cursor_sprite_metadata (src,
                                                                    spa_meta_cursor,
                                                                    x, y);
      return;
    }

  spa_meta_cursor->id = 1;
  spa_meta_cursor->position.x = x;
  spa_meta_cursor->position.y = y;

  spa_meta_cursor->bitmap_offset = sizeof (struct spa_meta_cursor);

  spa_meta_bitmap = SPA_MEMBER (spa_meta_cursor,
                                spa_meta_cursor->bitmap_offset,
                                struct spa_meta_bitmap);
  spa_meta_bitmap->format = SPA_VIDEO_FORMAT_RGBA;
  spa_meta_bitmap->offset = sizeof (struct spa_meta_bitmap);

  meta_cursor_sprite_get_hotspot (cursor_sprite, &hotspot_x, &hotspot_y);
  spa_meta_cursor->hotspot.x = (int32_t) roundf (hotspot_x * scale);
  spa_meta_cursor->hotspot.y = (int32_t) roundf (hotspot_y * scale);

  texture_width = cogl_texture_get_width (cursor_texture);
  texture_height = cogl_texture_get_height (cursor_texture);
  bitmap_width = ceilf (texture_width * scale);
  bitmap_height = ceilf (texture_height * scale);

  spa_meta_bitmap->size.width = bitmap_width;
  spa_meta_bitmap->size.height = bitmap_height;
  spa_meta_bitmap->stride = bitmap_width * 4;

  bitmap_data = SPA_MEMBER (spa_meta_bitmap,
                            spa_meta_bitmap->offset,
                            uint8_t);

  if (!meta_screen_cast_stream_src_draw_cursor_into (src,
                                                     cursor_texture,
                                                     scale,
                                                     transform,
                                                     bitmap_data,
                                                     &error))
    {
      g_warning ("Failed to draw cursor: %s", error->message);
      g_error_free (error);
      spa_meta_cursor->id = 0;
    }
}

static void
add_cursor_metadata (MetaScreenCastStreamSrc *src,
                     struct spa_buffer       *spa_buffer)
{
  struct spa_meta_cursor *spa_meta_cursor;

  spa_meta_cursor = spa_buffer_find_meta_data (spa_buffer, SPA_META_Cursor,
                                               sizeof (*spa_meta_cursor));
  if (spa_meta_cursor)
    meta_screen_cast_stream_src_set_cursor_metadata (src, spa_meta_cursor);
}

static MetaScreenCastRecordResult
maybe_record_cursor (MetaScreenCastStreamSrc *src,
                     struct spa_buffer       *spa_buffer)
{
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);

  switch (meta_screen_cast_stream_get_cursor_mode (stream))
    {
    case META_SCREEN_CAST_CURSOR_MODE_HIDDEN:
      return META_SCREEN_CAST_RECORD_RESULT_RECORDED_NOTHING;
    case META_SCREEN_CAST_CURSOR_MODE_EMBEDDED:
      return META_SCREEN_CAST_RECORD_RESULT_RECORDED_CURSOR;
    case META_SCREEN_CAST_CURSOR_MODE_METADATA:
      add_cursor_metadata (src, spa_buffer);
      return META_SCREEN_CAST_RECORD_RESULT_RECORDED_CURSOR;
    }

  g_assert_not_reached ();
}

static int32_t
meta_screen_cast_stream_src_calculate_stride (MetaScreenCastStreamSrc *src,
                                              struct spa_data         *spa_data)
{
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);
  CoglPixelFormat cogl_format;
  int bpp;

  if (spa_data->type == SPA_DATA_DmaBuf)
    {
      CoglDmaBufHandle *dmabuf_handle;

      dmabuf_handle = g_hash_table_lookup (priv->dmabuf_handles,
                                           GINT_TO_POINTER (spa_data->fd));
      g_assert (dmabuf_handle != NULL);
      return cogl_dma_buf_handle_get_stride (dmabuf_handle);
    }

  if (!cogl_pixel_format_from_spa_video_format (priv->video_format.format,
                                                &cogl_format))
    g_assert_not_reached ();

  bpp = cogl_pixel_format_get_bytes_per_pixel (cogl_format, 0);
  return SPA_ROUND_UP_N (priv->video_format.size.width * bpp, 4);
}

static gboolean
do_record_frame (MetaScreenCastStreamSrc   *src,
                 MetaScreenCastRecordFlag   flags,
                 MetaScreenCastPaintPhase   paint_phase,
                 struct spa_buffer         *spa_buffer,
                 GError                   **error)
{
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);
  struct spa_data *spa_data = &spa_buffer->datas[0];

  if (spa_data->data || spa_data->type == SPA_DATA_MemFd)
    {
      int width = priv->video_format.size.width;
      int height = priv->video_format.size.height;
      int stride = meta_screen_cast_stream_src_calculate_stride (src, spa_data);

      COGL_TRACE_BEGIN_SCOPED (RecordToBuffer,
                               "Meta::ScreenCastStreamSrc::record_to_buffer()");

      return meta_screen_cast_stream_src_record_to_buffer (src,
                                                           paint_phase,
                                                           width,
                                                           height,
                                                           stride,
                                                           spa_data->data,
                                                           error);
    }
  else if (spa_data->type == SPA_DATA_DmaBuf)
    {
      CoglDmaBufHandle *dmabuf_handle =
        g_hash_table_lookup (priv->dmabuf_handles,
                             GINT_TO_POINTER (spa_data->fd));
      CoglFramebuffer *dmabuf_fbo =
        cogl_dma_buf_handle_get_framebuffer (dmabuf_handle);

      COGL_TRACE_BEGIN_SCOPED (RecordToFramebuffer,
                               "Meta::ScreenCastStreamSrc::record_to_framebuffer()");

      return meta_screen_cast_stream_src_record_to_framebuffer (src,
                                                                paint_phase,
                                                                dmabuf_fbo,
                                                                error);
    }

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
               "Unknown SPA buffer type %u", spa_data->type);
  return FALSE;
}

gboolean
meta_screen_cast_stream_src_pending_follow_up_frame (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  return priv->follow_up_frame_source_id != 0;
}

static gboolean
follow_up_frame_cb (gpointer user_data)
{
  MetaScreenCastStreamSrc *src = user_data;
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  priv->follow_up_frame_source_id = 0;
  meta_screen_cast_stream_src_record_follow_up (src);

  return G_SOURCE_REMOVE;
}

static void
maybe_schedule_follow_up_frame (MetaScreenCastStreamSrc *src,
                                int64_t                  timeout_us)
{
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  if (priv->follow_up_frame_source_id)
    return;

  priv->follow_up_frame_source_id = g_timeout_add (us2ms (timeout_us),
                                                   follow_up_frame_cb,
                                                   src);
}

static void
maybe_add_damaged_regions_metadata (MetaScreenCastStreamSrc *src,
                                    struct spa_buffer       *spa_buffer)
{
  MetaScreenCastStreamSrcPrivate *priv;
  struct spa_meta *spa_meta_video_damage;
  struct spa_meta_region *meta_region;

  spa_meta_video_damage =
    spa_buffer_find_meta (spa_buffer, SPA_META_VideoDamage);
  if (!spa_meta_video_damage)
    return;

  priv = meta_screen_cast_stream_src_get_instance_private (src);
  if (!priv->redraw_clip)
    {
      spa_meta_for_each (meta_region, spa_meta_video_damage)
      {
        meta_region->region = SPA_REGION (0, 0, priv->video_format.size.width,
                                          priv->video_format.size.height);
        break;
      }
    }
  else
    {
      int i;
      int n_rectangles;
      int num_buffers_available;

      i = 0;
      n_rectangles = mtk_region_num_rectangles (priv->redraw_clip);
      num_buffers_available = 0;

      spa_meta_for_each (meta_region, spa_meta_video_damage)
      {
        ++num_buffers_available;
      }

      if (num_buffers_available < n_rectangles)
        {
          spa_meta_for_each (meta_region, spa_meta_video_damage)
          {
            g_warning ("Not enough buffers (%d) to accommodate damaged "
                       "regions (%d)", num_buffers_available, n_rectangles);
            meta_region->region = SPA_REGION (0, 0,
                                              priv->video_format.size.width,
                                              priv->video_format.size.height);

            break;
          }
        }
      else
        {
          spa_meta_for_each (meta_region, spa_meta_video_damage)
          {
            MtkRectangle rect;

            rect = mtk_region_get_rectangle (priv->redraw_clip, i);
            meta_region->region = SPA_REGION (rect.x, rect.y,
                                              rect.width, rect.height);

            if (++i == n_rectangles)
              break;
          }
        }
    }

  g_clear_pointer (&priv->redraw_clip, mtk_region_unref);
}

MetaScreenCastRecordResult
meta_screen_cast_stream_src_maybe_record_frame (MetaScreenCastStreamSrc  *src,
                                                MetaScreenCastRecordFlag  flags,
                                                MetaScreenCastPaintPhase  paint_phase,
                                                const MtkRegion          *redraw_clip)
{
  int64_t now_us = g_get_monotonic_time ();

  return meta_screen_cast_stream_src_maybe_record_frame_with_timestamp (src,
                                                                        flags,
                                                                        paint_phase,
                                                                        redraw_clip,
                                                                        now_us);
}

MetaScreenCastRecordResult
meta_screen_cast_stream_src_maybe_record_frame_with_timestamp (MetaScreenCastStreamSrc  *src,
                                                               MetaScreenCastRecordFlag  flags,
                                                               MetaScreenCastPaintPhase  paint_phase,
                                                               const MtkRegion          *redraw_clip,
                                                               int64_t                   frame_timestamp_us)
{
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);
  MetaScreenCastRecordResult record_result =
    META_SCREEN_CAST_RECORD_RESULT_RECORDED_NOTHING;
  MtkRectangle crop_rect;
  struct pw_buffer *buffer;
  struct spa_buffer *spa_buffer;
  struct spa_meta_header *header;
  struct spa_data *spa_data;

  COGL_TRACE_BEGIN_SCOPED (MaybeRecordFrame,
                           "Meta::ScreenCastStreamSrc::maybe_record_frame_with_timestamp()");

  /* Accumulate the damaged region since we might not schedule a frame capture
   * eventually but once we do, we should report all the previous damaged areas.
   */
  if (redraw_clip)
    {
      if (priv->redraw_clip)
        mtk_region_union (priv->redraw_clip, redraw_clip);
      else
        priv->redraw_clip = mtk_region_copy (redraw_clip);
    }

  if (priv->buffer_count == 0)
    {
      meta_topic (META_DEBUG_SCREEN_CAST,
                  "Buffers hasn't been added, "
                  "postponing recording on stream %u",
                  priv->node_id);

      priv->needs_follow_up_with_buffers = TRUE;
      return record_result;
    }

  if (priv->video_format.max_framerate.num > 0 &&
      priv->last_frame_timestamp_us != 0)
    {
      int64_t min_interval_us;
      int64_t time_since_last_frame_us;

      min_interval_us =
        ((G_USEC_PER_SEC * ((int64_t) priv->video_format.max_framerate.denom)) /
         ((int64_t) priv->video_format.max_framerate.num));

      time_since_last_frame_us = frame_timestamp_us - priv->last_frame_timestamp_us;
      if (time_since_last_frame_us < min_interval_us)
        {
          int64_t timeout_us;

          timeout_us = min_interval_us - time_since_last_frame_us;
          maybe_schedule_follow_up_frame (src, timeout_us);
          meta_topic (META_DEBUG_SCREEN_CAST,
                      "Skipped recording frame on stream %u, too early",
                      priv->node_id);
          return record_result;
        }
    }

  if (!priv->pipewire_stream)
    return META_SCREEN_CAST_RECORD_RESULT_RECORDED_NOTHING;

  meta_topic (META_DEBUG_SCREEN_CAST, "Recording %s frame on stream %u",
              flags & META_SCREEN_CAST_RECORD_FLAG_CURSOR_ONLY ?
              "cursor" : "full",
              priv->node_id);

  buffer = pw_stream_dequeue_buffer (priv->pipewire_stream);
  if (!buffer)
    {
      meta_topic (META_DEBUG_SCREEN_CAST,
                  "Couldn't dequeue a buffer from pipewire stream (node id %u), "
                  "maybe your encoding is too slow?",
                  pw_stream_get_node_id (priv->pipewire_stream));
      return record_result;
    }

  spa_buffer = buffer->buffer;
  spa_data = &spa_buffer->datas[0];

  header = spa_buffer_find_meta_data (spa_buffer,
                                      SPA_META_Header,
                                      sizeof (*header));

  if (spa_data->type != SPA_DATA_DmaBuf && !spa_data->data)
    {
      g_critical ("Invalid buffer data");
      if (header)
        header->flags = SPA_META_HEADER_FLAG_CORRUPTED;

      pw_stream_queue_buffer (priv->pipewire_stream, buffer);
      return record_result;
    }

  if (!(flags & META_SCREEN_CAST_RECORD_FLAG_CURSOR_ONLY))
    {
      g_autoptr (GError) error = NULL;

      g_clear_handle_id (&priv->follow_up_frame_source_id, g_source_remove);
      if (do_record_frame (src, flags, paint_phase, spa_buffer, &error))
        {
          maybe_add_damaged_regions_metadata (src, spa_buffer);
          struct spa_meta_region *spa_meta_video_crop;

          spa_data->chunk->size = spa_data->maxsize;
          spa_data->chunk->stride =
            meta_screen_cast_stream_src_calculate_stride (src, spa_data);
          spa_data->chunk->flags = SPA_CHUNK_FLAG_NONE;

          /* Update VideoCrop if needed */
          spa_meta_video_crop =
            spa_buffer_find_meta_data (spa_buffer, SPA_META_VideoCrop,
                                       sizeof (*spa_meta_video_crop));
          if (spa_meta_video_crop)
            {
              if (meta_screen_cast_stream_src_get_videocrop (src, &crop_rect))
                {
                  spa_meta_video_crop->region.position.x = crop_rect.x;
                  spa_meta_video_crop->region.position.y = crop_rect.y;
                  spa_meta_video_crop->region.size.width = crop_rect.width;
                  spa_meta_video_crop->region.size.height = crop_rect.height;
                }
              else
                {
                  spa_meta_video_crop->region.position.x = 0;
                  spa_meta_video_crop->region.position.y = 0;
                  spa_meta_video_crop->region.size.width =
                    priv->video_format.size.width;
                  spa_meta_video_crop->region.size.height =
                    priv->video_format.size.height;
                }
            }

          record_result |= META_SCREEN_CAST_RECORD_RESULT_RECORDED_FRAME;
        }
      else
        {
          if (error)
            g_warning ("Failed to record screen cast frame: %s", error->message);
          spa_data->chunk->size = 0;
          spa_data->chunk->flags = SPA_CHUNK_FLAG_CORRUPTED;
        }
    }
  else
    {
      spa_data->chunk->size = 0;
      spa_data->chunk->flags = SPA_CHUNK_FLAG_CORRUPTED;
    }

  record_result |= maybe_record_cursor (src, spa_buffer);

  priv->last_frame_timestamp_us = frame_timestamp_us;

  if (header)
    {
      header->pts = frame_timestamp_us * SPA_NSEC_PER_USEC;
      header->flags = 0;
    }

  pw_stream_queue_buffer (priv->pipewire_stream, buffer);

  return record_result;
}

gboolean
meta_screen_cast_stream_src_is_enabled (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  return priv->is_enabled;
}

static void
meta_screen_cast_stream_src_enable (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  META_SCREEN_CAST_STREAM_SRC_GET_CLASS (src)->enable (src);

  priv->is_enabled = TRUE;
}

static void
meta_screen_cast_stream_src_disable (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  META_SCREEN_CAST_STREAM_SRC_GET_CLASS (src)->disable (src);

  g_clear_handle_id (&priv->follow_up_frame_source_id, g_source_remove);

  priv->is_enabled = FALSE;
}

void
meta_screen_cast_stream_src_close (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  if (meta_screen_cast_stream_src_is_enabled (src))
    meta_screen_cast_stream_src_disable (src);
  priv->emit_closed_after_dispatch = TRUE;
}

static void
build_format_params (MetaScreenCastStreamSrc *src,
                     GPtrArray               *params)
{
  MetaScreenCastStream *stream =
    meta_screen_cast_stream_src_get_stream (src);
  MetaScreenCastSession *session =
    meta_screen_cast_stream_get_session (stream);
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);
  MetaScreenCast *screen_cast =
    meta_screen_cast_session_get_screen_cast (session);
  GArray *modifiers;
  CoglPixelFormat preferred_cogl_format;
  enum spa_video_format preferred_spa_video_format = 0;
  enum spa_video_format spa_video_formats[G_N_ELEMENTS (supported_formats)];
  int n_spa_video_formats = 0;
  struct spa_rectangle default_size = DEFAULT_SIZE;
  struct spa_rectangle min_size = MIN_SIZE;
  struct spa_rectangle max_size = MAX_SIZE;
  struct spa_fraction default_framerate = DEFAULT_FRAME_RATE;
  struct spa_fraction min_framerate = MIN_FRAME_RATE;
  struct spa_fraction max_framerate = MAX_FRAME_RATE;
  struct spa_pod *pod;
  int width;
  int height;
  float frame_rate;
  uint32_t i;

  if (meta_screen_cast_stream_src_get_specs (src, &width, &height, &frame_rate))
    {
      MetaFraction frame_rate_fraction;

      frame_rate_fraction = meta_fraction_from_double (frame_rate);

      min_framerate = SPA_FRACTION (1, 1);
      max_framerate = SPA_FRACTION (frame_rate_fraction.num,
                                    frame_rate_fraction.denom);
      default_framerate = max_framerate;
      min_size = max_size = default_size = SPA_RECTANGLE (width, height);
    }

  preferred_cogl_format = meta_screen_cast_stream_src_get_preferred_format (src);
  if (spa_video_format_from_cogl_pixel_format (preferred_cogl_format,
                                               &preferred_spa_video_format))
    spa_video_formats[n_spa_video_formats++] = preferred_spa_video_format;

  for (i = 0; i < G_N_ELEMENTS (supported_formats); i++)
    {
      if (supported_formats[i].spa_video_format != preferred_spa_video_format)
        spa_video_formats[n_spa_video_formats++] = supported_formats[i].spa_video_format;
    }

  g_assert (n_spa_video_formats > 0 &&
            n_spa_video_formats <= G_N_ELEMENTS (spa_video_formats));

  for (i = 0; i < n_spa_video_formats; i++)
    {
      CoglPixelFormat cogl_format;
      if (!cogl_pixel_format_from_spa_video_format (spa_video_formats[i], &cogl_format))
        continue;

      modifiers = g_hash_table_lookup (priv->modifiers,
                                       GINT_TO_POINTER (cogl_format));
      if (modifiers == NULL)
        {
          modifiers = meta_screen_cast_query_modifiers (screen_cast, cogl_format);
          g_hash_table_insert (priv->modifiers,
                               GINT_TO_POINTER (cogl_format),
                               modifiers);
        }
      if (modifiers->len == 0)
        continue;

      pod = push_format_object (
        spa_video_formats[i], (uint64_t *) modifiers->data, modifiers->len, FALSE,
        SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle (&default_size,
                                                               &min_size,
                                                               &max_size),
        SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction (&SPA_FRACTION (0, 1)),
        SPA_FORMAT_VIDEO_maxFramerate,
        SPA_POD_CHOICE_RANGE_Fraction (&default_framerate,
                                       &min_framerate,
                                       &max_framerate),
        0);
      g_ptr_array_add (params, g_steal_pointer (&pod));
    }
  for (i = 0; i < n_spa_video_formats; i++)
    {
      pod = push_format_object (
        spa_video_formats[i], NULL, 0, FALSE,
        SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle (&default_size,
                                                               &min_size,
                                                               &max_size),
        SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction (&SPA_FRACTION (0, 1)),
        SPA_FORMAT_VIDEO_maxFramerate,
        SPA_POD_CHOICE_RANGE_Fraction (&default_framerate,
                                       &min_framerate,
                                       &max_framerate),
        0);
      g_ptr_array_add (params, g_steal_pointer (&pod));
    }
}

static void
renegotiate_pipewire_stream (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);
  g_autoptr (GPtrArray) params = NULL;

  params = g_ptr_array_new_full (16, (GDestroyNotify) free);
  build_format_params (src, params);

  pw_stream_update_params (priv->pipewire_stream,
                           (const struct spa_pod **) params->pdata,
                           params->len);
}

static void
on_stream_state_changed (void                 *data,
                         enum pw_stream_state  old,
                         enum pw_stream_state  state,
                         const char           *error_message)
{
  MetaScreenCastStreamSrc *src = data;
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  meta_topic (META_DEBUG_SCREEN_CAST,
              "New PipeWire stream (%u) state '%s'",
              priv->node_id,
              pw_stream_state_as_string (state));

  switch (state)
    {
    case PW_STREAM_STATE_ERROR:
      if (meta_screen_cast_stream_src_is_enabled (src))
        meta_screen_cast_stream_src_disable (src);
      break;
    case PW_STREAM_STATE_PAUSED:
      if (priv->node_id == SPA_ID_INVALID && priv->pipewire_stream)
        {
          priv->node_id = pw_stream_get_node_id (priv->pipewire_stream);
          g_signal_emit (src, signals[READY], 0, (unsigned int) priv->node_id);
        }
      if (meta_screen_cast_stream_src_is_enabled (src))
        meta_screen_cast_stream_src_disable (src);
      break;
    case PW_STREAM_STATE_STREAMING:
      if (!meta_screen_cast_stream_src_is_enabled (src))
        meta_screen_cast_stream_src_enable (src);
      break;
    case PW_STREAM_STATE_UNCONNECTED:
    case PW_STREAM_STATE_CONNECTING:
      break;
    }
}

static void
add_video_damage_meta_param (GPtrArray *params)
{
  struct spa_pod_dynamic_builder pod_builder;
  struct spa_pod *pod;
  const size_t meta_region_size = sizeof (struct spa_meta_region);

  spa_pod_dynamic_builder_init (&pod_builder, NULL, 0, 1024);

  pod = spa_pod_builder_add_object (
    &pod_builder.b,
    SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
    SPA_PARAM_META_type, SPA_POD_Id (SPA_META_VideoDamage),
    SPA_PARAM_META_size,
    SPA_POD_CHOICE_RANGE_Int (meta_region_size * NUM_DAMAGED_RECTS,
                              meta_region_size * 1,
                              meta_region_size * NUM_DAMAGED_RECTS));
  g_ptr_array_add (params, g_steal_pointer (&pod));
}

static void
on_stream_param_changed (void                 *data,
                         uint32_t              id,
                         const struct spa_pod *format)
{
  MetaScreenCastStreamSrc *src = data;
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);
  MetaScreenCastStreamSrcClass *klass =
    META_SCREEN_CAST_STREAM_SRC_GET_CLASS (src);
  struct spa_pod_dynamic_builder pod_builder;
  struct spa_pod *pod;
  g_autoptr (GPtrArray) params = NULL;
  int buffer_types;
  const struct spa_pod_prop *prop_modifier;

  if (!format || id != SPA_PARAM_Format)
    return;

  params = g_ptr_array_new_full (16, (GDestroyNotify) free);

  spa_format_video_raw_parse (format,
                              &priv->video_format);

  prop_modifier = spa_pod_find_prop (format, NULL, SPA_FORMAT_VIDEO_modifier);

  if (prop_modifier)
    buffer_types = 1 << SPA_DATA_DmaBuf;
  else
    buffer_types = 1 << SPA_DATA_MemFd;

  if (prop_modifier && (prop_modifier->flags & SPA_POD_PROP_FLAG_DONT_FIXATE))
    {
      MetaScreenCastStream *stream =
        meta_screen_cast_stream_src_get_stream (src);
      MetaScreenCastSession *session =
        meta_screen_cast_stream_get_session (stream);
      MetaScreenCast *screen_cast =
        meta_screen_cast_session_get_screen_cast (session);
      CoglPixelFormat cogl_format;
      const struct spa_pod *pod_modifier = &prop_modifier->value;
      uint64_t *negotiated_modifiers = SPA_POD_CHOICE_VALUES (pod_modifier);
      uint32_t n_negotiated_modifiers = SPA_POD_CHOICE_N_VALUES (pod_modifier);
      GArray *supported_modifiers;
      uint64_t preferred_modifier;
      int i;

      if (!cogl_pixel_format_from_spa_video_format (priv->video_format.format,
                                                    &cogl_format))
        g_assert_not_reached ();

      supported_modifiers = g_hash_table_lookup (priv->modifiers,
                                                 GINT_TO_POINTER (cogl_format));
      g_array_set_size (supported_modifiers, 0);
      for (i = 0; i < n_negotiated_modifiers; i++)
        {
          uint64_t modifier = negotiated_modifiers[i];
          gboolean found = FALSE;
          int j;

          for (j = 0; j < supported_modifiers->len; j++)
            {
              if (g_array_index (supported_modifiers, uint64_t, j) == modifier)
                {
                  found = TRUE;
                  break;
                }
            }

          if (!found)
            g_array_append_vals (supported_modifiers, &modifier, 1);
        }

      if (meta_screen_cast_get_preferred_modifier (screen_cast,
                                                   cogl_format,
                                                   supported_modifiers,
                                                   priv->video_format.size.width,
                                                   priv->video_format.size.height,
                                                   &preferred_modifier))
        {
          pod = push_format_object (
            priv->video_format.format, &preferred_modifier, 1, TRUE,
            SPA_FORMAT_VIDEO_size, SPA_POD_Rectangle (&priv->video_format.size),
            SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction (&SPA_FRACTION (0, 1)),
            SPA_FORMAT_VIDEO_maxFramerate,
            SPA_POD_CHOICE_RANGE_Fraction (&priv->video_format.max_framerate,
                                           &MIN_FRAME_RATE,
                                           &priv->video_format.max_framerate),
            0);
          g_ptr_array_add (params, g_steal_pointer (&pod));
        }

      build_format_params (src, params);

      pw_stream_update_params (priv->pipewire_stream,
                               (const struct spa_pod **) params->pdata,
                               params->len);
      return;
    }

  spa_pod_dynamic_builder_init (&pod_builder, NULL, 0, 1024);
  pod = spa_pod_builder_add_object (
    &pod_builder.b,
    SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
    SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int (16, 2, 16),
    SPA_PARAM_BUFFERS_blocks, SPA_POD_Int (1),
    SPA_PARAM_BUFFERS_align, SPA_POD_Int (16),
    SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int (buffer_types));
  g_ptr_array_add (params, g_steal_pointer (&pod));

  spa_pod_dynamic_builder_init (&pod_builder, NULL, 0, 1024);
  pod = spa_pod_builder_add_object (
    &pod_builder.b,
    SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
    SPA_PARAM_META_type, SPA_POD_Id (SPA_META_VideoCrop),
    SPA_PARAM_META_size, SPA_POD_Int (sizeof (struct spa_meta_region)));
  g_ptr_array_add (params, g_steal_pointer (&pod));

  spa_pod_dynamic_builder_init (&pod_builder, NULL, 0, 1024);
  pod = spa_pod_builder_add_object (
    &pod_builder.b,
    SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
    SPA_PARAM_META_type, SPA_POD_Id (SPA_META_Cursor),
    SPA_PARAM_META_size, SPA_POD_Int (CURSOR_META_SIZE (384, 384)));
  g_ptr_array_add (params, g_steal_pointer (&pod));

  spa_pod_dynamic_builder_init (&pod_builder, NULL, 0, 1024);
  pod = spa_pod_builder_add_object (
    &pod_builder.b,
    SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
    SPA_PARAM_META_type, SPA_POD_Id (SPA_META_Header),
    SPA_PARAM_META_size, SPA_POD_Int (sizeof (struct spa_meta_header)));
  g_ptr_array_add (params, g_steal_pointer (&pod));

  add_video_damage_meta_param (params);

  pw_stream_update_params (priv->pipewire_stream,
                           (const struct spa_pod **) params->pdata,
                           params->len);

  if (klass->notify_params_updated)
    klass->notify_params_updated (src, &priv->video_format);
}

static void
on_stream_add_buffer (void             *data,
                      struct pw_buffer *buffer)
{
  MetaScreenCastStreamSrc *src = data;
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);
  CoglDmaBufHandle *dmabuf_handle;
  struct spa_buffer *spa_buffer = buffer->buffer;
  struct spa_data *spa_data = &spa_buffer->datas[0];
  int stride;

  priv->buffer_count++;

  spa_data->mapoffset = 0;
  spa_data->data = NULL;

  if (spa_data->type & (1 << SPA_DATA_DmaBuf))
    {
      MetaScreenCastStream *stream =
        meta_screen_cast_stream_src_get_stream (src);
      MetaScreenCastSession *session =
        meta_screen_cast_stream_get_session (stream);
      MetaScreenCast *screen_cast =
        meta_screen_cast_session_get_screen_cast (session);
      CoglPixelFormat cogl_format;

      if (!cogl_pixel_format_from_spa_video_format (priv->video_format.format,
                                                    &cogl_format))
        g_assert_not_reached ();

      dmabuf_handle =
        meta_screen_cast_create_dma_buf_handle (screen_cast,
                                                cogl_format,
                                                priv->video_format.modifier,
                                                priv->video_format.size.width,
                                                priv->video_format.size.height);
      if (!dmabuf_handle)
        {
          GArray *modifiers;
          int i;

          modifiers = g_hash_table_lookup (priv->modifiers,
                                           GINT_TO_POINTER (cogl_format));
          for (i = 0; i < modifiers->len; i++)
            {
              if (g_array_index (modifiers, uint64_t, i) == priv->video_format.modifier)
                {
                  g_array_remove_index (modifiers, i);
                  renegotiate_pipewire_stream (src);
                  break;
                }
            }

          return;
        }

      priv->uses_dma_bufs = TRUE;

      meta_topic (META_DEBUG_SCREEN_CAST,
                  "Allocating DMA buffer for pw_stream %u",
                  pw_stream_get_node_id (priv->pipewire_stream));

      spa_data->type = SPA_DATA_DmaBuf;
      spa_data->flags = SPA_DATA_FLAG_READWRITE;
      spa_data->fd = cogl_dma_buf_handle_get_fd (dmabuf_handle);

      g_hash_table_insert (priv->dmabuf_handles,
                           GINT_TO_POINTER (spa_data->fd),
                           dmabuf_handle);

      stride = meta_screen_cast_stream_src_calculate_stride (src, spa_data);
      spa_data->maxsize = stride * priv->video_format.size.height;
    }
  else
    {
      unsigned int seals;

      priv->uses_dma_bufs = FALSE;

      if (!(spa_data->type & (1 << SPA_DATA_MemFd)))
        {
          g_critical ("No supported PipeWire stream buffer data type could "
                      "be negotiated");
          return;
        }

      meta_topic (META_DEBUG_SCREEN_CAST,
                  "Allocating MemFd buffer for pw_stream %u",
                  pw_stream_get_node_id (priv->pipewire_stream));

      /* Fallback to a memfd buffer */
      spa_data->type = SPA_DATA_MemFd;
      spa_data->flags = SPA_DATA_FLAG_READWRITE;
      spa_data->fd = memfd_create ("mutter-screen-cast-memfd",
                                   MFD_CLOEXEC | MFD_ALLOW_SEALING);
      if (spa_data->fd == -1)
        {
          g_critical ("Can't create memfd: %m");
          return;
        }

      stride = meta_screen_cast_stream_src_calculate_stride (src, spa_data);
      spa_data->maxsize = stride * priv->video_format.size.height;

      if (ftruncate (spa_data->fd, spa_data->maxsize) < 0)
        {
          close (spa_data->fd);
          spa_data->fd = -1;
          g_critical ("Can't truncate to %d: %m", spa_data->maxsize);
          return;
        }

      seals = F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL;
      if (fcntl (spa_data->fd, F_ADD_SEALS, seals) == -1)
        g_warning ("Failed to add seals: %m");

      spa_data->data = mmap (NULL,
                             spa_data->maxsize,
                             PROT_READ | PROT_WRITE,
                             MAP_SHARED,
                             spa_data->fd,
                             spa_data->mapoffset);
      if (spa_data->data == MAP_FAILED)
        {
          close (spa_data->fd);
          spa_data->fd = -1;
          g_critical ("Failed to mmap memory: %m");
          return;
        }
    }

  if (priv->buffer_count == 1 && priv->needs_follow_up_with_buffers)
    {
      priv->needs_follow_up_with_buffers = FALSE;
      meta_screen_cast_stream_src_record_follow_up (src);
    }
}

static void
on_stream_remove_buffer (void             *data,
                         struct pw_buffer *buffer)
{
  MetaScreenCastStreamSrc *src = data;
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);
  struct spa_buffer *spa_buffer = buffer->buffer;
  struct spa_data *spa_data = &spa_buffer->datas[0];

  priv->buffer_count--;

  if (spa_data->type == SPA_DATA_DmaBuf)
    {
      if (!g_hash_table_remove (priv->dmabuf_handles, GINT_TO_POINTER (spa_data->fd)))
        g_critical ("Failed to remove non-exported DMA buffer");
    }
  else if (spa_data->type == SPA_DATA_MemFd)
    {
      g_warn_if_fail (spa_data->fd > 0 || !spa_data->data);

      if (spa_data->fd > 0)
        {
          munmap (spa_data->data, spa_data->maxsize);
          close (spa_data->fd);
        }
    }
}

static const struct pw_stream_events stream_events = {
  PW_VERSION_STREAM_EVENTS,
  .state_changed = on_stream_state_changed,
  .param_changed = on_stream_param_changed,
  .add_buffer = on_stream_add_buffer,
  .remove_buffer = on_stream_remove_buffer,
};

static struct pw_stream *
create_pipewire_stream (MetaScreenCastStreamSrc  *src,
                        GError                  **error)
{
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);
  struct pw_stream *pipewire_stream;
  g_autoptr (GPtrArray) params = NULL;
  int result;

  priv->node_id = SPA_ID_INVALID;

  pipewire_stream = pw_stream_new (priv->pipewire_core,
                                   "meta-screen-cast-src",
                                   NULL);
  if (!pipewire_stream)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to create PipeWire stream: %s",
                   strerror (errno));
      return NULL;
    }

  params = g_ptr_array_new_full (16, (GDestroyNotify) free);
  build_format_params (src, params);

  pw_stream_add_listener (pipewire_stream,
                          &priv->pipewire_stream_listener,
                          &stream_events,
                          src);

  result = pw_stream_connect (pipewire_stream,
                              PW_DIRECTION_OUTPUT,
                              SPA_ID_INVALID,
                              (PW_STREAM_FLAG_DRIVER |
                               PW_STREAM_FLAG_ALLOC_BUFFERS),
                              (const struct spa_pod **) params->pdata,
                              params->len);

  if (result != 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Could not connect: %s", spa_strerror (result));
      return NULL;
    }

  return pipewire_stream;
}

static void
on_core_error (void       *data,
               uint32_t    id,
	       int         seq,
	       int         res,
	       const char *message)
{
  MetaScreenCastStreamSrc *src = data;

  g_warning ("pipewire remote error: id:%u %s", id, message);

  if (id == PW_ID_CORE && res == -EPIPE)
    meta_screen_cast_stream_src_close (src);
}

static gboolean
pipewire_loop_source_prepare (GSource *source,
                              int     *timeout)
{
  *timeout = -1;
  return FALSE;
}

static gboolean
pipewire_loop_source_dispatch (GSource     *source,
                               GSourceFunc  callback,
                               gpointer     user_data)
{
  MetaPipeWireSource *pipewire_source = (MetaPipeWireSource *) source;
  MetaScreenCastStreamSrc *src = pipewire_source->src;
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);
  int result;

  result = pw_loop_iterate (pipewire_source->pipewire_loop, 0);
  if (result < 0)
    g_warning ("pipewire_loop_iterate failed: %s", spa_strerror (result));

  if (priv->emit_closed_after_dispatch)
    g_signal_emit (src, signals[CLOSED], 0);

  return TRUE;
}

static void
pipewire_loop_source_finalize (GSource *source)
{
  MetaPipeWireSource *pipewire_source = (MetaPipeWireSource *) source;

  pw_loop_leave (pipewire_source->pipewire_loop);
  pw_loop_destroy (pipewire_source->pipewire_loop);
}

static GSourceFuncs pipewire_source_funcs =
{
  pipewire_loop_source_prepare,
  NULL,
  pipewire_loop_source_dispatch,
  pipewire_loop_source_finalize
};

static GSource *
create_pipewire_source (MetaScreenCastStreamSrc *src,
                        struct pw_loop          *pipewire_loop)
{
  GSource *source;
  MetaPipeWireSource *pipewire_source;

  source = g_source_new (&pipewire_source_funcs, sizeof (MetaPipeWireSource));
  g_source_set_name (source, "[mutter] PipeWire");

  pipewire_source = (MetaPipeWireSource *) source;
  pipewire_source->src = src;
  pipewire_source->pipewire_loop = pipewire_loop;

  g_source_add_unix_fd (source,
                        pw_loop_get_fd (pipewire_source->pipewire_loop),
                        G_IO_IN | G_IO_ERR);

  pw_loop_enter (pipewire_source->pipewire_loop);
  g_source_attach (source, NULL);
  g_source_unref (source);

  return source;
}

static const struct pw_core_events core_events = {
  PW_VERSION_CORE_EVENTS,
  .error = on_core_error,
};

static gboolean
meta_screen_cast_stream_src_initable_init (GInitable     *initable,
                                           GCancellable  *cancellable,
                                           GError       **error)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (initable);
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);
  struct pw_loop *pipewire_loop;

  pipewire_loop = pw_loop_new (NULL);
  if (!pipewire_loop)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to create PipeWire loop");
      return FALSE;
    }

  priv->pipewire_source = create_pipewire_source (src, pipewire_loop);
  if (!priv->pipewire_source)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to create PipeWire source");
      return FALSE;
    }

  priv->pipewire_context = pw_context_new (pipewire_loop,
                                           NULL, 0);
  if (!priv->pipewire_context)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to create pipewire context");
      return FALSE;
    }

  priv->pipewire_core = pw_context_connect (priv->pipewire_context, NULL, 0);
  if (!priv->pipewire_core)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Couldn't connect pipewire context");
      return FALSE;
    }

  pw_core_add_listener (priv->pipewire_core,
                        &priv->pipewire_core_listener,
                        &core_events,
                        src);

  priv->pipewire_stream = create_pipewire_stream (src, error);
  if (!priv->pipewire_stream)
    return FALSE;

  return TRUE;
}

static void
meta_screen_cast_stream_src_init_initable_iface (GInitableIface *iface)
{
  iface->init = meta_screen_cast_stream_src_initable_init;
}

MetaScreenCastStream *
meta_screen_cast_stream_src_get_stream (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  return priv->stream;
}

static CoglPixelFormat
meta_screen_cast_stream_src_default_get_preferred_format (MetaScreenCastStreamSrc *src)
{
  return DEFAULT_COGL_PIXEL_FORMAT;
}

static void
meta_screen_cast_stream_src_dispose (GObject *object)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (object);
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);
  GHashTableIter modifierIter;
  gpointer key, value;

  if (meta_screen_cast_stream_src_is_enabled (src))
    meta_screen_cast_stream_src_disable (src);

  g_hash_table_iter_init (&modifierIter,
                          priv->modifiers);
  while (g_hash_table_iter_next (&modifierIter, &key, &value))
    g_array_free (value, TRUE);

  g_clear_pointer (&priv->modifiers, g_hash_table_destroy);
  g_clear_pointer (&priv->pipewire_stream, pw_stream_destroy);
  g_clear_pointer (&priv->dmabuf_handles, g_hash_table_destroy);
  g_clear_pointer (&priv->pipewire_core, pw_core_disconnect);
  g_clear_pointer (&priv->pipewire_context, pw_context_destroy);
  g_clear_pointer (&priv->pipewire_source, g_source_destroy);

  G_OBJECT_CLASS (meta_screen_cast_stream_src_parent_class)->dispose (object);
}

static void
meta_screen_cast_stream_src_set_property (GObject      *object,
                                          guint         prop_id,
                                          const GValue *value,
                                          GParamSpec   *pspec)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (object);
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  switch (prop_id)
    {
    case PROP_STREAM:
      priv->stream = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_screen_cast_stream_src_get_property (GObject    *object,
                                          guint       prop_id,
                                          GValue     *value,
                                          GParamSpec *pspec)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (object);
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  switch (prop_id)
    {
    case PROP_STREAM:
      g_value_set_object (value, priv->stream);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_screen_cast_stream_src_init (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  priv->dmabuf_handles =
    g_hash_table_new_full (NULL, NULL, NULL,
                           (GDestroyNotify) cogl_dma_buf_handle_free);

  priv->modifiers = g_hash_table_new (NULL, NULL);
}

static void
meta_screen_cast_stream_src_class_init (MetaScreenCastStreamSrcClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_screen_cast_stream_src_dispose;
  object_class->set_property = meta_screen_cast_stream_src_set_property;
  object_class->get_property = meta_screen_cast_stream_src_get_property;

  klass->get_preferred_format =
    meta_screen_cast_stream_src_default_get_preferred_format;

  g_object_class_install_property (object_class,
                                   PROP_STREAM,
                                   g_param_spec_object ("stream", NULL, NULL,
                                                        META_TYPE_SCREEN_CAST_STREAM,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  signals[READY] = g_signal_new ("ready",
                                 G_TYPE_FROM_CLASS (klass),
                                 G_SIGNAL_RUN_LAST,
                                 0,
                                 NULL, NULL, NULL,
                                 G_TYPE_NONE, 1,
                                 G_TYPE_UINT);
  signals[CLOSED] = g_signal_new ("closed",
                                  G_TYPE_FROM_CLASS (klass),
                                  G_SIGNAL_RUN_LAST,
                                  0,
                                  NULL, NULL, NULL,
                                  G_TYPE_NONE, 0);
}

gboolean
meta_screen_cast_stream_src_uses_dma_bufs (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  return priv->uses_dma_bufs;
}

CoglPixelFormat
meta_screen_cast_stream_src_get_preferred_format (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastStreamSrcClass *klass =
    META_SCREEN_CAST_STREAM_SRC_GET_CLASS (src);

  return klass->get_preferred_format (src);
}
