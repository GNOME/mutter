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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#include "config.h"

#include "backends/meta-screen-cast-stream-src.h"

#include <errno.h>
#include <pipewire/pipewire.h>
#include <spa/param/props.h>
#include <spa/param/format-utils.h>
#include <spa/param/video/format-utils.h>
#include <stdint.h>
#include <sys/mman.h>

#include "backends/meta-screen-cast-session.h"
#include "backends/meta-screen-cast-stream.h"
#include "clutter/clutter-mutter.h"
#include "core/meta-fraction.h"
#include "meta/boxes.h"

#define PRIVATE_OWNER_FROM_FIELD(TypeName, field_ptr, field_name) \
  (TypeName *)((guint8 *)(field_ptr) - G_PRIVATE_OFFSET (TypeName, field_name))

#define CURSOR_META_SIZE(width, height) \
  (sizeof (struct spa_meta_cursor) + \
   sizeof (struct spa_meta_bitmap) + width * height * 4)

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

typedef struct _MetaSpaType
{
  struct spa_type_media_type media_type;
  struct spa_type_media_subtype media_subtype;
  struct spa_type_format_video format_video;
  struct spa_type_video_format video_format;
  uint32_t meta_cursor;
} MetaSpaType;

typedef struct _MetaPipeWireSource
{
  GSource base;

  struct pw_loop *pipewire_loop;
} MetaPipeWireSource;

typedef struct _MetaScreenCastStreamSrcPrivate
{
  MetaScreenCastStream *stream;

  struct pw_core *pipewire_core;
  struct pw_remote *pipewire_remote;
  struct pw_type *pipewire_type;
  MetaPipeWireSource *pipewire_source;
  struct spa_hook pipewire_remote_listener;

  gboolean is_enabled;

  struct pw_stream *pipewire_stream;
  struct spa_hook pipewire_stream_listener;

  MetaSpaType spa_type;
  struct spa_video_info_raw video_format;

  uint64_t last_frame_timestamp_us;

  int stream_width;
  int stream_height;
} MetaScreenCastStreamSrcPrivate;

static void
meta_screen_cast_stream_src_init_initable_iface (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaScreenCastStreamSrc,
                         meta_screen_cast_stream_src,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                meta_screen_cast_stream_src_init_initable_iface)
                         G_ADD_PRIVATE (MetaScreenCastStreamSrc))

#define PROP_RANGE(min, max) 2, (min), (max)

static void
meta_screen_cast_stream_src_get_specs (MetaScreenCastStreamSrc *src,
                                       int                     *width,
                                       int                     *height,
                                       float                   *frame_rate)
{
  MetaScreenCastStreamSrcClass *klass =
    META_SCREEN_CAST_STREAM_SRC_GET_CLASS (src);

  klass->get_specs (src, width, height, frame_rate);
}

static gboolean
meta_screen_cast_stream_src_get_videocrop (MetaScreenCastStreamSrc *src,
                                           MetaRectangle           *crop_rect)
{
  MetaScreenCastStreamSrcClass *klass =
    META_SCREEN_CAST_STREAM_SRC_GET_CLASS (src);

  if (klass->get_videocrop)
    return klass->get_videocrop (src, crop_rect);

  return FALSE;
}

static gboolean
meta_screen_cast_stream_src_record_frame (MetaScreenCastStreamSrc *src,
                                          uint8_t                 *data)
{
  MetaScreenCastStreamSrcClass *klass =
    META_SCREEN_CAST_STREAM_SRC_GET_CLASS (src);

  return klass->record_frame (src, data);
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
  CoglTexture2D *bitmap_texture;
  CoglOffscreen *offscreen;
  CoglFramebuffer *fb;
  CoglPipeline *pipeline;
  CoglColor clear_color;

  bitmap_texture = cogl_texture_2d_new_with_size (cogl_context,
                                                  bitmap_width, bitmap_height);
  cogl_primitive_texture_set_auto_mipmap (COGL_PRIMITIVE_TEXTURE (bitmap_texture),
                                          FALSE);
  if (!cogl_texture_allocate (COGL_TEXTURE (bitmap_texture), error))
    {
      cogl_object_unref (bitmap_texture);
      return FALSE;
    }

  offscreen = cogl_offscreen_new_with_texture (COGL_TEXTURE (bitmap_texture));
  fb = COGL_FRAMEBUFFER (offscreen);
  cogl_object_unref (bitmap_texture);
  if (!cogl_framebuffer_allocate (fb, error))
    {
      cogl_object_unref (fb);
      return FALSE;
    }

  pipeline = cogl_pipeline_new (cogl_context);
  cogl_pipeline_set_layer_texture (pipeline, 0, cursor_texture);
  cogl_pipeline_set_layer_filters (pipeline, 0,
                                   COGL_PIPELINE_FILTER_LINEAR,
                                   COGL_PIPELINE_FILTER_LINEAR);
  cogl_color_init_from_4ub (&clear_color, 0, 0, 0, 0);
  cogl_framebuffer_clear (fb, COGL_BUFFER_BIT_COLOR, &clear_color);
  cogl_framebuffer_draw_rectangle (fb, pipeline,
                                   -1, 1, 1, -1);
  cogl_object_unref (pipeline);

  cogl_framebuffer_read_pixels (fb,
                                0, 0,
                                bitmap_width, bitmap_height,
                                COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                                bitmap_data);
  cogl_object_unref (fb);

  return TRUE;
}

gboolean
meta_screen_cast_stream_src_draw_cursor_into (MetaScreenCastStreamSrc  *src,
                                              CoglTexture              *cursor_texture,
                                              float                     scale,
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
      texture_height == height)
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
  spa_meta_cursor->id = 1;
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
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);
  MetaSpaType *spa_type = &priv->spa_type;
  struct spa_meta_bitmap *spa_meta_bitmap;

  spa_meta_cursor->id = 1;
  spa_meta_cursor->position.x = x;
  spa_meta_cursor->position.y = y;

  spa_meta_cursor->bitmap_offset = sizeof (struct spa_meta_cursor);

  spa_meta_bitmap = SPA_MEMBER (spa_meta_cursor,
                                spa_meta_cursor->bitmap_offset,
                                struct spa_meta_bitmap);
  spa_meta_bitmap->format = spa_type->video_format.RGBA;
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
                                                        float                    scale)
{
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);
  MetaSpaType *spa_type = &priv->spa_type;
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
  spa_meta_bitmap->format = spa_type->video_format.RGBA;
  spa_meta_bitmap->offset = sizeof (struct spa_meta_bitmap);

  meta_cursor_sprite_get_hotspot (cursor_sprite, &hotspot_x, &hotspot_y);
  spa_meta_cursor->hotspot.x = (int32_t) roundf (hotspot_x * scale);
  spa_meta_cursor->hotspot.y = (int32_t) roundf (hotspot_y * scale);

  texture_width = cogl_texture_get_width (cursor_texture);
  texture_height = cogl_texture_get_height (cursor_texture);
  bitmap_width = texture_width * scale;
  bitmap_height = texture_height * scale;

  spa_meta_bitmap->size.width = bitmap_width;
  spa_meta_bitmap->size.height = bitmap_height;
  spa_meta_bitmap->stride = bitmap_width * 4;

  bitmap_data = SPA_MEMBER (spa_meta_bitmap,
                            spa_meta_bitmap->offset,
                            uint8_t);

  if (!meta_screen_cast_stream_src_draw_cursor_into (src,
                                                     cursor_texture,
                                                     scale,
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
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);
  MetaSpaType *spa_type = &priv->spa_type;
  struct spa_meta_cursor *spa_meta_cursor;

  spa_meta_cursor = spa_buffer_find_meta (spa_buffer, spa_type->meta_cursor);
  if (spa_meta_cursor)
    meta_screen_cast_stream_src_set_cursor_metadata (src, spa_meta_cursor);
}

static void
maybe_record_cursor (MetaScreenCastStreamSrc *src,
                     struct spa_buffer       *spa_buffer,
                     uint8_t                 *data)
{
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);

  switch (meta_screen_cast_stream_get_cursor_mode (stream))
    {
    case META_SCREEN_CAST_CURSOR_MODE_HIDDEN:
    case META_SCREEN_CAST_CURSOR_MODE_EMBEDDED:
      return;
    case META_SCREEN_CAST_CURSOR_MODE_METADATA:
      add_cursor_metadata (src, spa_buffer);
      return;
    }

  g_assert_not_reached ();
}

void
meta_screen_cast_stream_src_maybe_record_frame (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);
  MetaRectangle crop_rect;
  struct pw_buffer *buffer;
  struct spa_buffer *spa_buffer;
  uint8_t *map = NULL;
  uint8_t *data;
  uint64_t now_us;

  now_us = g_get_monotonic_time ();
  if (priv->last_frame_timestamp_us != 0 &&
      (now_us - priv->last_frame_timestamp_us <
       ((1000000 * priv->video_format.max_framerate.denom) /
        priv->video_format.max_framerate.num)))
    return;

  if (!priv->pipewire_stream)
    return;

  buffer = pw_stream_dequeue_buffer (priv->pipewire_stream);
  if (!buffer)
    {
      g_warning ("Failed to dequeue at PipeWire buffer");
      return;
    }

  spa_buffer = buffer->buffer;

  if (spa_buffer->datas[0].data)
    {
      data = spa_buffer->datas[0].data;
    }
  else if (spa_buffer->datas[0].type == priv->pipewire_type->data.MemFd)
    {
      map = mmap (NULL, spa_buffer->datas[0].maxsize + spa_buffer->datas[0].mapoffset,
                  PROT_READ | PROT_WRITE, MAP_SHARED,
                  spa_buffer->datas[0].fd, 0);
      if (map == MAP_FAILED)
        {
          g_warning ("Failed to mmap pipewire stream buffer: %s\n",
                     strerror (errno));
          return;
        }

      data = SPA_MEMBER (map, spa_buffer->datas[0].mapoffset, uint8_t);
    }
  else
    {
      g_warning ("Unhandled spa buffer type: %d", spa_buffer->datas[0].type);
      return;
    }

  if (meta_screen_cast_stream_src_record_frame (src, data))
    {
      struct spa_meta_video_crop *spa_meta_video_crop;

      spa_buffer->datas[0].chunk->size = spa_buffer->datas[0].maxsize;

      /* Update VideoCrop if needed */
      spa_meta_video_crop =
        spa_buffer_find_meta (spa_buffer, priv->pipewire_type->meta.VideoCrop);
      if (spa_meta_video_crop)
        {
          if (meta_screen_cast_stream_src_get_videocrop (src, &crop_rect))
            {
              spa_meta_video_crop->x = crop_rect.x;
              spa_meta_video_crop->y = crop_rect.y;
              spa_meta_video_crop->width = crop_rect.width;
              spa_meta_video_crop->height = crop_rect.height;
            }
          else
            {
              spa_meta_video_crop->x = 0;
              spa_meta_video_crop->y = 0;
              spa_meta_video_crop->width = priv->stream_width;
              spa_meta_video_crop->height = priv->stream_height;
            }
        }
    }
  else
    {
      spa_buffer->datas[0].chunk->size = 0;
    }

  maybe_record_cursor (src, spa_buffer, data);

  priv->last_frame_timestamp_us = now_us;

  if (map)
    munmap (map, spa_buffer->datas[0].maxsize + spa_buffer->datas[0].mapoffset);

  pw_stream_queue_buffer (priv->pipewire_stream, buffer);
}

static gboolean
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

  priv->is_enabled = FALSE;
}

static void
meta_screen_cast_stream_src_notify_closed (MetaScreenCastStreamSrc *src)
{
  g_signal_emit (src, signals[CLOSED], 0);
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
  uint32_t node_id;

  switch (state)
    {
    case PW_STREAM_STATE_ERROR:
      g_warning ("pipewire stream error: %s", error_message);
      meta_screen_cast_stream_src_notify_closed (src);
      break;
    case PW_STREAM_STATE_CONFIGURE:
      node_id = pw_stream_get_node_id (priv->pipewire_stream);
      g_signal_emit (src, signals[READY], 0, (unsigned int) node_id);
      break;
    case PW_STREAM_STATE_UNCONNECTED:
    case PW_STREAM_STATE_CONNECTING:
    case PW_STREAM_STATE_READY:
    case PW_STREAM_STATE_PAUSED:
      if (meta_screen_cast_stream_src_is_enabled (src))
        meta_screen_cast_stream_src_disable (src);
      break;
    case PW_STREAM_STATE_STREAMING:
      if (!meta_screen_cast_stream_src_is_enabled (src))
        meta_screen_cast_stream_src_enable (src);
      break;
    }
}

static void
on_stream_format_changed (void                 *data,
                          const struct spa_pod *format)
{
  MetaScreenCastStreamSrc *src = data;
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);
  struct pw_type *pipewire_type = priv->pipewire_type;
  uint8_t params_buffer[1024];
  int32_t width, height, stride, size;
  struct spa_pod_builder pod_builder;
  const struct spa_pod *params[3];
  const int bpp = 4;

  if (!format)
    {
      pw_stream_finish_format (priv->pipewire_stream, 0, NULL, 0);
      return;
    }

  spa_format_video_raw_parse (format,
                              &priv->video_format,
                              &priv->spa_type.format_video);

  width = priv->video_format.size.width;
  height = priv->video_format.size.height;
  stride = SPA_ROUND_UP_N (width * bpp, 4);
  size = height * stride;

  pod_builder = SPA_POD_BUILDER_INIT (params_buffer, sizeof (params_buffer));

  params[0] = spa_pod_builder_object (
    &pod_builder,
    pipewire_type->param.idBuffers, pipewire_type->param_buffers.Buffers,
    ":", pipewire_type->param_buffers.size, "i", size,
    ":", pipewire_type->param_buffers.stride, "i", stride,
    ":", pipewire_type->param_buffers.buffers, "iru", 16, PROP_RANGE (2, 16),
    ":", pipewire_type->param_buffers.align, "i", 16);

  params[1] = spa_pod_builder_object (
    &pod_builder,
    pipewire_type->param.idMeta, pipewire_type->param_meta.Meta,
    ":", pipewire_type->param_meta.type, "I", pipewire_type->meta.VideoCrop,
    ":", pipewire_type->param_meta.size, "i", sizeof (struct spa_meta_video_crop));

  params[2] = spa_pod_builder_object (
    &pod_builder,
    pipewire_type->param.idMeta, pipewire_type->param_meta.Meta,
    ":", pipewire_type->param_meta.type, "I", priv->spa_type.meta_cursor,
    ":", pipewire_type->param_meta.size, "i", CURSOR_META_SIZE (64, 64));

  pw_stream_finish_format (priv->pipewire_stream, 0,
                           params, G_N_ELEMENTS (params));
}

static const struct pw_stream_events stream_events = {
  PW_VERSION_STREAM_EVENTS,
  .state_changed = on_stream_state_changed,
  .format_changed = on_stream_format_changed,
};

static struct pw_stream *
create_pipewire_stream (MetaScreenCastStreamSrc  *src,
                        GError                  **error)
{
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);
  struct pw_stream *pipewire_stream;
  uint8_t buffer[1024];
  struct spa_pod_builder pod_builder =
    SPA_POD_BUILDER_INIT (buffer, sizeof (buffer));
  MetaSpaType *spa_type = &priv->spa_type;
  struct pw_type *pipewire_type = priv->pipewire_type;
  float frame_rate;
  MetaFraction frame_rate_fraction;
  struct spa_fraction max_framerate;
  struct spa_fraction min_framerate;
  const struct spa_pod *params[1];
  int result;

  pipewire_stream = pw_stream_new (priv->pipewire_remote,
                                   "meta-screen-cast-src",
                                   NULL);
  if (!pipewire_stream)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to create PipeWire stream: %s",
                   strerror (errno));
      return NULL;
    }

  meta_screen_cast_stream_src_get_specs (src,
                                         &priv->stream_width,
                                         &priv->stream_height,
                                         &frame_rate);
  frame_rate_fraction = meta_fraction_from_double (frame_rate);

  min_framerate = SPA_FRACTION (1, 1);
  max_framerate = SPA_FRACTION (frame_rate_fraction.num,
                                frame_rate_fraction.denom);

  params[0] = spa_pod_builder_object (
    &pod_builder,
    pipewire_type->param.idEnumFormat, pipewire_type->spa_format,
    "I", spa_type->media_type.video,
    "I", spa_type->media_subtype.raw,
    ":", spa_type->format_video.format, "I", spa_type->video_format.BGRx,
    ":", spa_type->format_video.size, "R", &SPA_RECTANGLE (priv->stream_width,
                                                           priv->stream_height),
    ":", spa_type->format_video.framerate, "F", &SPA_FRACTION (0, 1),
    ":", spa_type->format_video.max_framerate, "Fru", &max_framerate,
                                                      PROP_RANGE (&min_framerate,
                                                                  &max_framerate));

  pw_stream_add_listener (pipewire_stream,
                          &priv->pipewire_stream_listener,
                          &stream_events,
                          src);

  result = pw_stream_connect (pipewire_stream,
                              PW_DIRECTION_OUTPUT,
                              NULL,
                              (PW_STREAM_FLAG_DRIVER |
                               PW_STREAM_FLAG_MAP_BUFFERS),
                              params, G_N_ELEMENTS (params));
  if (result != 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Could not connect: %s", spa_strerror (result));
      return NULL;
    }

  return pipewire_stream;
}

static void
on_state_changed (void                 *data,
                  enum pw_remote_state  old,
                  enum pw_remote_state  state,
                  const char           *error_message)
{
  MetaScreenCastStreamSrc *src = data;
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);
  struct pw_stream *pipewire_stream;
  GError *error = NULL;

  switch (state)
    {
    case PW_REMOTE_STATE_ERROR:
      g_warning ("pipewire remote error: %s\n", error_message);
      meta_screen_cast_stream_src_notify_closed (src);
      break;
    case PW_REMOTE_STATE_CONNECTED:
      pipewire_stream = create_pipewire_stream (src, &error);
      if (!pipewire_stream)
        {
          g_warning ("Could not create pipewire stream: %s", error->message);
          g_error_free (error);
          meta_screen_cast_stream_src_notify_closed (src);
        }
      else
        {
          priv->pipewire_stream = pipewire_stream;
        }
      break;
    case PW_REMOTE_STATE_UNCONNECTED:
    case PW_REMOTE_STATE_CONNECTING:
      break;
    }
}

static gboolean
pipewire_loop_source_prepare (GSource *base,
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
  int result;

  result = pw_loop_iterate (pipewire_source->pipewire_loop, 0);
  if (result < 0)
    g_warning ("pipewire_loop_iterate failed: %s", spa_strerror (result));

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

static void
init_spa_type (MetaSpaType         *type,
               struct spa_type_map *map)
{
  spa_type_media_type_map (map, &type->media_type);
  spa_type_media_subtype_map (map, &type->media_subtype);
  spa_type_format_video_map (map, &type->format_video);
  spa_type_video_format_map (map, &type->video_format);
  type->meta_cursor = spa_type_map_get_id(map, SPA_TYPE_META__Cursor);
}

static MetaPipeWireSource *
create_pipewire_source (void)
{
  MetaPipeWireSource *pipewire_source;

  pipewire_source =
    (MetaPipeWireSource *) g_source_new (&pipewire_source_funcs,
                                         sizeof (MetaPipeWireSource));
  pipewire_source->pipewire_loop = pw_loop_new (NULL);
  if (!pipewire_source->pipewire_loop)
    {
      g_source_destroy ((GSource *) pipewire_source);
      return NULL;
    }

  g_source_add_unix_fd (&pipewire_source->base,
                        pw_loop_get_fd (pipewire_source->pipewire_loop),
                        G_IO_IN | G_IO_ERR);

  pw_loop_enter (pipewire_source->pipewire_loop);
  g_source_attach (&pipewire_source->base, NULL);

  return pipewire_source;
}

static const struct pw_remote_events remote_events = {
  PW_VERSION_REMOTE_EVENTS,
  .state_changed = on_state_changed,
};

static gboolean
meta_screen_cast_stream_src_initable_init (GInitable     *initable,
                                           GCancellable  *cancellable,
                                           GError       **error)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (initable);
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  priv->pipewire_source = create_pipewire_source ();
  if (!priv->pipewire_source)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to create PipeWire source");
      return FALSE;
    }

  priv->pipewire_core = pw_core_new (priv->pipewire_source->pipewire_loop,
                                     NULL);
  if (!priv->pipewire_core)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to create pipewire core");
      return FALSE;
    }

  priv->pipewire_remote = pw_remote_new (priv->pipewire_core, NULL, 0);
  if (!priv->pipewire_remote)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Couldn't creat pipewire remote");
      return FALSE;
    }

  pw_remote_add_listener (priv->pipewire_remote,
                          &priv->pipewire_remote_listener,
                          &remote_events,
                          src);

  priv->pipewire_type = pw_core_get_type (priv->pipewire_core);
  init_spa_type (&priv->spa_type, priv->pipewire_type->map);

  if (pw_remote_connect (priv->pipewire_remote) != 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Couldn't connect pipewire remote");
      return FALSE;
    }

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

static void
meta_screen_cast_stream_src_finalize (GObject *object)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (object);
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  if (meta_screen_cast_stream_src_is_enabled (src))
    meta_screen_cast_stream_src_disable (src);

  g_clear_pointer (&priv->pipewire_stream, pw_stream_destroy);
  g_clear_pointer (&priv->pipewire_remote, pw_remote_destroy);
  g_clear_pointer (&priv->pipewire_core, pw_core_destroy);
  g_source_destroy (&priv->pipewire_source->base);

  G_OBJECT_CLASS (meta_screen_cast_stream_src_parent_class)->finalize (object);
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
      break;;
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
}

static void
meta_screen_cast_stream_src_class_init (MetaScreenCastStreamSrcClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_screen_cast_stream_src_finalize;
  object_class->set_property = meta_screen_cast_stream_src_set_property;
  object_class->get_property = meta_screen_cast_stream_src_get_property;

  g_object_class_install_property (object_class,
                                   PROP_STREAM,
                                   g_param_spec_object ("stream",
                                                        "stream",
                                                        "MetaScreenCastStream",
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
