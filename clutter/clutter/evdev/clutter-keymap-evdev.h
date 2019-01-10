/*
 * Copyright (C) 2018 Red Hat
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
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "clutter-xkb-utils.h"
#include "clutter-keymap.h"

#define CLUTTER_TYPE_KEYMAP_EVDEV (clutter_keymap_evdev_get_type ())
G_DECLARE_FINAL_TYPE (ClutterKeymapEvdev, clutter_keymap_evdev,
                      CLUTTER, KEYMAP_EVDEV,
                      ClutterKeymap)

void                clutter_keymap_evdev_set_keyboard_map (ClutterKeymapEvdev *keymap,
                                                           struct xkb_keymap  *xkb_keymap);
struct xkb_keymap * clutter_keymap_evdev_get_keyboard_map (ClutterKeymapEvdev *keymap);
