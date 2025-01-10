/*
 * Copyright (C) 2021-2024 Red Hat Inc.
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

#include "remote-desktop-utils.h"

#include <spa/param/format-utils.h>
#include <spa/param/props.h>
#include <spa/utils/result.h>
#include <stdint.h>
#include <sys/mman.h>

#include "backends/meta-fd-source.h"

#define CURSOR_META_SIZE(width, height) \
 (sizeof(struct spa_meta_cursor) + \
  sizeof(struct spa_meta_bitmap) + width * height * 4)

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

void
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

void
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
    SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int (8, 1, 8),
    SPA_PARAM_BUFFERS_dataType, SPA_POD_Int (1 << SPA_DATA_MemFd),
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

  size = buffer->datas[0].maxsize + buffer->datas[0].mapoffset;
  g_assert_cmpint (size, >, 0);
  g_assert_nonnull (buffer->datas[0].data);
}

static void
process_memfd_buffer (Stream           *stream,
                      struct pw_buffer *buffer)
{
  sanity_check_memfd (buffer->buffer);
  if (stream->buffer)
    pw_stream_queue_buffer (stream->pipewire_stream, stream->buffer);
  stream->buffer = buffer;
}

static void
process_buffer (Stream           *stream,
                struct pw_buffer *buffer)
{
  struct spa_buffer *spa_buffer = buffer->buffer;

  process_buffer_metadata (stream, buffer->buffer);

  if (spa_buffer->datas[0].chunk->size != 0)
    {
      if (spa_buffer->datas[0].type == SPA_DATA_MemFd)
        process_memfd_buffer (stream, buffer);
      else
        g_assert_not_reached ();
    }
}

static void
on_stream_process (void *user_data)
{
  Stream *stream = user_data;
  struct pw_buffer *next_buffer;
  struct pw_buffer *buffer = NULL;

  if (!stream->pipewire_stream)
    return;

  next_buffer = pw_stream_dequeue_buffer (stream->pipewire_stream);
  if (next_buffer)
    g_debug ("Dequeued buffer, queue previous");
  while (next_buffer)
    {

      buffer = next_buffer;
      next_buffer = pw_stream_dequeue_buffer (stream->pipewire_stream);

      if (next_buffer)
        {
          g_debug ("Dequeued another buffer, queuing previous");
          pw_stream_queue_buffer (stream->pipewire_stream, buffer);
        }
    }
  if (!buffer)
    return;

  process_buffer (stream, buffer);
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
  struct spa_rectangle min_rect;
  struct spa_rectangle max_rect;
  struct spa_fraction min_framerate;
  struct spa_fraction max_framerate;
  const struct spa_pod *params[2];
  int ret;

  pipewire_stream = pw_stream_new (_pipewire_core,
                                   "mutter-test-pipewire-stream",
                                   NULL);

  switch (stream->stream_type)
    {
    case STREAM_TYPE_VIRTUAL:
      rect = SPA_RECTANGLE (stream->virtual.target_width,
                            stream->virtual.target_height);
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
      break;
    case STREAM_TYPE_MONITOR:
      min_rect = SPA_RECTANGLE (1, 1);
      max_rect = SPA_RECTANGLE (INT32_MAX, INT32_MAX);
      min_framerate = SPA_FRACTION (1, 1);
      max_framerate = SPA_FRACTION (30, 1);

      pod_builder = SPA_POD_BUILDER_INIT (params_buffer, sizeof (params_buffer));
      params[0] = spa_pod_builder_add_object (
        &pod_builder,
        SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
        SPA_FORMAT_mediaType, SPA_POD_Id (SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id (SPA_MEDIA_SUBTYPE_raw),
        SPA_FORMAT_VIDEO_format, SPA_POD_Id (SPA_VIDEO_FORMAT_BGRx),
        SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle (&min_rect,
                                                               &min_rect,
                                                               &max_rect),
        SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction (&SPA_FRACTION(0, 1)),
        SPA_FORMAT_VIDEO_maxFramerate, SPA_POD_CHOICE_RANGE_Fraction (&min_framerate,
                                                                      &min_framerate,
                                                                      &max_framerate),
        0);
      break;
    }

  stream->pipewire_stream = pipewire_stream;

  pw_stream_add_listener (pipewire_stream,
                          &stream->pipewire_stream_listener,
                          &stream_events,
                          stream);

  ret = pw_stream_connect (stream->pipewire_stream,
                           PW_DIRECTION_INPUT,
                           stream->pipewire_node_id,
                           (PW_STREAM_FLAG_AUTOCONNECT |
                            PW_STREAM_FLAG_MAP_BUFFERS),
                           params, 1);
  if (ret < 0)
    g_error ("Failed to connect PipeWire stream: %s", g_strerror (-ret));
}

void
stream_resize (Stream *stream,
               int     width,
               int     height)
{
  uint8_t params_buffer[1024];
  struct spa_pod_builder pod_builder;
  const struct spa_pod *params[1];
  struct spa_rectangle rect;

  stream->virtual.target_width = width;
  stream->virtual.target_height = height;

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

void
stream_wait_for_render (Stream *stream)
{
  int initial_buffer_count = stream->buffer_count;

  g_debug ("Waiting for new buffer");
  while (stream->buffer_count == initial_buffer_count)
    g_main_context_iteration (NULL, TRUE);
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
stream_new_virtual (const char *path,
                    int         width,
                    int         height,
                    CursorMode  cursor_mode)
{
  Stream *stream;
  GError *error = NULL;

  stream = g_new0 (Stream, 1);
  stream->stream_type = STREAM_TYPE_VIRTUAL;
  stream->cursor_mode = cursor_mode;
  stream->virtual.target_width = width;
  stream->virtual.target_height = height;

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

static Stream *
stream_new_monitor (const char *path,
                    CursorMode  cursor_mode)
{
  Stream *stream;
  GError *error = NULL;

  stream = g_new0 (Stream, 1);
  stream->stream_type = STREAM_TYPE_MONITOR;
  stream->cursor_mode = cursor_mode;

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

void
stream_free (Stream *stream)
{
  g_clear_pointer (&stream->pipewire_stream, pw_stream_destroy);
  g_clear_object (&stream->proxy);
  g_free (stream);
}

void
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

static gboolean
ei_source_prepare (gpointer user_data)
{
  Session *session = user_data;
  struct ei_event *ei_event;
  gboolean retval;

  ei_event = ei_peek_event (session->ei);
  retval = !!ei_event;
  ei_event_unref (ei_event);

  return retval;
}

static void
process_ei_event (Session         *session,
                  struct ei_event *ei_event)
{
  g_debug ("Processing event %s",
           ei_event_type_to_string (ei_event_get_type (ei_event)));

  switch (ei_event_get_type (ei_event))
    {
    case EI_EVENT_SEAT_ADDED:
      {
        struct ei_seat *ei_seat = ei_event_get_seat (ei_event);
        size_t i;

        g_assert_null (session->ei_seat);
        session->ei_seat = ei_seat;

        for (i = 0; i < session->seat_caps->len; i++)
          {
            enum ei_device_capability cap =
              g_array_index (session->seat_caps, enum ei_device_capability, i);

            g_assert_true (ei_seat_has_capability (ei_seat, cap));
            ei_seat_bind_capabilities (ei_seat, cap, NULL);
          }
        break;
      }
    case EI_EVENT_SEAT_REMOVED:
      {
        g_assert_true (session->ei_seat == ei_event_get_seat (ei_event));

        session->ei_seat = NULL;
        break;
      }
    case EI_EVENT_DEVICE_RESUMED:
      {
        struct ei_device *ei_device = ei_event_get_device (ei_event);

        if (ei_device_has_capability (ei_device, EI_DEVICE_CAP_POINTER) ||
            ei_device_has_capability (ei_device, EI_DEVICE_CAP_POINTER_ABSOLUTE))
          session->pointer = ei_device;
        else if (ei_device_has_capability (ei_device, EI_DEVICE_CAP_KEYBOARD))
          session->keyboard = ei_device;

        ei_device_start_emulating (ei_event_get_device (ei_event),
                                   ++session->ei_sequence);
        break;
      }
    case EI_EVENT_DEVICE_REMOVED:
      {
        struct ei_device *ei_device = ei_event_get_device (ei_event);

        if (ei_device == session->pointer)
          session->pointer = NULL;
        if (ei_device == session->keyboard)
          session->keyboard = NULL;
        break;
      }
    case EI_EVENT_PONG:
      {
        g_assert_true (session->ping == ei_event_pong_get_ping (ei_event));
        g_clear_pointer (&session->ping, ei_ping_unref);
      }
    default:
      break;
    }
}

static gboolean
ei_source_dispatch (gpointer user_data)
{
  Session *session = user_data;
  struct ei_event *ei_event;

  ei_event = ei_get_event (session->ei);
  if (!ei_event)
    {
      ei_dispatch (session->ei);

      ei_event = ei_get_event (session->ei);
      if (!ei_event)
        return G_SOURCE_CONTINUE;
    }

  process_ei_event (session, ei_event);
  ei_event_unref (ei_event);

  return G_SOURCE_CONTINUE;
}

static void
log_handler (struct ei             *ei,
             enum ei_log_priority   priority,
             const char            *message,
             struct ei_log_context *ctx)
{
  int message_length = strlen (message);

  if (priority >= EI_LOG_PRIORITY_ERROR)
    g_critical ("libei: %.*s", message_length, message);
  else if (priority >= EI_LOG_PRIORITY_WARNING)
    g_warning ("libei: %.*s", message_length, message);
  else if (priority >= EI_LOG_PRIORITY_INFO)
    g_info ("libei: %.*s", message_length, message);
  else
    g_debug ("libei: %.*s", message_length, message);
}

void
session_connect_to_eis (Session *session)
{
  GVariantBuilder builder;
  GVariant *options;
  MetaDBusRemoteDesktopSession *proxy;
  g_autoptr (GVariant) fd_variant = NULL;
  g_autoptr (GUnixFDList) fd_list = NULL;
  GError *error = NULL;
  int fd;
  struct ei *ei;
  int ret;

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  options = g_variant_builder_end (&builder);

  proxy = session->remote_desktop_session_proxy;
  if (!meta_dbus_remote_desktop_session_call_connect_to_eis_sync (proxy,
                                                                  options,
                                                                  NULL,
                                                                  &fd_variant,
                                                                  &fd_list,
                                                                  NULL, &error))
    g_error ("Failed to connect to EIS: %s", error->message);

  fd = g_unix_fd_list_get (fd_list, g_variant_get_handle (fd_variant), &error);
  if (fd == -1)
    g_error ("Failed to get EIS file descriptor: %s", error->message);

  ei = ei_new_sender (session);
  ei_log_set_handler (ei, log_handler);
  ei_log_set_priority (ei, EI_LOG_PRIORITY_DEBUG);

  ret = ei_setup_backend_fd (ei, fd);
  if (ret < 0)
    g_error ("Failed to setup libei backend: %s", g_strerror (errno));

  session->ei = ei;
  session->ei_source = meta_create_fd_source (ei_get_fd (ei),
                                              "libei",
                                              ei_source_prepare,
                                              ei_source_dispatch,
                                              session,
                                              NULL);
  g_source_attach (session->ei_source, NULL);
  g_source_unref (session->ei_source);
}

static int
find_seat_capability_index (Session                   *session,
                            enum ei_device_capability  cap)
{
  int i;

  for (i = 0; i < session->seat_caps->len; i++)
    {
      enum ei_device_capability device_cap =
        g_array_index (session->seat_caps, enum ei_device_capability, i);

      if (device_cap == cap)
        return i;
    }

  return -1;
}

static gboolean
has_seat_capability (Session                   *session,
                     enum ei_device_capability  cap)
{
  return find_seat_capability_index (session, cap) >= 0;
}

void
session_add_seat_capability (Session                   *session,
                             enum ei_device_capability  cap)
{
  g_assert_false (has_seat_capability (session, cap));
  g_array_append_val (session->seat_caps, cap);

  if (session->ei_seat)
    ei_seat_bind_capabilities (session->ei_seat, cap, NULL);
}

void
session_remove_seat_capability (Session                   *session,
                                enum ei_device_capability  cap)
{
  g_assert_true (has_seat_capability (session, cap));
  g_array_remove_index (session->seat_caps,
                        find_seat_capability_index (session, cap));

  if (session->ei_seat)
    ei_seat_unbind_capabilities (session->ei_seat, cap, NULL);
}

void
session_ei_roundtrip (Session *session)
{
  g_assert_null (session->ping);

  session->ping = ei_new_ping (session->ei);
  ei_ping (session->ping);

  while (session->ping)
    g_main_context_iteration (NULL, TRUE);
}

void
session_start (Session *session)
{
  GError *error = NULL;

  if (session->remote_desktop_session_proxy)
    {
      if (!meta_dbus_remote_desktop_session_call_start_sync (
          session->remote_desktop_session_proxy,
          NULL,
          &error))
        g_error ("Failed to start session: %s", error->message);
    }
  else
    {
      if (!meta_dbus_screen_cast_session_call_start_sync (
          session->screen_cast_session_proxy,
          NULL,
          &error))
        g_error ("Failed to start session: %s", error->message);
    }
}

void
session_stop (Session *session)
{
  GError *error = NULL;

  g_clear_pointer (&session->ei, ei_unref);
  g_clear_pointer (&session->ei_source, g_source_destroy);
  g_clear_pointer (&session->seat_caps, g_array_unref);

  if (session->remote_desktop_session_proxy)
    {
      if (!meta_dbus_remote_desktop_session_call_stop_sync (
          session->remote_desktop_session_proxy,
          NULL,
          &error))
        g_error ("Failed to stop session: %s", error->message);
    }
  else
    {
      if (!meta_dbus_screen_cast_session_call_stop_sync (
          session->screen_cast_session_proxy,
          NULL,
          &error))
        g_error ("Failed to stop session: %s", error->message);
    }
}

Stream *
session_record_virtual (Session    *session,
                        int         width,
                        int         height,
                        CursorMode  cursor_mode)
{
  GVariantBuilder properties_builder;
  GVariant *properties_variant;
  GError *error = NULL;
  g_autofree char *stream_path = NULL;
  Stream *stream;

  g_variant_builder_init (&properties_builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&properties_builder, "{sv}",
                         "cursor-mode",
                         g_variant_new_uint32 (cursor_mode));
  properties_variant = g_variant_builder_end (&properties_builder);

  if (!meta_dbus_screen_cast_session_call_record_virtual_sync (
        session->screen_cast_session_proxy,
        properties_variant,
        &stream_path,
        NULL,
        &error))
    g_error ("Failed to create session: %s", error->message);

  stream = stream_new_virtual (stream_path, width, height, cursor_mode);
  g_assert_nonnull (stream);
  return stream;
}

Stream *
session_record_monitor (Session    *session,
                        const char *connector,
                        CursorMode  cursor_mode)
{
  GVariantBuilder properties_builder;
  GVariant *properties_variant;
  GError *error = NULL;
  g_autofree char *stream_path = NULL;
  Stream *stream;

  g_variant_builder_init (&properties_builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&properties_builder, "{sv}",
                         "cursor-mode",
                         g_variant_new_uint32 (cursor_mode));
  properties_variant = g_variant_builder_end (&properties_builder);

  if (!meta_dbus_screen_cast_session_call_record_monitor_sync (
        session->screen_cast_session_proxy,
        connector ? connector : "",
        properties_variant,
        &stream_path,
        NULL,
        &error))
    g_error ("Failed to create session: %s", error->message);

  stream = stream_new_monitor (stream_path, cursor_mode);
  g_assert_nonnull (stream);
  return stream;
}

const char *
session_get_id (Session *session)
{
  MetaDBusRemoteDesktopSession *proxy =
    session->remote_desktop_session_proxy;

  return meta_dbus_remote_desktop_session_get_session_id (proxy);
}

Session *
session_new (MetaDBusRemoteDesktopSession *remote_desktop_session_proxy,
             MetaDBusScreenCastSession    *screen_cast_session_proxy)
{
  Session *session;

  session = g_new0 (Session, 1);
  session->remote_desktop_session_proxy = remote_desktop_session_proxy;
  session->screen_cast_session_proxy = screen_cast_session_proxy;
  session->seat_caps = g_array_new (FALSE, FALSE, sizeof (enum ei_device_capability));

  return session;
}

void
session_free (Session *session)
{
  g_assert_null (session->ei);
  g_clear_object (&session->screen_cast_session_proxy);
  g_clear_object (&session->remote_desktop_session_proxy);
  g_free (session);
}

Session *
screen_cast_create_session (RemoteDesktop *remote_desktop,
                            ScreenCast    *screen_cast)
{
  GVariantBuilder properties_builder;
  GError *error = NULL;
  g_autofree char *remote_desktop_session_path = NULL;
  MetaDBusRemoteDesktopSession *remote_desktop_session_proxy = NULL;
  g_autofree char *screen_cast_session_path = NULL;
  MetaDBusScreenCastSession *screen_cast_session_proxy;
  const char *session_id = NULL;
  Session *session;

  if (remote_desktop)
    {
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
    }

  g_variant_builder_init (&properties_builder, G_VARIANT_TYPE ("a{sv}"));

  if (session_id)
    {
      g_variant_builder_add (&properties_builder, "{sv}",
                             "remote-desktop-session-id",
                             g_variant_new_string (session_id));
    }

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

RemoteDesktop *
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

void
remote_desktop_free (RemoteDesktop *remote_desktop)
{
  g_clear_object (&remote_desktop->proxy);
  g_free (remote_desktop);
}

ScreenCast *
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

void
screen_cast_free (ScreenCast *screen_cast)
{
  g_clear_object (&screen_cast->proxy);
  g_free (screen_cast);
}
