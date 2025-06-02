/*
 * Copyright (C) 2021 Red Hat
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

#include "clutter/clutter-keymap.h"

CLUTTER_EXPORT
void clutter_keymap_update_state (ClutterKeymap      *keymap,
                                  gboolean            caps_lock_state,
                                  gboolean            num_lock_state,
                                  xkb_layout_index_t  locked_layout_group,
                                  xkb_mod_mask_t      depressed_mods,
                                  xkb_mod_mask_t      latched_mods,
                                  xkb_mod_mask_t      locked_mods);
