/*
 * Copyright (C) 2016 Red Hat Inc.
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

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <glib-object.h>

#include "cogl/cogl.h"
#include "clutter/clutter-macros.h"
#include "clutter/clutter-frame-clock.h"
#include "clutter/clutter-types.h"
#include "mtk/mtk.h"

#define CLUTTER_TYPE_STAGE_VIEW (clutter_stage_view_get_type ())
CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterStageView, clutter_stage_view,
                          CLUTTER, STAGE_VIEW,
                          GObject)

struct _ClutterStageViewClass
{
  GObjectClass parent_class;

  void (* setup_offscreen_transform) (ClutterStageView *view,
                                      CoglPipeline     *pipeline);

  void (* get_offscreen_transformation_matrix) (ClutterStageView  *view,
                                                graphene_matrix_t *matrix);

  void (* transform_rect_to_onscreen) (ClutterStageView   *view,
                                       const MtkRectangle *src_rect,
                                       int                 dst_width,
                                       int                 dst_height,
                                       MtkRectangle       *dst_rect);

  ClutterFrame * (* new_frame) (ClutterStageView *view);

  ClutterPaintFlag (* get_default_paint_flags) (ClutterStageView *view);
};

CLUTTER_EXPORT
void clutter_stage_view_destroy (ClutterStageView *view);

CLUTTER_EXPORT
void clutter_stage_view_get_layout (ClutterStageView *view,
                                    MtkRectangle     *rect);

CLUTTER_EXPORT
CoglFramebuffer *clutter_stage_view_get_framebuffer (ClutterStageView *view);
CLUTTER_EXPORT
CoglFramebuffer *clutter_stage_view_get_onscreen (ClutterStageView *view);
CLUTTER_EXPORT
void             clutter_stage_view_invalidate_offscreen_blit_pipeline (ClutterStageView *view);

CLUTTER_EXPORT
float clutter_stage_view_get_scale (ClutterStageView *view);

CLUTTER_EXPORT
void clutter_stage_view_get_offscreen_transformation_matrix (ClutterStageView  *view,
                                                             graphene_matrix_t *matrix);

CLUTTER_EXPORT
ClutterFrameClock * clutter_stage_view_get_frame_clock (ClutterStageView *view);

CLUTTER_EXPORT
CoglScanout * clutter_stage_view_peek_scanout (ClutterStageView *view);

CLUTTER_EXPORT
float clutter_stage_view_get_refresh_rate (ClutterStageView *view);

CLUTTER_EXPORT
gboolean clutter_stage_view_has_shadowfb (ClutterStageView *view);

CLUTTER_EXPORT
void clutter_stage_view_schedule_update_now (ClutterStageView *view);

CLUTTER_EXPORT
ClutterPaintFlag clutter_stage_view_get_default_paint_flags (ClutterStageView *view);
