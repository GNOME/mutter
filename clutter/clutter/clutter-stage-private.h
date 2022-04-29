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

#ifndef __CLUTTER_STAGE_PRIVATE_H__
#define __CLUTTER_STAGE_PRIVATE_H__

#include <clutter/clutter-grab.h>
#include <clutter/clutter-stage-window.h>
#include <clutter/clutter-stage.h>
#include <clutter/clutter-input-device.h>
#include <clutter/clutter-private.h>

#include <cogl/cogl.h>

G_BEGIN_DECLS

typedef enum
{
  CLUTTER_DEVICE_UPDATE_NONE = 0,
  CLUTTER_DEVICE_UPDATE_EMIT_CROSSING = 1 << 0,
  CLUTTER_DEVICE_UPDATE_IGNORE_CACHE = 1 << 1,
} ClutterDeviceUpdateFlags;

/* stage */
ClutterStageWindow *_clutter_stage_get_default_window    (void);

CLUTTER_EXPORT
void                clutter_stage_paint_view             (ClutterStage          *stage,
                                                          ClutterStageView      *view,
                                                          const cairo_region_t  *redraw_clip);

void                clutter_stage_emit_before_update     (ClutterStage          *stage,
                                                          ClutterStageView      *view);
void                clutter_stage_emit_prepare_frame     (ClutterStage          *stage,
                                                          ClutterStageView      *view);
void                clutter_stage_emit_before_paint      (ClutterStage          *stage,
                                                          ClutterStageView      *view);
void                clutter_stage_emit_after_paint       (ClutterStage          *stage,
                                                          ClutterStageView      *view);
void                clutter_stage_emit_after_update      (ClutterStage          *stage,
                                                          ClutterStageView      *view);

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
void                clutter_stage_maybe_finish_queue_redraws (ClutterStage      *stage);
GSList *            clutter_stage_find_updated_devices   (ClutterStage          *stage,
                                                          ClutterStageView      *view);
void                clutter_stage_update_devices         (ClutterStage          *stage,
                                                          GSList                *devices);
void                clutter_stage_finish_layout          (ClutterStage          *stage);

CLUTTER_EXPORT
void     _clutter_stage_queue_event                       (ClutterStage *stage,
                                                           ClutterEvent *event,
                                                           gboolean      copy_event);
gboolean _clutter_stage_has_queued_events                 (ClutterStage *stage);
void     _clutter_stage_process_queued_events             (ClutterStage *stage);
void     _clutter_stage_update_input_devices              (ClutterStage *stage);
gboolean _clutter_stage_has_full_redraw_queued            (ClutterStage *stage);

ClutterPaintVolume *_clutter_stage_paint_volume_stack_allocate (ClutterStage *stage);
void                _clutter_stage_paint_volume_stack_free_all (ClutterStage *stage);

void clutter_stage_queue_actor_redraw (ClutterStage             *stage,
                                       ClutterActor             *actor,
                                       const ClutterPaintVolume *clip);

void clutter_stage_dequeue_actor_redraw (ClutterStage *stage,
                                         ClutterActor *actor);

void            _clutter_stage_add_pointer_drag_actor    (ClutterStage       *stage,
                                                          ClutterInputDevice *device,
                                                          ClutterActor       *actor);
ClutterActor *  _clutter_stage_get_pointer_drag_actor    (ClutterStage       *stage,
                                                          ClutterInputDevice *device);
void            _clutter_stage_remove_pointer_drag_actor (ClutterStage       *stage,
                                                          ClutterInputDevice *device);

void            _clutter_stage_add_touch_drag_actor    (ClutterStage         *stage,
                                                        ClutterEventSequence *sequence,
                                                        ClutterActor         *actor);
ClutterActor *  _clutter_stage_get_touch_drag_actor    (ClutterStage         *stage,
                                                        ClutterEventSequence *sequence);
void            _clutter_stage_remove_touch_drag_actor (ClutterStage         *stage,
                                                        ClutterEventSequence *sequence);

void                    _clutter_stage_set_scale_factor (ClutterStage      *stage,
                                                         int                factor);

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

void clutter_stage_update_device_entry (ClutterStage         *self,
                                        ClutterInputDevice   *device,
                                        ClutterEventSequence *sequence,
                                        graphene_point_t      coords,
                                        ClutterActor         *actor,
                                        cairo_region_t       *clear_area);

void clutter_stage_remove_device_entry (ClutterStage         *self,
                                        ClutterInputDevice   *device,
                                        ClutterEventSequence *sequence);
ClutterActor * clutter_stage_pick_and_update_device (ClutterStage             *stage,
                                                     ClutterInputDevice       *device,
                                                     ClutterEventSequence     *sequence,
                                                     ClutterDeviceUpdateFlags  flags,
                                                     graphene_point_t          point,
                                                     uint32_t                  time_ms);

void clutter_stage_unlink_grab (ClutterStage *self,
                                ClutterGrab  *grab);

void clutter_stage_invalidate_focus (ClutterStage *self,
                                     ClutterActor *actor);

G_END_DECLS

#endif /* __CLUTTER_STAGE_PRIVATE_H__ */
