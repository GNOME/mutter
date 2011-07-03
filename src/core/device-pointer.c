/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Pointer device abstraction */

/*
 * Copyright (C) 2011 Carlos Garnacho
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
 */

#include <config.h>
#include "device-pointer.h"

G_DEFINE_ABSTRACT_TYPE (MetaDevicePointer,
                        meta_device_pointer,
                        META_TYPE_DEVICE)

static void
meta_device_pointer_class_init (MetaDevicePointerClass *klass)
{
}

static void
meta_device_pointer_init (MetaDevicePointer *pointer)
{
}

void
meta_device_pointer_warp (MetaDevicePointer *pointer,
                          MetaScreen        *screen,
                          gint               x,
                          gint               y)
{
  MetaDevicePointerClass *klass;

  g_return_if_fail (META_IS_DEVICE_POINTER (pointer));
  g_return_if_fail (META_IS_SCREEN (screen));

  klass = META_DEVICE_POINTER_GET_CLASS (pointer);

  if (klass->warp)
    (klass->warp) (pointer, screen, x, y);
}

void
meta_device_pointer_set_window_cursor (MetaDevicePointer *pointer,
                                       Window             xwindow,
                                       MetaCursor         cursor)
{
  MetaDevicePointerClass *klass;

  g_return_if_fail (META_IS_DEVICE_POINTER (pointer));
  g_return_if_fail (xwindow != None);

  klass = META_DEVICE_POINTER_GET_CLASS (pointer);

  if (klass->set_window_cursor)
    (klass->set_window_cursor) (pointer, xwindow, cursor);
}

gboolean
meta_device_pointer_query_position (MetaDevicePointer *pointer,
                                    Window             xwindow,
                                    Window            *root_ret,
                                    Window            *child_ret,
                                    gint              *root_x_ret,
                                    gint              *root_y_ret,
                                    gint              *x_ret,
                                    gint              *y_ret,
                                    guint             *mask_ret)
{
  MetaDevicePointerClass *klass;
  gint root_x, root_y, x, y;
  Window root, child;
  gboolean retval;
  guint mask;

  g_return_val_if_fail (META_IS_DEVICE_POINTER (pointer), FALSE);
  g_return_val_if_fail (xwindow != None, FALSE);

  klass = META_DEVICE_POINTER_GET_CLASS (pointer);

  if (!klass->query_position)
    return FALSE;

  retval = (klass->query_position) (pointer, xwindow, &root, &child,
                                    &root_x, &root_y, &x, &y, &mask);

  if (root_ret)
    *root_ret = root;

  if (child_ret)
    *child_ret = child;

  if (root_x_ret)
    *root_x_ret = root_x;

  if (root_y_ret)
    *root_y_ret = root_y;

  if (x_ret)
    *x_ret = x;

  if (y_ret)
    *y_ret = y;

  if (mask_ret)
    *mask_ret = mask;

  return retval;
}
