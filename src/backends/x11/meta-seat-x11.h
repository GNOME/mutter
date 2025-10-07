/*
 * Copyright (C) 2019 Red Hat Inc.
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
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#pragma once

#include "clutter/clutter.h"

G_BEGIN_DECLS

#define META_TYPE_SEAT_X11 meta_seat_x11_get_type ()
G_DECLARE_FINAL_TYPE (MetaSeatX11, meta_seat_x11, META, SEAT_X11, ClutterSeat)

MetaSeatX11 * meta_seat_x11_new (MetaBackend *backend,
                                 int          opcode,
                                 int          logical_pointer,
                                 int          logical_keyboard);

MetaBackend * meta_seat_x11_get_backend (MetaSeatX11 *seat_x11);

ClutterEvent * meta_seat_x11_translate_event (MetaSeatX11  *seat,
                                              XEvent       *xevent);
void meta_seat_x11_select_stage_events (MetaSeatX11  *seat,
                                        ClutterStage *stage);
void meta_seat_x11_notify_devices (MetaSeatX11  *seat_x11,
                                   ClutterStage *stage);

ClutterInputDevice * meta_seat_x11_get_core_pointer (MetaSeatX11 *seat_x11);

ClutterEventSequence * meta_seat_x11_get_pointer_emulating_sequence (MetaSeatX11 *seat_x11);

G_END_DECLS
