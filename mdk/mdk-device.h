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

#define MDK_TYPE_DEVICE (mdk_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (MdkDevice, mdk_device, MDK, DEVICE, GObject)

struct _MdkDeviceClass
{
  GObjectClass parent_class;
};

MdkSeat * mdk_device_get_seat (MdkDevice *device);

struct ei_device * mdk_device_get_ei_device (MdkDevice *device);

void mdk_device_process_event (MdkDevice       *device,
                               struct ei_event *ei_event);
