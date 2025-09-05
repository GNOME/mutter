/*
 * Copyright (C) 2021-2024 Red Hat Inc.
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

#include "config.h"

#include "mdk-session.h"

#include <gdk/wayland/gdkwayland.h>
#include <gio/gio.h>
#include <gio/gunixoutputstream.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>
#include <libei.h>
#include <sys/mman.h>
#include <xkbcommon/xkbcommon.h>

#include "mdk-context.h"
#include "mdk-ei.h"
#include "mdk-keyboard.h"
#include "mdk-pointer.h"
#include "mdk-stream.h"
#include "mdk-touch.h"

#include "mdk-dbus-remote-desktop.h"
#include "mdk-dbus-screen-cast.h"

typedef enum _MdkScreenCastCursorMode
{
  MDK_SCREEN_CAST_CURSOR_MODE_HIDDEN = 0,
  MDK_SCREEN_CAST_CURSOR_MODE_EMBEDDED = 1,
  MDK_SCREEN_CAST_CURSOR_MODE_METADATA = 2,
} MdkScreenCastCursorMode;

typedef enum _MdkRemoteDesktopKeymapType
{
  MDK_REMOTE_DESKTOP_KEYMAP_TYPE_XKB = 0,
} MdkRemoteDesktopKeymapType;

typedef enum _MdkRemoteDesktopKeymapFormat
{
  MDK_REMOTE_DESKTOP_KEYMAP_FORMAT_XKB_TEXT_V1 = 1,
  MDK_REMOTE_DESKTOP_KEYMAP_FORMAT_XKB_TEXT_V2 = 2,
} MdkRemoteDesktopKeymapFormat;

enum
{
  PROP_0,

  PROP_CONTEXT,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

enum
{
  CLOSED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

struct _MdkSession
{
  GObject parent;

  MdkContext *context;

  MdkEi *ei;

  MdkDBusRemoteDesktop *remote_desktop_proxy;
  MdkDBusScreenCast *screen_cast_proxy;
  MdkDBusRemoteDesktopSession *remote_desktop_session_proxy;
  MdkDBusScreenCastSession *screen_cast_session_proxy;

  struct xkb_keymap *xkb_keymap;
  int layout_index;

  gulong clipboard_changed_handler_id;

  GCancellable *cancellable;
};

static void
initable_iface_init (GInitableIface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (MdkSession, mdk_session, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                      initable_iface_init))

static void
on_session_closed (MdkDBusRemoteDesktopSession *remote_desktop_session_proxy,
                   MdkSession                  *session)
{
  g_signal_emit (session, signals[CLOSED], 0);
}

static void
maybe_sync_keymap (MdkSession *session)
{
  GdkDisplay *display = gdk_display_get_default ();
  GdkSeat *seat = gdk_display_get_default_seat (display);
  GdkDevice *keyboard = gdk_seat_get_keyboard (seat);
  struct xkb_keymap *xkb_keymap;
  int layout_index;

  if (!mdk_context_get_use_host_keymap (session->context))
    {
      if (session->xkb_keymap)
        {
          GVariantBuilder options_builder;

          session->xkb_keymap = NULL;

          g_variant_builder_init (&options_builder, G_VARIANT_TYPE ("a{sv}"));
          mdk_dbus_remote_desktop_session_call_set_keymap (
            session->remote_desktop_session_proxy,
            g_variant_builder_end (&options_builder),
            NULL,
            NULL, NULL, NULL);
        }
      return;
    }

  if (!GDK_IS_WAYLAND_DISPLAY (display))
    {
      g_warning ("Changing keymap not supported when running from X11");
      return;
    }

  xkb_keymap = gdk_wayland_device_get_xkb_keymap (keyboard);
  layout_index = gdk_device_get_active_layout_index (keyboard);

  if (xkb_keymap == session->xkb_keymap &&
      layout_index == session->layout_index)
    return;

  session->layout_index = layout_index;

  if (xkb_keymap != session->xkb_keymap)
    {
      g_autofree char *keymap_serialized = NULL;
      g_autofd int fd = -1;
      int fd_idx;
      size_t keymap_size;
      void *keymap_mem;
      GVariantBuilder options_builder;
      g_autoptr (GUnixFDList) fd_list = NULL;
      g_autoptr (GError) error = NULL;

      session->xkb_keymap = xkb_keymap;

      keymap_serialized = xkb_keymap_get_as_string (xkb_keymap,
                                                    XKB_KEYMAP_FORMAT_TEXT_V1);
      if (!keymap_serialized)
        {
          g_warning ("Failed to serialize current keymap.");
          return;
        }

      fd = memfd_create ("mdk-keymap", MFD_ALLOW_SEALING | MFD_CLOEXEC);
      if (fd == -1)
        {
          g_warning ("Failed to create keymap memfd: %s", g_strerror (errno));
          return;
        }

      keymap_size = strlen (keymap_serialized) + 1;
      if (ftruncate (fd, keymap_size) == -1)
        {
          g_warning ("ftruncate of keymap fd failed: %s", g_strerror (errno));
          return;
        }

      keymap_mem = mmap (NULL, keymap_size, PROT_WRITE, MAP_SHARED, fd, 0);
      if (keymap_mem == MAP_FAILED)
        {
          g_warning ("Failed mmap keymap fd: %s", g_strerror (errno));
          return;
        }

      strcpy (keymap_mem, keymap_serialized);

      if (munmap (keymap_mem, keymap_size) == -1)
        {
          g_warning ("Failed munmap keymap fd: %s", g_strerror (errno));
          return;
        }

      fd_list = g_unix_fd_list_new ();

      fd_idx = g_unix_fd_list_append (fd_list, fd, &error);
      if (fd_idx == -1)
        {
          g_warning ("Failed to append file descriptor to fd list: %s",
                     error->message);
          return;
        }

      layout_index = gdk_device_get_active_layout_index (keyboard);

      g_variant_builder_init (&options_builder, G_VARIANT_TYPE ("a{sv}"));
      g_variant_builder_add (&options_builder, "{sv}",
                             "keymap-type",
                             g_variant_new_uint32 (MDK_REMOTE_DESKTOP_KEYMAP_TYPE_XKB));
      g_variant_builder_add (&options_builder, "{sv}",
                             "xkb-keymap-format",
                             g_variant_new_uint32 (MDK_REMOTE_DESKTOP_KEYMAP_FORMAT_XKB_TEXT_V1));
      g_variant_builder_add (&options_builder, "{sv}",
                             "xkb-keymap",
                             g_variant_new_handle (fd_idx));
      g_variant_builder_add (&options_builder, "{sv}",
                             "xkb-keymap-layout-index",
                             g_variant_new_uint32 (layout_index));
      g_variant_builder_add (&options_builder, "{sv}",
                             "lock-keymap",
                             g_variant_new_boolean (TRUE));

      mdk_dbus_remote_desktop_session_call_set_keymap (
        session->remote_desktop_session_proxy,
        g_variant_builder_end (&options_builder),
        fd_list,
        NULL, NULL, NULL);
    }
  else
    {
      mdk_dbus_remote_desktop_session_call_set_keymap_layout_index (
        session->remote_desktop_session_proxy,
        layout_index,
        NULL, NULL, NULL);
    }
}

static void
on_use_host_keymap_changed (MdkContext *context,
                            GParamSpec *pspec,
                            MdkSession *session)
{
  maybe_sync_keymap (session);
}

static gboolean
init_session (MdkSession    *session,
              GCancellable  *cancellable,
              GError       **error)
{
  g_autofree char *session_path = NULL;
  GVariantBuilder builder;
  const char *remote_desktop_session_id;
  GVariant *properties;
  g_autoptr (GVariant) fd_variant = NULL;
  g_autoptr (GUnixFDList) fd_list = NULL;
  int ei_fd_idx = -1;
  g_autofd int ei_fd = -1;

  g_debug ("Opening remote desktop and screen cast session");

  if (!mdk_dbus_remote_desktop_call_create_session_sync (
        session->remote_desktop_proxy,
        &session_path,
        cancellable,
        error))
    return FALSE;

  session->remote_desktop_session_proxy =
    mdk_dbus_remote_desktop_session_proxy_new_for_bus_sync (
      G_BUS_TYPE_SESSION,
      G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
      "org.gnome.Mutter.RemoteDesktop",
      session_path,
      cancellable,
      error);
  if (!session->remote_desktop_session_proxy)
    return FALSE;

  g_clear_pointer (&session_path, g_free);

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
  remote_desktop_session_id =
    mdk_dbus_remote_desktop_session_get_session_id (
      session->remote_desktop_session_proxy);
  g_variant_builder_add (&builder, "{sv}",
                         "remote-desktop-session-id",
                         g_variant_new_string (remote_desktop_session_id));
  properties = g_variant_builder_end (&builder);

  if (!mdk_dbus_screen_cast_call_create_session_sync (
        session->screen_cast_proxy,
        properties,
        &session_path,
        cancellable,
        error))
    return FALSE;

  session->screen_cast_session_proxy =
    mdk_dbus_screen_cast_session_proxy_new_for_bus_sync (
      G_BUS_TYPE_SESSION,
      G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
      "org.gnome.Mutter.ScreenCast",
      session_path,
      cancellable,
      error);
  if (!session->screen_cast_session_proxy)
    return FALSE;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
  if (!mdk_dbus_remote_desktop_session_call_connect_to_eis_sync (
      session->remote_desktop_session_proxy,
      g_variant_builder_end (&builder),
      NULL,
      &fd_variant,
      &fd_list,
      NULL,
      error))
    return FALSE;

  ei_fd_idx = g_variant_get_handle (fd_variant);
  if (!G_IS_UNIX_FD_LIST (fd_list) ||
      ei_fd_idx < 0 || ei_fd_idx >= g_unix_fd_list_get_length (fd_list))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to acquire file descriptor for EI backend: Invalid "
                   "file descriptor list sent by display server");
      return FALSE;
    }

  ei_fd = g_unix_fd_list_get (fd_list, ei_fd_idx, error);
  if (ei_fd <= 0)
    return FALSE;

  session->ei = mdk_ei_new (session, g_steal_fd (&ei_fd), error);
  if (!session->ei)
    return FALSE;

  g_signal_connect (session->remote_desktop_session_proxy,
                    "closed",
                    G_CALLBACK (on_session_closed),
                    session);

  g_signal_connect_object (session->context,
                           "notify::use-host-keymap",
                           G_CALLBACK (on_use_host_keymap_changed),
                           session,
                           G_CONNECT_DEFAULT);

  return TRUE;
}

static void
on_selection_owner_changed (MdkDBusRemoteDesktopSession *session_proxy,
                            GVariant                    *options_variant,
                            MdkSession                  *session)
{
  gboolean session_is_owner = TRUE;

  g_variant_lookup (options_variant, "session-is-owner",
                    "b", &session_is_owner);

  if (session_is_owner)
    {
      g_debug ("Selection owner changed to devkit");
      return;
    }

  g_debug ("Selection owner changed compositor");
}

typedef struct _ClipboardWriteTransfer
{
  grefcount ref_count;
  MdkSession *session;
  GInputStream *input_stream;
  GOutputStream *output_stream;
  uint32_t serial;
} ClipboardWriteTransfer;

static ClipboardWriteTransfer *
clipboard_write_transfer_ref (ClipboardWriteTransfer *transfer)
{
  g_ref_count_inc (&transfer->ref_count);
  return transfer;
}

static void
clipboard_write_transfer_unref (ClipboardWriteTransfer *transfer)
{
  if (g_ref_count_dec (&transfer->ref_count))
    {
      g_clear_object (&transfer->input_stream);
      g_clear_object (&transfer->output_stream);
      g_free (transfer);
    }
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClipboardWriteTransfer,
                               clipboard_write_transfer_unref)

static void
write_splice_cb (GObject      *source_object,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  GOutputStream *output_stream = G_OUTPUT_STREAM (source_object);
  g_autoptr (ClipboardWriteTransfer) transfer = user_data;
  MdkSession *session = transfer->session;
  g_autoptr (GError) error = NULL;

  g_output_stream_splice_finish (output_stream, result, &error);
  if (error)
    {
      mdk_dbus_remote_desktop_session_call_selection_write_done (
        session->remote_desktop_session_proxy,
        transfer->serial, FALSE,
        NULL, NULL, NULL);
    }
  else
    {
      mdk_dbus_remote_desktop_session_call_selection_write_done (
        session->remote_desktop_session_proxy,
        transfer->serial, TRUE,
        NULL, NULL, NULL);
    }
}

static void
selection_write_cb (GObject      *source_object,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  g_autoptr (ClipboardWriteTransfer) transfer = user_data;
  MdkSession *session = transfer->session;
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) fd_variant = NULL;
  g_autoptr (GUnixFDList) fd_list = NULL;
  int fd_idx = -1;
  g_autofd int fd = -1;

  if (!mdk_dbus_remote_desktop_session_call_selection_write_finish (
        session->remote_desktop_session_proxy,
        &fd_variant,
        &fd_list,
        result,
        &error))
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

      g_warning ("Failed to begin write: %s", error->message);
      goto err;
    }

  g_variant_get (fd_variant, "h", &fd_idx);
  if (fd_idx < g_unix_fd_list_get_length (fd_list))
    {
      fd = g_unix_fd_list_get (fd_list, fd_idx, &error);
      if (fd < 0)
        {
          g_warning ("Failed to get fd: %s", error->message);
          goto err;
        }
    }
  else
    {
      g_warning ("Bad file descriptor index");
      goto err;
    }

  transfer->output_stream = g_unix_output_stream_new (g_steal_fd (&fd), TRUE);

  g_output_stream_splice_async (transfer->output_stream,
                                transfer->input_stream,
                                (G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE |
                                 G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET),
                                G_PRIORITY_DEFAULT,
                                session->cancellable,
                                write_splice_cb,
                                clipboard_write_transfer_ref (transfer));

err:
  mdk_dbus_remote_desktop_session_call_selection_write_done (
    session->remote_desktop_session_proxy,
    transfer->serial, FALSE,
    NULL, NULL, NULL);
}

static void
clipboard_read_cb (GObject      *source_object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  g_autoptr (ClipboardWriteTransfer) transfer = user_data;
  MdkSession *session = transfer->session;
  GdkClipboard *clipboard = GDK_CLIPBOARD (source_object);
  g_autoptr (GError) error = NULL;
  g_autoptr (GInputStream) input_stream = NULL;

  input_stream = gdk_clipboard_read_finish (clipboard, result, NULL, &error);
  if (!input_stream)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
          g_warning ("Failed to read GDK clipboard: %s", error->message);
          return;
        }

      mdk_dbus_remote_desktop_session_call_selection_write_done (
        session->remote_desktop_session_proxy,
        transfer->serial, FALSE,
        NULL, NULL, NULL);
      return;
    }

  transfer->input_stream = g_steal_pointer (&input_stream);

  mdk_dbus_remote_desktop_session_call_selection_write (
    session->remote_desktop_session_proxy,
    transfer->serial,
    NULL,
    session->cancellable,
    selection_write_cb,
    clipboard_write_transfer_ref (transfer));
}

static void
on_selection_transfer (MdkDBusRemoteDesktopSession *session_proxy,
                       char                        *mime_type_string,
                       unsigned int                 serial,
                       MdkSession                  *session)
{
  GdkDisplay *display = gdk_display_get_default ();
  GdkClipboard *clipboard = gdk_display_get_clipboard (display);
  const char *mime_types[] = {
    mime_type_string,
    NULL,
  };
  g_autoptr (ClipboardWriteTransfer) transfer = NULL;

  transfer = g_new0 (ClipboardWriteTransfer, 1);
  g_ref_count_init (&transfer->ref_count);
  transfer->serial = serial;
  transfer->session = session;

  gdk_clipboard_read_async (clipboard,
                            mime_types,
                            G_PRIORITY_DEFAULT,
                            session->cancellable,
                            clipboard_read_cb,
                            clipboard_write_transfer_ref (transfer));
}

static GVariant *
create_clipboard_options (void)
{
  GdkDisplay *display = gdk_display_get_default ();
  GdkClipboard *clipboard = gdk_display_get_clipboard (display);
  GdkContentFormats *formats;
  GVariantBuilder builder;
  const char * const *mime_types;
  size_t i;

  formats = gdk_clipboard_get_formats (clipboard);
  mime_types = gdk_content_formats_get_mime_types (formats, NULL);

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
  if (mime_types && *mime_types[0])
    {
      g_variant_builder_open (&builder, G_VARIANT_TYPE ("{sv}"));
      g_variant_builder_add (&builder, "s", "mime-types");
      g_variant_builder_open (&builder, G_VARIANT_TYPE ("v"));
      g_variant_builder_open (&builder, G_VARIANT_TYPE ("as"));
      for (i = 0; mime_types && mime_types[i]; i++)
        g_variant_builder_add (&builder, "s", mime_types[i]);
      g_variant_builder_close (&builder);
      g_variant_builder_close (&builder);
      g_variant_builder_close (&builder);
    }

  return g_variant_builder_end (&builder);
}

static void
on_clipboard_changed (GdkClipboard *clipboard,
                      MdkSession   *session)
{
  mdk_dbus_remote_desktop_session_call_set_selection (
    session->remote_desktop_session_proxy,
    create_clipboard_options (),
    NULL, NULL, NULL);
}

static gboolean
mdk_session_initable_init (GInitable      *initable,
                           GCancellable   *cancellable,
                           GError        **error)
{
  MdkSession *session = MDK_SESSION (initable);
  GdkDisplay *display = gdk_display_get_default ();
  GdkSeat *seat = gdk_display_get_default_seat (display);
  GdkDevice *keyboard = gdk_seat_get_keyboard (seat);
  GdkClipboard *clipboard = gdk_display_get_clipboard (display);

  g_debug ("Initializing session");

  session->remote_desktop_proxy =
    mdk_dbus_remote_desktop_proxy_new_for_bus_sync (
      G_BUS_TYPE_SESSION,
      G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
      "org.gnome.Mutter.RemoteDesktop",
      "/org/gnome/Mutter/RemoteDesktop",
      cancellable,
      error);
  if (!session->remote_desktop_proxy)
    return FALSE;

  session->screen_cast_proxy =
    mdk_dbus_screen_cast_proxy_new_for_bus_sync (
      G_BUS_TYPE_SESSION,
      G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
      "org.gnome.Mutter.ScreenCast",
      "/org/gnome/Mutter/ScreenCast",
      cancellable,
      error);
  if (!session->screen_cast_proxy)
    return FALSE;

  if (!init_session (session, cancellable, error))
    return FALSE;

  if (!mdk_dbus_remote_desktop_session_call_start_sync (
        session->remote_desktop_session_proxy,
        cancellable,
        error))
    return FALSE;

  g_signal_connect_object (keyboard, "notify::layout-names",
                           G_CALLBACK (maybe_sync_keymap),
                           session,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (keyboard, "notify::active-layout-index",
                           G_CALLBACK (maybe_sync_keymap),
                           session,
                           G_CONNECT_SWAPPED);

  session->cancellable = g_cancellable_new ();

  session->clipboard_changed_handler_id =
    g_signal_connect (clipboard, "changed",
                      G_CALLBACK (on_clipboard_changed),
                      session);

  if (!mdk_dbus_remote_desktop_session_call_enable_clipboard_sync (
        session->remote_desktop_session_proxy,
        create_clipboard_options (),
        cancellable,
        error))
    return FALSE;

  g_signal_connect (session->remote_desktop_session_proxy,
                    "selection-owner-changed",
                    G_CALLBACK (on_selection_owner_changed),
                    session);
  g_signal_connect (session->remote_desktop_session_proxy,
                    "selection-transfer",
                    G_CALLBACK (on_selection_transfer),
                    session);

  return TRUE;
}

static void
initable_iface_init (GInitableIface *iface)
{
  iface->init = mdk_session_initable_init;
}

static void
mdk_session_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  MdkSession *session = MDK_SESSION (object);

  switch (prop_id)
    {
    case PROP_CONTEXT:
      session->context = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
mdk_session_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  MdkSession *session = MDK_SESSION (object);

  switch (prop_id)
    {
    case PROP_CONTEXT:
      g_value_set_object (value, session->context);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
mdk_session_finalize (GObject *object)
{
  MdkSession *session = MDK_SESSION (object);
  GdkDisplay *display = gdk_display_get_default ();
  GdkClipboard *clipboard = gdk_display_get_clipboard (display);

  g_cancellable_cancel (session->cancellable);
  g_clear_object (&session->cancellable);

  g_clear_signal_handler (&session->clipboard_changed_handler_id, clipboard);

  g_clear_object (&session->ei);

  if (session->remote_desktop_session_proxy)
    {
      mdk_dbus_remote_desktop_session_call_stop_sync (session->remote_desktop_session_proxy,
                                                      NULL, NULL);
      g_clear_object (&session->remote_desktop_session_proxy);
    }
  g_clear_object (&session->screen_cast_session_proxy);

  g_clear_object (&session->screen_cast_proxy);
  g_clear_object (&session->remote_desktop_proxy);

  G_OBJECT_CLASS (mdk_session_parent_class)->finalize (object);
}

static void
mdk_session_class_init (MdkSessionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = mdk_session_set_property;
  object_class->get_property = mdk_session_get_property;
  object_class->finalize = mdk_session_finalize;

  obj_props[PROP_CONTEXT] =
    g_param_spec_object ("context", NULL, NULL,
                         MDK_TYPE_CONTEXT,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);

  signals[CLOSED] = g_signal_new ("closed",
                                  G_TYPE_FROM_CLASS (klass),
                                  G_SIGNAL_RUN_LAST,
                                  0,
                                  NULL, NULL, NULL,
                                  G_TYPE_NONE, 0);
}

static void
mdk_session_init (MdkSession *session)
{
}

char *
mdk_session_create_monitor (MdkSession      *session,
                            MdkMonitorInfo  *monitor_info,
                            GError         **error)
{
  MdkDBusScreenCastSession *proxy = session->screen_cast_session_proxy;
  GVariantBuilder properties_builder;
  g_autofree char *stream_path = NULL;

  g_debug ("Creating virtual monitor");

  g_variant_builder_init (&properties_builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&properties_builder, "{sv}",
                         "cursor-mode",
                         g_variant_new_uint32 (MDK_SCREEN_CAST_CURSOR_MODE_METADATA));
  g_variant_builder_add (&properties_builder, "{sv}",
                         "is-platform",
                         g_variant_new_boolean (TRUE));

  if (monitor_info->modes)
    {
      GVariantBuilder modes_builder;
      gboolean has_marked_as_preferred = FALSE;
      GList *l;

      g_variant_builder_init (&modes_builder, G_VARIANT_TYPE ("aa{sv}"));

      for (l = monitor_info->modes; l; l = l->next)
        {
          MdkMonitorMode *monitor_mode = l->data;
          double preferred_scale = monitor_mode->preferred_scale;

          g_variant_builder_open (&modes_builder, G_VARIANT_TYPE ("a{sv}"));
          g_variant_builder_add (&modes_builder, "{sv}",
                                 "size", g_variant_new ("(uu)",
                                                        monitor_mode->width,
                                                        monitor_mode->height));

          g_variant_builder_add (&modes_builder, "{sv}",
                                 "preferred-scale",
                                 g_variant_new_double (preferred_scale));

          if (!has_marked_as_preferred)
            {
              g_variant_builder_add (&modes_builder, "{sv}",
                                     "is-preferred",
                                     g_variant_new_boolean (TRUE));
              has_marked_as_preferred = TRUE;
            }

          g_variant_builder_close (&modes_builder);
        }

      g_variant_builder_add (&properties_builder, "{sv}",
                             "modes",
                             g_variant_builder_end (&modes_builder));
    }

  if (!mdk_dbus_screen_cast_session_call_record_virtual_sync (
        proxy,
        g_variant_builder_end (&properties_builder),
        &stream_path,
        NULL,
        error))
    return NULL;

  return g_steal_pointer (&stream_path);
}

MdkContext *
mdk_session_get_context (MdkSession *session)
{
  return session->context;
}

MdkSeat *
mdk_session_get_default_seat (MdkSession *session)
{
  return mdk_ei_get_default_seat (session->ei);
}
