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

#include <gdk/gdkx.h>

#include "x11/meta-x11-selection-input-stream-private.h"
#include "x11/meta-x11-selection-source-private.h"

#define MAX_MIMETYPE_SIZE 4096

struct _MetaX11SelectionSource
{
  MetaSelectionSource parent_instance;
  MetaX11Display *display;
  GList *mimetypes;
  Window owner;
  uint32_t timestamp;
};

G_DEFINE_TYPE (MetaX11SelectionSource, meta_x11_selection_source,
               META_TYPE_SELECTION_SOURCE)

static Atom
selection_to_atom (MetaSelectionType  type,
                   Display           *xdisplay)
{
  Atom atom;

  switch (type)
    {
    case META_SELECTION_PRIMARY:
      atom = XInternAtom (xdisplay, "PRIMARY", False);
      break;
    case META_SELECTION_CLIPBOARD:
      atom = XInternAtom (xdisplay, "CLIPBOARD", False);
      break;
    case META_SELECTION_DND:
      atom = XInternAtom (xdisplay, "XdndSelection", False);
      break;
    default:
      g_warn_if_reached ();
      atom = None;
      break;
    }

  return atom;
}

static void
stream_new_cb (GObject      *source,
               GAsyncResult *res,
               GTask        *task)
{
  GInputStream *stream;
  GError *error = NULL;

  stream = meta_x11_selection_input_stream_new_finish (res, NULL, NULL, &error);

  if (stream)
    g_task_return_pointer (task, stream, g_object_unref);
  else
    g_task_return_error (task, error);

  g_object_unref (task);
}

static void
meta_x11_selection_source_read_async (MetaSelectionSource *source,
                                      const gchar         *mimetype,
                                      GCancellable        *cancellable,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data)
{
  MetaX11SelectionSource *source_x11 = META_X11_SELECTION_SOURCE (source);
  MetaSelectionType selection;
  Atom selection_atom;
  GTask *task;

  task = g_task_new (source, cancellable, callback, user_data);
  g_task_set_source_tag (task, meta_x11_selection_source_read_async);

  meta_selection_source_get_selection (source, &selection);
  selection_atom = selection_to_atom (selection, source_x11->display->xdisplay);

  meta_x11_selection_input_stream_new_async (source_x11->display,
                                             source_x11->display->selection.window,
                                             gdk_x11_get_xatom_name (selection_atom),
                                             mimetype,
                                             source_x11->timestamp,
                                             G_PRIORITY_DEFAULT,
                                             cancellable,
                                             (GAsyncReadyCallback) stream_new_cb,
                                             task);
}

static GInputStream *
meta_x11_selection_source_read_finish (MetaSelectionSource  *source,
                                       GAsyncResult         *result,
                                       GError              **error)
{
  g_return_val_if_fail (g_task_is_valid (result, source), NULL);
  g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) ==
                        meta_x11_selection_source_read_async, NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

static GList *
meta_x11_selection_source_get_mimetypes (MetaSelectionSource  *source)
{
  MetaX11SelectionSource *source_x11 = META_X11_SELECTION_SOURCE (source);

  return g_list_copy_deep (source_x11->mimetypes, (GCopyFunc) g_strdup, NULL);
}


static void
meta_x11_selection_source_class_init (MetaX11SelectionSourceClass *klass)
{
  MetaSelectionSourceClass *source_class = META_SELECTION_SOURCE_CLASS (klass);

  source_class->read_async = meta_x11_selection_source_read_async;
  source_class->read_finish = meta_x11_selection_source_read_finish;
  source_class->get_mimetypes = meta_x11_selection_source_get_mimetypes;
}

static void
meta_x11_selection_source_init (MetaX11SelectionSource *source)
{
}

static GList *
atoms_to_mimetypes (MetaX11Display *display,
                    GBytes         *bytes)
{
  GList *mimetypes = NULL;
  const Atom *atoms;
  gsize size;
  guint i, n_atoms;

  atoms = g_bytes_get_data (bytes, &size);
  n_atoms = size / sizeof (Atom);

  for (i = 0; i < n_atoms; i++)
    {
      const gchar *mimetype;

      mimetype = gdk_x11_get_xatom_name (atoms[i]);
      mimetypes = g_list_prepend (mimetypes, g_strdup (mimetype));
    }

  return mimetypes;
}

static void
read_mimetypes_cb (GInputStream *stream,
                   GAsyncResult *res,
                   GTask        *task)
{
  MetaX11SelectionSource *source_x11 = g_task_get_task_data (task);
  GError *error = NULL;
  GBytes *bytes;

  bytes = g_input_stream_read_bytes_finish (stream, res, &error);
  if (error)
    {
      g_task_return_error (task, error);
      g_object_unref (task);
      return;
    }

  source_x11->mimetypes = atoms_to_mimetypes (source_x11->display, bytes);
  g_bytes_unref (bytes);

  g_task_return_pointer (task,
                         g_object_ref (g_task_get_task_data (task)),
                         g_object_unref);
  g_object_unref (task);
}

static void
get_mimetypes_cb (GObject      *source,
                  GAsyncResult *res,
                  GTask        *task)
{
  GInputStream *stream;
  GError *error = NULL;

  stream = meta_x11_selection_input_stream_new_finish (res, NULL, NULL, &error);
  if (error)
    {
      g_task_return_error (task, error);
      g_object_unref (task);
      return;
    }

  g_input_stream_read_bytes_async (stream,
                                   MAX_MIMETYPE_SIZE,
                                   G_PRIORITY_DEFAULT,
                                   g_task_get_cancellable (task),
                                   (GAsyncReadyCallback) read_mimetypes_cb,
                                   task);
}

void
meta_x11_selection_source_new_async (MetaX11Display      *display,
                                     Window               owner,
                                     uint32_t             timestamp,
                                     Atom                 xselection,
                                     GCancellable        *cancellable,
                                     GAsyncReadyCallback  callback,
                                     gpointer             user_data)
{
  MetaX11SelectionSource *source;
  GTask *task;

  source = g_object_new (META_TYPE_X11_SELECTION_SOURCE, NULL);
  source->display = display;
  source->owner = owner;
  source->timestamp = timestamp;

  task = g_task_new (NULL, cancellable, callback, user_data);
  g_task_set_source_tag (task, meta_x11_selection_source_new_async);
  g_task_set_task_data (task, source, g_object_unref);

  meta_x11_selection_input_stream_new_async (display,
                                             display->selection.window,
                                             gdk_x11_get_xatom_name (xselection),
                                             "TARGETS",
                                             timestamp,
                                             G_PRIORITY_DEFAULT,
                                             cancellable,
                                             (GAsyncReadyCallback) get_mimetypes_cb,
                                             task);
}

MetaSelectionSource *
meta_x11_selection_source_new_finish (GAsyncResult  *result,
                                      GError       **error)
{
  GTask *task = G_TASK (result);

  g_return_val_if_fail (g_task_is_valid (task, NULL), NULL);
  g_return_val_if_fail (g_task_get_source_tag (task) ==
                        meta_x11_selection_source_new_async, NULL);

  return g_task_propagate_pointer (task, error);
}
