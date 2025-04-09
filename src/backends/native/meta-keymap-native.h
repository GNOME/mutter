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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#pragma once

#include "backends/native/meta-xkb-utils.h"
#include "clutter/clutter.h"

#define META_TYPE_KEYMAP_NATIVE (meta_keymap_native_get_type ())
META_EXPORT_TEST
G_DECLARE_FINAL_TYPE (MetaKeymapNative, meta_keymap_native,
                      META, KEYMAP_NATIVE,
                      ClutterKeymap)

META_EXPORT_TEST
void meta_keymap_native_get_modifier_state (MetaKeymapNative *keymap_native,
                                            xkb_mod_mask_t   *depressed_mods,
                                            xkb_mod_mask_t   *latched_mods,
                                            xkb_mod_mask_t   *locked_mods);

xkb_layout_index_t meta_keymap_native_get_layout_index (MetaKeymapNative *keymap_native);
