/*
 * Copyright (C) 2024 Red Hat Inc.
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
#include <libei.h>

#include "mdk-types.h"

#define MDK_TYPE_SEAT (mdk_seat_get_type ())
G_DECLARE_FINAL_TYPE (MdkSeat, mdk_seat, MDK, SEAT, GObject)

MdkSeat * mdk_seat_new (MdkEi          *ei,
                        struct ei_seat *ei_seat);

void mdk_seat_process_event (MdkSeat         *seat,
                             struct ei_event *ei_event);

void mdk_seat_bind_pointer (MdkSeat *seat);
void mdk_seat_unbind_pointer (MdkSeat *seat);
MdkPointer * mdk_seat_get_pointer (MdkSeat *seat);

void mdk_seat_bind_keyboard (MdkSeat *seat);
void mdk_seat_unbind_keyboard (MdkSeat *seat);
MdkKeyboard * mdk_seat_get_keyboard (MdkSeat *seat);

void mdk_seat_bind_touch (MdkSeat *seat);
void mdk_seat_unbind_touch (MdkSeat *seat);
MdkTouch * mdk_seat_get_touch (MdkSeat *seat);
