/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
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
 *
 */

#ifndef __CLUTTER_MUTTER_H__
#define __CLUTTER_MUTTER_H__

#define __CLUTTER_H_INSIDE__

#include "clutter-backend.h"
#include "clutter-backend-private.h"
#include "clutter-damage-history.h"
#include "clutter-event-private.h"
#include "clutter-frame-private.h"
#include "clutter-input-device-private.h"
#include "clutter-input-pointer-a11y-private.h"
#include "clutter-macros.h"
#include "clutter-private.h"
#include "clutter-stage-private.h"
#include "clutter-stage-view.h"
#include "clutter-stage-view-private.h"
#include "clutter.h"

/* An epsilon larger than FLT_EPSILON that is useful when comparing coordinates
 * while ignoring floating point precision loss that might happen during
 * various matrix calculations. */
#define CLUTTER_COORDINATE_EPSILON (1.0 / 256.0)

typedef struct _ClutterMainContext ClutterContext;

typedef ClutterBackend * (* ClutterBackendConstructor) (gpointer user_data);

/**
 * clutter_context_new: (skip)
 */
CLUTTER_EXPORT
ClutterContext * clutter_context_new (ClutterBackendConstructor   backend_constructor,
                                      gpointer                    user_data,
                                      GError                    **error);

/**
 * clutter_context_free: (skip)
 */
CLUTTER_EXPORT
void clutter_context_free (ClutterContext *clutter_context);

/**
 * clutter_context_get_backend:
 *
 * Returns: (transfer none): The corresponding %ClutterBackend
 */
CLUTTER_EXPORT
ClutterBackend * clutter_context_get_backend (ClutterContext *clutter_context);

CLUTTER_EXPORT
GList * clutter_stage_peek_stage_views (ClutterStage *stage);

CLUTTER_EXPORT
gboolean clutter_actor_is_effectively_on_stage_view (ClutterActor     *self,
                                                     ClutterStageView *view);

CLUTTER_EXPORT
int64_t clutter_stage_get_frame_counter (ClutterStage *stage);

CLUTTER_EXPORT
void clutter_stage_capture_view_into (ClutterStage          *stage,
                                      ClutterStageView      *view,
                                      cairo_rectangle_int_t *rect,
                                      uint8_t               *data,
                                      int                    stride);

CLUTTER_EXPORT
void clutter_stage_clear_stage_views (ClutterStage *stage);

CLUTTER_EXPORT
void clutter_stage_view_assign_next_scanout (ClutterStageView *stage_view,
                                             CoglScanout      *scanout);

CLUTTER_EXPORT
gboolean clutter_actor_has_damage (ClutterActor *actor);

CLUTTER_EXPORT
gboolean clutter_actor_has_transitions (ClutterActor *actor);

CLUTTER_EXPORT
ClutterFrameClock * clutter_actor_pick_frame_clock (ClutterActor  *self,
                                                    ClutterActor **out_actor);
CLUTTER_EXPORT
gboolean clutter_seat_handle_event_post (ClutterSeat        *seat,
                                         const ClutterEvent *event);

CLUTTER_EXPORT
void clutter_stage_update_device (ClutterStage         *stage,
                                  ClutterInputDevice   *device,
                                  ClutterEventSequence *sequence,
                                  ClutterInputDevice   *source_device,
                                  graphene_point_t      point,
                                  uint32_t              time,
                                  ClutterActor         *new_actor,
                                  cairo_region_t       *region,
                                  gboolean              emit_crossing);

CLUTTER_EXPORT
gboolean clutter_stage_get_device_coords (ClutterStage         *stage,
                                          ClutterInputDevice   *device,
                                          ClutterEventSequence *sequence,
                                          graphene_point_t     *coords);
CLUTTER_EXPORT
void clutter_stage_repick_device (ClutterStage       *stage,
                                  ClutterInputDevice *device);

CLUTTER_EXPORT
void clutter_get_debug_flags (ClutterDebugFlag     *debug_flags,
                              ClutterDrawDebugFlag *draw_flags,
                              ClutterPickDebugFlag *pick_flags);

#undef __CLUTTER_H_INSIDE__

#endif /* __CLUTTER_MUTTER_H__ */
