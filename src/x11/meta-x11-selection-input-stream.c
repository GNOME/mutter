/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Benjamin Otte <otte@gnome.org>
 *         Christian Kellner <gicmo@gnome.org>
 */

#include "config.h"

#include "meta-x11-selection-input-stream-private.h"

#include "mtk/mtk-x11.h"
#include "x11/meta-x11-display-private.h"

typedef struct MetaX11SelectionInputStreamPrivate MetaX11SelectionInputStreamPrivate;

struct _MetaX11SelectionInputStream
{
  GInputStream parent_instance;
};

struct MetaX11SelectionInputStreamPrivate
{
  MetaX11Display *x11_display;
  Window window;
  GAsyncQueue *chunks;
  Atom xselection;
  Atom xtarget;
  Atom xproperty;
  Atom xtype;

  GTask *pending_task;
  uint8_t *pending_data;
  size_t pending_size;

  guint complete : 1;
  guint incr : 1;
};

G_DEFINE_TYPE_WITH_PRIVATE (MetaX11SelectionInputStream,
                            meta_x11_selection_input_stream,
                            G_TYPE_INPUT_STREAM)

static gboolean
meta_x11_selection_input_stream_has_data (MetaX11SelectionInputStream *stream)
{
  MetaX11SelectionInputStreamPrivate *priv =
    meta_x11_selection_input_stream_get_instance_private (stream);

  return g_async_queue_length (priv->chunks) > 0 || priv->complete;
}

/* NB: blocks when no data is in buffer */
static size_t
meta_x11_selection_input_stream_fill_buffer (MetaX11SelectionInputStream *stream,
                                             uint8_t                     *buffer,
                                             size_t                       count)
{
  MetaX11SelectionInputStreamPrivate *priv =
    meta_x11_selection_input_stream_get_instance_private (stream);
  GBytes *bytes;
  size_t result = 0;

  g_async_queue_lock (priv->chunks);

  for (bytes = g_async_queue_pop_unlocked (priv->chunks);
       bytes != NULL && count > 0;
       bytes = g_async_queue_try_pop_unlocked (priv->chunks))
  {
    size_t size = g_bytes_get_size (bytes);

    if (size == 0)
      {
        /* EOF marker, put it back */
        g_async_queue_push_front_unlocked (priv->chunks, bytes);
        bytes = NULL;
        break;
      }
    else if (size > count)
      {
        GBytes *subbytes;
        if (buffer)
          memcpy (buffer, g_bytes_get_data (bytes, NULL), count);
        subbytes = g_bytes_new_from_bytes (bytes, count, size - count);
        g_async_queue_push_front_unlocked (priv->chunks, subbytes);
        size = count;
      }
    else
      {
        if (buffer)
          memcpy (buffer, g_bytes_get_data (bytes, NULL), size);
      }

    g_bytes_unref (bytes);
    result += size;
    if (buffer)
      buffer += size;
    count -= size;
  }

  if (bytes)
    g_async_queue_push_front_unlocked (priv->chunks, bytes);

  g_async_queue_unlock (priv->chunks);

  return result;
}

static void
meta_x11_selection_input_stream_flush (MetaX11SelectionInputStream *stream)
{
  MetaX11SelectionInputStreamPrivate *priv =
    meta_x11_selection_input_stream_get_instance_private (stream);
  Display *xdisplay = priv->x11_display->xdisplay;
  gssize written;

  mtk_x11_error_trap_push (xdisplay);
  XDeleteProperty (xdisplay, priv->window, priv->xproperty);
  mtk_x11_error_trap_pop (xdisplay);

  if (!meta_x11_selection_input_stream_has_data (stream))
    return;

  if (priv->pending_task == NULL)
    return;

  written = meta_x11_selection_input_stream_fill_buffer (stream,
                                                         priv->pending_data,
                                                         priv->pending_size);
  g_task_return_int (priv->pending_task, written);

  g_clear_object (&priv->pending_task);
  priv->pending_data = NULL;
  priv->pending_size = 0;
}

static void
meta_x11_selection_input_stream_complete (MetaX11SelectionInputStream *stream)
{
  MetaX11SelectionInputStreamPrivate *priv =
    meta_x11_selection_input_stream_get_instance_private (stream);

  if (priv->complete)
    return;

  priv->complete = TRUE;

  g_async_queue_push (priv->chunks, g_bytes_new (NULL, 0));
  meta_x11_selection_input_stream_flush (stream);

  priv->x11_display->selection.input_streams =
    g_list_remove (priv->x11_display->selection.input_streams, stream);

  g_object_unref (stream);
}

static gssize
meta_x11_selection_input_stream_read (GInputStream  *input_stream,
                                      void          *buffer,
                                      size_t         count,
                                      GCancellable  *cancellable,
                                      GError       **error)
{
  MetaX11SelectionInputStream *stream =
    META_X11_SELECTION_INPUT_STREAM (input_stream);

  return meta_x11_selection_input_stream_fill_buffer (stream, buffer, count);
}

static gboolean
meta_x11_selection_input_stream_close (GInputStream  *stream,
                                       GCancellable  *cancellable,
                                       GError       **error)
{
  return TRUE;
}

static void
meta_x11_selection_input_stream_read_async (GInputStream        *input_stream,
                                            void                *buffer,
                                            size_t               count,
                                            int                  io_priority,
                                            GCancellable        *cancellable,
                                            GAsyncReadyCallback  callback,
                                            gpointer             user_data)
{
  MetaX11SelectionInputStream *stream =
    META_X11_SELECTION_INPUT_STREAM (input_stream);
  MetaX11SelectionInputStreamPrivate *priv =
    meta_x11_selection_input_stream_get_instance_private (stream);
  GTask *task;

  task = g_task_new (stream, cancellable, callback, user_data);
  g_task_set_source_tag (task, meta_x11_selection_input_stream_read_async);
  g_task_set_priority (task, io_priority);

  if (meta_x11_selection_input_stream_has_data (stream))
    {
      gssize size;

      size = meta_x11_selection_input_stream_fill_buffer (stream, buffer, count);
      g_task_return_int (task, size);
      g_object_unref (task);
    }
  else
    {
      priv->pending_data = buffer;
      priv->pending_size = count;
      priv->pending_task = task;
    }
}

static gssize
meta_x11_selection_input_stream_read_finish (GInputStream  *stream,
                                             GAsyncResult  *result,
                                             GError       **error)
{
  g_return_val_if_fail (g_task_is_valid (result, stream), -1);
  g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) ==
                        meta_x11_selection_input_stream_read_async, -1);

  return g_task_propagate_int (G_TASK (result), error);
}

static void
meta_x11_selection_input_stream_close_async (GInputStream        *stream,
                                             int                  io_priority,
                                             GCancellable        *cancellable,
                                             GAsyncReadyCallback  callback,
                                             gpointer             user_data)
{
  GTask *task;

  task = g_task_new (stream, cancellable, callback, user_data);
  g_task_set_source_tag (task, meta_x11_selection_input_stream_close_async);
  g_task_return_boolean (task, TRUE);
  g_object_unref (task);
}

static gboolean
meta_x11_selection_input_stream_close_finish (GInputStream  *stream,
                                              GAsyncResult  *result,
                                              GError       **error)
{
  return TRUE;
}

static void
meta_x11_selection_input_stream_dispose (GObject *object)
{
  MetaX11SelectionInputStream *stream =
    META_X11_SELECTION_INPUT_STREAM (object);
  MetaX11SelectionInputStreamPrivate *priv =
    meta_x11_selection_input_stream_get_instance_private (stream);
  MetaX11Display *x11_display;

  x11_display = priv->x11_display;
  if (x11_display)
    {
      x11_display->selection.input_streams =
        g_list_remove (x11_display->selection.input_streams, stream);
    }

  G_OBJECT_CLASS (meta_x11_selection_input_stream_parent_class)->dispose (object);
}

static void
meta_x11_selection_input_stream_finalize (GObject *object)
{
  MetaX11SelectionInputStream *stream =
    META_X11_SELECTION_INPUT_STREAM (object);
  MetaX11SelectionInputStreamPrivate *priv =
    meta_x11_selection_input_stream_get_instance_private (stream);
  MetaX11Display *x11_display;

  g_async_queue_unref (priv->chunks);

  x11_display = priv->x11_display;
  if (x11_display)
    {
      Display *xdisplay = meta_x11_display_get_xdisplay (x11_display);

      XDestroyWindow (xdisplay, priv->window);

      g_object_remove_weak_pointer (G_OBJECT (x11_display),
                                    (gpointer *) &priv->x11_display);
    }

  G_OBJECT_CLASS (meta_x11_selection_input_stream_parent_class)->finalize (object);
}

static void
meta_x11_selection_input_stream_class_init (MetaX11SelectionInputStreamClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GInputStreamClass *istream_class = G_INPUT_STREAM_CLASS (klass);

  object_class->dispose = meta_x11_selection_input_stream_dispose;
  object_class->finalize = meta_x11_selection_input_stream_finalize;

  istream_class->read_fn = meta_x11_selection_input_stream_read;
  istream_class->close_fn = meta_x11_selection_input_stream_close;

  istream_class->read_async = meta_x11_selection_input_stream_read_async;
  istream_class->read_finish = meta_x11_selection_input_stream_read_finish;
  istream_class->close_async = meta_x11_selection_input_stream_close_async;
  istream_class->close_finish = meta_x11_selection_input_stream_close_finish;
}

static void
meta_x11_selection_input_stream_init (MetaX11SelectionInputStream *stream)
{
  MetaX11SelectionInputStreamPrivate *priv =
    meta_x11_selection_input_stream_get_instance_private (stream);

  priv->chunks = g_async_queue_new_full ((GDestroyNotify) g_bytes_unref);
}

static void
XFree_without_return_value (gpointer data)
{
  XFree (data);
}

static GBytes *
get_selection_property (MetaX11Display *x11_display,
                        Window          owner,
                        Atom            property,
                        Atom           *ret_type)
{
  gulong nitems;
  gulong nbytes;
  Atom prop_type;
  gint prop_format;
  uint8_t *data = NULL;

  mtk_x11_error_trap_push (x11_display->xdisplay);

  if (XGetWindowProperty (x11_display->xdisplay, owner, property,
                          0, 0x1FFFFFFF, False,
                          AnyPropertyType, &prop_type, &prop_format,
                          &nitems, &nbytes, &data) != Success)
    {
      mtk_x11_error_trap_pop (x11_display->xdisplay);
      goto err;
    }

  if (mtk_x11_error_trap_pop_with_return (x11_display->xdisplay) != Success)
    goto err;

  if (prop_type != None)
    {
      size_t length;

      switch (prop_format)
        {
        case 8:
          length = nitems;
          break;
        case 16:
          length = sizeof (short) * nitems;
          break;
        case 32:
          length = sizeof (long) * nitems;
          break;
        default:
          g_warning ("Unknown XGetWindowProperty() format %u", prop_format);
          goto err;
        }

      *ret_type = prop_type;

      return g_bytes_new_with_free_func (data,
                                         length,
                                         XFree_without_return_value,
                                         data);
    }

err:
  if (data)
    XFree (data);

  *ret_type = None;

  return NULL;
}

gboolean
meta_x11_selection_input_stream_xevent (MetaX11SelectionInputStream *stream,
                                        const XEvent                *xevent)
{
  MetaX11SelectionInputStreamPrivate *priv =
    meta_x11_selection_input_stream_get_instance_private (stream);
  Display *xdisplay;
  Window xwindow;
  GBytes *bytes;
  Atom type;
  char *target;

  xdisplay = priv->x11_display->xdisplay;
  xwindow = priv->window;

  if (xevent->xany.display != xdisplay ||
      xevent->xany.window != xwindow)
    return FALSE;

  switch (xevent->type)
    {
    case PropertyNotify:
      if (!priv->incr ||
          xevent->xproperty.atom != priv->xproperty ||
          xevent->xproperty.state != PropertyNewValue)
        return FALSE;

      bytes = get_selection_property (priv->x11_display, xwindow,
                                      xevent->xproperty.atom,
                                      &type);

      if (bytes == NULL)
        {
          g_debug ("INCR request came out empty");
          meta_x11_selection_input_stream_complete (stream);
        }
      else if (g_bytes_get_size (bytes) == 0 || type == None)
        {
          g_bytes_unref (bytes);
          meta_x11_selection_input_stream_complete (stream);
        }
      else
        {
          g_async_queue_push (priv->chunks, bytes);
          meta_x11_selection_input_stream_flush (stream);
        }
      return FALSE;

    case SelectionNotify:
      {
        GTask *task;

        /* selection is not for us */
        if (priv->xselection != xevent->xselection.selection ||
            priv->xtarget != xevent->xselection.target)
          return FALSE;

        /* We already received a selectionNotify before */
        if (priv->pending_task == NULL ||
            g_task_get_source_tag (priv->pending_task) != meta_x11_selection_input_stream_new_async)
          {
            g_debug ("Misbehaving client sent a reentrant SelectionNotify");
            return FALSE;
          }

        task = g_steal_pointer (&priv->pending_task);

        if (xevent->xselection.property == None)
          {
            target = XGetAtomName (xdisplay, priv->xtarget);
            g_task_return_new_error (task,
                                     G_IO_ERROR,
                                     G_IO_ERROR_NOT_FOUND,
                                     _("Format %s not supported"), target);
            meta_x11_selection_input_stream_complete (stream);
            XFree (target);
          }
        else
          {
            bytes = get_selection_property (priv->x11_display, xwindow,
                                            xevent->xselection.property,
                                            &priv->xtype);

            g_task_return_pointer (task, g_object_ref (stream), g_object_unref);

            if (bytes == NULL)
              {
                meta_x11_selection_input_stream_complete (stream);
              }
            else
              {
                if (priv->xtype == XInternAtom (priv->x11_display->xdisplay, "INCR", False))
                  {
                    /* The remainder of the selection will come through PropertyNotify
                       events on xwindow */
                    priv->incr = TRUE;
                    meta_x11_selection_input_stream_flush (stream);
                  }
                else
                  {
                    g_async_queue_push (priv->chunks, bytes);

                    meta_x11_selection_input_stream_complete (stream);
                  }
              }
          }

        g_object_unref (task);
      }
      return TRUE;

    default:
      return FALSE;
    }
}

void
meta_x11_selection_input_stream_new_async (MetaX11Display      *x11_display,
                                           Atom                 xselection,
                                           const char          *target,
                                           guint32              timestamp,
                                           int                  io_priority,
                                           GCancellable        *cancellable,
                                           GAsyncReadyCallback  callback,
                                           gpointer             user_data)
{
  MetaX11SelectionInputStream *stream;
  MetaX11SelectionInputStreamPrivate *priv;
  XSetWindowAttributes attributes = { 0 };

  stream = g_object_new (META_TYPE_X11_SELECTION_INPUT_STREAM, NULL);
  priv = meta_x11_selection_input_stream_get_instance_private (stream);

  attributes.event_mask = PropertyChangeMask;
  attributes.override_redirect = True;

  priv->x11_display = x11_display;
  g_object_add_weak_pointer (G_OBJECT (x11_display),
                             (gpointer *) &priv->x11_display);

  x11_display->selection.input_streams =
    g_list_prepend (x11_display->selection.input_streams, stream);
  priv->xselection = xselection;
  priv->xtarget = XInternAtom (x11_display->xdisplay, target, False);
  priv->xproperty = XInternAtom (x11_display->xdisplay, "META_SELECTION", False);
  priv->window = XCreateWindow (x11_display->xdisplay,
                                x11_display->xroot,
                                -1, -1, 1, 1,
                                0, /* border width */
                                0, /* depth */
                                InputOnly, /* class */
                                CopyFromParent, /* visual */
                                CWEventMask | CWOverrideRedirect,
                                &attributes);

  XConvertSelection (x11_display->xdisplay,
                     priv->xselection,
                     priv->xtarget,
                     priv->xproperty,
                     priv->window,
                     timestamp);

  priv->pending_task = g_task_new (NULL, cancellable, callback, user_data);
  g_task_set_source_tag (priv->pending_task, meta_x11_selection_input_stream_new_async);
  g_task_set_priority (priv->pending_task, io_priority);
}

GInputStream *
meta_x11_selection_input_stream_new_finish (GAsyncResult  *result,
                                            GError       **error)
{
  MetaX11SelectionInputStream *stream;
  GTask *task;

  g_return_val_if_fail (g_task_is_valid (result, NULL), NULL);
  task = G_TASK (result);
  g_return_val_if_fail (g_task_get_source_tag (task) ==
                        meta_x11_selection_input_stream_new_async, NULL);

  stream = g_task_propagate_pointer (task, error);
  if (!stream)
    return NULL;

  return G_INPUT_STREAM (stream);
}
