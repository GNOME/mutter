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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include "config.h"

#include "backends/meta-stage-private.h"

#include "backends/meta-backend-private.h"
#include "clutter/clutter-mutter.h"
#include "meta/meta-backend.h"
#include "meta/meta-monitor-manager.h"
#include "meta/util.h"

enum
{
  ACTORS_PAINTED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

struct _MetaOverlay
{
  gboolean enabled;

  CoglPipeline *pipeline;
  CoglTexture *texture;

  ClutterRect current_rect;
  ClutterRect previous_rect;
  gboolean previous_is_valid;
};

struct _MetaStage
{
  ClutterStage parent;

  GList *overlays;
  gboolean is_active;
};

G_DEFINE_TYPE (MetaStage, meta_stage, CLUTTER_TYPE_STAGE);

static MetaOverlay *
meta_overlay_new (void)
{
  MetaOverlay *overlay;
  CoglContext *ctx = clutter_backend_get_cogl_context (clutter_get_default_backend ());

  overlay = g_slice_new0 (MetaOverlay);
  overlay->pipeline = cogl_pipeline_new (ctx);

  return overlay;
}

static void
meta_overlay_free (MetaOverlay *overlay)
{
  if (overlay->pipeline)
    cogl_object_unref (overlay->pipeline);

  g_slice_free (MetaOverlay, overlay);
}

static void
meta_overlay_set (MetaOverlay *overlay,
                  CoglTexture *texture,
                  ClutterRect *rect)
{
  if (overlay->texture != texture)
    {
      overlay->texture = texture;

      if (texture)
        {
          cogl_pipeline_set_layer_texture (overlay->pipeline, 0, texture);
          overlay->enabled = TRUE;
        }
      else
        {
          cogl_pipeline_set_layer_texture (overlay->pipeline, 0, NULL);
          overlay->enabled = FALSE;
        }
    }

  overlay->current_rect = *rect;
}

static void
meta_overlay_paint (MetaOverlay *overlay)
{
  if (!overlay->enabled)
    return;

  g_assert (meta_is_wayland_compositor ());

  cogl_framebuffer_draw_rectangle (cogl_get_draw_framebuffer (),
                                   overlay->pipeline,
                                   overlay->current_rect.origin.x,
                                   overlay->current_rect.origin.y,
                                   (overlay->current_rect.origin.x +
                                    overlay->current_rect.size.width),
                                   (overlay->current_rect.origin.y +
                                    overlay->current_rect.size.height));

  if (!clutter_rect_equals (&overlay->previous_rect, &overlay->current_rect))
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

  l = stage->overlays;
  while (l)
    {
      meta_overlay_free (l->data);
      l = g_list_delete_link (l, l);
    }

  G_OBJECT_CLASS (meta_stage_parent_class)->finalize (object);
}

static void
meta_stage_paint (ClutterActor *actor)
{
  MetaStage *stage = META_STAGE (actor);
  GList *l;

  CLUTTER_ACTOR_CLASS (meta_stage_parent_class)->paint (actor);

  g_signal_emit (stage, signals[ACTORS_PAINTED], 0);

  for (l = stage->overlays; l; l = l->next)
    meta_overlay_paint (l->data);
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
on_power_save_changed (MetaMonitorManager *monitor_manager,
                       MetaStage          *stage)
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

  signals[ACTORS_PAINTED] = g_signal_new ("actors-painted",
                                          G_TYPE_FROM_CLASS (klass),
                                          G_SIGNAL_RUN_LAST,
                                          0,
                                          NULL, NULL, NULL,
                                          G_TYPE_NONE, 0);
}

static void
meta_stage_init (MetaStage *stage)
{
  clutter_stage_set_user_resizable (CLUTTER_STAGE (stage), FALSE);
}

ClutterActor *
meta_stage_new (MetaBackend *backend)
{
  MetaStage *stage;
  MetaMonitorManager *monitor_manager;

  stage = g_object_new (META_TYPE_STAGE,
                        "cursor-visible", FALSE,
                        NULL);

  monitor_manager = meta_backend_get_monitor_manager (backend);
  g_signal_connect (monitor_manager, "power-save-mode-changed",
                    G_CALLBACK (on_power_save_changed),
                    stage);

  return CLUTTER_ACTOR (stage);
}

static void
queue_redraw_clutter_rect (MetaStage   *stage,
                           MetaOverlay *overlay,
                           ClutterRect *rect)
{
  cairo_rectangle_int_t clip = {
    .x = floorf (rect->origin.x),
    .y = floorf (rect->origin.y),
    .width = ceilf (rect->size.width),
    .height = ceilf (rect->size.height)
  };

  /* Since we're flooring the coordinates, we need to enlarge the clip by the
   * difference between the actual coordinate and the floored value */
  clip.width += ceilf (rect->origin.x - clip.x) * 2;
  clip.height += ceilf (rect->origin.y - clip.y) * 2;

  clutter_actor_queue_redraw_with_clip (CLUTTER_ACTOR (stage), &clip);
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
  if (overlay->enabled)
    queue_redraw_clutter_rect (stage, overlay, &overlay->current_rect);
}

MetaOverlay *
meta_stage_create_cursor_overlay (MetaStage *stage)
{
  MetaOverlay *overlay;

  overlay = meta_overlay_new ();
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
meta_stage_update_cursor_overlay (MetaStage   *stage,
                                  MetaOverlay *overlay,
                                  CoglTexture *texture,
                                  ClutterRect *rect)
{
  g_assert (meta_is_wayland_compositor () || texture == NULL);

  meta_overlay_set (overlay, texture, rect);
  queue_redraw_for_overlay (stage, overlay);
}

void
meta_stage_set_active (MetaStage *stage,
                       gboolean   is_active)
{
  ClutterEvent event = { 0 };

  /* Used by the native backend to inform accessibility technologies
   * about when the stage loses and gains input focus.
   *
   * For the X11 backend, clutter transparently takes care of this
   * for us.
   */

  if (stage->is_active == is_active)
    return;

  event.type = CLUTTER_STAGE_STATE;
  clutter_event_set_stage (&event, CLUTTER_STAGE (stage));
  event.stage_state.changed_mask = CLUTTER_STAGE_STATE_ACTIVATED;

  if (is_active)
    event.stage_state.new_state = CLUTTER_STAGE_STATE_ACTIVATED;

  /* Emitting this StageState event will result in the stage getting
   * activated or deactivated (with the activated or deactivated signal
   * getting emitted from the stage)
   *
   * FIXME: This won't update ClutterStage's own notion of its
   * activeness. For that we would need to somehow trigger a
   * _clutter_stage_update_state call, which will probably
   * require new API in clutter. In practice, nothing relies
   * on the ClutterStage's own notion of activeness when using
   * the EGL backend.
   *
   * See http://bugzilla.gnome.org/746670
   */
  clutter_stage_event (CLUTTER_STAGE (stage), &event);
}
