/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * \file device-pointer.h  Pointer device abstraction
 *
 * Input devices.
 * This file contains the internal abstraction of pointer devices so
 * XInput2/core events can be handled similarly.
 */

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

#ifndef META_DEVICE_POINTER_H
#define META_DEVICE_POINTER_H

#include "display-private.h"
#include <meta/screen.h>
#include "device.h"

#define META_TYPE_DEVICE_POINTER            (meta_device_pointer_get_type ())
#define META_DEVICE_POINTER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_DEVICE_POINTER, MetaDevicePointer))
#define META_DEVICE_POINTER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  META_TYPE_DEVICE_POINTER, MetaDevicePointerClass))
#define META_IS_DEVICE_POINTER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_DEVICE_POINTER))
#define META_IS_DEVICE_POINTER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  META_TYPE_DEVICE_POINTER))
#define META_DEVICE_POINTER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  META_TYPE_DEVICE_POINTER, MetaDevicePointerClass))

typedef struct _MetaDevicePointer MetaDevicePointer;
typedef struct _MetaDevicePointerClass MetaDevicePointerClass;

struct _MetaDevicePointer
{
  MetaDevice parent_instance;
};

struct _MetaDevicePointerClass
{
  MetaDeviceClass parent_instance;

  void     (* warp)           (MetaDevicePointer *pointer,
                               MetaScreen        *screen,
                               gint               x,
                               gint               y);

  void (* set_window_cursor)  (MetaDevicePointer *pointer,
                               Window             xwindow,
                               MetaCursor         cursor);
  void (* query_position)     (MetaDevicePointer *pointer,
                               Window             xwindow,
                               Window            *root,
                               Window            *child,
                               gint              *root_x,
                               gint              *root_y,
                               gint              *x,
                               gint              *y,
                               guint             *mask);
};

GType    meta_device_pointer_get_type      (void) G_GNUC_CONST;

void     meta_device_pointer_warp              (MetaDevicePointer *pointer,
                                                MetaScreen        *screen,
                                                gint               x,
                                                gint               y);
void     meta_device_pointer_set_window_cursor (MetaDevicePointer *pointer,
						Window             xwindow,
						MetaCursor         cursor);

void     meta_device_pointer_query_position    (MetaDevicePointer *pointer,
                                                Window             xwindow,
                                                Window            *root,
                                                Window            *child,
                                                gint              *root_x,
                                                gint              *root_y,
                                                gint              *x,
                                                gint              *y,
                                                guint             *mask);

#endif /* META_DEVICE_POINTER_H */
