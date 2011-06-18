/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * \file devices-core.h  Core input devices implementation
 *
 * Input devices.
 * This file contains the core X protocol implementation of input devices.
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

#ifndef META_DEVICES_CORE_H
#define META_DEVICES_CORE_H

#include "device-pointer.h"
#include "device-keyboard.h"

/* Pointer */
#define META_TYPE_DEVICE_POINTER_CORE            (meta_device_pointer_core_get_type ())
#define META_DEVICE_POINTER_CORE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_DEVICE_POINTER_CORE, MetaDevicePointerCore))
#define META_DEVICE_POINTER_CORE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  META_TYPE_DEVICE_POINTER_CORE, MetaDevicePointerCoreClass))
#define META_IS_DEVICE_POINTER_CORE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_DEVICE_POINTER_CORE))
#define META_IS_DEVICE_POINTER_CORE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  META_TYPE_DEVICE_POINTER_CORE))
#define META_DEVICE_POINTER_CORE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  META_TYPE_DEVICE_POINTER_CORE, MetaDevicePointerCoreClass))

typedef struct _MetaDevicePointerCore MetaDevicePointerCore;
typedef struct _MetaDevicePointerCoreClass MetaDevicePointerCoreClass;

struct _MetaDevicePointerCore
{
  MetaDevicePointer parent_instance;
};

struct _MetaDevicePointerCoreClass
{
  MetaDevicePointerClass parent_class;
};

GType       meta_device_pointer_core_get_type (void) G_GNUC_CONST;

MetaDevice *meta_device_pointer_core_new      (MetaDisplay *display);

/* Keyboard */
#define META_TYPE_DEVICE_KEYBOARD_CORE            (meta_device_keyboard_core_get_type ())
#define META_DEVICE_KEYBOARD_CORE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_DEVICE_KEYBOARD_CORE, MetaDeviceKeyboardCore))
#define META_DEVICE_KEYBOARD_CORE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  META_TYPE_DEVICE_KEYBOARD_CORE, MetaDeviceKeyboardCoreClass))
#define META_IS_DEVICE_KEYBOARD_CORE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_DEVICE_KEYBOARD_CORE))
#define META_IS_DEVICE_KEYBOARD_CORE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  META_TYPE_DEVICE_KEYBOARD_CORE))
#define META_DEVICE_KEYBOARD_CORE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  META_TYPE_DEVICE_KEYBOARD_CORE, MetaDeviceKeyboardCoreClass))

typedef struct _MetaDeviceKeyboardCore MetaDeviceKeyboardCore;
typedef struct _MetaDeviceKeyboardCoreClass MetaDeviceKeyboardCoreClass;

struct _MetaDeviceKeyboardCore
{
  MetaDeviceKeyboard parent_instance;
};

struct _MetaDeviceKeyboardCoreClass
{
  MetaDeviceKeyboardClass parent_class;
};

GType       meta_device_keyboard_core_get_type (void) G_GNUC_CONST;

MetaDevice *meta_device_keyboard_core_new      (MetaDisplay *display);

#endif /* META_DEVICES_CORE_H */
