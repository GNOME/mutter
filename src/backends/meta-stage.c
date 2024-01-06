/*
 * Copyright (C) 2014 Red Hat
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
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include "config.h"

#include "backends/meta-stage-private.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-cursor-tracker-private.h"
#include "clutter/clutter-mutter.h"
#include "meta/meta-backend.h"
#include "meta/meta-monitor-manager.h"
#include "meta/util.h"

#define N_WATCH_MODES 4

struct _MetaStageWatch
{
  ClutterStageView *view;
  MetaStageWatchFunc callback;
  gpointer user_data;
};

struct _MetaOverlay
{
  MetaStage *stage;

  gboolean is_visible;

  CoglPipeline *pipeline;
  CoglTexture *texture;

  MetaMonitorTransform buffer_transform;

  graphene_rect_t current_rect;
  graphene_rect_t previous_rect;
  gboolean previous_is_valid;
};

struct _MetaStage
{
  ClutterStage parent;

  MetaBackend *backend;

  GPtrArray *watchers[N_WATCH_MODES];

  GList *overlays;
  gboolean is_active;
};

G_DEFINE_TYPE (MetaStage, meta_stage, CLUTTER_TYPE_STAGE);

static MetaOverlay *
meta_overlay_new (MetaStage *stage)
{
  ClutterBackend *clutter_backend =
    meta_backend_get_clutter_backend (stage->backend);
  CoglContext *ctx = clutter_backend_get_cogl_context (clutter_backend);
  MetaOverlay *overlay;

  overlay = g_new0 (MetaOverlay, 1);
  overlay->stage = stage;
  overlay->pipeline = cogl_pipeline_new (ctx);

  return overlay;
}

static void
meta_overlay_free (MetaOverlay *overlay)
{
  if (overlay->pipeline)
    g_object_unref (overlay->pipeline);

  g_free (overlay);
}

static void
meta_overlay_set (MetaOverlay          *overlay,
                  CoglTexture          *texture,
                  graphene_rect_t      *rect,
                  MetaMonitorTransform  buffer_transform)
{
  if (overlay->texture != texture)
    {
      overlay->texture = texture;

      if (texture)
        cogl_pipeline_set_layer_texture (overlay->pipeline, 0, texture);
      else
        cogl_pipeline_set_layer_texture (overlay->pipeline, 0, NULL);
    }

  if (overlay->buffer_transform != buffer_transform)
    {
      graphene_matrix_t matrix;

      graphene_matrix_init_identity (&matrix);
      meta_monitor_transform_transform_matrix (buffer_transform,
                                               &matrix);
      cogl_pipeline_set_layer_matrix (overlay->pipeline, 0, &matrix);

      overlay->buffer_transform = buffer_transform;
    }

  overlay->current_rect = *rect;
}

static void
meta_overlay_paint (MetaOverlay         *overlay,
                    ClutterPaintContext *paint_context)
{
  CoglFramebuffer *framebuffer;

  if (!overlay->texture)
    return;

  if (!overlay->is_visible &&
      !(clutter_paint_context_get_paint_flags (paint_context) &
        CLUTTER_PAINT_FLAG_FORCE_CURSORS))
    return;

  framebuffer = clutter_paint_context_get_framebuffer (paint_context);
  cogl_framebuffer_draw_rectangle (framebuffer,
                                   overlay->pipeline,
                                   overlay->current_rect.origin.x,
                                   overlay->current_rect.origin.y,
                                   (overlay->current_rect.origin.x +
                                    overlay->current_rect.size.width),
                                   (overlay->current_rect.origin.y +
                                    overlay->current_rect.size.height));

  if (!graphene_rect_equal (&overlay->previous_rect, &overlay->current_rect))
    {
      overlay->previous_rect = overlay->current_rect;
      overlay->previous_is_valid = TRUE;
    }
}

static void
meta_stage_finalize (GObject *object)
{
  MetaStage *stage = META_STAGE (object);
  GList *l;
  int i;

  l = stage->overlays;
  while (l)
    {
      meta_overlay_free (l->data);
      l = g_list_delete_link (l, l);
    }

  for (i = 0; i < N_WATCH_MODES; i++)
    g_clear_pointer (&stage->watchers[i], g_ptr_array_unref);

  G_OBJECT_CLASS (meta_stage_parent_class)->finalize (object);
}

static void
notify_watchers_for_mode (MetaStage           *stage,
                          ClutterStageView    *view,
                          const MtkRegion     *redraw_clip,
                          ClutterFrame        *frame,
                          MetaStageWatchPhase  watch_phase)
{
  GPtrArray *watchers;
  int i;

  watchers = stage->watchers[watch_phase];

  for (i = 0; i < watchers->len; i++)
    {
      MetaStageWatch *watch = g_ptr_array_index (watchers, i);

      if (watch->view && view != watch->view)
        continue;

      watch->callback (stage, view, redraw_clip, frame, watch->user_data);
    }
}

static void
meta_stage_before_paint (ClutterStage     *stage,
                         ClutterStageView *view,
                         ClutterFrame     *frame)
{
  MetaStage *meta_stage = META_STAGE (stage);

  notify_watchers_for_mode (meta_stage, view, NULL, frame,
                            META_STAGE_WATCH_BEFORE_PAINT);
}

static void
meta_stage_paint (ClutterActor        *actor,
                  ClutterPaintContext *paint_context)
{
  MetaStage *stage = META_STAGE (actor);
  ClutterStageView *view;
  ClutterFrame *frame;
  const MtkRegion *redraw_clip;

  CLUTTER_ACTOR_CLASS (meta_stage_parent_class)->paint (actor, paint_context);

  frame = clutter_paint_context_get_frame (paint_context);
  view = clutter_paint_context_get_stage_view (paint_context);
  redraw_clip = clutter_paint_context_get_redraw_clip (paint_context);
  if (view)
    {
      notify_watchers_for_mode (stage, view, redraw_clip, frame,
                                META_STAGE_WATCH_AFTER_ACTOR_PAINT);
    }

  if ((clutter_paint_context_get_paint_flags (paint_context) &
       CLUTTER_PAINT_FLAG_FORCE_CURSORS))
    {
      MetaCursorTracker *cursor_tracker =
        meta_backend_get_cursor_tracker (stage->backend);

      meta_cursor_tracker_track_position (cursor_tracker);
    }

  if (!(clutter_paint_context_get_paint_flags (paint_context) &
        CLUTTER_PAINT_FLAG_NO_CURSORS))
    g_list_foreach (stage->overlays, (GFunc) meta_overlay_paint, paint_context);

  if ((clutter_paint_context_get_paint_flags (paint_context) &
       CLUTTER_PAINT_FLAG_FORCE_CURSORS))
    {
      MetaCursorTracker *cursor_tracker =
        meta_backend_get_cursor_tracker (stage->backend);

      meta_cursor_tracker_untrack_position (cursor_tracker);
    }

  if (view)
    {
      notify_watchers_for_mode (stage, view, redraw_clip, frame,
                                META_STAGE_WATCH_AFTER_OVERLAY_PAINT);
    }
}

static void
meta_stage_paint_view (ClutterStage     *stage,
                       ClutterStageView *view,
                       const MtkRegion  *redraw_clip,
                       ClutterFrame     *frame)
{
  MetaStage *meta_stage = META_STAGE (stage);

  CLUTTER_STAGE_CLASS (meta_stage_parent_class)->paint_view (stage, view,
                                                             redraw_clip,
                                                             frame);

  notify_watchers_for_mode (meta_stage, view, redraw_clip, frame,
                            META_STAGE_WATCH_AFTER_PAINT);
}

static void
meta_stage_activate (ClutterStage *actor)
{
  MetaStage *stage = META_STAGE (actor);

  CLUTTER_STAGE_CLASS (meta_stage_parent_class)->activate (actor);

  stage->is_active = TRUE;
}

static void
meta_stage_deactivate (ClutterStage *actor)
{
  MetaStage *stage = META_STAGE (actor);

  CLUTTER_STAGE_CLASS (meta_stage_parent_class)->deactivate (actor);

  stage->is_active = FALSE;
}

static void
on_power_save_changed (MetaMonitorManager        *monitor_manager,
                       MetaPowerSaveChangeReason  reason,
                       MetaStage                 *stage)
{
  if (meta_monitor_manager_get_power_save_mode (monitor_manager) ==
      META_POWER_SAVE_ON)
    clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));
}

static void
meta_stage_class_init (MetaStageClass *klass)
{
  ClutterStageClass *stage_class = (ClutterStageClass *) klass;
  ClutterActorClass *actor_class = (ClutterActorClass *) klass;
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = meta_stage_finalize;

  actor_class->paint = meta_stage_paint;

  stage_class->activate = meta_stage_activate;
  stage_class->deactivate = meta_stage_deactivate;
  stage_class->before_paint = meta_stage_before_paint;
  stage_class->paint_view = meta_stage_paint_view;
}

static void
key_focus_actor_changed (ClutterStage *stage,
                         GParamSpec   *param,
                         gpointer      user_data)
{
  ClutterActor *key_focus = clutter_stage_get_key_focus (stage);

  /* If there's no explicit key focus, clutter_stage_get_key_focus()
   * returns the stage.
   */
  if (key_focus == CLUTTER_ACTOR (stage))
    key_focus = NULL;

  meta_stage_set_active (META_STAGE (stage), key_focus != NULL);
}

static void
meta_stage_init (MetaStage *stage)
{
  int i;

  for (i = 0; i < N_WATCH_MODES; i++)
    stage->watchers[i] = g_ptr_array_new_with_free_func (g_free);

  if (meta_is_wayland_compositor ())
    {
      g_signal_connect (stage,
                        "notify::key-focus",
                        G_CALLBACK (key_focus_actor_changed), NULL);
    }
}

ClutterActor *
meta_stage_new (MetaBackend *backend)
{
  MetaStage *stage;
  MetaMonitorManager *monitor_manager;

  stage = g_object_new (META_TYPE_STAGE, NULL);
  stage->backend = backend;

  monitor_manager = meta_backend_get_monitor_manager (backend);
  g_signal_connect (monitor_manager, "power-save-mode-changed",
                    G_CALLBACK (on_power_save_changed),
                    stage);

  return CLUTTER_ACTOR (stage);
}

static void
queue_redraw_clutter_rect (MetaStage       *stage,
                           MetaOverlay     *overlay,
                           graphene_rect_t *rect)
{
  MtkRectangle clip = {
    .x = floorf (rect->origin.x),
    .y = floorf (rect->origin.y),
    .width = ceilf (rect->size.width),
    .height = ceilf (rect->size.height)
  };
  GList *l;

  /* Since we're flooring the coordinates, we need to enlarge the clip by the
   * difference between the actual coordinate and the floored value */
  clip.width += ceilf (rect->origin.x - clip.x) * 2;
  clip.height += ceilf (rect->origin.y - clip.y) * 2;

  for (l = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));
       l;
       l = l->next)
    {
      ClutterStageView *view = l->data;
      MtkRectangle view_layout;
      MtkRectangle view_clip;

      if (clutter_stage_view_get_default_paint_flags (view) &
          CLUTTER_PAINT_FLAG_NO_CURSORS)
        continue;

      clutter_stage_view_get_layout (view, &view_layout);

      if (mtk_rectangle_intersect (&clip, &view_layout, &view_clip))
        {
          clutter_stage_view_add_redraw_clip (view, &view_clip);
          clutter_stage_view_schedule_update (view);
        }
    }
}

static void
queue_redraw_for_overlay (MetaStage   *stage,
                          MetaOverlay *overlay)
{
  /* Clear the location the overlay was at before, if we need to. */
  if (overlay->previous_is_valid)
    {
      queue_redraw_clutter_rect (stage, overlay, &overlay->previous_rect);
      overlay->previous_is_valid = FALSE;
    }

  /* Draw the overlay at the new position */
  if (overlay->is_visible && overlay->texture)
    queue_redraw_clutter_rect (stage, overlay, &overlay->current_rect);
}

MetaOverlay *
meta_stage_create_cursor_overlay (MetaStage *stage)
{
  MetaOverlay *overlay;

  overlay = meta_overlay_new (stage);
  stage->overlays = g_list_prepend (stage->overlays, overlay);

  return overlay;
}

void
meta_stage_remove_cursor_overlay (MetaStage   *stage,
                                  MetaOverlay *overlay)
{
  GList *link;

  link = g_list_find (stage->overlays, overlay);
  if (!link)
    return;

  stage->overlays = g_list_delete_link (stage->overlays, link);
  meta_overlay_free (overlay);
}

void
meta_stage_update_cursor_overlay (MetaStage            *stage,
                                  MetaOverlay          *overlay,
                                  CoglTexture          *texture,
                                  graphene_rect_t      *rect,
                                  MetaMonitorTransform  buffer_transform)
{
  meta_overlay_set (overlay, texture, rect, buffer_transform);
  queue_redraw_for_overlay (stage, overlay);
}

void
meta_overlay_set_visible (MetaOverlay *overlay,
                          gboolean     is_visible)
{
  if (overlay->is_visible == is_visible)
    return;

  overlay->is_visible = is_visible;
  queue_redraw_for_overlay (overlay->stage, overlay);
}

void
meta_stage_set_active (MetaStage *stage,
                       gboolean   is_active)
{
  if (stage->is_active == is_active)
    return;

  if (is_active)
    g_signal_emit_by_name (CLUTTER_STAGE (stage), "activate");
  else
    g_signal_emit_by_name (CLUTTER_STAGE (stage), "deactivate");
}

MetaStageWatch *
meta_stage_watch_view (MetaStage           *stage,
                       ClutterStageView    *view,
                       MetaStageWatchPhase  watch_phase,
                       MetaStageWatchFunc   callback,
                       gpointer             user_data)
{
  MetaStageWatch *watch;
  GPtrArray *watchers;

  watch = g_new0 (MetaStageWatch, 1);
  watch->view = view;
  watch->callback = callback;
  watch->user_data = user_data;

  watchers = stage->watchers[watch_phase];
  g_ptr_array_add (watchers, watch);

  return watch;
}

void
meta_stage_remove_watch (MetaStage      *stage,
                         MetaStageWatch *watch)
{
  GPtrArray *watchers;
  gboolean removed = FALSE;
  int i;

  for (i = 0; i < N_WATCH_MODES; i++)
    {
      watchers = stage->watchers[i];
      removed = g_ptr_array_remove_fast (watchers, watch);

      if (removed)
        break;
    }

  g_assert (removed);
}
