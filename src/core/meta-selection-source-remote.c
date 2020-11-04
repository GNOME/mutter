/*
 * Copyright (C) 2020 Red Hat
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

#include "core/meta-selection-source-remote.h"

#include <gio/gunixinputstream.h>

#include "backends/meta-remote-desktop-session.h"

struct _MetaSelectionSourceRemote
{
  MetaSelectionSource parent;

  MetaRemoteDesktopSession *session;
  GList *mime_types;
};

G_DEFINE_TYPE (MetaSelectionSourceRemote,
               meta_selection_source_remote,
               META_TYPE_SELECTION_SOURCE)

MetaSelectionSourceRemote *
meta_selection_source_remote_new (MetaRemoteDesktopSession *session,
                                  GList                    *mime_types)
{
  MetaSelectionSourceRemote *source_remote;

  source_remote = g_object_new (META_TYPE_SELECTION_SOURCE_REMOTE, NULL);
  source_remote->session = session;
  source_remote->mime_types = mime_types;

  return source_remote;
}

static void
meta_selection_source_remote_read_async (MetaSelectionSource *source,
                                         const char          *mimetype,
                                         GCancellable        *cancellable,
                                         GAsyncReadyCallback  callback,
                                         gpointer             user_data)
{
  MetaSelectionSourceRemote *source_remote =
    META_SELECTION_SOURCE_REMOTE (source);
  GTask *task;

  task = g_task_new (source, cancellable, callback, user_data);
  g_task_set_source_tag (task, meta_selection_source_remote_read_async);

  meta_remote_desktop_session_request_transfer (source_remote->session,
                                                mimetype,
                                                task);
}

static GInputStream *
meta_selection_source_remote_read_finish (MetaSelectionSource  *source,
                                          GAsyncResult         *result,
                                          GError              **error)
{
  g_return_val_if_fail (g_task_is_valid (result, source), FALSE);
  g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) ==
                        meta_selection_source_remote_read_async, FALSE);

  return g_task_propagate_pointer (G_TASK (result), error);
}

void
meta_selection_source_remote_complete_transfer (MetaSelectionSourceRemote *source_remote,
                                                int                        fd,
                                                GTask                     *task)
{
  GInputStream *stream;

  stream = g_unix_input_stream_new (fd, TRUE);
  g_task_return_pointer (task, stream, g_object_unref);
  g_object_unref (task);
}

void
meta_selection_source_remote_cancel_transfer (MetaSelectionSourceRemote *source_remote,
                                              GTask                     *task)
{
  g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                           "Remote selection transfer was cancelled");
  g_object_unref (task);
}

static GList *
meta_selection_source_remote_get_mimetypes (MetaSelectionSource *source)
{
  MetaSelectionSourceRemote *source_remote =
    META_SELECTION_SOURCE_REMOTE (source);

  return g_list_copy_deep (source_remote->mime_types,
                           (GCopyFunc) g_strdup,
                           NULL);
}

static void
meta_selection_source_remote_finalize (GObject *object)
{
  MetaSelectionSourceRemote *source_remote =
    META_SELECTION_SOURCE_REMOTE (object);

  g_list_free_full (source_remote->mime_types, g_free);

  G_OBJECT_CLASS (meta_selection_source_remote_parent_class)->finalize (object);
}

static void
meta_selection_source_remote_init (MetaSelectionSourceRemote *source_remote)
{
}

static void
meta_selection_source_remote_class_init (MetaSelectionSourceRemoteClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaSelectionSourceClass *source_class = META_SELECTION_SOURCE_CLASS (klass);

  object_class->finalize = meta_selection_source_remote_finalize;

  source_class->read_async = meta_selection_source_remote_read_async;
  source_class->read_finish = meta_selection_source_remote_read_finish;
  source_class->get_mimetypes = meta_selection_source_remote_get_mimetypes;
}
