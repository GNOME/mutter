/*
 * Copyright (C) 2022 Red Hat Inc.
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

#include <gtk/gtk.h>
#include <X11/Xlib.h>

#define META_TYPE_FRAME (meta_frame_get_type ())
G_DECLARE_FINAL_TYPE (MetaFrame, meta_frame, META, FRAME, GtkWindow)

GtkWidget * meta_frame_new (Window window);

void meta_frame_handle_xevent (MetaFrame *frame,
                               Window     window,
                               XEvent    *xevent);
