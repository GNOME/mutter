/*
 * Copyright (C) 2019 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "clutter/clutter-paint-context.h"

ClutterPaintContext *
clutter_paint_context_new_for_view (ClutterStageView *view,
                                    const MtkRegion  *redraw_clip,
                                    GArray           *clip_frusta,
                                    ClutterPaintFlag  paint_flags);

gboolean clutter_paint_context_is_drawing_off_stage (ClutterPaintContext *paint_context);

CoglFramebuffer * clutter_paint_context_get_base_framebuffer (ClutterPaintContext *paint_context);

const GArray *
clutter_paint_context_get_clip_frusta (ClutterPaintContext *paint_context);

void clutter_paint_context_assign_frame (ClutterPaintContext *paint_context,
                                         ClutterFrame        *frame);

void clutter_paint_context_push_target_color_state (ClutterPaintContext *paint_context,
                                                    ClutterColorState   *color_state);

void clutter_paint_context_pop_target_color_state (ClutterPaintContext *paint_context);
