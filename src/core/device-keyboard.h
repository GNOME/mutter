/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * \file device-keyboard.h  Keyboard device abstraction
 *
 * Input devices.
 * This file contains the internal abstraction of keyboard devices so
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

#ifndef META_DEVICE_KEYBOARD_H
#define META_DEVICE_KEYBOARD_H

#include "display-private.h"
#include "device-private.h"

#define META_TYPE_DEVICE_KEYBOARD            (meta_device_keyboard_get_type ())
#define META_DEVICE_KEYBOARD(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_DEVICE_KEYBOARD, MetaDeviceKeyboard))
#define META_DEVICE_KEYBOARD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  META_TYPE_DEVICE_KEYBOARD, MetaDeviceKeyboardClass))
#define META_IS_DEVICE_KEYBOARD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_DEVICE_KEYBOARD))
#define META_IS_DEVICE_KEYBOARD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  META_TYPE_DEVICE_KEYBOARD))
#define META_DEVICE_KEYBOARD_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  META_TYPE_DEVICE_KEYBOARD, MetaDeviceKeyboardClass))

typedef struct _MetaDeviceKeyboard MetaDeviceKeyboard;
typedef struct _MetaDeviceKeyboardClass MetaDeviceKeyboardClass;

struct _MetaDeviceKeyboard
{
  MetaDevice parent_instance;
};

struct _MetaDeviceKeyboardClass
{
  MetaDeviceClass parent_instance;

  Window (* get_focus_window) (MetaDeviceKeyboard *keyboard);
  void   (* set_focus_window) (MetaDeviceKeyboard *keyboard,
                               Window              xwindow,
                               Time                timestamp);
};

GType    meta_device_keyboard_get_type   (void) G_GNUC_CONST;

Window   meta_device_keyboard_get_focus_window (MetaDeviceKeyboard *keyboard);
void     meta_device_keyboard_set_focus_window (MetaDeviceKeyboard *keyboard,
                                                Window              xwindow,
                                                Time                timestamp);


#endif /* META_DEVICE_KEYBOARD_H */
