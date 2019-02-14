/*
 * Copyright (C) 2018 Red Hat
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include <glib-unix.h>
#include <gio/gunixinputstream.h>

#include "wayland/meta-selection-source-wayland-private.h"

struct _MetaSelectionSourceWayland
{
  MetaSelectionSource parent_instance;
  GList *mimetypes;
  MetaWaylandSendFunc send_func;
  MetaWaylandCancelFunc cancel_func;
  struct wl_resource *resource;
};

G_DEFINE_TYPE (MetaSelectionSourceWayland, meta_selection_source_wayland,
               META_TYPE_SELECTION_SOURCE)

static void
meta_selection_source_wayland_read_async (MetaSelectionSource *source,
                                          const gchar         *mimetype,
                                          GCancellable        *cancellable,
                                          GAsyncReadyCallback  callback,
                                          gpointer             user_data)
{
  MetaSelectionSourceWayland *source_wayland = META_SELECTION_SOURCE_WAYLAND (source);
  GInputStream *stream;
  GTask *task;
  int pipe_fds[2];

  if (!g_unix_open_pipe (pipe_fds, FD_CLOEXEC, NULL))
    {
      g_task_report_new_error (source, callback, user_data,
                               meta_selection_source_wayland_read_async,
                               G_IO_ERROR, G_IO_ERROR_FAILED,
                               "Could not open pipe to read wayland selection");
      return;
    }

  if (!g_unix_set_fd_nonblocking (pipe_fds[0], TRUE, NULL) ||
      !g_unix_set_fd_nonblocking (pipe_fds[1], TRUE, NULL))
    {
      g_task_report_new_error (source, callback, user_data,
                               meta_selection_source_wayland_read_async,
                               G_IO_ERROR, G_IO_ERROR_FAILED,
                               "Could not make pipe nonblocking");
      close (pipe_fds[0]);
      close (pipe_fds[1]);
      return;
    }

  task = g_task_new (source, cancellable, callback, user_data);
  g_task_set_source_tag (task, meta_selection_source_wayland_read_async);

  stream = g_unix_input_stream_new (pipe_fds[0], TRUE);
  source_wayland->send_func (source_wayland->resource, mimetype, pipe_fds[1]);
  close (pipe_fds[1]);

  g_task_return_pointer (task, stream, g_object_unref);
  g_object_unref (task);
}

static GInputStream *
meta_selection_source_wayland_read_finish (MetaSelectionSource  *source,
                                           GAsyncResult         *result,
                                           GError              **error)
{
  g_return_val_if_fail (g_task_is_valid (result, source), FALSE);
  g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) ==
                        meta_selection_source_wayland_read_async, FALSE);

  return g_task_propagate_pointer (G_TASK (result), error);
}

static GList *
meta_selection_source_wayland_get_mimetypes (MetaSelectionSource  *source)
{
  MetaSelectionSourceWayland *source_wayland = META_SELECTION_SOURCE_WAYLAND (source);

  return g_list_copy_deep (source_wayland->mimetypes,
                           (GCopyFunc) g_strdup, NULL);
}

static void
meta_selection_source_wayland_inactive (MetaSelectionSource *source)
{
  MetaSelectionSourceWayland *source_wayland =
    META_SELECTION_SOURCE_WAYLAND (source);

  source_wayland->cancel_func (source_wayland->resource);
  META_SELECTION_SOURCE_CLASS (meta_selection_source_wayland_parent_class)->inactive (source);
}

static void
meta_selection_source_wayland_class_init (MetaSelectionSourceWaylandClass *klass)
{
  MetaSelectionSourceClass *source_class = META_SELECTION_SOURCE_CLASS (klass);

  source_class->inactive = meta_selection_source_wayland_inactive;

  source_class->read_async = meta_selection_source_wayland_read_async;
  source_class->read_finish = meta_selection_source_wayland_read_finish;
  source_class->get_mimetypes = meta_selection_source_wayland_get_mimetypes;
}

static void
meta_selection_source_wayland_init (MetaSelectionSourceWayland *source)
{
}

MetaSelectionSource *
meta_selection_source_wayland_new (struct wl_resource    *resource,
                                   GList                 *mime_types,
                                   MetaWaylandSendFunc    send_func,
                                   MetaWaylandCancelFunc  cancel_func)
{
  MetaSelectionSourceWayland *source;

  source = g_object_new (META_TYPE_SELECTION_SOURCE_WAYLAND, NULL);
  source->mimetypes = g_list_copy_deep (mime_types, (GCopyFunc) g_strdup, NULL);
  source->send_func = send_func;
  source->cancel_func = cancel_func;
  source->resource = resource;

  return META_SELECTION_SOURCE (source);
}
