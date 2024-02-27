/*
 * Wayland Support
 *
 * Copyright (C) 2013 Intel Corporation
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

#include <glib.h>
#include <wayland-server.h>

#include "meta/meta-cursor-tracker.h"
#include "wayland/meta-wayland-pointer-constraints.h"
#include "wayland/meta-wayland-pointer-gesture-hold.h"
#include "wayland/meta-wayland-pointer-gesture-pinch.h"
#include "wayland/meta-wayland-pointer-gesture-swipe.h"
#include "wayland/meta-wayland-seat.h"
#include "wayland/meta-wayland-surface-private.h"
#include "wayland/meta-wayland-types.h"

#define META_TYPE_WAYLAND_POINTER (meta_wayland_pointer_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandPointer, meta_wayland_pointer,
                      META, WAYLAND_POINTER,
                      MetaWaylandInputDevice)

struct _MetaWaylandPointerClient
{
  struct wl_list pointer_resources;
  struct wl_list swipe_gesture_resources;
  struct wl_list pinch_gesture_resources;
  struct wl_list hold_gesture_resources;
  struct wl_list relative_pointer_resources;
  ClutterEventType active_touchpad_gesture;
};

void meta_wayland_pointer_enable (MetaWaylandPointer *pointer);

void meta_wayland_pointer_disable (MetaWaylandPointer *pointer);

void meta_wayland_pointer_update (MetaWaylandPointer *pointer,
                                  const ClutterEvent *event);

gboolean meta_wayland_pointer_handle_event (MetaWaylandPointer *pointer,
                                            const ClutterEvent *event);

void meta_wayland_pointer_send_relative_motion (MetaWaylandPointer *pointer,
                                                const ClutterEvent *event);

void meta_wayland_pointer_broadcast_frame (MetaWaylandPointer *pointer);

void meta_wayland_pointer_get_relative_coordinates (MetaWaylandPointer *pointer,
                                                    MetaWaylandSurface *surface,
                                                    wl_fixed_t         *x,
                                                    wl_fixed_t         *y);

void meta_wayland_pointer_create_new_resource (MetaWaylandPointer *pointer,
                                               struct wl_client   *client,
                                               struct wl_resource *seat_resource,
                                               uint32_t id);

gboolean meta_wayland_pointer_get_grab_info (MetaWaylandPointer    *pointer,
                                             MetaWaylandSurface    *surface,
                                             uint32_t               serial,
                                             gboolean               require_pressed,
                                             ClutterInputDevice   **device_out,
                                             float                 *x,
                                             float                 *y);

gboolean meta_wayland_pointer_can_popup (MetaWaylandPointer *pointer,
                                         uint32_t            serial);

MetaWaylandPointerClient * meta_wayland_pointer_get_pointer_client (MetaWaylandPointer *pointer,
                                                                    struct wl_client   *client);
void meta_wayland_pointer_unbind_pointer_client_resource (struct wl_resource *resource);

void meta_wayland_relative_pointer_init (MetaWaylandCompositor *compositor);

MetaWaylandSeat *meta_wayland_pointer_get_seat (MetaWaylandPointer *pointer);

void meta_wayland_pointer_update_cursor_surface (MetaWaylandPointer *pointer);

MetaWaylandSurface * meta_wayland_pointer_get_current_surface (MetaWaylandPointer *pointer);

MetaWaylandSurface * meta_wayland_pointer_get_focus_surface (MetaWaylandPointer *pointer);

void meta_wayland_pointer_focus_surface (MetaWaylandPointer *pointer,
                                         MetaWaylandSurface *surface);

MetaWaylandSurface * meta_wayland_pointer_get_implicit_grab_surface (MetaWaylandPointer *pointer);

MetaWaylandPointerClient * meta_wayland_pointer_get_focus_client (MetaWaylandPointer *pointer);
