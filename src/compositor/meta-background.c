/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * meta-background.c: Utilities for drawing the background
 *
 * Copyright 2012 Giovanni Campagna <scampa.giovanni@gmail.com>
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

#include <gio/gio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "meta-background-actor-private.h"
#include <core/screen-private.h>

static void
meta_background_draw_thread (GTask        *task,
                             gpointer      source_object,
                             gpointer      task_data,
                             GCancellable *cancellable)
{
  GFile *file;
  GInputStream *stream;
  GdkPixbuf *pixbuf;
  GError *error;

  /* TODO: handle slideshows and other complications */

  file = g_file_new_for_uri (task_data);

  error = NULL;
  stream = G_INPUT_STREAM (g_file_read (file, cancellable, &error));
  if (stream == NULL)
    {
      g_object_unref (file);
      g_task_return_error (task, error);
      return;
    }

  pixbuf = gdk_pixbuf_new_from_stream (stream, cancellable, &error);
  if (pixbuf == NULL)
    {
      g_object_unref (file);
      g_object_unref (stream);
      g_task_return_error (task, error);
    }

  g_task_return_pointer (task, pixbuf, g_object_unref);
  g_object_unref (file);
  g_object_unref (stream);
}

GTask *
meta_background_draw_async (MetaScreen          *screen,
                            const char          *picture_uri,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
  GTask *task;

  g_return_val_if_fail (META_IS_SCREEN (screen), NULL);

  task = g_task_new (screen, cancellable, callback, user_data);
  g_task_set_source_tag (task, meta_background_draw_async);
  g_task_set_return_on_cancel (task, TRUE);
  g_task_set_check_cancellable (task, TRUE);

  g_task_set_task_data (task, g_strdup (picture_uri), g_free);
  g_task_run_in_thread (task, meta_background_draw_thread);

  return task;
}

CoglHandle
meta_background_draw_finish (MetaScreen    *screen,
                             GAsyncResult  *result,
                             char         **picture_uri,
                             GError       **error)
{
  GdkPixbuf *pixbuf;
  CoglHandle handle;

  pixbuf = g_task_propagate_pointer (G_TASK (result), error);
  if (pixbuf == NULL)
    return COGL_INVALID_HANDLE;

  if (picture_uri != NULL)
    *picture_uri = g_strdup (g_task_get_task_data (G_TASK (result)));

  handle = cogl_texture_new_from_data (gdk_pixbuf_get_width (pixbuf),
                                       gdk_pixbuf_get_height (pixbuf),
                                       COGL_TEXTURE_NO_ATLAS | COGL_TEXTURE_NO_SLICING,
                                       COGL_PIXEL_FORMAT_RGB_888,
                                       COGL_PIXEL_FORMAT_RGB_888,
                                       gdk_pixbuf_get_rowstride (pixbuf),
                                       gdk_pixbuf_get_pixels (pixbuf));

  g_object_unref (pixbuf);
  return handle;
}
