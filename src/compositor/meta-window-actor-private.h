/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#pragma once

#include "compositor/meta-plugin-manager.h"
#include "compositor/meta-surface-actor.h"
#include "meta/compositor-mutter.h"

struct _MetaWindowActorClass
{
  ClutterActorClass parent;

  void (*frame_complete) (MetaWindowActor  *actor,
                          ClutterFrameInfo *frame_info,
                          int64_t           presentation_time);

  MetaSurfaceActor * (*get_scanout_candidate) (MetaWindowActor *actor);

  void (*assign_surface_actor) (MetaWindowActor  *actor,
                                MetaSurfaceActor *surface_actor);

  void (*queue_frame_drawn) (MetaWindowActor *actor,
                             gboolean         skip_sync_delay);

  void (*before_paint) (MetaWindowActor  *actor,
                        ClutterStageView *stage_view);
  void (*after_paint) (MetaWindowActor  *actor,
                       ClutterStageView *stage_view);

  void (*queue_destroy) (MetaWindowActor *actor);
  void (*set_frozen) (MetaWindowActor *actor,
                      gboolean         frozen);
  void (*update_regions) (MetaWindowActor *actor);
  gboolean (*can_freeze_commits) (MetaWindowActor *actor);

  void (*sync_geometry) (MetaWindowActor *actor);
  gboolean (*is_single_surface_actor) (MetaWindowActor *actor);
};

typedef enum
{
  META_WINDOW_ACTOR_CHANGE_SIZE     = 1 << 0,
  META_WINDOW_ACTOR_CHANGE_POSITION = 1 << 1
} MetaWindowActorChanges;

void meta_window_actor_queue_destroy   (MetaWindowActor *self);

void meta_window_actor_show (MetaWindowActor *self,
                             MetaCompEffect   effect);
void meta_window_actor_hide (MetaWindowActor *self,
                             MetaCompEffect   effect);

void meta_window_actor_size_change   (MetaWindowActor *self,
                                      MetaSizeChange   which_change,
                                      MtkRectangle    *old_frame_rect,
                                      MtkRectangle    *old_buffer_rect);

void meta_window_actor_before_paint   (MetaWindowActor    *self,
                                       ClutterStageView   *stage_view);
void meta_window_actor_after_paint    (MetaWindowActor    *self,
                                       ClutterStageView   *stage_view);
void meta_window_actor_frame_complete (MetaWindowActor    *self,
                                       ClutterFrameInfo   *frame_info,
                                       gint64              presentation_time);

META_EXPORT_TEST
gboolean meta_window_actor_effect_in_progress  (MetaWindowActor *self);

MetaWindowActorChanges meta_window_actor_sync_actor_geometry (MetaWindowActor *self,
                                                              gboolean         did_placement);

void     meta_window_actor_update_opacity      (MetaWindowActor *self);
void     meta_window_actor_mapped              (MetaWindowActor *self);
void     meta_window_actor_unmapped            (MetaWindowActor *self);
void     meta_window_actor_sync_updates_frozen (MetaWindowActor *self);

META_EXPORT_TEST
void     meta_window_actor_queue_frame_drawn   (MetaWindowActor *self,
                                                gboolean         no_delay_frame);

void meta_window_actor_effect_completed (MetaWindowActor  *actor,
                                         MetaPluginEffect  event);

MetaSurfaceActor *meta_window_actor_get_surface (MetaWindowActor *self);

MetaSurfaceActor *meta_window_actor_get_scanout_candidate (MetaWindowActor *self);

void meta_window_actor_assign_surface_actor (MetaWindowActor  *self,
                                             MetaSurfaceActor *surface_actor);

META_EXPORT_TEST
MetaWindowActor *meta_window_actor_from_window (MetaWindow *window);

META_EXPORT_TEST
MetaWindowActor *meta_window_actor_from_actor (ClutterActor *actor);

void meta_window_actor_set_geometry_scale (MetaWindowActor *window_actor,
                                           int              geometry_scale);

int meta_window_actor_get_geometry_scale (MetaWindowActor *window_actor);

gboolean meta_window_actor_is_streaming (MetaWindowActor *window_actor);

void meta_window_actor_notify_damaged (MetaWindowActor *window_actor);

gboolean meta_window_actor_is_frozen (MetaWindowActor *self);

gboolean meta_window_actor_is_opaque (MetaWindowActor *self);

void meta_window_actor_update_regions (MetaWindowActor *self);

gboolean meta_window_actor_can_freeze_commits (MetaWindowActor *self);

void meta_window_actor_add_surface_actor (MetaWindowActor  *window_actor,
                                          MetaSurfaceActor *surface_actor);

void meta_window_actor_remove_surface_actor (MetaWindowActor  *window_actor,
                                             MetaSurfaceActor *surface_actor);
