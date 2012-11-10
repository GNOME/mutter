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

typedef struct {
  GnomeBG   *bg;
  GdkPixbuf *pixbuf;

  GdkRectangle *monitors;
  int           num_monitors;
} TaskData;

static void
task_data_free (gpointer task_data)
{
  TaskData *td = task_data;

  g_object_unref (td->bg);
  g_object_unref (td->pixbuf);

  g_free (td->monitors);

  g_slice_free (TaskData, td);
}

static void
meta_background_draw_thread (GTask        *task,
                             gpointer      source_object,
                             gpointer      task_data,
                             GCancellable *cancellable)
{
  TaskData *td = task_data;

  gnome_bg_draw_areas (td->bg,
                       td->pixbuf,
                       TRUE,
                       td->monitors,
                       td->num_monitors);

  g_task_return_pointer (task, g_object_ref (td->pixbuf), g_object_unref);
}

GTask *
meta_background_draw_async (MetaScreen          *screen,
                            GnomeBG             *bg,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
  GTask *task;
  TaskData *td;
  int i;

  g_return_val_if_fail (META_IS_SCREEN (screen), NULL);
  g_return_val_if_fail (GNOME_IS_BG (bg), NULL);

  task = g_task_new (screen, cancellable, callback, user_data);
  g_task_set_source_tag (task, meta_background_draw_async);
  g_task_set_return_on_cancel (task, TRUE);
  g_task_set_check_cancellable (task, TRUE);

  td = g_slice_new (TaskData);
  td->bg = g_object_ref (bg);
  td->pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
                               FALSE, /* has alpha */
                               8, /* bits per sample */
                               screen->rect.width,
                               screen->rect.height);

  td->num_monitors = meta_screen_get_n_monitors (screen);
  td->monitors = g_new (GdkRectangle, td->num_monitors);
  for (i = 0; i < td->num_monitors; i++)
    meta_screen_get_monitor_geometry (screen, i, (MetaRectangle*)(td->monitors + i));

  g_task_set_task_data (task, td, task_data_free);

  g_task_run_in_thread (task, meta_background_draw_thread);

  return task;
}

CoglHandle
meta_background_draw_finish (MetaScreen    *screen,
                             GAsyncResult  *result,
                             GError       **error)
{
  GdkPixbuf *pixbuf;
  CoglHandle handle;

  pixbuf = g_task_propagate_pointer (G_TASK (result), error);
  if (pixbuf == NULL)
    return COGL_INVALID_HANDLE;

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
