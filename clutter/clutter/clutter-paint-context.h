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

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <glib-object.h>

#include "clutter/clutter-macros.h"
#include "clutter/clutter-stage-view.h"

typedef struct _ClutterPaintContext ClutterPaintContext;

typedef enum _ClutterPaintFlag
{
  CLUTTER_PAINT_FLAG_NONE = 0,
  CLUTTER_PAINT_FLAG_NO_CURSORS = 1 << 0,
  CLUTTER_PAINT_FLAG_FORCE_CURSORS = 1 << 1,
  CLUTTER_PAINT_FLAG_CLEAR = 1 << 2,
} ClutterPaintFlag;

#define CLUTTER_TYPE_PAINT_CONTEXT (clutter_paint_context_get_type ())

CLUTTER_EXPORT
GType clutter_paint_context_get_type (void);

CLUTTER_EXPORT
ClutterPaintContext * clutter_paint_context_new_for_framebuffer (CoglFramebuffer   *framebuffer,
                                                                 const MtkRegion   *redraw_clip,
                                                                 ClutterPaintFlag   paint_flags,
                                                                 ClutterColorState *color_state);

CLUTTER_EXPORT
ClutterPaintContext * clutter_paint_context_ref (ClutterPaintContext *paint_context);

CLUTTER_EXPORT
void clutter_paint_context_unref (ClutterPaintContext *paint_context);

CLUTTER_EXPORT
void clutter_paint_context_destroy (ClutterPaintContext *paint_context);

CLUTTER_EXPORT
CoglFramebuffer * clutter_paint_context_get_framebuffer (ClutterPaintContext *paint_context);

CLUTTER_EXPORT
ClutterStageView * clutter_paint_context_get_stage_view (ClutterPaintContext *paint_context);

CLUTTER_EXPORT
void clutter_paint_context_push_framebuffer (ClutterPaintContext *paint_context,
                                             CoglFramebuffer     *framebuffer);

CLUTTER_EXPORT
void clutter_paint_context_pop_framebuffer (ClutterPaintContext *paint_context);

CLUTTER_EXPORT
const MtkRegion * clutter_paint_context_get_redraw_clip (ClutterPaintContext *paint_context);

CLUTTER_EXPORT
ClutterPaintFlag clutter_paint_context_get_paint_flags (ClutterPaintContext *paint_context);

CLUTTER_EXPORT
ClutterFrame * clutter_paint_context_get_frame (ClutterPaintContext *paint_context);

CLUTTER_EXPORT
void clutter_paint_context_push_color_state (ClutterPaintContext *paint_context,
                                             ClutterColorState   *color_state);

CLUTTER_EXPORT
void clutter_paint_context_pop_color_state (ClutterPaintContext *paint_context);

CLUTTER_EXPORT
ClutterColorState * clutter_paint_context_get_target_color_state (ClutterPaintContext *paint_context);

CLUTTER_EXPORT
ClutterColorState * clutter_paint_context_get_color_state (ClutterPaintContext *paint_context);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterPaintContext, clutter_paint_context_unref)
