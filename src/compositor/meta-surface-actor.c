/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * MetaSurfaceActor:
 *
 * An actor representing a surface in the scene graph
 *
 * MetaSurfaceActor is an abstract class which represents a surface in the
 * Clutter scene graph. A subclass can implement the specifics of a surface
 * depending on the way it is handled by a display protocol.
 *
 * An important feature of #MetaSurfaceActor is that it allows you to set an
 * "input region": all events that occur in the surface, but outside of the
 * input region are to be explicitly ignored. By default, this region is to
 * %NULL, which means events on the whole surface is allowed.
 */

#include "config.h"

#include "compositor/meta-surface-actor.h"

#include "clutter/clutter.h"
#include "compositor/clutter-utils.h"
#include "compositor/meta-cullable.h"
#include "compositor/meta-shaped-texture-private.h"
#include "compositor/meta-window-actor-private.h"
#include "meta/meta-shaped-texture.h"

enum
{
  PROP_0,

  PROP_IS_OBSCURED,
  PROP_IS_FROZEN,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MetaSurfaceActorPrivate
{
  MetaShapedTexture *texture;

  MtkRegion *input_region;

  /* MetaCullable regions, see that documentation for more details */
  MtkRegion *unobscured_region;
  gboolean is_obscured;

  /* Freeze/thaw accounting */
  MtkRegion *pending_damage;
  gboolean is_frozen;
} MetaSurfaceActorPrivate;

static void cullable_iface_init (MetaCullableInterface *iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (MetaSurfaceActor, meta_surface_actor, CLUTTER_TYPE_ACTOR,
                                  G_ADD_PRIVATE (MetaSurfaceActor)
                                  G_IMPLEMENT_INTERFACE (META_TYPE_CULLABLE, cullable_iface_init));

enum
{
  REPAINT_SCHEDULED,
  UPDATE_SCHEDULED,
  SIZE_CHANGED,

  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL];

typedef enum
{
  IN_STAGE_PERSPECTIVE,
  IN_ACTOR_PERSPECTIVE
} ScalePerspectiveType;

static MtkRegion *
effective_unobscured_region (MetaSurfaceActor *surface_actor)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (surface_actor);
  ClutterActor *actor = CLUTTER_ACTOR (surface_actor);

  /* Fail if we have any mapped clones. */
  if (clutter_actor_has_mapped_clones (actor))
    return NULL;

  return priv->unobscured_region;
}

static void
update_is_obscured (MetaSurfaceActor *surface_actor)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (surface_actor);
  MtkRegion *unobscured_region;
  gboolean is_obscured;

  unobscured_region = priv->unobscured_region;

  if (unobscured_region)
    is_obscured = mtk_region_is_empty (unobscured_region);
  else
    is_obscured = FALSE;

  if (priv->is_obscured == is_obscured)
    return;

  priv->is_obscured = is_obscured;
  g_object_notify_by_pspec (G_OBJECT (surface_actor),
                            obj_props[PROP_IS_OBSCURED]);
}

static void
set_unobscured_region (MetaSurfaceActor *surface_actor,
                       MtkRegion        *unobscured_region)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (surface_actor);

  g_clear_pointer (&priv->unobscured_region, mtk_region_unref);
  if (unobscured_region)
    {
      if (mtk_region_is_empty (unobscured_region))
        {
          priv->unobscured_region = mtk_region_ref (unobscured_region);
        }
      else
        {
          MtkRectangle bounds = { 0, };
          float width, height;

          clutter_content_get_preferred_size (CLUTTER_CONTENT (priv->texture),
                                              &width,
                                              &height);
          bounds = (MtkRectangle) {
            .width = width,
            .height = height,
          };

          priv->unobscured_region = mtk_region_copy (unobscured_region);

          mtk_region_intersect_rectangle (priv->unobscured_region, &bounds);
        }
    }

  update_is_obscured (surface_actor);
}

static void
set_clip_region (MetaSurfaceActor *surface_actor,
                 MtkRegion        *clip_region)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (surface_actor);
  MetaShapedTexture *stex = priv->texture;

  if (clip_region && !mtk_region_is_empty (clip_region))
    {
      g_autoptr (MtkRegion) clip_region_copy = NULL;

      clip_region_copy = mtk_region_copy (clip_region);
      meta_shaped_texture_set_clip_region (stex, clip_region_copy);
    }
  else
    {
      meta_shaped_texture_set_clip_region (stex, clip_region);
    }
}

static void
meta_surface_actor_pick (ClutterActor       *actor,
                         ClutterPickContext *pick_context)
{
  MetaSurfaceActor *self = META_SURFACE_ACTOR (actor);
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);
  ClutterActorIter iter;
  ClutterActor *child;

  if (!clutter_actor_should_pick (actor, pick_context))
    return;

  /* If there is no region then use the regular pick */
  if (priv->input_region == NULL)
    {
      ClutterActorClass *actor_class =
        CLUTTER_ACTOR_CLASS (meta_surface_actor_parent_class);

      actor_class->pick (actor, pick_context);
    }
  else
    {
      int n_rects;
      int i;

      n_rects = mtk_region_num_rectangles (priv->input_region);

      for (i = 0; i < n_rects; i++)
        {
          MtkRectangle rect;
          ClutterActorBox box;

          rect = mtk_region_get_rectangle (priv->input_region, i);

          box.x1 = rect.x;
          box.y1 = rect.y;
          box.x2 = rect.x + rect.width;
          box.y2 = rect.y + rect.height;
          clutter_actor_pick_box (actor, pick_context, &box);
        }
    }

  clutter_actor_iter_init (&iter, actor);

  while (clutter_actor_iter_next (&iter, &child))
    clutter_actor_pick (child, pick_context);
}

static gboolean
meta_surface_actor_get_paint_volume (ClutterActor       *actor,
                                     ClutterPaintVolume *volume)
{
  return clutter_paint_volume_set_from_allocation (volume, actor);
}

static void
meta_surface_actor_get_property (GObject      *object,
                                 guint         prop_id,
                                 GValue       *value,
                                 GParamSpec   *pspec)
{
  MetaSurfaceActor *surface_actor = META_SURFACE_ACTOR (object);
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (surface_actor);

  switch (prop_id)
    {
    case PROP_IS_OBSCURED:
      g_value_set_boolean (value, priv->is_obscured);
      break;
    case PROP_IS_FROZEN:
      g_value_set_boolean (value, priv->is_frozen);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_surface_actor_dispose (GObject *object)
{
  MetaSurfaceActor *self = META_SURFACE_ACTOR (object);
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);

  g_clear_pointer (&priv->input_region, mtk_region_unref);
  g_clear_object (&priv->texture);

  set_unobscured_region (self, NULL);

  G_OBJECT_CLASS (meta_surface_actor_parent_class)->dispose (object);
}

static void
meta_surface_actor_class_init (MetaSurfaceActorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);

  object_class->dispose = meta_surface_actor_dispose;
  object_class->get_property = meta_surface_actor_get_property;

  actor_class->pick = meta_surface_actor_pick;
  actor_class->get_paint_volume = meta_surface_actor_get_paint_volume;

  obj_props[PROP_IS_OBSCURED] =
    g_param_spec_boolean ("is-obscured", NULL, NULL,
                          TRUE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  obj_props[PROP_IS_FROZEN] =
    g_param_spec_boolean ("is-frozen", NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);

  signals[REPAINT_SCHEDULED] = g_signal_new ("repaint-scheduled",
                                             G_TYPE_FROM_CLASS (object_class),
                                             G_SIGNAL_RUN_LAST,
                                             0,
                                             NULL, NULL, NULL,
                                             G_TYPE_NONE, 0);

  signals[UPDATE_SCHEDULED] = g_signal_new ("update-scheduled",
                                            G_TYPE_FROM_CLASS (object_class),
                                            G_SIGNAL_RUN_LAST,
                                            0,
                                            NULL, NULL, NULL,
                                            G_TYPE_NONE, 0);


  signals[SIZE_CHANGED] = g_signal_new ("size-changed",
                                        G_TYPE_FROM_CLASS (object_class),
                                        G_SIGNAL_RUN_LAST,
                                        0,
                                        NULL, NULL, NULL,
                                        G_TYPE_NONE, 0);
}

gboolean
meta_surface_actor_is_opaque (MetaSurfaceActor *self)
{
  return META_SURFACE_ACTOR_GET_CLASS (self)->is_opaque (self);
}

static void
subtract_opaque_region (MetaSurfaceActor *surface_actor,
                        MtkRegion        *region)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (surface_actor);
  uint8_t opacity = clutter_actor_get_paint_opacity (CLUTTER_ACTOR (surface_actor));

  if (!region)
    return;

  if (opacity == 0xff)
    {
      MtkRegion *opaque_region;

      opaque_region = meta_shaped_texture_get_opaque_region (priv->texture);

      if (!opaque_region)
        return;

      mtk_region_subtract (region, opaque_region);
    }
}

static void
meta_surface_actor_cull_redraw_clip (MetaCullable *cullable,
                                     MtkRegion    *clip_region)
{
  MetaSurfaceActor *surface_actor = META_SURFACE_ACTOR (cullable);

  set_clip_region (surface_actor, clip_region);

  subtract_opaque_region (surface_actor, clip_region);
}

static void
meta_surface_actor_cull_unobscured (MetaCullable *cullable,
                                    MtkRegion    *unobscured_region)
{
  MetaSurfaceActor *surface_actor = META_SURFACE_ACTOR (cullable);

  set_unobscured_region (surface_actor, unobscured_region);

  subtract_opaque_region (surface_actor, unobscured_region);
}

static void
cullable_iface_init (MetaCullableInterface *iface)
{
  iface->cull_redraw_clip = meta_surface_actor_cull_redraw_clip;
  iface->cull_unobscured = meta_surface_actor_cull_unobscured;
}

static void
texture_size_changed (MetaShapedTexture *texture,
                      gpointer           user_data)
{
  MetaSurfaceActor *actor = META_SURFACE_ACTOR (user_data);
  g_signal_emit (actor, signals[SIZE_CHANGED], 0);
}

static void
meta_surface_actor_init (MetaSurfaceActor *self)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);

  priv->is_obscured = TRUE;
  priv->texture = meta_shaped_texture_new ();
  g_signal_connect_object (priv->texture, "size-changed",
                           G_CALLBACK (texture_size_changed), self, 0);
  clutter_actor_set_content (CLUTTER_ACTOR (self),
                             CLUTTER_CONTENT (priv->texture));
  clutter_actor_set_request_mode (CLUTTER_ACTOR (self),
                                  CLUTTER_REQUEST_CONTENT_SIZE);
}

MetaShapedTexture *
meta_surface_actor_get_texture (MetaSurfaceActor *self)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);

  return priv->texture;
}

void
meta_surface_actor_schedule_update (MetaSurfaceActor *self)
{
  ClutterStage *stage;

  stage = CLUTTER_STAGE (clutter_actor_get_stage (CLUTTER_ACTOR (self)));
  if (!stage)
    return;

  clutter_stage_schedule_update (stage);

  g_signal_emit (self, signals[UPDATE_SCHEDULED], 0);
}

void
meta_surface_actor_update_area (MetaSurfaceActor *self,
                                int               x,
                                int               y,
                                int               width,
                                int               height)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);
  gboolean repaint_scheduled = FALSE;
  MtkRectangle clip;

  if (meta_shaped_texture_update_area (priv->texture, x, y, width, height, &clip))
    {
      MtkRegion *unobscured_region;

      unobscured_region = effective_unobscured_region (self);

      if (unobscured_region)
        {
          g_autoptr (MtkRegion) intersection = NULL;

          if (mtk_region_is_empty (unobscured_region))
            return;

          intersection = mtk_region_copy (unobscured_region);
          mtk_region_intersect_rectangle (intersection, &clip);

          if (!mtk_region_is_empty (intersection))
            {
              int i, n_rectangles;

              n_rectangles = mtk_region_num_rectangles (intersection);
              for (i = 0; i < n_rectangles; i++)
                {
                  MtkRectangle rect;

                  rect = mtk_region_get_rectangle (intersection, i);
                  clutter_actor_queue_redraw_with_clip (CLUTTER_ACTOR (self), &rect);
                }

              repaint_scheduled = TRUE;
            }
        }
      else
        {
          clutter_actor_queue_redraw_with_clip (CLUTTER_ACTOR (self), &clip);
          repaint_scheduled = TRUE;
        }
    }

  if (repaint_scheduled)
    g_signal_emit (self, signals[REPAINT_SCHEDULED], 0);
}

gboolean
meta_surface_actor_is_obscured (MetaSurfaceActor *self)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);

  return priv->is_obscured;
}

gboolean
meta_surface_actor_is_effectively_obscured (MetaSurfaceActor *surface_actor)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (surface_actor);

  if (clutter_actor_has_mapped_clones (CLUTTER_ACTOR (surface_actor)))
    return FALSE;
  else
    return priv->is_obscured;
}

gboolean
meta_surface_actor_is_obscured_on_stage_view (MetaSurfaceActor *self,
                                              ClutterStageView *stage_view,
                                              float            *unobscurred_fraction)
{
  MtkRegion *unobscured_region;

  unobscured_region = effective_unobscured_region (self);

  if (unobscured_region)
    {
      MetaSurfaceActorPrivate *priv =
        meta_surface_actor_get_instance_private (self);
      ClutterActor *stage = clutter_actor_get_stage (CLUTTER_ACTOR (self));
      g_autoptr (MtkRegion) intersection_region = NULL;
      MtkRectangle stage_rect;
      graphene_matrix_t transform;
      graphene_rect_t actor_bounds;
      float bounds_width, bounds_height;
      float bounds_size;
      int intersection_size = 0;
      int n_rects, i;

      if (mtk_region_is_empty (unobscured_region))
        return TRUE;

      clutter_actor_get_relative_transformation_matrix (CLUTTER_ACTOR (self),
                                                        stage,
                                                        &transform);

      intersection_region = mtk_region_apply_matrix_transform_expand (unobscured_region, &transform);

      clutter_stage_view_get_layout (stage_view, &stage_rect);
      mtk_region_intersect_rectangle (intersection_region,
                                      &stage_rect);

      if (mtk_region_is_empty (intersection_region))
        return TRUE;
      else if (!unobscurred_fraction)
        return FALSE;

      clutter_content_get_preferred_size (CLUTTER_CONTENT (priv->texture),
                                          &bounds_width,
                                          &bounds_height);
      graphene_rect_init (&actor_bounds, 0, 0, bounds_width, bounds_height);
      graphene_matrix_transform_bounds (&transform, &actor_bounds, &actor_bounds);
      graphene_rect_round_extents (&actor_bounds, &actor_bounds);
      bounds_size = graphene_rect_get_area (&actor_bounds);

      n_rects = mtk_region_num_rectangles (intersection_region);
      for (i = 0; i < n_rects; i++)
        {
          MtkRectangle rect;

          rect = mtk_region_get_rectangle (intersection_region, i);
          intersection_size += rect.width * rect.height;
        }

      g_return_val_if_fail (bounds_size > 0, FALSE);

      *unobscurred_fraction = CLAMP (intersection_size / bounds_size, 0, 1);
      return FALSE;
    }

  return !clutter_actor_is_effectively_on_stage_view (CLUTTER_ACTOR (self),
                                                      stage_view);
}

gboolean
meta_surface_actor_contains_rect (MetaSurfaceActor *surface_actor,
                                  MtkRectangle     *rect)
{
  ClutterActor *actor = CLUTTER_ACTOR (surface_actor);
  graphene_rect_t bounding_rect;
  graphene_rect_t bound_rect;

  clutter_actor_get_transformed_extents (actor, &bounding_rect);

  bound_rect = mtk_rectangle_to_graphene_rect (rect);

  return graphene_rect_contains_rect (&bounding_rect,
                                      &bound_rect);
}

void
meta_surface_actor_set_input_region (MetaSurfaceActor *self,
                                     MtkRegion        *region)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);

  g_clear_pointer (&priv->input_region, mtk_region_unref);

  if (region)
    priv->input_region = mtk_region_ref (region);
  else
    priv->input_region = NULL;
}

void
meta_surface_actor_set_opaque_region (MetaSurfaceActor *self,
                                      MtkRegion        *region)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);

  meta_shaped_texture_set_opaque_region (priv->texture, region);
}

MtkRegion *
meta_surface_actor_get_opaque_region (MetaSurfaceActor *self)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);

  return meta_shaped_texture_get_opaque_region (priv->texture);
}

void
meta_surface_actor_process_damage (MetaSurfaceActor *self,
                                   int               x,
                                   int               y,
                                   int               width,
                                   int               height)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);

  if (meta_surface_actor_is_frozen (self))
    {
      /* The window is frozen due to an effect in progress: we ignore damage
       * here on the off chance that this will stop the corresponding
       * texture_from_pixmap from being update.
       *
       * pending_damage tracks any damage that happened while the window was
       * frozen so that when can apply it when the window becomes unfrozen.
       *
       * It should be noted that this is an unreliable mechanism since it's
       * quite likely that drivers will aim to provide a zero-copy
       * implementation of the texture_from_pixmap extension and in those cases
       * any drawing done to the window is always immediately reflected in the
       * texture regardless of damage event handling.
       */
      MtkRectangle rect = { .x = x, .y = y, .width = width, .height = height };

      if (!priv->pending_damage)
        priv->pending_damage = mtk_region_create_rectangle (&rect);
      else
        mtk_region_union_rectangle (priv->pending_damage, &rect);
      return;
    }

  META_SURFACE_ACTOR_GET_CLASS (self)->process_damage (self, x, y, width, height);
}

void
meta_surface_actor_set_frozen (MetaSurfaceActor *self,
                               gboolean          frozen)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);

  if (priv->is_frozen == frozen)
    return;

  priv->is_frozen = frozen;
  g_object_notify_by_pspec (G_OBJECT (self),
                            obj_props[PROP_IS_FROZEN]);

  if (!frozen && priv->pending_damage)
    {
      int i, n_rects = mtk_region_num_rectangles (priv->pending_damage);
      MtkRectangle rect;

      /* Since we ignore damage events while a window is frozen for certain effects
       * we need to apply the tracked damage now. */

      for (i = 0; i < n_rects; i++)
        {
          rect = mtk_region_get_rectangle (priv->pending_damage, i);
          meta_surface_actor_process_damage (self, rect.x, rect.y,
                                             rect.width, rect.height);
        }
      g_clear_pointer (&priv->pending_damage, mtk_region_unref);
    }
}

gboolean
meta_surface_actor_is_frozen (MetaSurfaceActor *self)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);

  return priv->is_frozen;
}
