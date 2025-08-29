/*
 * Copyright (C) 2018-2024 Red Hat Inc.
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

/* Partly based on pipewire-media-stream by Georges Basile Stavracas Neto. */

#include "config.h"

#include "mdk-stream.h"

#include <drm_fourcc.h>
#include <epoxy/egl.h>
#include <gio/gio.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <linux/dma-buf.h>
#include <pipewire/keys.h>
#include <pipewire/stream.h>
#include <spa/debug/format.h>
#include <spa/node/command.h>
#include <spa/param/video/format-utils.h>
#include <spa/utils/hook.h>
#include <sys/mman.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/x11/gdkx.h>
#endif
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/wayland/gdkwayland.h>
#endif

#include "mdk-context.h"
#include "mdk-pipewire.h"
#include "mdk-session.h"

#include "mdk-dbus-screen-cast.h"

enum
{
  ERROR,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

typedef struct
{
  uint32_t spa_format;
  uint32_t drm_format;
  GArray *modifiers;
} MdkFormat;

struct _MdkStream
{
  GtkMediaStream parent;

  MdkSession *session;
  int width;
  int height;

  GCancellable *init_cancellable;

  MdkDBusScreenCastStream *proxy;

  GdkPaintable *paintable;
  GArray *formats;

  uint32_t node_id;
  struct pw_stream *pipewire_stream;
  struct spa_hook pipewire_stream_listener;
  struct spa_source *renegotiate_event;

  struct spa_video_info format;

  int64_t frame_sequence;
  gboolean process_requested;

  guint reinvalidate_source_id;

  struct pw_buffer *active_buffer;

  GMainContext *main_context;

  struct {
    gboolean valid;
    float x, y;
    float hotspot_x, hotspot_y;
    float width, height;
    GdkPaintable *paintable;
  } cursor;
};

#define CURSOR_META_SIZE(width, height) \
  (sizeof(struct spa_meta_cursor) + \
   sizeof(struct spa_meta_bitmap) + width * height * 4)

static const struct
{
  uint32_t spa_format;
  uint32_t drm_format;
  const char *name;
} supported_formats[] = {
  {
    SPA_VIDEO_FORMAT_BGRA,
    DRM_FORMAT_ARGB8888,
    "ARGB8888",
  },
  {
    SPA_VIDEO_FORMAT_RGBA,
    DRM_FORMAT_ABGR8888,
    "ABGR8888",
  },
  {
    SPA_VIDEO_FORMAT_BGRx,
    DRM_FORMAT_XRGB8888,
    "XRGB8888",
  },
  {
    SPA_VIDEO_FORMAT_RGBx,
    DRM_FORMAT_XBGR8888,
    "XBGR8888",
  },
};

static void paintable_iface_init (GdkPaintableInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (MdkStream, mdk_stream, GTK_TYPE_MEDIA_STREAM,
                               G_IMPLEMENT_INTERFACE (GDK_TYPE_PAINTABLE,
                                                      paintable_iface_init))

static gboolean
spa_pixel_format_to_gdk_memory_format (uint32_t         spa_format,
                                       GdkMemoryFormat *out_format,
                                       uint32_t        *out_bpp)
{
  switch (spa_format)
    {
    case SPA_VIDEO_FORMAT_RGBA:
    case SPA_VIDEO_FORMAT_RGBx:
      *out_format = GDK_MEMORY_R8G8B8A8;
      *out_bpp = 4;
      break;
    case SPA_VIDEO_FORMAT_BGRA:
    case SPA_VIDEO_FORMAT_BGRx:
      *out_format = GDK_MEMORY_B8G8R8A8;
      *out_bpp = 4;
      break;
    default:
      return FALSE;
    }

  return TRUE;
}

static gboolean
spa_pixel_format_to_drm_format (uint32_t  spa_format,
                                uint32_t *out_format)
{
  switch (spa_format)
    {
    case SPA_VIDEO_FORMAT_RGBA:
      *out_format = DRM_FORMAT_ABGR8888;
      break;
    case SPA_VIDEO_FORMAT_RGBx:
      *out_format = DRM_FORMAT_XBGR8888;
      break;
    case SPA_VIDEO_FORMAT_BGRA:
      *out_format = DRM_FORMAT_ARGB8888;
      break;
    case SPA_VIDEO_FORMAT_BGRx:
      *out_format = DRM_FORMAT_XRGB8888;
      break;
    default:
      return FALSE;
    }

  return TRUE;
}

static inline struct spa_pod *
build_format_param (MdkStream              *stream,
                    struct spa_pod_builder *pod_builder,
                    const MdkFormat        *format,
                    gboolean                build_modifiers)
{
  struct spa_pod_frame object_frame;
  struct spa_rectangle rect;
  struct spa_fraction min_framerate;
  struct spa_fraction max_framerate;

  spa_pod_builder_push_object (pod_builder, &object_frame,
                               SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
  spa_pod_builder_add (pod_builder,
                       SPA_FORMAT_mediaType, SPA_POD_Id (SPA_MEDIA_TYPE_video),
                       0);
  spa_pod_builder_add (pod_builder,
                       SPA_FORMAT_mediaSubtype, SPA_POD_Id (SPA_MEDIA_SUBTYPE_raw),
                       0);
  spa_pod_builder_add (pod_builder,
                       SPA_FORMAT_VIDEO_format, SPA_POD_Id (format->spa_format),
                       0);

  if (build_modifiers && format->modifiers->len > 0)
    {
      struct spa_pod_frame modifiers_frame;
      uint32_t i;

      spa_pod_builder_prop (pod_builder,
                            SPA_FORMAT_VIDEO_modifier,
                            (SPA_POD_PROP_FLAG_MANDATORY |
                             SPA_POD_PROP_FLAG_DONT_FIXATE));

      spa_pod_builder_push_choice (pod_builder,
                                   &modifiers_frame, SPA_CHOICE_Enum, 0);
      spa_pod_builder_long (pod_builder,
                            g_array_index (format->modifiers, uint64_t, 0));
      for (i = 0; i < format->modifiers->len; i++)
        {
          spa_pod_builder_long (pod_builder,
                                g_array_index (format->modifiers, uint64_t, i));
        }
      spa_pod_builder_pop (pod_builder, &modifiers_frame);
    }

  rect = SPA_RECTANGLE (stream->width, stream->height);
  min_framerate = SPA_FRACTION (0, 1);
  max_framerate = SPA_FRACTION (60, 1);
  spa_pod_builder_add (
    pod_builder,
    SPA_FORMAT_VIDEO_size, SPA_POD_Rectangle (&rect),
    SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction (&SPA_FRACTION (0, 1)),
    SPA_FORMAT_VIDEO_maxFramerate, SPA_POD_CHOICE_RANGE_Fraction (&min_framerate,
                                                                  &min_framerate,
                                                                  &max_framerate),
    0);

  return spa_pod_builder_pop (pod_builder, &object_frame);
}

static GPtrArray *
build_stream_format_params (MdkStream              *stream,
                            struct spa_pod_builder *pod_builder)
{
  g_autoptr (GPtrArray) params = NULL;
  uint32_t i;

  g_assert (stream->formats);

  params = g_ptr_array_sized_new (2 * stream->formats->len);

  for (i = 0; i < stream->formats->len; i++)
    {
      const MdkFormat *format = &g_array_index (stream->formats, MdkFormat, i);

      g_ptr_array_add (params,
                       build_format_param (stream, pod_builder, format, TRUE));
      g_ptr_array_add (params,
                       build_format_param (stream, pod_builder, format, FALSE));
    }

  return g_steal_pointer (&params);
}

static GArray *
query_modifiers_for_format (uint32_t drm_format)
{
  GdkDmabufFormats *formats;
  g_autoptr (GArray) modifiers = NULL;

  formats = gdk_display_get_dmabuf_formats (gdk_display_get_default ());

  modifiers = g_array_new (FALSE, FALSE, sizeof (uint64_t));
  for (size_t i = 0; i < gdk_dmabuf_formats_get_n_formats (formats); i++)
    {
      uint32_t fmt;
      uint64_t mod;

      gdk_dmabuf_formats_get_format (formats, i, &fmt, &mod);
      if (fmt == drm_format)
        g_array_append_val (modifiers, mod);
    }

  return g_steal_pointer (&modifiers);
}

static void
remove_modifier_from_format (MdkStream *stream,
                             uint32_t   spa_format,
                             uint64_t   modifier)
{
  size_t i;

  for (i = 0; i < stream->formats->len; i++)
    {
      const MdkFormat *format = &g_array_index (stream->formats, MdkFormat, i);
      int64_t j;

      if (format->spa_format != spa_format)
        continue;


      for (j = 0; j < format->modifiers->len; j++)
        {
          if (g_array_index (format->modifiers, uint64_t, j) == modifier)
            g_array_remove_index_fast (format->modifiers, j--);
        }
    }
}

static void
clear_format_func (MdkFormat *format)
{
  g_clear_pointer (&format->modifiers, g_array_unref);
}

static void
query_formats_and_modifiers (MdkStream *stream)
{
  size_t i;

  g_clear_pointer (&stream->formats, g_array_unref);
  stream->formats = g_array_new (FALSE, FALSE, sizeof (MdkFormat));
  g_array_set_clear_func (stream->formats, (GDestroyNotify) clear_format_func);

  for (i = 0; i < G_N_ELEMENTS (supported_formats); i++)
    {
      MdkFormat format;
      GArray *modifiers;

      modifiers = query_modifiers_for_format (supported_formats[i].drm_format);
      if (modifiers->len == 0)
        {
          g_array_unref (modifiers);
          continue;
        }

      format.spa_format = supported_formats[i].spa_format;
      format.drm_format = supported_formats[i].drm_format;
      format.modifiers = modifiers;

      g_debug ("Display supports format %s (%u modifiers)",
               supported_formats[i].name,
               format.modifiers->len);

      g_array_append_val (stream->formats, format);
    }
}

static void
on_stream_state_changed (void                 *user_data,
                         enum pw_stream_state  old,
                         enum pw_stream_state  state,
                         const char           *error)
{
  MdkStream *stream = MDK_STREAM (user_data);

  g_debug ("Pipewire stream state changed from %s to %s",
           pw_stream_state_as_string (old),
           pw_stream_state_as_string (state));

  switch (state)
    {
    case PW_STREAM_STATE_ERROR:
      g_warning ("PipeWire stream error: %s", error);
      break;
    case PW_STREAM_STATE_PAUSED:
      break;
    case PW_STREAM_STATE_STREAMING:
      gdk_paintable_invalidate_contents (GDK_PAINTABLE (stream));
      break;
    case PW_STREAM_STATE_UNCONNECTED:
    case PW_STREAM_STATE_CONNECTING:
      break;
    }
}

static void
on_stream_param_changed (void                 *user_data,
                         uint32_t              id,
                         const struct spa_pod *format)
{
  MdkStream *stream = MDK_STREAM (user_data);
  uint8_t params_buffer[1024];
  struct spa_pod_builder pod_builder;
  const struct spa_pod *params[3];
  int result;

  if (!format || id != SPA_PARAM_Format)
    return;

  result = spa_format_parse (format,
                             &stream->format.media_type,
                             &stream->format.media_subtype);
  if (result < 0)
    return;

  if (stream->format.media_type != SPA_MEDIA_TYPE_video ||
      stream->format.media_subtype != SPA_MEDIA_SUBTYPE_raw)
    return;

  spa_format_video_raw_parse (format, &stream->format.info.raw);

  g_debug ("Negotiated format:");
  g_debug ("     Format: %d (%s)",
           stream->format.info.raw.format,
           spa_debug_type_find_name (spa_type_video_format,
                                     stream->format.info.raw.format));
  g_debug ("     Size: %dx%d",
           stream->format.info.raw.size.width,
           stream->format.info.raw.size.height);
  g_debug ("     Framerate: %d/%d",
           stream->format.info.raw.framerate.num,
           stream->format.info.raw.framerate.denom);

  pod_builder = SPA_POD_BUILDER_INIT (params_buffer, sizeof (params_buffer));

  params[0] = spa_pod_builder_add_object (
    &pod_builder,
    SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
    SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int (2, 2, 2),
    SPA_PARAM_BUFFERS_dataType, SPA_POD_Int ((1 << SPA_DATA_MemFd) |
                                             (1 << SPA_DATA_DmaBuf)),
    0);

  params[1] = spa_pod_builder_add_object (
    &pod_builder,
    SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
    SPA_PARAM_META_type, SPA_POD_Id (SPA_META_Header),
    SPA_PARAM_META_size, SPA_POD_Int (sizeof (struct spa_meta_header)),
    0);

  params[2] = spa_pod_builder_add_object(
    &pod_builder,
    SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
    SPA_PARAM_META_type, SPA_POD_Id (SPA_META_Cursor),
    SPA_PARAM_META_size, SPA_POD_CHOICE_RANGE_Int (CURSOR_META_SIZE (384, 384),
                                                   CURSOR_META_SIZE (1,1),
                                                   CURSOR_META_SIZE (384, 384)),
    0);

  pw_stream_update_params (stream->pipewire_stream,
                           params, G_N_ELEMENTS (params));
}

static void
renegotiate_stream_format (void     *user_data,
                           uint64_t  expirations)
{
  MdkStream *stream = user_data;
  g_autoptr (GPtrArray) new_params = NULL;
  struct spa_pod_builder builder;
  uint8_t params_buffer[2048];

  builder = SPA_POD_BUILDER_INIT (params_buffer, sizeof (params_buffer));
  new_params = build_stream_format_params (stream, &builder);

  pw_stream_update_params (stream->pipewire_stream,
                           (const struct spa_pod **) new_params->pdata,
                           new_params->len);
}

static void
read_cursor_metadata (MdkStream         *stream,
                      struct spa_buffer *spa_buffer)
{
  struct spa_meta_cursor *cursor;

  cursor = spa_buffer_find_meta_data (spa_buffer,
                                      SPA_META_Cursor,
                                      sizeof (*cursor));
  stream->cursor.valid = cursor && spa_meta_cursor_is_valid (cursor);
  if (stream->cursor.valid)
    {
      struct spa_meta_bitmap *bitmap = NULL;
      GdkMemoryFormat gdk_format;
      uint32_t bpp;

      if (cursor->bitmap_offset)
        {
          bitmap = SPA_MEMBER (cursor,
                               cursor->bitmap_offset,
                               struct spa_meta_bitmap);
        }

      if (bitmap &&
          bitmap->size.width > 0 &&
          bitmap->size.height > 0 &&
          spa_pixel_format_to_gdk_memory_format (bitmap->format,
                                                 &gdk_format,
                                                 &bpp))
        {
          g_autoptr (GdkTexture) texture = NULL;
          g_autoptr (GBytes) bytes = NULL;
          const uint8_t *bitmap_data;

          bitmap_data = SPA_MEMBER (bitmap, bitmap->offset, uint8_t);
          stream->cursor.width = bitmap->size.width;
          stream->cursor.height = bitmap->size.height;
          stream->cursor.hotspot_x = cursor->hotspot.x;
          stream->cursor.hotspot_y = cursor->hotspot.y;

          bytes = g_bytes_new (bitmap_data,
                               bitmap->size.width * bitmap->size.height * bpp);

          texture = gdk_memory_texture_new (bitmap->size.width,
                                            bitmap->size.height,
                                            gdk_format,
                                            bytes,
                                            bitmap->stride);
          g_set_object (&stream->cursor.paintable, GDK_PAINTABLE (texture));
        }

      stream->cursor.x = cursor->position.x;
      stream->cursor.y = cursor->position.y;

      g_debug ("Stream has cursor %.0lfx%.0lf +%.0lf+%.0lf (hotspot: %.0lfx%.0lf)",
               stream->cursor.x,
               stream->cursor.y,
               stream->cursor.width,
               stream->cursor.height,
               stream->cursor.hotspot_x,
               stream->cursor.hotspot_y);
    }
}

static void
on_stream_process (void *user_data)
{
  MdkStream *stream = MDK_STREAM (user_data);
  struct pw_buffer *next_buffer;
  struct pw_buffer *buffer = NULL;
  struct spa_buffer *spa_buffer;
  struct spa_meta_header *spa_header;
  gboolean hold_buffer = FALSE;
  gboolean has_buffer;
  uint32_t drm_format;

  next_buffer = pw_stream_dequeue_buffer (stream->pipewire_stream);
  buffer = next_buffer;
  if (!buffer)
    {
      g_debug ("Stream process had no buffer to dequeue");
      return;
    }

  spa_buffer = buffer->buffer;
  has_buffer = spa_buffer->datas[0].chunk->size != 0;
  spa_header = spa_buffer_find_meta_data (spa_buffer,
                                          SPA_META_Header,
                                          sizeof (*spa_header));

  g_debug ("Dequeued %sbuffer %ld",
           has_buffer ? "" : "empty ", spa_header->seq);

  if (!has_buffer)
    goto read_metadata;

  if (spa_buffer->datas[0].type == SPA_DATA_DmaBuf)
    {
      g_autoptr (GdkDmabufTextureBuilder) builder = NULL;
      unsigned int i;
      g_autoptr (GError) error = NULL;

      if (!spa_pixel_format_to_drm_format (stream->format.info.raw.format,
                                           &drm_format))
        {
          g_critical ("Unsupported DMA buffer format: %d",
                      stream->format.info.raw.format);
          goto read_metadata;
        }

      builder = gdk_dmabuf_texture_builder_new ();
      gdk_dmabuf_texture_builder_set_display (builder,
                                              gdk_display_get_default ());
      gdk_dmabuf_texture_builder_set_width (builder,
                                            stream->format.info.raw.size.width);
      gdk_dmabuf_texture_builder_set_height (builder,
                                             stream->format.info.raw.size.height);
      gdk_dmabuf_texture_builder_set_fourcc (builder,
                                             drm_format);
      gdk_dmabuf_texture_builder_set_modifier (builder,
                                               stream->format.info.raw.modifier);
      gdk_dmabuf_texture_builder_set_n_planes (builder,
                                               spa_buffer->n_datas);

      for (i = 0; i < spa_buffer->n_datas; i++)
        {
          gdk_dmabuf_texture_builder_set_fd (builder, i, spa_buffer->datas[i].fd);
          gdk_dmabuf_texture_builder_set_offset (builder, i, spa_buffer->datas[i].chunk->offset);
          gdk_dmabuf_texture_builder_set_stride (builder, i, spa_buffer->datas[i].chunk->stride);
        }

      g_clear_object (&stream->paintable);
      stream->paintable =
        GDK_PAINTABLE (gdk_dmabuf_texture_builder_build (builder,
                                                         NULL,
                                                         NULL,
                                                         &error));

      if (!stream->paintable)
        {
          MdkContext *context = mdk_session_get_context (stream->session);
          MdkPipewire *pipewire = mdk_context_get_pipewire (context);
          struct pw_loop *pipewire_loop = mdk_pipewire_get_loop (pipewire);

          g_warning ("%s", error->message);

          remove_modifier_from_format (stream,
                                       stream->format.info.raw.format,
                                       stream->format.info.raw.modifier);
          pw_loop_signal_event (pipewire_loop, stream->renegotiate_event);
        }

      hold_buffer = TRUE;
    }
  else
    {
      g_autoptr (GdkTexture) texture = NULL;
      g_autoptr (GBytes) bytes = NULL;
      GdkMemoryFormat gdk_format;
      uint8_t *map;
      void *data;
      uint32_t bpp;
      size_t size;

      if (!spa_pixel_format_to_gdk_memory_format (stream->format.info.raw.format,
                                                  &gdk_format,
                                                  &bpp))
        {
          g_critical ("Unsupported memory buffer format: %d",
                      stream->format.info.raw.format);
          goto read_metadata;
        }

      size = spa_buffer->datas[0].maxsize + spa_buffer->datas[0].mapoffset;

      map = mmap (NULL, size, PROT_READ, MAP_PRIVATE, spa_buffer->datas[0].fd, 0);
      if (map == MAP_FAILED)
        {
          g_critical ("Failed to mmap buffer: %s", g_strerror (errno));
          goto read_metadata;
        }
      data = SPA_MEMBER (map, spa_buffer->datas[0].mapoffset, uint8_t);

      bytes = g_bytes_new (data, size);

      texture = gdk_memory_texture_new (stream->format.info.raw.size.width,
                                        stream->format.info.raw.size.height,
                                        gdk_format,
                                        bytes,
                                        spa_buffer->datas[0].chunk->stride);
      g_set_object (&stream->paintable, GDK_PAINTABLE (texture));

      munmap (map, size);
    }

read_metadata:
  read_cursor_metadata (stream, spa_buffer);

  stream->frame_sequence++;
  stream->process_requested = FALSE;

  if (hold_buffer)
    {
      if (stream->active_buffer)
        {
          g_warn_if_fail (!pw_stream_is_driving (stream->pipewire_stream));
          pw_stream_queue_buffer (stream->pipewire_stream,
                                  stream->active_buffer);
        }

      stream->active_buffer = buffer;
    }
  else
    {
      pw_stream_queue_buffer (stream->pipewire_stream, buffer);
    }

  if (!pw_stream_is_driving (stream->pipewire_stream))
    gdk_paintable_invalidate_contents (GDK_PAINTABLE (stream));
}

static void
on_stream_command (void                     *user_data,
                   const struct spa_command *command)
{
  MdkStream *stream = MDK_STREAM (user_data);

  switch (SPA_NODE_COMMAND_ID (command))
    {
    case SPA_NODE_COMMAND_RequestProcess:
      {
        g_debug ("Received RequestProcess command");
        stream->process_requested = TRUE;
        gdk_paintable_invalidate_contents (GDK_PAINTABLE (stream));
        break;
      }
    default:
      break;
    }
}

static const struct pw_stream_events stream_events = {
  PW_VERSION_STREAM_EVENTS,
  .state_changed = on_stream_state_changed,
  .param_changed = on_stream_param_changed,
  .process = on_stream_process,
  .command = on_stream_command,
};

static gboolean
connect_to_stream (MdkStream  *stream,
                   GError    **error)
{
  MdkSession *session = mdk_stream_get_session (stream);
  MdkContext *context = mdk_session_get_context (session);
  MdkPipewire *pipewire = mdk_context_get_pipewire (context);
  struct pw_properties *pipewire_props;
  struct pw_stream *pipewire_stream;
  uint8_t params_buffer[1024];
  struct spa_pod_builder pod_builder;
  g_autoptr (GPtrArray) params = NULL;
  int ret;

  pipewire_props = pw_properties_new (PW_KEY_NODE_SUPPORTS_LAZY, "2",
                                      NULL);

  pipewire_stream = pw_stream_new (mdk_pipewire_get_core (pipewire),
                                   "mdk-pipewire-stream",
                                   pipewire_props);

  pod_builder = SPA_POD_BUILDER_INIT (params_buffer, sizeof (params_buffer));
  params = build_stream_format_params (stream, &pod_builder);

  stream->pipewire_stream = pipewire_stream;

  pw_stream_add_listener (pipewire_stream,
                          &stream->pipewire_stream_listener,
                          &stream_events,
                          stream);

  ret = pw_stream_connect (stream->pipewire_stream,
                           PW_DIRECTION_INPUT,
                           stream->node_id,
                           (PW_STREAM_FLAG_AUTOCONNECT |
                            PW_STREAM_FLAG_DRIVER),
                           (const struct spa_pod **) params->pdata,
                           params->len);
  if (ret < 0)
    {
      g_set_error_literal (error, G_IO_ERROR, g_io_error_from_errno (-ret),
                           strerror (-ret));
      return FALSE;
    }

  return TRUE;
}

static void
on_pipewire_stream_added (MdkDBusScreenCastStream *proxy,
                          unsigned int             node_id,
                          MdkStream               *stream)
{
  g_autoptr (GError) error = NULL;

  stream->node_id = (uint32_t) node_id;

  g_debug ("Received PipeWire stream node %u, connecting", node_id);

  if (!connect_to_stream (stream, &error))
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_signal_emit (stream, signals[ERROR], 0, error);
      return;
    }
}

static void
start_cb (GObject      *source_object,
          GAsyncResult *res,
          gpointer      user_data)
{
  MdkStream *stream = MDK_STREAM (user_data);
  g_autoptr (GError) error = NULL;

  if (!mdk_dbus_screen_cast_stream_call_start_finish (stream->proxy,
                                                      res,
                                                      &error))
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_signal_emit (stream, signals[ERROR], 0, error);
      return;
    }
}

static void
stream_proxy_ready_cb (GObject      *source_object,
                       GAsyncResult *res,
                       gpointer      user_data)
{
  MdkStream *stream = user_data;
  g_autoptr (GError) error = NULL;

  stream->proxy =
    mdk_dbus_screen_cast_stream_proxy_new_for_bus_finish (res, &error);
  if (!stream->proxy)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_signal_emit (stream, signals[ERROR], 0, error);
      return;
    }

  g_debug ("Stream ready, waiting for PipeWire stream node");

  g_signal_connect (stream->proxy, "pipewire-stream-added",
                    G_CALLBACK (on_pipewire_stream_added),
                    stream);

  mdk_dbus_screen_cast_stream_call_start (stream->proxy,
                                          stream->init_cancellable,
                                          start_cb,
                                          stream);
}

static void
render_compositor_frame (MdkStream *stream)
{
  MdkSession *session = mdk_stream_get_session (stream);
  MdkContext *context = mdk_session_get_context (session);
  MdkPipewire *pipewire = mdk_context_get_pipewire (context);
  struct pw_buffer *active_buffer;
  int64_t frame_sequence;

  active_buffer = g_steal_pointer (&stream->active_buffer);

  if (active_buffer)
    pw_stream_queue_buffer (stream->pipewire_stream, active_buffer);

  pw_stream_trigger_process (stream->pipewire_stream);

  mdk_pipewire_push_main_context (pipewire, stream->main_context);

  frame_sequence = stream->frame_sequence;
  while (frame_sequence == stream->frame_sequence &&
         pw_stream_get_state (stream->pipewire_stream, NULL) ==
         PW_STREAM_STATE_STREAMING)
    g_main_context_iteration (stream->main_context, TRUE);

  mdk_pipewire_pop_main_context (pipewire, stream->main_context);
}

static void
reinvalidate_paintable_cb (gpointer user_data)
{
  MdkStream *stream = MDK_STREAM (user_data);

  stream->reinvalidate_source_id = 0;
  gdk_paintable_invalidate_contents (GDK_PAINTABLE (stream));
}

static void
mdk_stream_paintable_snapshot (GdkPaintable *paintable,
                               GdkSnapshot  *snapshot,
                               double        width,
                               double        height)
{
  MdkStream *stream = MDK_STREAM (paintable);

  if (stream->pipewire_stream &&
      pw_stream_is_driving (stream->pipewire_stream) &&
      pw_stream_get_state (stream->pipewire_stream, NULL) ==
      PW_STREAM_STATE_STREAMING)
    render_compositor_frame (stream);

  gdk_paintable_snapshot (stream->paintable, snapshot, width, height);

  if (stream->cursor.valid && stream->cursor.paintable)
    {
      float scale = MIN ((float) width / stream->width,
                         (float) height / stream->height);
      float x_offset = stream->cursor.x - stream->cursor.hotspot_x;
      float y_offset = stream->cursor.y - stream->cursor.hotspot_y;

      gtk_snapshot_save (snapshot);
      gtk_snapshot_push_clip (snapshot,
                              &GRAPHENE_RECT_INIT (0,
                                                   0,
                                                   (float) width,
                                                   (float) height));
      gtk_snapshot_scale (snapshot, scale, scale);
      gtk_snapshot_translate (snapshot,
                              &GRAPHENE_POINT_INIT (x_offset, y_offset));

      gdk_paintable_snapshot (stream->cursor.paintable,
                              snapshot,
                              stream->cursor.width,
                              stream->cursor.height);

      gtk_snapshot_pop (snapshot);
      gtk_snapshot_restore (snapshot);
    }

  if (stream->process_requested &&
      !stream->reinvalidate_source_id &&
      stream->pipewire_stream &&
      pw_stream_is_driving (stream->pipewire_stream))
    {
      stream->reinvalidate_source_id =
        g_idle_add_once (reinvalidate_paintable_cb, stream);
    }
}

static GdkPaintable *
mdk_stream_paintable_get_current_image (GdkPaintable *paintable)
{
  MdkStream *stream = MDK_STREAM (paintable);

  return g_object_ref (stream->paintable);
}

static int
mdk_stream_paintable_get_intrinsic_width (GdkPaintable *paintable)
{
  MdkStream *stream = MDK_STREAM (paintable);

  return stream->width;
}

static int
mdk_stream_paintable_get_intrinsic_height (GdkPaintable *paintable)
{
  MdkStream *stream = MDK_STREAM (paintable);

  return stream->height;
}

static double
mdk_stream_paintable_get_intrinsic_aspect_ratio (GdkPaintable *paintable)
{
  MdkStream *stream = MDK_STREAM (paintable);

  return (double) stream->width / (double) stream->height;
};

static void
paintable_iface_init (GdkPaintableInterface *iface)
{
  iface->snapshot = mdk_stream_paintable_snapshot;
  iface->get_current_image = mdk_stream_paintable_get_current_image;
  iface->get_intrinsic_width = mdk_stream_paintable_get_intrinsic_width;
  iface->get_intrinsic_height = mdk_stream_paintable_get_intrinsic_height;
  iface->get_intrinsic_aspect_ratio = mdk_stream_paintable_get_intrinsic_aspect_ratio;
}

static void
mdk_stream_finalize (GObject *object)
{
  MdkStream *stream = MDK_STREAM (object);

  if (stream->init_cancellable)
    {
      g_cancellable_cancel (stream->init_cancellable);
      g_clear_object (&stream->init_cancellable);
    }
  g_clear_pointer (&stream->pipewire_stream, pw_stream_destroy);
  g_clear_handle_id (&stream->reinvalidate_source_id, g_source_remove);
  g_clear_object (&stream->proxy);
  g_clear_pointer (&stream->formats, g_array_unref);
  g_clear_object (&stream->paintable);
  g_clear_pointer (&stream->main_context, g_main_context_unref);

  G_OBJECT_CLASS (mdk_stream_parent_class)->finalize (object);
}

static void
mdk_stream_class_init (MdkStreamClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = mdk_stream_finalize;

  signals[ERROR] = g_signal_new ("error",
                                 G_TYPE_FROM_CLASS (klass),
                                 G_SIGNAL_RUN_LAST,
                                 0,
                                 NULL, NULL, NULL,
                                 G_TYPE_NONE, 1,
                                 G_TYPE_ERROR);
}

static void
mdk_stream_init (MdkStream *stream)
{
  stream->process_requested = TRUE;
  stream->main_context = g_main_context_new ();
}

static void
create_monitor_cb (GObject      *source_object,
                   GAsyncResult *res,
                   gpointer      user_data)
{
  MdkStream *stream = MDK_STREAM (user_data);
  g_autoptr (GError) error = NULL;
  g_autofree char *stream_path = NULL;

  stream_path = mdk_session_create_monitor_finish (stream->session,
                                                   res,
                                                   &error);
  if (!stream_path)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_signal_emit (stream, signals[ERROR], 0, error);
      return;
    }

  g_debug ("Creating stream proxy for '%s'", stream_path);

  mdk_dbus_screen_cast_stream_proxy_new_for_bus (
    G_BUS_TYPE_SESSION,
    G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
    "org.gnome.Mutter.ScreenCast",
    stream_path,
    stream->init_cancellable,
    stream_proxy_ready_cb,
    stream);
}

static void
init_async (MdkStream *stream)
{
  GCancellable *cancellable;

  cancellable = g_cancellable_new ();
  stream->init_cancellable = cancellable;

  mdk_session_create_monitor_async (stream->session,
                                    cancellable,
                                    create_monitor_cb,
                                    stream);
}

MdkStream *
mdk_stream_new (MdkSession *session,
                int         width,
                int         height)
{
  MdkContext *context = mdk_session_get_context (session);
  MdkPipewire *pipewire = mdk_context_get_pipewire (context);
  struct pw_loop *pipewire_loop = mdk_pipewire_get_loop (pipewire);
  MdkStream *stream;


  stream = g_object_new (MDK_TYPE_STREAM, NULL);
  stream->session = session;
  stream->width = width;
  stream->height = height;
  stream->paintable = gdk_paintable_new_empty (stream->width, stream->height);

  stream->renegotiate_event = pw_loop_add_event (pipewire_loop,
                                                 renegotiate_stream_format,
                                                 stream);


  init_async (stream);

  return stream;
}

MdkSession *
mdk_stream_get_session (MdkStream *stream)
{
  return stream->session;
}

const char *
mdk_stream_get_path (MdkStream *stream)
{
  return g_dbus_proxy_get_object_path (G_DBUS_PROXY (stream->proxy));
}

void
mdk_stream_realize (MdkStream *stream)
{
  query_formats_and_modifiers (stream);
}

void
mdk_stream_unrealize (MdkStream *stream)
{
  g_clear_pointer (&stream->formats, g_array_unref);
}
