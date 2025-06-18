/*
 * Copyright (C) 2025 Red Hat Inc.
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include <libevdev/libevdev.h>

#include "meta/meta-base.h"

META_EXPORT
struct libevdev_uinput * meta_create_test_keyboard (void);

META_EXPORT
struct libevdev_uinput * meta_create_test_mouse (void);

META_EXPORT
void meta_wait_for_uinput_device (struct libevdev_uinput *evdev_uinput);
