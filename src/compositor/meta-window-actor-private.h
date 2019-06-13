/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#ifndef META_WINDOW_ACTOR_PRIVATE_H
#define META_WINDOW_ACTOR_PRIVATE_H

#include <X11/extensions/Xdamage.h>

#include "compositor/meta-plugin-manager.h"
#include "compositor/meta-surface-actor.h"
#include "meta/compositor-mutter.h"

struct _MetaWindowActorClass
{
  ClutterActorClass parent;

  void (*frame_complete) (MetaWindowActor  *actor,
                          ClutterFrameInfo *frame_info,
                          int64_t           presentation_time);

  void (*assign_surface_actor) (MetaWindowActor  *actor,
                                MetaSurfaceActor *surface_actor);

  void (*queue_frame_drawn) (MetaWindowActor *actor,
                             gboolean         skip_sync_delay);

  void (*pre_paint) (MetaWindowActor *actor);
  void (*post_paint) (MetaWindowActor *actor);
  void (*queue_destroy) (MetaWindowActor *actor);
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
                                      MetaRectangle   *old_frame_rect,
                                      MetaRectangle   *old_buffer_rect);

void meta_window_actor_process_x11_damage (MetaWindowActor    *self,
                                           XDamageNotifyEvent *event);

void meta_window_actor_pre_paint      (MetaWindowActor    *self);
void meta_window_actor_post_paint     (MetaWindowActor    *self);
void meta_window_actor_frame_complete (MetaWindowActor    *self,
                                       ClutterFrameInfo   *frame_info,
                                       gint64              presentation_time);

void meta_window_actor_invalidate_shadow (MetaWindowActor *self);

void meta_window_actor_get_shape_bounds (MetaWindowActor       *self,
                                          cairo_rectangle_int_t *bounds);

gboolean meta_window_actor_should_unredirect   (MetaWindowActor *self);
void     meta_window_actor_set_unredirected    (MetaWindowActor *self,
                                                gboolean         unredirected);

gboolean meta_window_actor_effect_in_progress  (MetaWindowActor *self);

MetaWindowActorChanges meta_window_actor_sync_actor_geometry (MetaWindowActor *self,
                                                              gboolean         did_placement);

void     meta_window_actor_update_shape        (MetaWindowActor *self);
void     meta_window_actor_update_opacity      (MetaWindowActor *self);
void     meta_window_actor_mapped              (MetaWindowActor *self);
void     meta_window_actor_unmapped            (MetaWindowActor *self);
void     meta_window_actor_sync_updates_frozen (MetaWindowActor *self);
void     meta_window_actor_queue_frame_drawn   (MetaWindowActor *self,
                                                gboolean         no_delay_frame);

void meta_window_actor_effect_completed (MetaWindowActor  *actor,
                                         MetaPluginEffect  event);

MetaSurfaceActor *meta_window_actor_get_surface (MetaWindowActor *self);

void meta_window_actor_assign_surface_actor (MetaWindowActor  *self,
                                             MetaSurfaceActor *surface_actor);

MetaWindowActor *meta_window_actor_from_window (MetaWindow *window);
MetaWindowActor *meta_window_actor_from_actor (ClutterActor *actor);

#endif /* META_WINDOW_ACTOR_PRIVATE_H */
