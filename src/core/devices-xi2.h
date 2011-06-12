/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * \file devices-xi2.h  XInput2 input devices implementation
 *
 * Input devices.
 * This file contains the XInput2 implementation of input devices.
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

#ifndef META_DEVICES_XI2_H
#define META_DEVICES_XI2_H

#include "device-pointer.h"
#include "device-keyboard.h"

/* Pointer */
#define META_TYPE_DEVICE_POINTER_XI2            (meta_device_pointer_xi2_get_type ())
#define META_DEVICE_POINTER_XI2(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_DEVICE_POINTER_XI2, MetaDevicePointerXI2))
#define META_DEVICE_POINTER_XI2_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  META_TYPE_DEVICE_POINTER_XI2, MetaDevicePointerXI2Class))
#define META_IS_DEVICE_POINTER_XI2(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_DEVICE_POINTER_XI2))
#define META_IS_DEVICE_POINTER_XI2_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  META_TYPE_DEVICE_POINTER_XI2))
#define META_DEVICE_POINTER_XI2_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  META_TYPE_DEVICE_POINTER_XI2, MetaDevicePointerXI2Class))

typedef struct _MetaDevicePointerXI2 MetaDevicePointerXI2;
typedef struct _MetaDevicePointerXI2Class MetaDevicePointerXI2Class;

struct _MetaDevicePointerXI2
{
  MetaDevicePointer parent_instance;
};

struct _MetaDevicePointerXI2Class
{
  MetaDevicePointerClass parent_class;
};

GType       meta_device_pointer_xi2_get_type (void) G_GNUC_CONST;

MetaDevice *meta_device_pointer_xi2_new      (MetaDisplay *display,
                                              gint         device_id);

/* Keyboard */
#define META_TYPE_DEVICE_KEYBOARD_XI2            (meta_device_keyboard_xi2_get_type ())
#define META_DEVICE_KEYBOARD_XI2(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_DEVICE_KEYBOARD_XI2, MetaDeviceKeyboardXI2))
#define META_DEVICE_KEYBOARD_XI2_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  META_TYPE_DEVICE_KEYBOARD_XI2, MetaDeviceKeyboardXI2Class))
#define META_IS_DEVICE_KEYBOARD_XI2(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_DEVICE_KEYBOARD_XI2))
#define META_IS_DEVICE_KEYBOARD_XI2_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  META_TYPE_DEVICE_KEYBOARD_XI2))
#define META_DEVICE_KEYBOARD_XI2_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  META_TYPE_DEVICE_KEYBOARD_XI2, MetaDeviceKeyboardXI2Class))

typedef struct _MetaDeviceKeyboardXI2 MetaDeviceKeyboardXI2;
typedef struct _MetaDeviceKeyboardXI2Class MetaDeviceKeyboardXI2Class;

struct _MetaDeviceKeyboardXI2
{
  MetaDeviceKeyboard parent_instance;
};

struct _MetaDeviceKeyboardXI2Class
{
  MetaDeviceKeyboardClass parent_class;
};

GType       meta_device_keyboard_xi2_get_type (void) G_GNUC_CONST;

MetaDevice *meta_device_keyboard_xi2_new      (MetaDisplay *display,
                                               gint         device_id);

/* Helper function for translating event masks */
guchar * meta_device_xi2_translate_event_mask (guint        evmask,
                                               gint        *len);


#endif /* META_DEVICES_XI2_H */
