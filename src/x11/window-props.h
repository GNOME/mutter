/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * MetaWindow property handling
 *
 * A system which can inspect sets of properties of given windows
 * and take appropriate action given their values.
 *
 * Note that all the meta_window_reload_property* functions require a
 * round trip to the server.
 */

/*
 * Copyright (C) 2001, 2002 Red Hat, Inc.
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

#include <X11/Xutil.h>

#include "core/window-private.h"

/**
 * meta_window_reload_property_from_xwindow:
 * @window:     A window on the same display as the one we're
 *                   investigating (only used to find the display)
 * @xwindow:    The X handle for the window.
 * @property:   A single X atom.
 *
 * Requests the current values of a single property for a given
 * window from the server, and deals with it appropriately.
 * Does not return it to the caller (it's been dealt with!)
 */
void meta_window_reload_property_from_xwindow (MetaWindow      *window,
                                               Window           xwindow,
                                               Atom             property,
                                               gboolean         initial);

/**
 * meta_window_load_initial_properties:
 * @window:      The window.
 *
 * Requests the current values for standard properties for a given
 * window from the server, and deals with them appropriately.
 * Does not return them to the caller (they've been dealt with!)
 */
void meta_window_load_initial_properties (MetaWindow *window);

/**
 * meta_x11_display_init_window_prop_hooks:
 * @x11_display:  The X11 display.
 *
 * Initialises the hooks used for the reload_property* functions
 * on a particular display, and stores a pointer to them in the
 * x11_display.
 */
void meta_x11_display_init_window_prop_hooks (MetaX11Display *x11_display);

/**
 * meta_x11_display_free_window_prop_hooks:
 * @x11_display:  The X11 display.
 * Frees the hooks used for the reload_property* functions
 * for a particular display.
 */
void meta_x11_display_free_window_prop_hooks (MetaX11Display *x11_display);
