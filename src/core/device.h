/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * \file device.h  Input device abstraction
 *
 * Input devices.
 * This file contains the internal abstraction of input devices so
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

#ifndef META_DEVICE_H
#define META_DEVICE_H

typedef struct _MetaDevice MetaDevice;
typedef struct _MetaDeviceClass MetaDeviceClass;

#include "display-private.h"

#define META_TYPE_DEVICE            (meta_device_get_type ())
#define META_DEVICE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_DEVICE, MetaDevice))
#define META_DEVICE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  META_TYPE_DEVICE, MetaDeviceClass))
#define META_IS_DEVICE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_DEVICE))
#define META_IS_DEVICE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  META_TYPE_DEVICE))
#define META_DEVICE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  META_TYPE_DEVICE, MetaDeviceClass))

struct _MetaDevice
{
  GObject parent_instance;
  gpointer priv;
};

struct _MetaDeviceClass
{
  GObjectClass parent_instance;

  void     (* allow_events) (MetaDevice  *device,
                             int          mode,
                             Time         time);

  gboolean (* grab)         (MetaDevice *device,
                             Window      xwindow,
                             guint       evmask,
                             MetaCursor  cursor,
                             gboolean    owner_events,
                             gboolean    sync,
                             Time        time);
  void     (* ungrab)       (MetaDevice *device,
                             Time        time);
};

GType        meta_device_get_type     (void) G_GNUC_CONST;

int          meta_device_get_id       (MetaDevice  *device);
MetaDisplay *meta_device_get_display  (MetaDevice *device);

void         meta_device_allow_events (MetaDevice  *device,
                                       int          mode,
                                       Time         time);

gboolean     meta_device_grab         (MetaDevice *device,
                                       Window      xwindow,
                                       guint       evmask,
                                       MetaCursor  cursor,
                                       gboolean    owner_events,
                                       gboolean    sync,
                                       Time        time);
void         meta_device_ungrab       (MetaDevice *device,
                                       Time        time);

#endif /* META_DEVICE_H */
