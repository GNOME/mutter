/*
 * Copyright (C) 2024 Bilal Elmoussaoui
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
 */

#include "backends/x11/meta-backend-x11.h"
#include "core/keybindings-private.h"
#include "core/window-private.h"
#include "mtk/mtk-x11-errors.h"
#include "x11/meta-x11-display-private.h"
#include "x11/meta-x11-frame.h"
#include "x11/window-x11-private.h"

#pragma once

void meta_x11_keybindings_grab_window_buttons (MetaKeyBindingManager *keys,
                                               MetaWindow            *window);

void meta_x11_keybindings_ungrab_window_buttons (MetaKeyBindingManager *keys,
                                                 MetaWindow            *window);

void meta_x11_keybindings_grab_focus_window_button (MetaKeyBindingManager *keys,
                                                    MetaWindow            *window);

void meta_x11_keybindings_ungrab_focus_window_button (MetaKeyBindingManager *keys,
                                                      MetaWindow            *window);

void meta_x11_display_grab_keys (MetaX11Display *x11_display);

void meta_x11_display_ungrab_keys (MetaX11Display *x11_display);

void meta_x11_keybindings_change_keygrab (MetaKeyBindingManager *keys,
                                          Window                 xwindow,
                                          gboolean               grab,
                                          MetaResolvedKeyCombo  *resolved_combo);

void meta_x11_keybindings_grab_key_bindings (MetaDisplay *display);

void meta_x11_keybindings_ungrab_key_bindings (MetaDisplay *display);

void meta_x11_keybindings_maybe_update_locate_pointer_keygrab (MetaDisplay *display,
                                                               gboolean     grab);

void meta_window_grab_keys (MetaWindow *window);

void meta_window_ungrab_keys (MetaWindow *window);
