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

#include "clutter-cursor.h"

CLUTTER_EXPORT
void clutter_cursor_prepare_at (ClutterCursor *cursor,
                                float          best_scale,
                                int            x,
                                int            y);

CLUTTER_EXPORT
void clutter_cursor_invalidate (ClutterCursor *cursor);

CLUTTER_EXPORT
gboolean clutter_cursor_realize_texture (ClutterCursor *cursor);

CLUTTER_EXPORT
void clutter_cursor_set_texture_scale (ClutterCursor *cursor,
                                       float          scale);

CLUTTER_EXPORT
void clutter_cursor_set_texture_transform (ClutterCursor       *cursor,
                                           MtkMonitorTransform  transform);

CLUTTER_EXPORT
void clutter_cursor_set_viewport_src_rect (ClutterCursor         *cursor,
                                           const graphene_rect_t *src_rect);

CLUTTER_EXPORT
void clutter_cursor_reset_viewport_src_rect (ClutterCursor *cursor);

CLUTTER_EXPORT
void clutter_cursor_set_viewport_dst_size (ClutterCursor *cursor,
                                           int            dst_width,
                                           int            dst_height);

CLUTTER_EXPORT
void clutter_cursor_reset_viewport_dst_size (ClutterCursor *cursor);

CLUTTER_EXPORT
CoglTexture * clutter_cursor_get_texture (ClutterCursor *cursor,
                                          int           *hot_x,
                                          int           *hot_y);

CLUTTER_EXPORT
float clutter_cursor_get_texture_scale (ClutterCursor *cursor);

CLUTTER_EXPORT
MtkMonitorTransform clutter_cursor_get_texture_transform (ClutterCursor *cursor);

CLUTTER_EXPORT
const graphene_rect_t * clutter_cursor_get_viewport_src_rect (ClutterCursor *cursor);

CLUTTER_EXPORT
gboolean clutter_cursor_get_viewport_dst_size (ClutterCursor *cursor,
                                               int           *dst_width,
                                               int           *dst_height);

CLUTTER_EXPORT
gboolean clutter_cursor_is_animated (ClutterCursor *cursor);

CLUTTER_EXPORT
void clutter_cursor_tick_frame (ClutterCursor *cursor);

CLUTTER_EXPORT
unsigned int clutter_cursor_get_current_frame_time (ClutterCursor *cursor);

CLUTTER_EXPORT
ClutterColorState * clutter_cursor_get_color_state (ClutterCursor *cursor);

CLUTTER_EXPORT
void clutter_cursor_emit_texture_changed (ClutterCursor *cursor);
