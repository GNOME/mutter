/*
 * Copyright (C) 2026 Red Hat Inc.
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
 */

#pragma once

#include <glib.h>
#include <X11/Xlib.h>

typedef gboolean (* X11EventSourceCallback) (XEvent   *xevent,
                                             gpointer  user_data);

GSource *
x11_event_source_new (Display                *xdisplay,
                      X11EventSourceCallback  callback,
                      gpointer                user_data);

void set_x11_window_title (Display    *xdisplay,
                           Window      window,
                           const char *title);
