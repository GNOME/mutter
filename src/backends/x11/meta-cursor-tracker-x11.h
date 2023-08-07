/*
 * Copyright (C) 2020 Red Hat
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

#include "backends/meta-cursor-tracker-private.h"

#define META_TYPE_CURSOR_TRACKER_X11 (meta_cursor_tracker_x11_get_type ())
G_DECLARE_FINAL_TYPE (MetaCursorTrackerX11, meta_cursor_tracker_x11,
                      META, CURSOR_TRACKER_X11,
                      MetaCursorTracker)

gboolean meta_cursor_tracker_x11_handle_xevent (MetaCursorTrackerX11 *tracker_x11,
                                                XEvent               *xevent);
