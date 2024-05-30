/*
 * Wayland Support
 *
 * Copyright (C) 2012 Intel Corporation
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

#pragma once

#include <wayland-server.h>

#include "clutter/clutter.h"
#include "wayland/meta-wayland-data-device.h"
#include "wayland/meta-wayland-data-device-primary.h"
#include "wayland/meta-wayland-input.h"
#include "wayland/meta-wayland-input-device.h"
#include "wayland/meta-wayland-keyboard.h"
#include "wayland/meta-wayland-pointer.h"
#include "wayland/meta-wayland-tablet-tool.h"
#include "wayland/meta-wayland-text-input.h"
#include "wayland/meta-wayland-touch.h"
#include "wayland/meta-wayland-types.h"

struct _MetaWaylandSeat
{
  MetaWaylandCompositor *compositor;

  struct wl_list base_resource_list;
  struct wl_display *wl_display;

  MetaWaylandPointer *pointer;
  MetaWaylandKeyboard *keyboard;
  MetaWaylandTouch *touch;
  MetaWaylandTabletSeat *tablet_seat;

  MetaWaylandDataDevice data_device;
  MetaWaylandDataDevicePrimary primary_data_device;

  MetaWaylandTextInput *text_input;

  MetaWaylandInput *input_handler;
  MetaWaylandEventHandler *default_handler;

  MetaWaylandSurface *input_focus;
  gulong input_focus_destroy_id;

  guint capabilities;
};

void meta_wayland_seat_init (MetaWaylandCompositor *compositor);

void meta_wayland_seat_free (MetaWaylandSeat *seat);

void meta_wayland_seat_update (MetaWaylandSeat    *seat,
                               const ClutterEvent *event);

gboolean meta_wayland_seat_handle_event (MetaWaylandSeat *seat,
                                         const ClutterEvent *event);

void meta_wayland_seat_set_input_focus (MetaWaylandSeat    *seat,
                                        MetaWaylandSurface *surface);

MetaWaylandSurface * meta_wayland_seat_get_input_focus (MetaWaylandSeat *seat);

gboolean meta_wayland_seat_get_grab_info (MetaWaylandSeat       *seat,
                                          MetaWaylandSurface    *surface,
                                          uint32_t               serial,
                                          gboolean               require_pressed,
                                          ClutterInputDevice   **device_out,
                                          ClutterEventSequence **sequence_out,
                                          float                 *x,
                                          float                 *y);
gboolean meta_wayland_seat_can_popup     (MetaWaylandSeat *seat,
                                          uint32_t         serial);

gboolean meta_wayland_seat_has_keyboard (MetaWaylandSeat *seat);

gboolean meta_wayland_seat_has_pointer (MetaWaylandSeat *seat);

gboolean meta_wayland_seat_has_touch (MetaWaylandSeat *seat);

MetaWaylandCompositor * meta_wayland_seat_get_compositor (MetaWaylandSeat *seat);

MetaWaylandInput * meta_wayland_seat_get_input (MetaWaylandSeat *seat);

MetaWaylandSurface * meta_wayland_seat_get_current_surface (MetaWaylandSeat      *seat,
                                                            ClutterInputDevice   *device,
                                                            ClutterEventSequence *sequence);
