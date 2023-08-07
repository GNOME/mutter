/*
 *
 * Copyright © 2001 Ximian, Inc.
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2017 Red Hat
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

#include <X11/Xlib.h>

#include "backends/meta-input-settings-private.h"
#include "clutter/clutter.h"

void
meta_seat_x11_apply_kbd_a11y_settings (ClutterSeat         *seat,
                                       MetaKbdA11ySettings *kbd_a11y_settings);

gboolean
meta_seat_x11_a11y_init               (ClutterSeat            *seat);

void meta_seat_x11_check_xkb_a11y_settings_changed (ClutterSeat *seat);
