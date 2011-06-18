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

#ifndef META_DEVICE_PRIVATE_H
#define META_DEVICE_PRIVATE_H

#include <meta/device.h>
#include "display-private.h"

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

void         meta_device_pair_devices      (MetaDevice *device,
                                            MetaDevice *other_device);

#endif /* META_DEVICE_PRIVATE_H */
