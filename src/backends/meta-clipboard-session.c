/*
 * Copyright (C) 2025 Red Hat
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

#include "backends/meta-clipboard-session.h"

#include <gio/gunixfdlist.h>
#include <gio/gunixoutputstream.h>
#include <glib-unix.h>

#include "core/meta-selection-private.h"
#include "core/meta-selection-source-remote.h"
#include "core/util-private.h"
#include "meta/meta-backend.h"
#include "meta/meta-context.h"

#define TRANSFER_REQUEST_CLEANUP_TIMEOUT_MS (s2ms (15))

enum
{
  PROP_0,

  PROP_BACKEND,
  PROP_CONNECTION,
  PROP_OBJECT_PATH,
  PROP_PEER_NAME,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

enum
{
  OWNER_CHANGED,
  TRANSFER,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

typedef struct _SelectionReadData
{
  MetaClipboardSession *session;
  GOutputStream *stream;
  GCancellable *cancellable;
} SelectionReadData;

struct _MetaClipboardSession
{
  MetaDBusClipboardSkeleton parent;

  MetaBackend *backend;
  GDBusConnection *connection;
  char *object_path;
  char *peer_name;

  gboolean is_clipboard_enabled;
  gulong owner_changed_handler_id;
  SelectionReadData *read_data;
  unsigned int transfer_serial;
  MetaSelectionSourceRemote *current_source;
  GHashTable *transfer_requests;
  guint transfer_request_timeout_id;
};

static void meta_dbus_clipboard_init_iface (MetaDBusClipboardIface *iface);

static void initable_init_iface (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaClipboardSession,
                         meta_clipboard_session,
                         META_DBUS_TYPE_CLIPBOARD_SKELETON,
                         G_IMPLEMENT_INTERFACE (META_DBUS_TYPE_CLIPBOARD,
                                                meta_dbus_clipboard_init_iface)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                initable_init_iface))

static gboolean
check_permission (MetaClipboardSession  *session,
                  GDBusMethodInvocation *invocation)
{
  return g_strcmp0 (session->peer_name,
                    g_dbus_method_invocation_get_sender (invocation)) == 0;
}

static MetaDisplay *
display_from_session (MetaClipboardSession *session)
{
  MetaContext *context = meta_backend_get_context (session->backend);

  return meta_context_get_display (context);
}

static MetaSelectionSourceRemote *
create_selection_source (MetaClipboardSession  *session,
                         GVariant              *mime_types_variant,
                         GError               **error)
{
  GVariantIter iter;
  char *mime_type;
  GList *mime_types = NULL;

  g_variant_iter_init (&iter, mime_types_variant);
  if (g_variant_iter_n_children (&iter) == 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "No mime types in mime types list");
      return NULL;
    }

  while (g_variant_iter_next (&iter, "s", &mime_type))
    mime_types = g_list_prepend (mime_types, mime_type);

  mime_types = g_list_reverse (mime_types);

  return meta_selection_source_remote_new (session, mime_types);
}

static const char *
mime_types_to_string (char **formats,
                      char  *buf,
                      int    buf_len)
{
  g_autofree char *mime_types_string = NULL;
  int len;

  if (!formats)
    return "N/A";

  mime_types_string = g_strjoinv (",", formats);
  len = strlen (mime_types_string);
  strncpy (buf, mime_types_string, buf_len - 1);
  if (len >= buf_len - 1)
    buf[buf_len - 2] = '*';
  buf[buf_len - 1] = '\0';

  return buf;
}

static gboolean
is_own_source (MetaClipboardSession *session,
               MetaSelectionSource  *source)
{
  return source && source == META_SELECTION_SOURCE (session->current_source);
}

static GVariant *
generate_owner_changed_variant (char     **mime_types_array,
                                gboolean   is_own_source)
{
  GVariantBuilder builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
  if (mime_types_array)
    {
      g_variant_builder_add (&builder, "{sv}", "mime-types",
                             g_variant_new ("(^as)", mime_types_array));
      g_variant_builder_add (&builder, "{sv}", "session-is-owner",
                             g_variant_new_boolean (is_own_source));
    }

  return g_variant_builder_end (&builder);
}

static void
emit_owner_changed (MetaClipboardSession *session,
                    MetaSelectionSource  *owner)
{
  char log_buf[255];
  g_auto (GStrv) mime_types_array = NULL;
  g_autoptr (GVariant) options_variant = NULL;

  if (owner)
    {
      g_autoptr (GList) mime_types = NULL;
      GList *l;
      int i;

      mime_types = meta_selection_source_get_mimetypes (owner);
      mime_types_array = g_new0 (char *, g_list_length (mime_types) + 1);
      for (l = mime_types, i = 0; l; l = l->next, i++)
        mime_types_array[i] = l->data;
    }

  meta_topic (META_DEBUG_BACKEND,
              "Clipboard owner changed, owner: %p (%s, is own? %s), mime types: [%s], "
              "notifying %s",
              owner,
              owner ? g_type_name_from_instance ((GTypeInstance *) owner)
                    : "NULL",
              is_own_source (session, owner) ? "yes" : "no",
              mime_types_to_string (mime_types_array, log_buf,
                                    G_N_ELEMENTS (log_buf)),
              session->peer_name);

  options_variant =
    g_variant_ref_sink (generate_owner_changed_variant (mime_types_array,
                                                        is_own_source (session,
                                                                       owner)));

  if (session->object_path)
    {
      g_dbus_connection_emit_signal (session->connection,
                                     NULL,
                                     session->object_path,
                                     "org.gnome.Mutter.Clipboard",
                                     "SelectionOwnerChanged",
                                     g_variant_new ("(@a{sv})", options_variant),
                                     NULL);
    }

  g_signal_emit (session, signals[OWNER_CHANGED], 0, options_variant);
}

static void
on_selection_owner_changed (MetaSelection        *selection,
                            MetaSelectionType     selection_type,
                            MetaSelectionSource  *owner,
                            MetaClipboardSession *session)
{
  if (selection_type != META_SELECTION_CLIPBOARD)
    return;

  emit_owner_changed (session, owner);
}

gboolean
meta_clipboard_session_enable (MetaClipboardSession  *session,
                               GVariant              *arg_options,
                               GError               **error)
{
  GVariant *mime_types_variant;
  MetaDisplay *display = display_from_session (session);
  MetaSelection *selection = meta_display_get_selection (display);
  g_autoptr (MetaSelectionSourceRemote) source_remote = NULL;

  if (session->is_clipboard_enabled)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Already enabled");
      return FALSE;
    }

  meta_topic (META_DEBUG_BACKEND, "Enable clipboard for %s",
              session->peer_name);

  mime_types_variant = g_variant_lookup_value (arg_options,
                                               "mime-types",
                                               G_VARIANT_TYPE_STRING_ARRAY);
  if (mime_types_variant)
    {
      source_remote = create_selection_source (session,
                                               mime_types_variant,
                                               error);
      if (!source_remote)
        {
          g_prefix_error (error, "Invalid mime type list: ");
          return FALSE;
        }
    }

  if (source_remote)
    {
      meta_topic (META_DEBUG_BACKEND,
                  "Setting remote desktop clipboard source: %p from %s",
                  source_remote, session->peer_name);

      g_set_object (&session->current_source, source_remote);
      meta_selection_set_owner (selection,
                                META_SELECTION_CLIPBOARD,
                                META_SELECTION_SOURCE (source_remote));
    }
  else
    {
      MetaSelectionSource *owner;

      owner = meta_selection_get_current_owner (selection,
                                                META_SELECTION_CLIPBOARD);

      if (owner)
        emit_owner_changed (session, owner);
    }

  session->is_clipboard_enabled = TRUE;
  session->owner_changed_handler_id =
    g_signal_connect (selection, "owner-changed",
                      G_CALLBACK (on_selection_owner_changed),
                      session);

  return TRUE;
}

static gboolean
handle_enable (MetaDBusClipboard     *skeleton,
               GDBusMethodInvocation *invocation,
               GVariant              *arg_options)
{
  MetaClipboardSession *session = META_CLIPBOARD_SESSION (skeleton);
  g_autoptr (GError) error = NULL;

  if (!check_permission (session, invocation))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Permission denied");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  if (!meta_clipboard_session_enable (session, arg_options, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  meta_dbus_clipboard_complete_enable (skeleton, invocation);

  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
cancel_transfer_request (gpointer key,
                         gpointer value,
                         gpointer user_data)
{
  GTask *task = G_TASK (value);
  MetaClipboardSession *session = user_data;

  meta_selection_source_remote_cancel_transfer (session->current_source,
                                                task);

  return TRUE;
}

static void
meta_clipboard_session_cancel_transfer_requests (MetaClipboardSession *session)
{
  g_return_if_fail (session->current_source);

  g_hash_table_foreach_remove (session->transfer_requests,
                               cancel_transfer_request,
                               session);
}

static void
transfer_request_cleanup_timeout (gpointer user_data)
{
  MetaClipboardSession *session = user_data;

  meta_topic (META_DEBUG_BACKEND,
              "Cancel unanswered SelectionTransfer requests for %s, "
              "waited for %.02f seconds already",
              session->peer_name,
              TRANSFER_REQUEST_CLEANUP_TIMEOUT_MS / 1000.0);

  meta_clipboard_session_cancel_transfer_requests (session);

  session->transfer_request_timeout_id = 0;
}

static void
reset_current_selection_source (MetaClipboardSession *session)
{
  MetaDisplay *display = display_from_session (session);
  MetaSelection *selection = meta_display_get_selection (display);

  if (!session->current_source)
    return;

  meta_selection_unset_owner (selection,
                              META_SELECTION_CLIPBOARD,
                              META_SELECTION_SOURCE (session->current_source));
  meta_clipboard_session_cancel_transfer_requests (session);
  g_clear_handle_id (&session->transfer_request_timeout_id, g_source_remove);
  g_clear_object (&session->current_source);
}

static void
cancel_selection_read (MetaClipboardSession *session)
{
  if (!session->read_data)
    return;

  g_cancellable_cancel (session->read_data->cancellable);
  session->read_data->session = NULL;
  session->read_data = NULL;
}

gboolean
meta_clipboard_session_disable (MetaClipboardSession  *session,
                                GError               **error)
{
  MetaDisplay *display = display_from_session (session);
  MetaSelection *selection = meta_display_get_selection (display);

  meta_topic (META_DEBUG_BACKEND, "Disable clipboard for %s",
              session->peer_name);

  if (!session->is_clipboard_enabled)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Was not enabled");
      return FALSE;
    }

  g_clear_signal_handler (&session->owner_changed_handler_id, selection);
  reset_current_selection_source (session);
  cancel_selection_read (session);

  session->is_clipboard_enabled = FALSE;

  return TRUE;
}

static gboolean
handle_disable (MetaDBusClipboard     *skeleton,
                GDBusMethodInvocation *invocation)
{
  MetaClipboardSession *session = META_CLIPBOARD_SESSION (skeleton);
  g_autoptr (GError) error = NULL;

  if (!check_permission (session, invocation))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Permission denied");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  if (!meta_clipboard_session_disable (session, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  meta_dbus_clipboard_complete_disable (skeleton, invocation);

  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

gboolean
meta_clipboard_session_set_selection (MetaClipboardSession  *session,
                                      GVariant              *options,
                                      GError               **error)
{
  g_autoptr (GVariant) mime_types_variant = NULL;

  if (!session->is_clipboard_enabled)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Clipboard not enabled");
      return FALSE;
    }

  if (session->current_source)
    {
      meta_clipboard_session_cancel_transfer_requests (session);
      g_clear_handle_id (&session->transfer_request_timeout_id,
                         g_source_remove);
    }

  mime_types_variant = g_variant_lookup_value (options,
                                               "mime-types",
                                               G_VARIANT_TYPE_STRING_ARRAY);
  if (mime_types_variant)
    {
      g_autoptr (MetaSelectionSourceRemote) source_remote = NULL;
      MetaDisplay *display = display_from_session (session);

      source_remote = create_selection_source (session,
                                               mime_types_variant,
                                               error);
      if (!source_remote)
        {
          g_prefix_error (error, "Invalid format list: ");
          return FALSE;
        }

      meta_topic (META_DEBUG_BACKEND, "Set selection for %s to %p",
                  session->peer_name,
                  source_remote);

      g_set_object (&session->current_source, source_remote);
      meta_selection_set_owner (meta_display_get_selection (display),
                                META_SELECTION_CLIPBOARD,
                                META_SELECTION_SOURCE (source_remote));
    }
  else
    {
      meta_topic (META_DEBUG_BACKEND, "Unset selection for %s",
                  session->peer_name);

      reset_current_selection_source (session);
    }

  return TRUE;
}

static gboolean
handle_set_selection (MetaDBusClipboard     *skeleton,
                      GDBusMethodInvocation *invocation,
                      GVariant              *arg_options)
{
  MetaClipboardSession *session = META_CLIPBOARD_SESSION (skeleton);
  g_autoptr (GError) error = NULL;

  if (!check_permission (session, invocation))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Permission denied");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  if (!meta_clipboard_session_set_selection (session, arg_options, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  meta_dbus_clipboard_complete_set_selection (skeleton, invocation);
  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static void
reset_transfer_cleanup_timeout (MetaClipboardSession *session)
{
  g_clear_handle_id (&session->transfer_request_timeout_id, g_source_remove);
  session->transfer_request_timeout_id =
    g_timeout_add_once (TRANSFER_REQUEST_CLEANUP_TIMEOUT_MS,
                        transfer_request_cleanup_timeout,
                        session);
}

void
meta_clipboard_session_request_transfer (MetaClipboardSession *session,
                                         const char           *mime_type,
                                         GTask                *task)
{
  session->transfer_serial++;

  meta_topic (META_DEBUG_BACKEND, "Emit SelectionTransfer ('%s', %u) for %s",
              mime_type,
              session->transfer_serial,
              session->peer_name);

  g_hash_table_insert (session->transfer_requests,
                       GUINT_TO_POINTER (session->transfer_serial),
                       task);
  reset_transfer_cleanup_timeout (session);

  if (session->object_path)
    {
      g_dbus_connection_emit_signal (session->connection,
                                     NULL,
                                     session->object_path,
                                     "org.gnome.Mutter.Clipboard",
                                     "SelectionTransfer",
                                     g_variant_new ("(su)",
                                                    mime_type,
                                                    session->transfer_serial),
                                     NULL);
    }

  g_signal_emit (session, signals[TRANSFER], 0,
                 mime_type, session->transfer_serial);
}

gboolean
meta_clipboard_session_selection_write (MetaClipboardSession  *session,
                                        unsigned int           serial,
                                        GUnixFDList          **out_fd_list,
                                        GVariant             **out_fd_variant,
                                        GError               **error)
{
  int pipe_fds[2];
  g_autofd int pipe_in = -1;
  g_autofd int pipe_out = -1;
  g_autoptr (GUnixFDList) fd_list = NULL;
  int fd_idx;
  g_autoptr (GVariant) fd_variant = NULL;
  GTask *task;

  meta_topic (META_DEBUG_BACKEND, "Write selection for %s", session->peer_name);

  if (!session->is_clipboard_enabled)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Clipboard not enabled");
      return FALSE;
    }

  if (!session->current_source)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No current selection owned");
      return FALSE;
    }

  if (!g_hash_table_steal_extended (session->transfer_requests,
                                    GUINT_TO_POINTER (serial),
                                    NULL,
                                    (gpointer *) &task))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Transfer serial %u doesn't match "
                   "any transfer request",
                   serial);
      return FALSE;
    }

  if (!g_unix_open_pipe (pipe_fds, FD_CLOEXEC, error))
    {
      g_prefix_error (error, "Failed open pipe: ");
      return FALSE;
    }
  pipe_in = pipe_fds[0];
  pipe_out = pipe_fds[1];

  if (!g_unix_set_fd_nonblocking (pipe_in, TRUE, error))
    return FALSE;

  fd_list = g_unix_fd_list_new ();

  fd_idx = g_unix_fd_list_append (fd_list, pipe_out, error);
  if (fd_idx < 0)
    return FALSE;
  fd_variant = g_variant_ref_sink (g_variant_new_handle (fd_idx));

  meta_selection_source_remote_complete_transfer (session->current_source,
                                                  g_steal_fd (&pipe_in),
                                                  task);

  *out_fd_list = g_steal_pointer (&fd_list);
  *out_fd_variant = g_steal_pointer (&fd_variant);

  return TRUE;
}

static gboolean
handle_selection_write (MetaDBusClipboard     *skeleton,
                        GDBusMethodInvocation *invocation,
                        GUnixFDList           *fd_list_in,
                        unsigned int           serial)
{
  MetaClipboardSession *session = META_CLIPBOARD_SESSION (skeleton);
  g_autoptr (GError) error = NULL;
  g_autoptr (GUnixFDList) fd_list = NULL;
  g_autoptr (GVariant) fd_variant = NULL;

  if (!check_permission (session, invocation))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Permission denied");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  if (!meta_clipboard_session_selection_write (session,
                                               serial,
                                               &fd_list,
                                               &fd_variant,
                                               &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  meta_dbus_clipboard_complete_selection_write (skeleton,
                                                invocation,
                                                fd_list,
                                                fd_variant);
  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
handle_selection_write_done (MetaDBusClipboard     *skeleton,
                             GDBusMethodInvocation *invocation,
                             unsigned int           arg_serial,
                             gboolean               arg_success)
{
  MetaClipboardSession *session = META_CLIPBOARD_SESSION (skeleton);

  meta_topic (META_DEBUG_BACKEND, "Write selection done for %s",
              session->peer_name);

  if (!check_permission (session, invocation))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Permission denied");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  if (!session->is_clipboard_enabled)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Clipboard not enabled");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  meta_dbus_clipboard_complete_selection_write_done (skeleton, invocation);

  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
is_pipe_broken (int fd)
{
  GPollFD poll_fd = {0};
  int poll_ret;
  int errsv;

  poll_fd.fd = fd;
  poll_fd.events = G_IO_OUT;

  do
    {
      poll_ret = g_poll (&poll_fd, 1, 0);
      errsv = errno;
    }
  while (poll_ret == -1 && errsv == EINTR);

  if (poll_ret < 0)
    return FALSE;

  return !!(poll_fd.revents & G_IO_ERR);
}

static gboolean
has_pending_read_operation (SelectionReadData *read_data)
{
  int fd;

  if (!read_data)
    return FALSE;

  fd = g_unix_output_stream_get_fd (G_UNIX_OUTPUT_STREAM (read_data->stream));
  if (is_pipe_broken (fd))
    {
      cancel_selection_read (read_data->session);
      return FALSE;
    }

  return TRUE;
}

static void
transfer_cb (MetaSelection     *selection,
             GAsyncResult      *res,
             SelectionReadData *read_data)
{
  g_autoptr (GError) error = NULL;

  if (!meta_selection_transfer_finish (selection, res, &error))
    {
      g_warning ("Could not fetch selection data "
                 "for remote desktop session: %s",
                 error->message);
    }

  if (read_data->session)
    {
      meta_topic (META_DEBUG_BACKEND, "Finished selection transfer for %s",
                  read_data->session->peer_name);
    }

  g_output_stream_close (read_data->stream, NULL, NULL);
  g_clear_object (&read_data->stream);
  g_clear_object (&read_data->cancellable);

  if (read_data->session)
    read_data->session->read_data = NULL;

  g_free (read_data);
}

gboolean
meta_clipboard_session_selection_read (MetaClipboardSession  *session,
                                       const char            *mime_type,
                                       GUnixFDList          **out_fd_list,
                                       GVariant             **out_fd_variant,
                                       GError               **error)
{
  MetaDisplay *display = display_from_session (session);
  MetaSelection *selection = meta_display_get_selection (display);
  MetaSelectionSource *source;
  int pipe_fds[2];
  g_autofd int pipe_in = -1;
  g_autofd int pipe_out = -1;
  g_autoptr (GUnixFDList) fd_list = NULL;
  int fd_idx;
  g_autoptr (GVariant) fd_variant = NULL;
  SelectionReadData *read_data;

  meta_topic (META_DEBUG_BACKEND, "Read selection for %s", session->peer_name);

  if (!session->is_clipboard_enabled)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Clipboard not enabled");
      return FALSE;
    }

  source = meta_selection_get_current_owner (selection,
                                             META_SELECTION_CLIPBOARD);
  if (!source)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No selection owner available");
      return FALSE;
    }

  if (is_own_source (session, source))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Tried to read own selection");
      return FALSE;
    }

  if (has_pending_read_operation (session->read_data))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Tried to read in parallel");
      return FALSE;
    }

  if (!g_unix_open_pipe (pipe_fds, FD_CLOEXEC, error))
    return FALSE;
  pipe_in = pipe_fds[0];
  pipe_out = pipe_fds[1];

  if (!g_unix_set_fd_nonblocking (pipe_in, TRUE, error))
    return FALSE;

  fd_list = g_unix_fd_list_new ();

  fd_idx = g_unix_fd_list_append (fd_list, pipe_in, error);
  if (fd_idx < 0)
    return FALSE;
  fd_variant = g_variant_ref_sink (g_variant_new_handle (fd_idx));

  session->read_data = read_data = g_new0 (SelectionReadData, 1);
  read_data->session = session;
  read_data->stream = g_unix_output_stream_new (g_steal_fd (&pipe_out), TRUE);
  read_data->cancellable = g_cancellable_new ();
  meta_selection_transfer_async (selection,
                                 META_SELECTION_CLIPBOARD,
                                 mime_type,
                                 -1,
                                 read_data->stream,
                                 read_data->cancellable,
                                 (GAsyncReadyCallback) transfer_cb,
                                 read_data);

  *out_fd_list = g_steal_pointer (&fd_list);
  *out_fd_variant = g_steal_pointer (&fd_variant);
  return TRUE;
}

static gboolean
handle_selection_read (MetaDBusClipboard     *skeleton,
                       GDBusMethodInvocation *invocation,
                       GUnixFDList           *fd_list_in,
                       const char            *mime_type)
{
  MetaClipboardSession *session = META_CLIPBOARD_SESSION (skeleton);
  g_autoptr (GError) error = NULL;
  g_autoptr (GUnixFDList) fd_list = NULL;
  g_autoptr (GVariant) fd_variant = NULL;

  if (!check_permission (session, invocation))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Permission denied");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  if (!meta_clipboard_session_selection_read (session, mime_type,
                                              &fd_list,
                                              &fd_variant,
                                              &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  meta_dbus_clipboard_complete_selection_read (skeleton,
                                               invocation,
                                               fd_list,
                                               fd_variant);

  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static void
meta_dbus_clipboard_init_iface (MetaDBusClipboardIface *iface)
{
  iface->handle_enable = handle_enable;
  iface->handle_disable = handle_disable;
  iface->handle_set_selection = handle_set_selection;
  iface->handle_selection_write = handle_selection_write;
  iface->handle_selection_write_done = handle_selection_write_done;
  iface->handle_selection_read = handle_selection_read;
}

static gboolean
meta_clipboard_session_initable_init (GInitable     *initable,
                                      GCancellable  *cancellable,
                                      GError       **error)
{
  MetaClipboardSession *session = META_CLIPBOARD_SESSION (initable);
  GDBusInterfaceSkeleton *interface_skeleton = G_DBUS_INTERFACE_SKELETON (session);

  if (session->object_path)
    {
      if (!g_dbus_interface_skeleton_export (interface_skeleton,
                                             session->connection,
                                             session->object_path,
                                             error))
        return FALSE;
    }

  return TRUE;
}

static void
initable_init_iface (GInitableIface *iface)
{
  iface->init = meta_clipboard_session_initable_init;
}

static void
meta_clipboard_session_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  MetaClipboardSession *session = META_CLIPBOARD_SESSION (object);

  switch (prop_id)
    {
    case PROP_BACKEND:
      session->backend = g_value_get_object (value);
      break;
    case PROP_CONNECTION:
      session->connection = g_value_get_object (value);
      break;
    case PROP_OBJECT_PATH:
      session->object_path = g_value_dup_string (value);
      break;
    case PROP_PEER_NAME:
      session->peer_name = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_clipboard_session_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  MetaClipboardSession *session = META_CLIPBOARD_SESSION (object);

  switch (prop_id)
    {
    case PROP_BACKEND:
      g_value_set_object (value, session->backend);
      break;
    case PROP_CONNECTION:
      g_value_set_object (value, session->connection);
      break;
    case PROP_OBJECT_PATH:
      g_value_set_string (value, session->object_path);
      break;
    case PROP_PEER_NAME:
      g_value_set_string (value, session->peer_name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_clipboard_session_finalize (GObject *object)
{
  MetaClipboardSession *session = META_CLIPBOARD_SESSION (object);
  MetaDisplay *display = display_from_session (session);
  MetaSelection *selection = meta_display_get_selection (display);

  g_clear_signal_handler (&session->owner_changed_handler_id, selection);
  reset_current_selection_source (session);
  cancel_selection_read (session);
  g_hash_table_unref (session->transfer_requests);

  g_free (session->object_path);
  g_free (session->peer_name);

  G_OBJECT_CLASS (meta_clipboard_session_parent_class)->finalize (object);
}

static void
meta_clipboard_session_class_init (MetaClipboardSessionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = meta_clipboard_session_set_property;
  object_class->get_property = meta_clipboard_session_get_property;
  object_class->finalize = meta_clipboard_session_finalize;

  obj_props[PROP_BACKEND] =
    g_param_spec_object ("backend", NULL, NULL,
                         META_TYPE_BACKEND,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_CONNECTION] =
    g_param_spec_object ("connection", NULL, NULL,
                         G_TYPE_DBUS_CONNECTION,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_OBJECT_PATH] =
    g_param_spec_string ("object-path", NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_PEER_NAME] =
    g_param_spec_string ("peer-name", NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);

  signals[OWNER_CHANGED] =
    g_signal_new ("owner-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  G_TYPE_VARIANT);
  signals[TRANSFER] =
    g_signal_new ("transfer",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 2,
                  G_TYPE_STRING,
                  G_TYPE_UINT);
}

static void
meta_clipboard_session_init (MetaClipboardSession *session)
{
  session->transfer_requests = g_hash_table_new (NULL, NULL);
}

MetaClipboardSession *
meta_clipboard_session_new (MetaBackend      *backend,
                            const char       *peer_name,
                            GDBusConnection  *connection,
                            const char       *object_path,
                            GError          **error)
{
  return g_initable_new (META_TYPE_CLIPBOARD_SESSION, NULL, error,
                         "backend", backend,
                         "peer-name", peer_name,
                         "connection", connection,
                         "object-path", object_path,
                         NULL);
}
