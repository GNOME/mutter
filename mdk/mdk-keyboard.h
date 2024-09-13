/*
 * Copyright (C) 2021 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <glib-object.h>

#include "mdk-device.h"
#include "mdk-types.h"

#define MDK_TYPE_KEYBOARD (mdk_keyboard_get_type ())
G_DECLARE_FINAL_TYPE (MdkKeyboard, mdk_keyboard,
                      MDK, KEYBOARD,
                      MdkDevice)

MdkKeyboard * mdk_keyboard_new (MdkSeat          *seat,
                                struct ei_device *ei_device);

void mdk_keyboard_release_all (MdkKeyboard *keyboard);

void mdk_keyboard_notify_key (MdkKeyboard *keyboard,
                              int32_t     key,
                              int         state);
