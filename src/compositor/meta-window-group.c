/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#include "config.h"

#include <math.h>

#include "compositor/clutter-utils.h"
#include "compositor/compositor-private.h"
#include "compositor/meta-cullable.h"
#include "compositor/meta-window-actor-private.h"
#include "compositor/meta-window-group-private.h"
#include "core/display-private.h"
#include "core/window-private.h"

struct _MetaWindowGroupClass
{
  ClutterActorClass parent_class;
};

struct _MetaWindowGroup
{
  ClutterActor parent;

  MetaDisplay *display;
};

static void cullable_iface_init (MetaCullableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaWindowGroup, meta_window_group, CLUTTER_TYPE_ACTOR,
                         G_IMPLEMENT_INTERFACE (META_TYPE_CULLABLE, cullable_iface_init));

static void
meta_window_group_cull_unobscured (MetaCullable *cullable,
                                   MtkRegion    *unobscured_region)
{
  meta_cullable_cull_unobscured_children (cullable, unobscured_region);
}

static void
meta_window_group_cull_redraw_clip (MetaCullable *cullable,
                                    MtkRegion    *clip_region)
{
  meta_cullable_cull_redraw_clip_children (cullable, clip_region);
}

static void
cullable_iface_init (MetaCullableInterface *iface)
{
  iface->cull_unobscured = meta_window_group_cull_unobscured;
  iface->cull_redraw_clip = meta_window_group_cull_redraw_clip;
}

static void
meta_window_group_paint (ClutterActor        *actor,
                         ClutterPaintContext *paint_context)
{
  MetaWindowGroup *window_group = META_WINDOW_GROUP (actor);
  ClutterActorClass *parent_actor_class =
    CLUTTER_ACTOR_CLASS (meta_window_group_parent_class);
  ClutterActor *stage = clutter_actor_get_stage (actor);
  const MtkRegion *redraw_clip;
  g_autoptr (MtkRegion) clip_region = NULL;
  graphene_matrix_t stage_to_actor;

  redraw_clip = clutter_paint_context_get_redraw_clip (paint_context);
  if (!redraw_clip)
    goto fail;

  /* Normally we expect an actor to be drawn at it's position on the screen.
   * However, if we're inside the paint of a ClutterClone, that won't be the
   * case and we need to compensate.
   */
  if (clutter_actor_is_in_clone_paint (actor))
    {
      CoglFramebuffer *fb;
      ClutterStageView *view;
      graphene_matrix_t eye_to_actor, actor_to_eye, stage_to_eye;

      fb = clutter_paint_context_get_framebuffer (paint_context);
      view = clutter_paint_context_get_stage_view (paint_context);

      if (!view ||
          fb != clutter_stage_view_get_framebuffer (view))
        {
          goto fail;
        }

      cogl_framebuffer_get_modelview_matrix (fb, &actor_to_eye);

      /* We need to obtain the transformation matrix from eye coordinates
       * to cloned actor coordinates to be able to deduce the transformation
       * matrix from stage to cloned actor coordinates, which is needed to
       * calculate the redraw clip for the current actor.
       * If we cannot do this because the cloned actor modelview matrix is
       * non-invertible, give up on culling.
       */
      if (!graphene_matrix_inverse (&actor_to_eye, &eye_to_actor))
        goto fail;

      clutter_actor_get_transform (stage, &stage_to_eye);
      graphene_matrix_multiply (&stage_to_eye, &eye_to_actor,
                                &stage_to_actor);
    }
  else
    {
      graphene_matrix_t actor_to_stage;

      clutter_actor_get_relative_transformation_matrix (actor, stage,
                                                        &actor_to_stage);
      if (!graphene_matrix_inverse (&actor_to_stage, &stage_to_actor))
        goto fail;
    }

  if (!graphene_matrix_is_2d (&stage_to_actor))
    goto fail;

  /* Get the clipped redraw bounds so that we can avoid painting shadows on
   * windows that don't need to be painted in this frame. In the case of a
   * multihead setup with mismatched monitor sizes, we could intersect this
   * with an accurate union of the monitors to avoid painting shadows that are
   * visible only in the holes. */
  clip_region = mtk_region_apply_matrix_transform_expand (redraw_clip,
                                                          &stage_to_actor);

  meta_cullable_cull_redraw_clip (META_CULLABLE (window_group), clip_region);

  parent_actor_class->paint (actor, paint_context);

  meta_cullable_cull_redraw_clip (META_CULLABLE (window_group), NULL);

  return;

fail:
  parent_actor_class->paint (actor, paint_context);
}

/* Adapted from clutter_actor_update_default_paint_volume() */
static gboolean
meta_window_group_get_paint_volume (ClutterActor       *self,
                                    ClutterPaintVolume *volume)
{
  ClutterActorIter iter;
  ClutterActor *child;

  clutter_actor_iter_init (&iter, self);
  while (clutter_actor_iter_next (&iter, &child))
    {
      g_autoptr (ClutterPaintVolume) child_volume = NULL;

      if (!clutter_actor_is_mapped (child))
        continue;

      child_volume = clutter_actor_get_transformed_paint_volume (child, self);
      if (child_volume == NULL)
        return FALSE;

      clutter_paint_volume_union (volume, child_volume);
    }

  return TRUE;
}

/* This is a workaround for Clutter's awful allocation tracking.
 * Without this, any time the window group changed size, which is
 * any time windows are dragged around, we'll do a full repaint
 * of the window group, which includes the background actor, meaning
 * a full-stage repaint.
 *
 * Since actors are allowed to paint outside their allocation, and
 * since child actors are allowed to be outside their parents, this
 * doesn't affect anything, but it means that we'll get much more
 * sane and consistent clipped repaints from Clutter. */
static void
meta_window_group_get_preferred_width (ClutterActor *actor,
                                       gfloat        for_height,
                                       gfloat       *min_width,
                                       gfloat       *nat_width)
{
  *min_width = 0;
  *nat_width = 0;
}

static void
meta_window_group_get_preferred_height (ClutterActor *actor,
                                        gfloat        for_width,
                                        gfloat       *min_height,
                                        gfloat       *nat_height)
{
  *min_height = 0;
  *nat_height = 0;
}

static void
meta_window_group_class_init (MetaWindowGroupClass *klass)
{
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);

  actor_class->paint = meta_window_group_paint;
  actor_class->get_paint_volume = meta_window_group_get_paint_volume;
  actor_class->get_preferred_width = meta_window_group_get_preferred_width;
  actor_class->get_preferred_height = meta_window_group_get_preferred_height;
}

static void
meta_window_group_init (MetaWindowGroup *window_group)
{
}

ClutterActor *
meta_window_group_new (MetaDisplay *display)
{
  MetaWindowGroup *window_group;

  window_group = g_object_new (META_TYPE_WINDOW_GROUP, NULL);

  window_group->display = display;

  return CLUTTER_ACTOR (window_group);
}
