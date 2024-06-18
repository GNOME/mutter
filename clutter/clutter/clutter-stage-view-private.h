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

#include "clutter/clutter-stage-view.h"
#include "clutter/clutter-types.h"
#include "mtk/mtk.h"

CLUTTER_EXPORT
void clutter_stage_view_after_paint (ClutterStageView *view,
                                     MtkRegion        *redraw_clip);

CLUTTER_EXPORT
void clutter_stage_view_before_swap_buffer (ClutterStageView *view,
                                            const MtkRegion  *swap_region);

gboolean clutter_stage_view_is_dirty_viewport (ClutterStageView *view);

void clutter_stage_view_invalidate_viewport (ClutterStageView *view);

void clutter_stage_view_set_viewport (ClutterStageView *view,
                                      float             x,
                                      float             y,
                                      float             width,
                                      float             height);

gboolean clutter_stage_view_is_dirty_projection (ClutterStageView *view);

void clutter_stage_view_invalidate_projection (ClutterStageView *view);

void clutter_stage_view_set_projection (ClutterStageView        *view,
                                        const graphene_matrix_t *matrix);

CLUTTER_EXPORT
void clutter_stage_view_add_redraw_clip (ClutterStageView   *view,
                                         const MtkRectangle *clip);

gboolean clutter_stage_view_has_full_redraw_clip (ClutterStageView *view);

gboolean clutter_stage_view_has_redraw_clip (ClutterStageView *view);

CLUTTER_EXPORT
MtkRegion * clutter_stage_view_take_accumulated_redraw_clip (ClutterStageView *view);

CLUTTER_EXPORT
void clutter_stage_view_accumulate_redraw_clip (ClutterStageView *view);

CLUTTER_EXPORT
CoglScanout * clutter_stage_view_take_scanout (ClutterStageView *view);

CLUTTER_EXPORT
void clutter_stage_view_transform_rect_to_onscreen (ClutterStageView   *view,
                                                    const MtkRectangle *src_rect,
                                                    int                 dst_width,
                                                    int                 dst_height,
                                                    MtkRectangle       *dst_rect);

CLUTTER_EXPORT
void clutter_stage_view_schedule_update (ClutterStageView *view);

CLUTTER_EXPORT
void clutter_stage_view_notify_presented (ClutterStageView *view,
                                          ClutterFrameInfo *frame_info);

CLUTTER_EXPORT
void clutter_stage_view_notify_ready (ClutterStageView *view);

void clutter_stage_view_invalidate_input_devices (ClutterStageView *view);
