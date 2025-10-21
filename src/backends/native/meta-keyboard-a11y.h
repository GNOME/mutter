/*
 * Copyright (C) 2025 Red Hat
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

#include "backends/meta-input-settings-private.h"

#define META_TYPE_KEYBOARD_A11Y (meta_keyboard_a11y_get_type ())
G_DECLARE_FINAL_TYPE (MetaKeyboardA11y, meta_keyboard_a11y,
                      META, KEYBOARD_A11Y,
                      GObject)

void meta_keyboard_a11y_apply_settings_in_impl (MetaKeyboardA11y    *keyboard_a11y,
                                                MetaKbdA11ySettings *settings);

void meta_keyboard_a11y_maybe_notify_toggle_keys_in_impl (MetaKeyboardA11y *keyboard_a11y);

gboolean meta_keyboard_a11y_process_event_in_impl (MetaKeyboardA11y  *keyboard_a11y,
                                                   ClutterEvent      *event,
                                                   ClutterEvent     **out_event);
