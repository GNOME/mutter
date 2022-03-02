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

#include <pipewire/pipewire.h>
#include <spa/param/format-utils.h>
#include <spa/param/props.h>
#include <spa/param/video/format-utils.h>
#include <spa/utils/result.h>
#include <stdint.h>
#include <sys/mman.h>

#include "meta-dbus-remote-desktop.h"
#include "meta-dbus-screen-cast.h"

#define assert_cursor_position(stream, x, y) \
{ \
  g_assert_cmpint (stream->cursor_x, ==, (x)); \
  g_assert_cmpint (stream->cursor_y, ==, (y)); \
}

#define CURSOR_META_SIZE(width, height) \
 (sizeof(struct spa_meta_cursor) + \
  sizeof(struct spa_meta_bitmap) + width * height * 4)

enum
  {
    CURSOR_MODE_HIDDEN = 0,
    CURSOR_MODE_EMBEDDED = 1,
    CURSOR_MODE_METADATA = 2,
  };

typedef struct _Stream
{
  MetaDBusScreenCastStream *proxy;
  uint32_t pipewire_node_id;
  struct spa_video_info_raw spa_format;
  struct pw_stream *pipewire_stream;
  struct spa_hook pipewire_stream_listener;
  enum pw_stream_state state;
  int buffer_count;

  int target_width;
  int target_height;

  int cursor_x;
  int cursor_y;
} Stream;

typedef struct _Session
{
  MetaDBusScreenCastSession *screen_cast_session_proxy;
  MetaDBusRemoteDesktopSession *remote_desktop_session_proxy;
} Session;

typedef struct _RemoteDesktop
{
  MetaDBusRemoteDesktop *proxy;
} RemoteDesktop;

typedef struct _ScreenCast
{
  MetaDBusScreenCast *proxy;
} ScreenCast;

typedef struct _PipeWireSource
{
  GSource base;

  struct pw_loop *pipewire_loop;
} PipeWireSource;

static PipeWireSource *_pipewire_source;
static struct pw_context *_pipewire_context;
static struct pw_core *_pipewire_core;
static struct spa_hook _pipewire_core_listener;

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
  PipeWireSource *pipewire_source = (PipeWireSource *) source;
  int result;

  result = pw_loop_iterate (pipewire_source->pipewire_loop, 0);
  if (result < 0)
    g_error ("pipewire_loop_iterate failed: %s", spa_strerror (result));

  return TRUE;
}

static void
pipewire_loop_source_finalize (GSource *source)
{
  PipeWireSource *pipewire_source = (PipeWireSource *) source;

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

static PipeWireSource *
create_pipewire_source (void)
{
  PipeWireSource *pipewire_source;

  pipewire_source =
    (PipeWireSource *) g_source_new (&pipewire_source_funcs,
                                     sizeof (PipeWireSource));
  pipewire_source->pipewire_loop = pw_loop_new (NULL);
  g_assert_nonnull (pipewire_source->pipewire_loop);
  g_source_add_unix_fd (&pipewire_source->base,
                        pw_loop_get_fd (pipewire_source->pipewire_loop),
                        G_IO_IN | G_IO_ERR);

  pw_loop_enter (pipewire_source->pipewire_loop);
  g_source_attach (&pipewire_source->base, NULL);

  return pipewire_source;
}

static void
on_core_error (void       *user_data,
               uint32_t    id,
               int         seq,
               int         res,
               const char *message)
{
  g_error ("PipeWire core error: id:%u %s", id, message);
}

static const struct pw_core_events core_events = {
  PW_VERSION_CORE_EVENTS,
  .error = on_core_error,
};

static void
init_pipewire (void)
{
  pw_init (NULL, NULL);
  _pipewire_source = create_pipewire_source ();
  _pipewire_context = pw_context_new (_pipewire_source->pipewire_loop,
                                      NULL, 0);
  g_assert_nonnull (_pipewire_context);
  _pipewire_core = pw_context_connect (_pipewire_context, NULL, 0);
  g_assert_nonnull (_pipewire_core);

  pw_core_add_listener (_pipewire_core,
                        &_pipewire_core_listener,
                        &core_events,
                        NULL);
}

static void
release_pipewire (void)
{
  g_clear_pointer (&_pipewire_core, pw_core_disconnect);
  g_clear_pointer (&_pipewire_context, pw_context_destroy);
  if (_pipewire_source)
    {
      g_source_destroy ((GSource *) _pipewire_source);
      g_source_unref ((GSource *) _pipewire_source);
      _pipewire_source = NULL;
    }
}

static void
on_stream_state_changed (void                 *user_data,
                         enum pw_stream_state  old,
                         enum pw_stream_state  state,
                         const char           *error)
{
  Stream *stream = user_data;

  switch (state)
    {
    case PW_STREAM_STATE_ERROR:
      g_warning ("PipeWire stream error: %s", error);
      break;
    case PW_STREAM_STATE_PAUSED:
    case PW_STREAM_STATE_STREAMING:
    case PW_STREAM_STATE_UNCONNECTED:
    case PW_STREAM_STATE_CONNECTING:
      break;
    }

  stream->state = state;
}

static void
on_stream_param_changed (void                 *user_data,
                         uint32_t              id,
                         const struct spa_pod *format)
{
  Stream *stream = user_data;
  uint8_t params_buffer[1024];
  struct spa_pod_builder pod_builder;
  const struct spa_pod *params[3];

  if (!format || id != SPA_PARAM_Format)
    return;

  spa_format_video_raw_parse (format, &stream->spa_format);

  pod_builder = SPA_POD_BUILDER_INIT (params_buffer, sizeof (params_buffer));

  params[0] = spa_pod_builder_add_object (
    &pod_builder,
    SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
    SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int (8, 1, 8),
    SPA_PARAM_BUFFERS_dataType, SPA_POD_Int ((1 << SPA_DATA_MemPtr) |
                                             (1 << SPA_DATA_MemFd)),
    0);

  params[1] = spa_pod_builder_add_object (
    &pod_builder,
    SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
    SPA_PARAM_META_type, SPA_POD_Id (SPA_META_Header),
    SPA_PARAM_META_size, SPA_POD_Int (sizeof (struct spa_meta_header)),
    0);

  params[2] = spa_pod_builder_add_object (
    &pod_builder,
    SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
    SPA_PARAM_META_type, SPA_POD_Id (SPA_META_Cursor),
    SPA_PARAM_META_size, SPA_POD_CHOICE_RANGE_Int (CURSOR_META_SIZE (384, 384),
                                                   CURSOR_META_SIZE (1, 1),
                                                   CURSOR_META_SIZE (384, 384)),
    0);

  pw_stream_update_params (stream->pipewire_stream,
                           params, G_N_ELEMENTS (params));
}

static void
process_buffer_metadata (Stream            *stream,
                         struct spa_buffer *buffer)
{
  struct spa_meta_cursor *spa_meta_cursor;

  spa_meta_cursor = spa_buffer_find_meta_data (buffer, SPA_META_Cursor,
                                               sizeof *spa_meta_cursor);
  if (!spa_meta_cursor)
    return;

  if (!spa_meta_cursor_is_valid (spa_meta_cursor))
    return;

  stream->cursor_x = spa_meta_cursor->position.x;
  stream->cursor_y = spa_meta_cursor->position.y;
}

static void
sanity_check_memfd (struct spa_buffer *buffer)
{
  size_t size;
  uint8_t *map;

  size = buffer->datas[0].maxsize + buffer->datas[0].mapoffset;
  g_assert_cmpint (size, >, 0);
  map = mmap (NULL, size, PROT_READ, MAP_PRIVATE, buffer->datas[0].fd, 0);
  g_assert (map != MAP_FAILED);
  munmap (map, size);
}

static void
sanity_check_memptr (struct spa_buffer *buffer)
{
  size_t size;

  size = buffer->datas[0].maxsize + buffer->datas[0].mapoffset;
  g_assert_cmpint (size, >, 0);

  g_assert_nonnull (buffer->datas[0].data);
}

static void
process_buffer (Stream            *stream,
                struct spa_buffer *buffer)
{
  process_buffer_metadata (stream, buffer);

  if (buffer->datas[0].chunk->size == 0)
    g_assert_not_reached ();
  else if (buffer->datas[0].type == SPA_DATA_MemFd)
    sanity_check_memfd (buffer);
  else if (buffer->datas[0].type == SPA_DATA_DmaBuf)
    g_assert_not_reached ();
  else if (buffer->datas[0].type == SPA_DATA_MemPtr)
    sanity_check_memptr (buffer);
  else
    g_assert_not_reached ();
}

static void
on_stream_process (void *user_data)
{
  Stream *stream = user_data;
  struct pw_buffer *next_buffer;
  struct pw_buffer *buffer = NULL;

  next_buffer = pw_stream_dequeue_buffer (stream->pipewire_stream);
  while (next_buffer)
    {
      buffer = next_buffer;
      next_buffer = pw_stream_dequeue_buffer (stream->pipewire_stream);

      if (next_buffer)
        pw_stream_queue_buffer (stream->pipewire_stream, buffer);
    }
  if (!buffer)
    return;

  process_buffer (stream, buffer->buffer);
  pw_stream_queue_buffer (stream->pipewire_stream, buffer);

  stream->buffer_count++;
}

static const struct pw_stream_events stream_events = {
  PW_VERSION_STREAM_EVENTS,
  .state_changed = on_stream_state_changed,
  .param_changed = on_stream_param_changed,
  .process = on_stream_process,
};

static void
stream_connect (Stream *stream)
{
  struct pw_stream *pipewire_stream;
  uint8_t params_buffer[1024];
  struct spa_pod_builder pod_builder;
  struct spa_rectangle rect;
  struct spa_fraction min_framerate;
  struct spa_fraction max_framerate;
  const struct spa_pod *params[2];
  int ret;

  pipewire_stream = pw_stream_new (_pipewire_core,
                                   "mutter-test-pipewire-stream",
                                   NULL);

  rect = SPA_RECTANGLE (stream->target_width, stream->target_height);
  min_framerate = SPA_FRACTION (1, 1);
  max_framerate = SPA_FRACTION (30, 1);

  pod_builder = SPA_POD_BUILDER_INIT (params_buffer, sizeof (params_buffer));
  params[0] = spa_pod_builder_add_object (
    &pod_builder,
    SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
    SPA_FORMAT_mediaType, SPA_POD_Id (SPA_MEDIA_TYPE_video),
    SPA_FORMAT_mediaSubtype, SPA_POD_Id (SPA_MEDIA_SUBTYPE_raw),
    SPA_FORMAT_VIDEO_format, SPA_POD_Id (SPA_VIDEO_FORMAT_BGRx),
    SPA_FORMAT_VIDEO_size, SPA_POD_Rectangle (&rect),
    SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction (&SPA_FRACTION(0, 1)),
    SPA_FORMAT_VIDEO_maxFramerate, SPA_POD_CHOICE_RANGE_Fraction (&min_framerate,
                                                                  &min_framerate,
                                                                  &max_framerate),
    0);

  stream->pipewire_stream = pipewire_stream;

  pw_stream_add_listener (pipewire_stream,
                          &stream->pipewire_stream_listener,
                          &stream_events,
                          stream);

  ret = pw_stream_connect (stream->pipewire_stream,
                           PW_DIRECTION_INPUT,
                           stream->pipewire_node_id,
                           PW_STREAM_FLAG_AUTOCONNECT,
                           params, 1);
  if (ret < 0)
    g_error ("Failed to connect PipeWire stream: %s", g_strerror (-ret));
}

static void
stream_wait_for_node (Stream *stream)
{
  while (!stream->pipewire_node_id)
    g_main_context_iteration (NULL, TRUE);
}

static void
stream_wait_for_cursor_position (Stream *stream,
                                 int     x,
                                 int     y)
{
  while (stream->cursor_x != x &&
         stream->cursor_y != y)
    g_main_context_iteration (NULL, TRUE);
}

static G_GNUC_UNUSED void
stream_wait_for_streaming (Stream *stream)
{
  while (stream->state != PW_STREAM_STATE_STREAMING)
    g_main_context_iteration (NULL, TRUE);
}

static G_GNUC_UNUSED void
stream_wait_for_render (Stream *stream)
{
  int initial_buffer_count = stream->buffer_count;

  while (stream->buffer_count == initial_buffer_count)
    g_main_context_iteration (NULL, TRUE);
}

static void
stream_resize (Stream *stream,
               int     width,
               int     height)
{
  uint8_t params_buffer[1024];
  struct spa_pod_builder pod_builder;
  const struct spa_pod *params[1];
  struct spa_rectangle rect;

  stream->target_width = width;
  stream->target_height = height;

  rect = SPA_RECTANGLE (width, height);

  pod_builder = SPA_POD_BUILDER_INIT (params_buffer, sizeof (params_buffer));

  params[0] = spa_pod_builder_add_object (
    &pod_builder,
    SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
    SPA_FORMAT_VIDEO_size, SPA_POD_Rectangle (&rect),
    0);

  pw_stream_update_params (stream->pipewire_stream,
                           params, G_N_ELEMENTS (params));
}

static void
on_pipewire_stream_added (MetaDBusScreenCastStream *proxy,
                          unsigned int              node_id,
                          Stream                   *stream)
{
  stream->pipewire_node_id = (uint32_t) node_id;
  stream_connect (stream);
}

static Stream *
stream_new (const char *path,
            int         width,
            int         height)
{
  Stream *stream;
  GError *error = NULL;

  stream = g_new0 (Stream, 1);
  stream->target_width = width;
  stream->target_height = height;

  stream->proxy = meta_dbus_screen_cast_stream_proxy_new_for_bus_sync (
    G_BUS_TYPE_SESSION,
    G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
    "org.gnome.Mutter.ScreenCast",
    path,
    NULL,
    &error);
  if (!stream->proxy)
    g_error ("Failed to acquire proxy: %s", error->message);

  g_signal_connect (stream->proxy, "pipewire-stream-added",
                    G_CALLBACK (on_pipewire_stream_added),
                    stream);

  return stream;
}

static void
stream_free (Stream *stream)
{
  g_clear_pointer (&stream->pipewire_stream, pw_stream_destroy);
  g_clear_object (&stream->proxy);
  g_free (stream);
}

static void
session_notify_absolute_pointer (Session *session,
                                 Stream  *stream,
                                 double   x,
                                 double   y)
{
  GError *error = NULL;

  if (!meta_dbus_remote_desktop_session_call_notify_pointer_motion_absolute_sync (
        session->remote_desktop_session_proxy,
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (stream->proxy)),
        x, y, NULL, &error))
    g_error ("Failed to send absolute pointer motion event: %s", error->message);
}

static void
session_start (Session *session)
{
  GError *error = NULL;

  if (!meta_dbus_remote_desktop_session_call_start_sync (
        session->remote_desktop_session_proxy,
        NULL,
        &error))
    g_error ("Failed to start session: %s", error->message);
}

static void
session_stop (Session *session)
{
  GError *error = NULL;

  if (!meta_dbus_remote_desktop_session_call_stop_sync (
        session->remote_desktop_session_proxy,
        NULL,
        &error))
    g_error ("Failed to stop session: %s", error->message);
}

static Stream *
session_record_virtual (Session *session,
                        int      width,
                        int      height)
{
  GVariantBuilder properties_builder;
  GVariant *properties_variant;
  GError *error = NULL;
  g_autofree char *stream_path = NULL;
  Stream *stream;

  g_variant_builder_init (&properties_builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&properties_builder, "{sv}",
                         "cursor-mode",
                         g_variant_new_uint32 (CURSOR_MODE_METADATA));
  properties_variant = g_variant_builder_end (&properties_builder);

  if (!meta_dbus_screen_cast_session_call_record_virtual_sync (
        session->screen_cast_session_proxy,
        properties_variant,
        &stream_path,
        NULL,
        &error))
    g_error ("Failed to create session: %s", error->message);

  stream = stream_new (stream_path, width, height);
  g_assert_nonnull (stream);
  return stream;
}

static Session *
session_new (MetaDBusRemoteDesktopSession *remote_desktop_session_proxy,
             MetaDBusScreenCastSession    *screen_cast_session_proxy)
{
  Session *session;

  session = g_new0 (Session, 1);
  session->remote_desktop_session_proxy = remote_desktop_session_proxy;
  session->screen_cast_session_proxy = screen_cast_session_proxy;

  return session;
}

static void
session_free (Session *session)
{
  g_clear_object (&session->screen_cast_session_proxy);
  g_clear_object (&session->remote_desktop_session_proxy);
  g_free (session);
}

static Session *
screen_cast_create_session (RemoteDesktop *remote_desktop,
                            ScreenCast    *screen_cast)
{
  GVariantBuilder properties_builder;
  GError *error = NULL;
  g_autofree char *remote_desktop_session_path = NULL;
  MetaDBusRemoteDesktopSession *remote_desktop_session_proxy;
  g_autofree char *screen_cast_session_path = NULL;
  MetaDBusScreenCastSession *screen_cast_session_proxy;
  const char *session_id;
  Session *session;

  if (!meta_dbus_remote_desktop_call_create_session_sync (
        remote_desktop->proxy,
        &remote_desktop_session_path,
        NULL,
        &error))
    g_error ("Failed to create session: %s", error->message);

  remote_desktop_session_proxy =
    meta_dbus_remote_desktop_session_proxy_new_for_bus_sync (
      G_BUS_TYPE_SESSION,
      G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
      "org.gnome.Mutter.RemoteDesktop",
      remote_desktop_session_path,
      NULL,
      &error);
  if (!remote_desktop_session_proxy)
    g_error ("Failed to acquire proxy: %s", error->message);

  session_id =
    meta_dbus_remote_desktop_session_get_session_id (
      remote_desktop_session_proxy);

  g_variant_builder_init (&properties_builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&properties_builder, "{sv}",
                         "remote-desktop-session-id",
                         g_variant_new_string (session_id));

  if (!meta_dbus_screen_cast_call_create_session_sync (
        screen_cast->proxy,
        g_variant_builder_end (&properties_builder),
        &screen_cast_session_path,
        NULL,
        &error))
    g_error ("Failed to create session: %s", error->message);

  screen_cast_session_proxy =
    meta_dbus_screen_cast_session_proxy_new_for_bus_sync (
      G_BUS_TYPE_SESSION,
      G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
      "org.gnome.Mutter.ScreenCast",
      screen_cast_session_path,
      NULL,
      &error);
  if (!screen_cast_session_proxy)
    g_error ("Failed to acquire proxy: %s", error->message);

  session = session_new (remote_desktop_session_proxy,
                         screen_cast_session_proxy);
  g_assert_nonnull (session);
  return session;
}

static RemoteDesktop *
remote_desktop_new (void)
{
  RemoteDesktop *remote_desktop;
  GError *error = NULL;

  remote_desktop = g_new0 (RemoteDesktop, 1);
  remote_desktop->proxy = meta_dbus_remote_desktop_proxy_new_for_bus_sync (
    G_BUS_TYPE_SESSION,
    G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
    "org.gnome.Mutter.RemoteDesktop",
    "/org/gnome/Mutter/RemoteDesktop",
    NULL,
    &error);
  if (!remote_desktop->proxy)
    g_error ("Failed to acquire proxy: %s", error->message);

  return remote_desktop;
}

static void
remote_desktop_free (RemoteDesktop *remote_desktop)
{
  g_clear_object (&remote_desktop->proxy);
  g_free (remote_desktop);
}

static ScreenCast *
screen_cast_new (void)
{
  ScreenCast *screen_cast;
  GError *error = NULL;

  screen_cast = g_new0 (ScreenCast, 1);
  screen_cast->proxy = meta_dbus_screen_cast_proxy_new_for_bus_sync (
    G_BUS_TYPE_SESSION,
    G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
    "org.gnome.Mutter.ScreenCast",
    "/org/gnome/Mutter/ScreenCast",
    NULL,
    &error);
  if (!screen_cast->proxy)
    g_error ("Failed to acquire proxy: %s", error->message);

  return screen_cast;
}

static void
screen_cast_free (ScreenCast *screen_cast)
{
  g_clear_object (&screen_cast->proxy);
  g_free (screen_cast);
}

int
main (int    argc,
      char **argv)
{
  RemoteDesktop *remote_desktop;
  ScreenCast *screen_cast;
  Session *session;
  Stream *stream;

  init_pipewire ();

  remote_desktop = remote_desktop_new ();
  screen_cast = screen_cast_new ();
  session = screen_cast_create_session (remote_desktop, screen_cast);
  stream = session_record_virtual (session, 50, 40);

  session_start (session);

  /* Check that the display server handles events being emitted too early. */
  session_notify_absolute_pointer (session, stream, 2, 3);

  /* Check that we receive the initial frame */

  stream_wait_for_node (stream);
  stream_wait_for_render (stream);
  stream_wait_for_streaming (stream);
  session_notify_absolute_pointer (session, stream, 6, 5);
  session_notify_absolute_pointer (session, stream, 5, 6);
  stream_wait_for_render (stream);
  stream_wait_for_cursor_position (stream, 5, 6);
  g_assert_cmpint (stream->spa_format.size.width, ==, 50);
  g_assert_cmpint (stream->spa_format.size.height, ==, 40);

  /* Check that resizing works */
  stream_resize (stream, 70, 60);
  while (TRUE)
    {
      stream_wait_for_render (stream);

      if (stream->spa_format.size.width == 70 &&
          stream->spa_format.size.height == 60)
        break;

      g_assert_cmpint (stream->spa_format.size.width, ==, 50);
      g_assert_cmpint (stream->spa_format.size.height, ==, 40);
    }

  /* Check that resizing works */
  stream_resize (stream, 60, 60);

  session_stop (session);

  stream_free (stream);
  session_free (session);
  screen_cast_free (screen_cast);
  remote_desktop_free (remote_desktop);

  release_pipewire ();

  return EXIT_SUCCESS;
}
