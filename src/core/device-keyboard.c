/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Keyboard device abstraction */

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
#include "device-keyboard.h"

G_DEFINE_ABSTRACT_TYPE (MetaDeviceKeyboard,
                        meta_device_keyboard,
                        META_TYPE_DEVICE)

static void
meta_device_keyboard_class_init (MetaDeviceKeyboardClass *klass)
{
}

static void
meta_device_keyboard_init (MetaDeviceKeyboard *keyboard)
{
}

Window
meta_device_keyboard_get_focus_window (MetaDeviceKeyboard *keyboard)
{
  MetaDeviceKeyboardClass *klass;

  g_return_val_if_fail (META_IS_DEVICE_KEYBOARD (keyboard), None);

  klass = META_DEVICE_KEYBOARD_GET_CLASS (keyboard);

  if (!klass->get_focus_window)
    return None;

  return (klass->get_focus_window) (keyboard);
}

void
meta_device_keyboard_set_focus_window (MetaDeviceKeyboard *keyboard,
                                       Window              xwindow,
                                       Time                timestamp)
{
  MetaDeviceKeyboardClass *klass;

  g_return_if_fail (META_IS_DEVICE_KEYBOARD (keyboard));

  klass = META_DEVICE_KEYBOARD_GET_CLASS (keyboard);

  if (klass->set_focus_window)
    (klass->set_focus_window) (keyboard, xwindow, timestamp);
}
