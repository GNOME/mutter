/*
 * Copyright (C) 2024 Red Hat Inc.
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

/* Till https://gitlab.freedesktop.org/pipewire/pipewire/-/issues/4065 is fixed */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-conversion"

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

  gboolean ignore_requests;

  int target_width;
  int target_height;

  struct pw_buffer *active_buffer;
  uint64_t buffer_sequence;
  gboolean requested_buffer;
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
  GSource source;

  struct pw_loop *pipewire_loop;
} PipeWireSource;

static GSource *_pipewire_source;
static struct pw_context *_pipewire_context;
static struct pw_core *_pipewire_core;
static struct spa_hook _pipewire_core_listener;

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

static GSource *
create_pipewire_source (struct pw_loop *pipewire_loop)
{
  GSource *source;
  PipeWireSource *pipewire_source;

  source = g_source_new (&pipewire_source_funcs,
                         sizeof (PipeWireSource));

  pipewire_source = (PipeWireSource *) source;
  pipewire_source->pipewire_loop = pipewire_loop;

  g_source_add_unix_fd (source,
                        pw_loop_get_fd (pipewire_source->pipewire_loop),
                        G_IO_IN | G_IO_ERR);

  pw_loop_enter (pipewire_source->pipewire_loop);
  g_source_attach (source, NULL);

  return source;
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
  struct pw_loop *pipewire_loop;

  pw_init (NULL, NULL);

  pipewire_loop = pw_loop_new (NULL);
  g_assert_nonnull (pipewire_loop);

  _pipewire_source = create_pipewire_source (pipewire_loop);
  _pipewire_context = pw_context_new (pipewire_loop,
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
      g_source_destroy (_pipewire_source);
      g_source_unref (_pipewire_source);
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

  g_debug ("New PipeWire stream (%u) state '%s'",
           stream->pipewire_node_id,
           pw_stream_state_as_string (state));

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
    SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int (2, 1, 2),
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
    //SPA_PARAM_META_size, SPA_POD_CHOICE_RANGE_Int (CURSOR_META_SIZE (384, 384),
                                                   //CURSOR_META_SIZE (1, 1),
                                                   //CURSOR_META_SIZE (384, 384)),
    0);

  pw_stream_update_params (stream->pipewire_stream,
                           params, G_N_ELEMENTS (params));
}

static void
process_buffer_metadata (Stream            *stream,
                         struct spa_buffer *buffer)
{
  struct spa_meta_header *spa_meta_header;

  spa_meta_header = spa_buffer_find_meta_data (buffer, SPA_META_Header,
                                               sizeof *spa_meta_header);
  g_assert_nonnull (spa_meta_header);

  stream->buffer_sequence = spa_meta_header->seq;
  g_debug ("Received buffer #%" G_GUINT64_FORMAT, spa_meta_header->seq);
}


static void
sanity_check_cursor_only (struct spa_buffer *buffer)
{
  struct spa_meta_cursor *spa_meta_cursor;

  spa_meta_cursor = spa_buffer_find_meta_data (buffer, SPA_META_Cursor,
                                               sizeof *spa_meta_cursor);
  g_assert_nonnull (spa_meta_cursor);
  g_assert_true (spa_meta_cursor->id != 0);
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
process_buffer (Stream           *stream,
                struct pw_buffer *pipewire_buffer)
{
  struct spa_buffer *spa_buffer = pipewire_buffer->buffer;

  process_buffer_metadata (stream, spa_buffer);

  if (spa_buffer->datas[0].chunk->size == 0)
    sanity_check_cursor_only (spa_buffer);
  else if (spa_buffer->datas[0].type == SPA_DATA_MemFd)
    sanity_check_memfd (spa_buffer);
  else if (spa_buffer->datas[0].type == SPA_DATA_DmaBuf)
    g_assert_not_reached ();
  else if (spa_buffer->datas[0].type == SPA_DATA_MemPtr)
    g_assert_not_reached ();
  else
    g_assert_not_reached ();

  g_assert_null (stream->active_buffer);
  stream->active_buffer = pipewire_buffer;
}

static void
on_stream_process (void *user_data)
{
  Stream *stream = user_data;
  struct pw_buffer *pipewire_buffer;

  pipewire_buffer = pw_stream_dequeue_buffer (stream->pipewire_stream);
  process_buffer (stream, pipewire_buffer);
  stream->requested_buffer = FALSE;
}

static void
stream_release_active_buffer (Stream *stream)
{
  if (stream->active_buffer)
    {
      g_debug ("Releasing active buffer");
      pw_stream_queue_buffer (stream->pipewire_stream, stream->active_buffer);
      stream->active_buffer = NULL;
    }
}

static void
on_stream_command (void                     *user_data,
                   const struct spa_command *command)
{
  Stream *stream = user_data;

  switch (SPA_NODE_COMMAND_ID (command))
    {
    case SPA_NODE_COMMAND_RequestProcess:
      {
        enum pw_stream_state state;

        state = pw_stream_get_state (stream->pipewire_stream, NULL);

        if (!stream->ignore_requests &&
            state == PW_STREAM_STATE_STREAMING &&
            !stream->requested_buffer)
          {
            stream_release_active_buffer (stream);
            g_debug ("Triggering requested process");
            pw_stream_trigger_process (stream->pipewire_stream);
            stream->requested_buffer = TRUE;
          }
        else
          {
            g_debug ("Ignored triggering requested process: "
                     "should ignore: %s, "
                     "state: %s, "
                     "requested buffer: %s",
                     stream->ignore_requests ? "yes" : "no",
                     pw_stream_state_as_string (state),
                     stream->requested_buffer ? "yes" : "no");
          }
        break;
      }
    default:
      break;
    }
}

static void
on_stream_remove_buffer (void             *user_data,
                         struct pw_buffer *buffer)
{
  Stream *stream = user_data;

  if (buffer == stream->active_buffer)
    stream->active_buffer = NULL;
}

static const struct pw_stream_events stream_events = {
  PW_VERSION_STREAM_EVENTS,
  .state_changed = on_stream_state_changed,
  .param_changed = on_stream_param_changed,
  .process = on_stream_process,
  .command = on_stream_command,
  .remove_buffer = on_stream_remove_buffer,
};

static void
stream_connect (Stream *stream)
{
  struct pw_properties *pipewire_props;
  struct pw_stream *pipewire_stream;
  uint8_t params_buffer[1024];
  struct spa_pod_builder pod_builder;
  struct spa_rectangle rect;
  struct spa_fraction min_framerate;
  struct spa_fraction max_framerate;
  const struct spa_pod *params[2];
  int ret;

  pipewire_props = pw_properties_new (PW_KEY_PRIORITY_DRIVER, "10000",
                                      NULL);
  //pw_properties_set (pipewire_props,
                     //PW_KEY_PRIORITY_DRIVER,
                     //"10000");

  pipewire_stream = pw_stream_new (_pipewire_core,
                                   "mutter-test-pipewire-stream",
                                   pipewire_props);

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
                           (PW_STREAM_FLAG_AUTOCONNECT |
                            PW_STREAM_FLAG_DRIVER),
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
stream_wait_for_size (Stream *stream,
                      int     width,
                      int     height)
{
  while (stream->spa_format.size.width != width ||
         stream->spa_format.size.height != height)
    g_main_context_iteration (NULL, TRUE);
}

static void
stream_wait_for_streaming (Stream *stream)
{
  while (stream->state != PW_STREAM_STATE_STREAMING)
    g_main_context_iteration (NULL, TRUE);
  g_assert_true (pw_stream_is_driving (stream->pipewire_stream));
}

static void
stream_wait_for_paused (Stream *stream)
{
  while (stream->state != PW_STREAM_STATE_PAUSED)
    g_main_context_iteration (NULL, TRUE);
  g_assert_true (pw_stream_is_driving (stream->pipewire_stream));
}

static void
stream_wait_for_frame (Stream *stream)
{
  uint64_t target_buffer_sequence;

  target_buffer_sequence = stream->buffer_sequence + 1;
  while (target_buffer_sequence != stream->buffer_sequence)
    g_main_context_iteration (NULL, TRUE);
}

static void
stream_trigger_and_wait_for_frame (Stream *stream)
{
  stream_release_active_buffer (stream);

  g_debug ("Triggering process");
  pw_stream_trigger_process (stream->pipewire_stream);

  stream_wait_for_frame (stream);
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
stream_trigger_damage (Stream *stream)
{
  g_print ("post_damage\n");
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

  g_log_writer_default_set_use_stderr (TRUE);

  g_debug ("Initializing PipeWire");
  init_pipewire ();

  g_debug ("Creating screen cast session");
  remote_desktop = remote_desktop_new ();
  screen_cast = screen_cast_new ();
  session = screen_cast_create_session (remote_desktop, screen_cast);
  stream = session_record_virtual (session, 50, 40);

  /* The first part of the test, explicitly tests triggering frames and getting
   * a predictable result for each trigger. Ignore the requests for process
   * during this, to not have them interfere.
   */
  stream->ignore_requests = TRUE;

  g_debug ("Starting screen cast stream");
  session_start (session);

  g_debug ("Waiting for stream to be established");
  stream_wait_for_node (stream);
  stream_wait_for_streaming (stream);

  g_debug ("Triggering a few frames");
  stream_trigger_and_wait_for_frame (stream);
  stream_trigger_and_wait_for_frame (stream);
  stream_trigger_and_wait_for_frame (stream);

  g_debug ("Resizing stream");
  stream_resize (stream, 70, 60);
  stream_wait_for_paused (stream);
  stream_wait_for_size (stream, 70, 60);
  stream_wait_for_streaming (stream);

  g_debug ("Triggering frames with the new size");
  stream_trigger_and_wait_for_frame (stream);
  g_assert_cmpint (stream->spa_format.size.width, ==, 70);
  g_assert_cmpint (stream->spa_format.size.height, ==, 60);
  stream_trigger_and_wait_for_frame (stream);
  stream_trigger_and_wait_for_frame (stream);

  /* Test handling compositor side damage resulting in new frames. */
  stream->ignore_requests = FALSE;

  g_debug ("Trigger compositor side damage");
  stream_trigger_damage (stream);
  stream_wait_for_frame (stream);

  g_debug ("Trigger pointer movement");
  session_notify_absolute_pointer (session, stream, 2, 3);
  stream_wait_for_frame (stream);

  g_debug ("Stopping session");
  session_stop (session);

  stream_free (stream);
  session_free (session);
  screen_cast_free (screen_cast);
  remote_desktop_free (remote_desktop);

  release_pipewire ();

  g_debug ("Done");

  return EXIT_SUCCESS;
}

#pragma GCC diagnostic pop
