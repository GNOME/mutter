/*
 * Wayland Support
 *
 * Copyright (C) 2013 Intel Corporation
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

/*
 * Copyright © 2008-2011 Kristian Høgsberg
 * Copyright © 2012 Collabora, Ltd.
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#pragma once

#include <wayland-server.h>
#include <xkbcommon/xkbcommon.h>

#include "clutter/clutter.h"
#include "core/meta-anonymous-file.h"
#include "wayland/meta-wayland-input-device.h"
#include "wayland/meta-wayland-types.h"

#define META_TYPE_WAYLAND_KEYBOARD (meta_wayland_keyboard_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandKeyboard, meta_wayland_keyboard,
                      META, WAYLAND_KEYBOARD,
                      MetaWaylandInputDevice)

void meta_wayland_keyboard_enable (MetaWaylandKeyboard *keyboard);

void meta_wayland_keyboard_disable (MetaWaylandKeyboard *keyboard);

void meta_wayland_keyboard_update (MetaWaylandKeyboard *keyboard,
                                   const ClutterKeyEvent *event);

gboolean meta_wayland_keyboard_handle_event (MetaWaylandKeyboard *keyboard,
                                             const ClutterKeyEvent *event);
void meta_wayland_keyboard_update_key_state (MetaWaylandKeyboard *compositor,
                                             char                *key_vector,
                                             int                  key_vector_len,
                                             int                  offset);

void meta_wayland_keyboard_set_focus (MetaWaylandKeyboard *keyboard,
                                      MetaWaylandSurface *surface);

void meta_wayland_keyboard_create_new_resource (MetaWaylandKeyboard *keyboard,
                                                struct wl_client    *client,
                                                struct wl_resource  *seat_resource,
                                                uint32_t id);

gboolean meta_wayland_keyboard_can_grab_surface (MetaWaylandKeyboard *keyboard,
                                                 MetaWaylandSurface  *surface,
                                                 uint32_t             serial);
gboolean meta_wayland_keyboard_can_popup (MetaWaylandKeyboard *keyboard,
                                          uint32_t             serial);

META_EXPORT_TEST
MetaWaylandSurface * meta_wayland_keyboard_get_focus_surface (MetaWaylandKeyboard *keyboard);
