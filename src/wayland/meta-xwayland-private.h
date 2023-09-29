/*
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

#pragma once

#include <glib.h>

#include "wayland/meta-wayland-private.h"

gboolean
meta_xwayland_init (MetaXWaylandManager    *manager,
                    MetaWaylandCompositor  *compositor,
                    struct wl_display      *display,
                    GError                **error);

void
meta_xwayland_init_display (MetaXWaylandManager  *manager,
                            MetaDisplay          *display);

void
meta_xwayland_setup_xdisplay (MetaXWaylandManager *manager,
                              Display             *xdisplay);

gboolean
meta_xwayland_handle_xevent (XEvent *event);

/* wl_data_device/X11 selection interoperation */
void meta_xwayland_init_dnd (MetaX11Display *x11_display);
void meta_xwayland_shutdown_dnd (MetaXWaylandManager *manager,
                                 MetaX11Display      *x11_display);
gboolean meta_xwayland_dnd_handle_xevent (MetaXWaylandManager *manger,
                                          XEvent              *xevent);

const MetaWaylandDragDestFuncs * meta_xwayland_selection_get_drag_dest_funcs (void);

void meta_xwayland_start_xserver (MetaXWaylandManager *manager,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data);
gboolean meta_xwayland_start_xserver_finish (MetaXWaylandManager  *manager,
                                             GAsyncResult         *result,
                                             GError              **error);

gboolean meta_xwayland_manager_handle_xevent (MetaXWaylandManager *manager,
                                              XEvent              *xevent);

void meta_xwayland_set_should_enable_ei_portal (MetaXWaylandManager  *manager,
                                                gboolean              should_enable_ei_portal);
