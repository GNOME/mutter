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

  CoglTexture * (* get_texture) (ClutterCursor *cursor,
                                 int           *hot_x,
                                 int           *hot_y);

  void (* invalidate) (ClutterCursor *cursor);

  gboolean (* realize_texture) (ClutterCursor *cursor);

  gboolean (* is_animated) (ClutterCursor *cursor);

  void (* tick_frame) (ClutterCursor *cursor);

  unsigned int (* get_current_frame_time) (ClutterCursor *cursor);

  void (* prepare_at) (ClutterCursor *cursor,
                       float          best_scale,
                       int            x,
                       int            y);
};
