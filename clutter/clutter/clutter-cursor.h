/*
 * Copyright 2013 Red Hat, Inc.
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
 * Author: Giovanni Campagna <gcampagn@redhat.com>
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-types.h"
#include "cogl/cogl.h"

#define CLUTTER_TYPE_CURSOR (clutter_cursor_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterCursor,
                          clutter_cursor,
                          CLUTTER, CURSOR,
                          GObject)

struct _ClutterCursorClass
{
  GObjectClass parent_class;

  void (* get_geometry) (ClutterCursor *cursor,
                         int           *width,
                         int           *height,
                         int           *hot_x,
                         int           *hot_y);

  CoglTexture * (* get_texture) (ClutterCursor *cursor);

  void (* invalidate) (ClutterCursor *cursor);

  gboolean (* is_animated) (ClutterCursor *cursor);

  void (* tick_frame) (ClutterCursor *cursor);

  unsigned int (* get_current_frame_time) (ClutterCursor *cursor);

  void (* prepare_at) (ClutterCursor *cursor,
                       float          best_scale,
                       float          x,
                       float          y);
};

CLUTTER_EXPORT
const char * clutter_cursor_type_to_name (ClutterCursorType cursor);

CLUTTER_EXPORT
ClutterCursorType clutter_cursor_get_cursor_type (ClutterCursor *cursor);

CLUTTER_EXPORT
void clutter_cursor_emit_texture_changed (ClutterCursor *cursor);

CLUTTER_EXPORT
void clutter_cursor_set_viewport_dst_size (ClutterCursor *cursor,
                                           int            dst_width,
                                           int            dst_height);

CLUTTER_EXPORT
void clutter_cursor_reset_viewport_dst_size (ClutterCursor *cursor);
