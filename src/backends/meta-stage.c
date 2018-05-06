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

#include <config.h>

#include "meta-stage-private.h"

#include <meta/meta-backend.h>
#include <meta/meta-monitor-manager.h>
#include <meta/util.h>
#include "backends/meta-backend-private.h"
#include "clutter/clutter-mutter.h"

struct _MetaOverlay {
  gboolean enabled;

  CoglPipeline *pipeline;
  CoglTexture *texture;

  ClutterRect current_rect;
  ClutterRect previous_rect;
  gboolean previous_is_valid;
};

struct _MetaStagePrivate {
  GList *overlays;
  gboolean is_active;
};
typedef struct _MetaStagePrivate MetaStagePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaStage, meta_stage, CLUTTER_TYPE_STAGE);

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

  overlay->previous_rect = overlay->current_rect;
  overlay->previous_is_valid = TRUE;
}

static void
meta_stage_finalize (GObject *object)
{
  MetaStage *stage = META_STAGE (object);
  MetaStagePrivate *priv = meta_stage_get_instance_private (stage);
  GList *l = priv->overlays;

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
  MetaStagePrivate *priv = meta_stage_get_instance_private (stage);
  GList *l;

  CLUTTER_ACTOR_CLASS (meta_stage_parent_class)->paint (actor);

  for (l = priv->overlays; l; l = l->next)
    meta_overlay_paint (l->data);
}

static void
meta_stage_activate (ClutterStage *actor)
{
  MetaStage *stage = META_STAGE (actor);
  MetaStagePrivate *priv = meta_stage_get_instance_private (stage);

  CLUTTER_STAGE_CLASS (meta_stage_parent_class)->activate (actor);

  priv->is_active = TRUE;
}

static void
meta_stage_deactivate (ClutterStage *actor)
{
  MetaStage *stage = META_STAGE (actor);
  MetaStagePrivate *priv = meta_stage_get_instance_private (stage);

  CLUTTER_STAGE_CLASS (meta_stage_parent_class)->deactivate (actor);

  priv->is_active = FALSE;
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
}

static void
meta_stage_init (MetaStage *stage)
{
  clutter_stage_set_user_resizable (CLUTTER_STAGE (stage), FALSE);
}

ClutterActor *
meta_stage_new (void)
{
  return g_object_new (META_TYPE_STAGE,
                       "cursor-visible", FALSE,
                       NULL);
}

static void
queue_redraw_for_overlay (MetaStage   *stage,
                          MetaOverlay *overlay)
{
  cairo_rectangle_int_t clip;

  /* Clear the location the overlay was at before, if we need to. */
  if (overlay->previous_is_valid)
    {
      clip.x = floorf (overlay->previous_rect.origin.x),
      clip.y = floorf (overlay->previous_rect.origin.y),
      clip.width = ceilf (overlay->previous_rect.size.width),
      clip.height = ceilf (overlay->previous_rect.size.height),
      clutter_actor_queue_redraw_with_clip (CLUTTER_ACTOR (stage), &clip);
      overlay->previous_is_valid = FALSE;
    }

  /* Draw the overlay at the new position */
  if (overlay->enabled)
    {
      clip.x = floorf (overlay->current_rect.origin.x),
      clip.y = floorf (overlay->current_rect.origin.y),
      clip.width = ceilf (overlay->current_rect.size.width),
      clip.height = ceilf (overlay->current_rect.size.height),
      clutter_actor_queue_redraw_with_clip (CLUTTER_ACTOR (stage), &clip);
    }
}

MetaOverlay *
meta_stage_create_cursor_overlay (MetaStage *stage)
{
  MetaStagePrivate *priv = meta_stage_get_instance_private (stage);
  MetaOverlay *overlay;

  overlay = meta_overlay_new ();
  priv->overlays = g_list_prepend (priv->overlays, overlay);

  return overlay;
}

void
meta_stage_remove_cursor_overlay (MetaStage   *stage,
                                  MetaOverlay *overlay)
{
  MetaStagePrivate *priv = meta_stage_get_instance_private (stage);
  GList *link;

  link = g_list_find (priv->overlays, overlay);
  if (!link)
    return;

  priv->overlays = g_list_delete_link (priv->overlays, link);
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
  MetaStagePrivate *priv = meta_stage_get_instance_private (stage);
  ClutterEvent event = { 0 };

  /* Used by the native backend to inform accessibility technologies
   * about when the stage loses and gains input focus.
   *
   * For the X11 backend, clutter transparently takes care of this
   * for us.
   */

  if (priv->is_active == is_active)
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
