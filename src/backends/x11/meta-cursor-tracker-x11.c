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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#include "config.h"

#include "backends/x11/meta-cursor-tracker-x11.h"

struct _MetaCursorTrackerX11
{
  MetaCursorTracker parent;
};

G_DEFINE_TYPE (MetaCursorTrackerX11, meta_cursor_tracker_x11,
               META_TYPE_CURSOR_TRACKER)

static void
meta_cursor_tracker_x11_init (MetaCursorTrackerX11 *tracker_x11)
{
}

static void
meta_cursor_tracker_x11_class_init (MetaCursorTrackerX11Class *klass)
{
}
