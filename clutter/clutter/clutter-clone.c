/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2008  Intel Corporation.
 *
 * Authored By: Robert Bragg <robert@linux.intel.com>
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

/**
 * ClutterClone:
 *
 * An actor that displays a clone of a source actor
 *
 * #ClutterClone is a [class@Clutter.Actor] which draws with the paint
 * function of another actor, scaled to fit its own allocation.
 *
 * #ClutterClone can be used to efficiently clone any other actor.
 *
 * #ClutterClone does not require the presence of support for FBOs
 * in the underlying GL or GLES implementation.
 */

#include "config.h"

#include "clutter/clutter-actor-private.h"
#include "clutter/clutter-clone.h"
#include "clutter/clutter-debug.h"
#include "clutter/clutter-main.h"
#include "clutter/clutter-paint-volume-private.h"
#include "clutter/clutter-private.h"

#include "cogl/cogl.h"

typedef struct _ClutterClonePrivate
{
  ClutterActor *clone_source;
  float x_scale, y_scale;

  gulong source_destroy_id;
} ClutterClonePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (ClutterClone, clutter_clone, CLUTTER_TYPE_ACTOR)

enum
{
  PROP_0,

  PROP_SOURCE,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

static void clutter_clone_set_source_internal (ClutterClone *clone,
					       ClutterActor *source);
static void
clutter_clone_get_preferred_width (ClutterActor *self,
                                   gfloat        for_height,
                                   gfloat       *min_width_p,
                                   gfloat       *natural_width_p)
{
  ClutterClonePrivate *priv =
    clutter_clone_get_instance_private (CLUTTER_CLONE (self));
  ClutterActor *clone_source = priv->clone_source;

  if (clone_source == NULL)
    {
      if (min_width_p)
        *min_width_p = 0;

      if (natural_width_p)
        *natural_width_p = 0;
    }
  else
    clutter_actor_get_preferred_width (clone_source,
                                       for_height,
                                       min_width_p,
                                       natural_width_p);
}

static void
clutter_clone_get_preferred_height (ClutterActor *self,
                                    gfloat        for_width,
                                    gfloat       *min_height_p,
                                    gfloat       *natural_height_p)
{
  ClutterClonePrivate *priv =
    clutter_clone_get_instance_private (CLUTTER_CLONE (self));
  ClutterActor *clone_source = priv->clone_source;

  if (clone_source == NULL)
    {
      if (min_height_p)
        *min_height_p = 0;

      if (natural_height_p)
        *natural_height_p = 0;
    }
  else
    clutter_actor_get_preferred_height (clone_source,
                                        for_width,
                                        min_height_p,
                                        natural_height_p);
}

static void
clutter_clone_paint (ClutterActor        *actor,
                     ClutterPaintContext *paint_context)
{
  ClutterClone *self = CLUTTER_CLONE (actor);
  ClutterClonePrivate *priv = clutter_clone_get_instance_private (self);
  gboolean was_unmapped = FALSE;

  if (priv->clone_source == NULL)
    return;

  CLUTTER_NOTE (PAINT, "painting clone actor '%s'",
                _clutter_actor_get_debug_name (actor));

  /* The final bits of magic:
   * - We need to override the paint opacity of the actor with our own
   *   opacity.
   * - We need to inform the actor that it's in a clone paint (for the function
   *   clutter_actor_is_in_clone_paint())
   * - We need to stop clutter_actor_paint applying the model view matrix of
   *   the clone source actor.
   */
  _clutter_actor_set_in_clone_paint (priv->clone_source, TRUE);
  clutter_actor_set_opacity_override (priv->clone_source,
                                      clutter_actor_get_paint_opacity (actor));
  _clutter_actor_set_enable_model_view_transform (priv->clone_source, FALSE);

  if (!clutter_actor_is_mapped (priv->clone_source))
    {
      _clutter_actor_set_enable_paint_unmapped (priv->clone_source, TRUE);
      was_unmapped = TRUE;
    }

  /* If the source isn't ultimately parented to a toplevel, it can't be
   * realized or painted.
   */
  if (clutter_actor_is_realized (priv->clone_source))
    {
      CoglFramebuffer *fb = NULL;

      if (priv->x_scale != 1.0 || priv->y_scale != 1.0)
        {
          fb = clutter_paint_context_get_framebuffer (paint_context);

          cogl_framebuffer_push_matrix (fb);
          cogl_framebuffer_scale (fb, priv->x_scale, priv->y_scale, 1.0f);
        }

      _clutter_actor_push_clone_paint ();
      clutter_actor_paint (priv->clone_source, paint_context);
      _clutter_actor_pop_clone_paint ();

      if (fb != NULL)
        cogl_framebuffer_pop_matrix (fb);
    }

  if (was_unmapped)
    _clutter_actor_set_enable_paint_unmapped (priv->clone_source, FALSE);

  _clutter_actor_set_enable_model_view_transform (priv->clone_source, TRUE);
  clutter_actor_set_opacity_override (priv->clone_source, -1);
  _clutter_actor_set_in_clone_paint (priv->clone_source, FALSE);
}

static gboolean
clutter_clone_get_paint_volume (ClutterActor       *actor,
                                ClutterPaintVolume *volume)
{
  ClutterClonePrivate *priv =
    clutter_clone_get_instance_private (CLUTTER_CLONE (actor));
  const ClutterPaintVolume *source_volume;

  /* if the source is not set the paint volume is defined to be empty */
  if (priv->clone_source == NULL)
    return TRUE;

  /* query the volume of the source actor and simply masquerade it as
   * the clones volume... */
  source_volume = clutter_actor_get_paint_volume (priv->clone_source);
  if (source_volume == NULL)
    return FALSE;

  clutter_paint_volume_init_from_paint_volume (volume, source_volume);
  _clutter_paint_volume_set_reference_actor (volume, actor);

  return TRUE;
}

static gboolean
clutter_clone_has_overlaps (ClutterActor *actor)
{
  ClutterClonePrivate *priv =
    clutter_clone_get_instance_private (CLUTTER_CLONE (actor));

  /* The clone has overlaps iff the source has overlaps */

  if (priv->clone_source == NULL)
    return FALSE;

  return clutter_actor_has_overlaps (priv->clone_source);
}

static void
clutter_clone_allocate (ClutterActor           *self,
                        const ClutterActorBox  *box)
{
  ClutterClonePrivate *priv =
    clutter_clone_get_instance_private (CLUTTER_CLONE (self));
  ClutterActorClass *parent_class;
  ClutterActorBox source_box;
  float x_scale, y_scale;

  /* chain up */
  parent_class = CLUTTER_ACTOR_CLASS (clutter_clone_parent_class);
  parent_class->allocate (self, box);

  if (priv->clone_source == NULL)
    return;

  /* ClutterActor delays allocating until the actor is shown; however
   * we cannot paint it correctly in that case, so force an allocation.
   */
  if (clutter_actor_get_parent (priv->clone_source) != NULL &&
      !clutter_actor_has_allocation (priv->clone_source))
    {
      float x = 0.f;
      float y = 0.f;

      clutter_actor_get_fixed_position (priv->clone_source, &x, &y);
      clutter_actor_allocate_preferred_size (priv->clone_source, x, y);
    }

  clutter_actor_get_allocation_box (priv->clone_source, &source_box);

  /* We need to scale what the clone-source actor paints to fill our own
   * allocation...
   */
  x_scale = clutter_actor_box_get_width (box)
          / clutter_actor_box_get_width (&source_box);
  y_scale = clutter_actor_box_get_height (box)
          / clutter_actor_box_get_height (&source_box);

  if (!G_APPROX_VALUE (priv->x_scale, x_scale, FLT_EPSILON) ||
      !G_APPROX_VALUE (priv->y_scale, y_scale, FLT_EPSILON))
    {
      priv->x_scale = x_scale;
      priv->y_scale = y_scale;
      clutter_actor_queue_redraw (self);
    }

#if 0
  /* XXX - this is wrong: ClutterClone cannot clone unparented
   * actors, as it will break all invariants
   */

  /* we act like a "foster parent" for the source we are cloning;
   * if the source has not been parented we have to force an
   * allocation on it, so that we can paint it correctly from
   * within our paint() implementation. since the actor does not
   * have a parent, and thus it won't be painted by the usual
   * paint cycle, we can safely give it as much size as it requires
   */
  if (clutter_actor_get_parent (priv->clone_source) == NULL)
    clutter_actor_allocate_preferred_size (priv->clone_source);
#endif
}

static void
clutter_clone_set_property (GObject      *gobject,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  ClutterClone *self = CLUTTER_CLONE (gobject);

  switch (prop_id)
    {
    case PROP_SOURCE:
      clutter_clone_set_source (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
  }
}

static void
clutter_clone_get_property (GObject    *gobject,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  ClutterClonePrivate *priv =
    clutter_clone_get_instance_private (CLUTTER_CLONE (gobject));

  switch (prop_id)
    {
    case PROP_SOURCE:
      g_value_set_object (value, priv->clone_source);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_clone_dispose (GObject *gobject)
{
  clutter_clone_set_source_internal (CLUTTER_CLONE (gobject), NULL);

  G_OBJECT_CLASS (clutter_clone_parent_class)->dispose (gobject);
}

static void
clutter_clone_class_init (ClutterCloneClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);

  actor_class->paint = clutter_clone_paint;
  actor_class->get_paint_volume = clutter_clone_get_paint_volume;
  actor_class->get_preferred_width = clutter_clone_get_preferred_width;
  actor_class->get_preferred_height = clutter_clone_get_preferred_height;
  actor_class->allocate = clutter_clone_allocate;
  actor_class->has_overlaps = clutter_clone_has_overlaps;

  gobject_class->dispose = clutter_clone_dispose;
  gobject_class->set_property = clutter_clone_set_property;
  gobject_class->get_property = clutter_clone_get_property;

  /**
   * ClutterClone:source:
   *
   * This property specifies the source actor being cloned.
   */
  obj_props[PROP_SOURCE] =
    g_param_spec_object ("source", NULL, NULL,
                         CLUTTER_TYPE_ACTOR,
                         G_PARAM_CONSTRUCT |
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, PROP_LAST, obj_props);
}

static void
clutter_clone_init (ClutterClone *self)
{
  ClutterClonePrivate *priv = clutter_clone_get_instance_private (self);

  priv->x_scale = 1.f;
  priv->y_scale = 1.f;
}

/**
 * clutter_clone_new:
 * @source: a #ClutterActor, or %NULL
 *
 * Creates a new #ClutterActor which clones @source/
 *
 * Return value: the newly created #ClutterClone
 */
ClutterActor *
clutter_clone_new (ClutterActor *source)
{
  return g_object_new (CLUTTER_TYPE_CLONE,
                       "source", source,
                       "accessible-role", ATK_ROLE_IMAGE,
                       NULL);
}

static void
on_source_destroyed (ClutterActor *source,
                     ClutterClone *self)
{
  clutter_clone_set_source_internal (self, NULL);
}

static void
clutter_clone_set_source_internal (ClutterClone *self,
				   ClutterActor *source)
{
  ClutterClonePrivate *priv = clutter_clone_get_instance_private (self);

  if (priv->clone_source == source)
    return;

  if (priv->clone_source != NULL)
    {
      g_clear_signal_handler (&priv->source_destroy_id, priv->clone_source);
      _clutter_actor_detach_clone (priv->clone_source, CLUTTER_ACTOR (self));
      g_object_unref (priv->clone_source);
      priv->clone_source = NULL;
    }

  if (source != NULL)
    {
      priv->clone_source = g_object_ref (source);
      _clutter_actor_attach_clone (priv->clone_source, CLUTTER_ACTOR (self));
      priv->source_destroy_id = g_signal_connect (priv->clone_source, "destroy",
                                                  G_CALLBACK (on_source_destroyed), self);
    }

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_SOURCE]);

  clutter_actor_queue_relayout (CLUTTER_ACTOR (self));
}

/**
 * clutter_clone_set_source:
 * @self: a #ClutterClone
 * @source: (allow-none): a #ClutterActor, or %NULL
 *
 * Sets @source as the source actor to be cloned by @self.
 */
void
clutter_clone_set_source (ClutterClone *self,
                          ClutterActor *source)
{
  g_return_if_fail (CLUTTER_IS_CLONE (self));
  g_return_if_fail (source == NULL || CLUTTER_IS_ACTOR (source));

  clutter_clone_set_source_internal (self, source);
  clutter_actor_queue_relayout (CLUTTER_ACTOR (self));
}

/**
 * clutter_clone_get_source:
 * @self: a #ClutterClone
 *
 * Retrieves the source #ClutterActor being cloned by @self.
 *
 * Return value: (transfer none): the actor source for the clone
 */
ClutterActor *
clutter_clone_get_source (ClutterClone *self)
{
  ClutterClonePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_CLONE (self), NULL);

  priv = clutter_clone_get_instance_private (self);
  return priv->clone_source;
}
