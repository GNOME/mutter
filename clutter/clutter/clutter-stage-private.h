/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2010  Intel Corporation.
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

#include "clutter/clutter-grab.h"
#include "clutter/clutter-stage-window.h"
#include "clutter/clutter-stage.h"
#include "clutter/clutter-input-device.h"
#include "clutter/clutter-private.h"

#include "cogl/cogl.h"

G_BEGIN_DECLS

typedef gboolean (* ClutterEventHandler) (const ClutterEvent *event,
                                          gpointer            user_data);
typedef enum
{
  CLUTTER_DEVICE_UPDATE_NONE = 0,
  CLUTTER_DEVICE_UPDATE_EMIT_CROSSING = 1 << 0,
  CLUTTER_DEVICE_UPDATE_IGNORE_CACHE = 1 << 1,
} ClutterDeviceUpdateFlags;

/* stage */
CLUTTER_EXPORT
void                clutter_stage_paint_view             (ClutterStage      *stage,
                                                          ClutterStageView  *view,
                                                          const MtkRegion   *redraw_clip,
                                                          ClutterFrame      *frame);

void                clutter_stage_emit_before_update     (ClutterStage          *stage,
                                                          ClutterStageView      *view,
                                                          ClutterFrame          *frame);
void                clutter_stage_emit_prepare_frame     (ClutterStage          *stage,
                                                          ClutterStageView      *view,
                                                          ClutterFrame          *frame);
void                clutter_stage_emit_before_paint      (ClutterStage          *stage,
                                                          ClutterStageView      *view,
                                                          ClutterFrame          *frame);
void                clutter_stage_emit_after_paint       (ClutterStage          *stage,
                                                          ClutterStageView      *view,
                                                          ClutterFrame          *frame);
void                clutter_stage_after_update           (ClutterStage          *stage,
                                                          ClutterStageView      *view,
                                                          ClutterFrame          *frame);

CLUTTER_EXPORT
void                _clutter_stage_set_window            (ClutterStage          *stage,
                                                          ClutterStageWindow    *stage_window);
CLUTTER_EXPORT
ClutterStageWindow *_clutter_stage_get_window            (ClutterStage          *stage);
void                _clutter_stage_get_projection_matrix (ClutterStage          *stage,
                                                          graphene_matrix_t     *projection);
void                _clutter_stage_dirty_projection      (ClutterStage          *stage);
void                _clutter_stage_get_viewport          (ClutterStage          *stage,
                                                          float                 *x,
                                                          float                 *y,
                                                          float                 *width,
                                                          float                 *height);
void                _clutter_stage_dirty_viewport        (ClutterStage          *stage);
CLUTTER_EXPORT
void                _clutter_stage_maybe_setup_viewport  (ClutterStage          *stage,
                                                          ClutterStageView      *view);
void                clutter_stage_maybe_relayout         (ClutterActor          *stage);
GSList *            clutter_stage_find_updated_devices   (ClutterStage          *stage,
                                                          ClutterStageView      *view);
void                clutter_stage_update_devices         (ClutterStage          *stage,
                                                          GSList                *devices);
void                clutter_stage_finish_layout          (ClutterStage          *stage);

CLUTTER_EXPORT
void     _clutter_stage_queue_event                       (ClutterStage *stage,
                                                           ClutterEvent *event,
                                                           gboolean      copy_event);
void     _clutter_stage_process_queued_events             (ClutterStage *stage);

void            clutter_stage_presented                 (ClutterStage      *stage,
                                                         ClutterStageView  *view,
                                                         ClutterFrameInfo  *frame_info);

void            clutter_stage_queue_actor_relayout      (ClutterStage *stage,
                                                         ClutterActor *actor);

void clutter_stage_dequeue_actor_relayout (ClutterStage *stage,
                                           ClutterActor *actor);

GList * clutter_stage_get_views_for_rect (ClutterStage          *stage,
                                          const graphene_rect_t *rect);

void clutter_stage_set_actor_needs_immediate_relayout (ClutterStage *stage);

void clutter_stage_remove_device_entry (ClutterStage         *self,
                                        ClutterInputDevice   *device,
                                        ClutterEventSequence *sequence);
ClutterActor * clutter_stage_pick_and_update_device (ClutterStage             *stage,
                                                     ClutterInputDevice       *device,
                                                     ClutterEventSequence     *sequence,
                                                     ClutterInputDevice       *source_device,
                                                     ClutterDeviceUpdateFlags  flags,
                                                     graphene_point_t          point,
                                                     uint32_t                  time_ms);

void clutter_stage_unlink_grab (ClutterStage *self,
                                ClutterGrab  *grab);

void clutter_stage_invalidate_focus (ClutterStage *self,
                                     ClutterActor *actor);

void clutter_stage_maybe_invalidate_focus (ClutterStage *self,
                                           ClutterActor *actor);

void clutter_stage_emit_event (ClutterStage       *self,
                               const ClutterEvent *event);

void clutter_stage_maybe_lost_implicit_grab (ClutterStage         *self,
                                             ClutterInputDevice   *device,
                                             ClutterEventSequence *sequence);

void clutter_stage_implicit_grab_actor_unmapped (ClutterStage *self,
                                                 ClutterActor *actor);

CLUTTER_EXPORT_TEST
void clutter_stage_notify_action_implicit_grab (ClutterStage         *self,
                                                ClutterInputDevice   *device,
                                                ClutterEventSequence *sequence);

void clutter_stage_add_to_redraw_clip (ClutterStage       *self,
                                       ClutterPaintVolume *clip);

CLUTTER_EXPORT
ClutterGrab * clutter_stage_grab_input_only (ClutterStage        *self,
                                             ClutterEventHandler  handler,
                                             gpointer             user_data,
                                             GDestroyNotify       user_data_destroy);

void clutter_stage_invalidate_devices (ClutterStage *stage);

G_END_DECLS
