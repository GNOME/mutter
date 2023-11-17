/*
 * Interface for Wayland events
 *
 * Copyright (C) 2023 Red Hat Inc.
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

#include <glib-object.h>

#include "clutter/clutter.h"
#include "wayland/meta-wayland-types.h"

#define META_TYPE_WAYLAND_INPUT (meta_wayland_input_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandInput,
                      meta_wayland_input,
                      META, WAYLAND_INPUT,
                      GObject);

typedef struct _MetaWaylandEventHandler MetaWaylandEventHandler;

typedef struct _MetaWaylandEventInterface MetaWaylandEventInterface;

struct _MetaWaylandEventInterface
{
  MetaWaylandSurface * (*get_focus_surface) (MetaWaylandEventHandler *handler,
                                             ClutterInputDevice      *device,
                                             ClutterEventSequence    *sequence,
                                             gpointer                user_data);

  /* Pointer/stylus/touch */
  void (*focus) (MetaWaylandEventHandler *handler,
                 ClutterInputDevice      *device,
                 ClutterEventSequence    *sequence,
                 MetaWaylandSurface      *surface,
                 gpointer                 user_data);

  gboolean (*motion) (MetaWaylandEventHandler *handler,
                      const ClutterEvent      *event,
                      gpointer                 user_data);

  gboolean (*press) (MetaWaylandEventHandler *handler,
		     const ClutterEvent      *event,
                     gpointer                 user_data);

  gboolean (*release) (MetaWaylandEventHandler *handler,
		       const ClutterEvent      *event,
                       gpointer                 user_data);

  /* Key */
  gboolean (*key) (MetaWaylandEventHandler *handler,
		   const ClutterEvent      *event,
                   gpointer                 user_data);

  /* Other (Pads/IM/...) */
  gboolean (*other) (MetaWaylandEventHandler *handler,
		     const ClutterEvent      *event,
                     gpointer                 user_data);
};

MetaWaylandInput * meta_wayland_input_new (MetaWaylandSeat *seat);

MetaWaylandEventHandler * meta_wayland_input_attach_event_handler (MetaWaylandInput                *input,
                                                                   const MetaWaylandEventInterface *iface,
                                                                   gboolean                         grab,
                                                                   gpointer                         user_data);

gboolean meta_wayland_input_is_current_handler (MetaWaylandInput        *input,
                                                MetaWaylandEventHandler *handler);

void meta_wayland_input_detach_event_handler (MetaWaylandInput        *input,
                                              MetaWaylandEventHandler *handler);

gboolean meta_wayland_input_handle_event (MetaWaylandInput   *input,
                                          const ClutterEvent *event);

void meta_wayland_input_invalidate_focus (MetaWaylandInput     *input,
                                          ClutterInputDevice   *device,
                                          ClutterEventSequence *sequence);

MetaWaylandSurface * meta_wayland_event_handler_chain_up_get_focus_surface (MetaWaylandEventHandler *handler,
                                                                            ClutterInputDevice      *device,
                                                                            ClutterEventSequence    *sequence);

void meta_wayland_event_handler_chain_up_focus (MetaWaylandEventHandler *handler,
                                                ClutterInputDevice      *device,
                                                ClutterEventSequence    *sequence,
                                                MetaWaylandSurface      *surface);
