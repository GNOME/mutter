/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Hyungwon Hwang
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

#include <glib.h>

#ifdef HAVE_X11
#include <X11/Xlib.h>
#include "compositor/meta-compositor-x11.h"
#endif

#include "backends/meta-backend-private.h"

#ifdef HAVE_X11
gboolean meta_dnd_handle_xdnd_event (MetaBackend       *backend,
                                     MetaCompositorX11 *compositor_x11,
                                     Display           *xdisplay,
                                     XEvent            *xev);

void meta_dnd_init_xdnd (MetaX11Display *x11_display);
#endif

#ifdef HAVE_WAYLAND
void meta_dnd_wayland_handle_begin_modal (MetaCompositor *compositor);

void meta_dnd_wayland_handle_end_modal (MetaCompositor *compositor);

void meta_dnd_wayland_on_motion_event (MetaDnd            *dnd,
                                       const ClutterEvent *event);
#endif
