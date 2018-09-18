/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *             Jorn Baayen  <jorn@openedhand.com>
 *             Emmanuele Bassi  <ebassi@openedhand.com>
 *
 * Copyright (C) 2006 OpenedHand
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

#include "clutter-build-config.h"

#define CLUTTER_DISABLE_DEPRECATION_WARNINGS

#include "deprecated/clutter-actor.h"
#include "clutter-alpha.h"
#include "clutter-behaviour.h"
#include "clutter-behaviour-depth.h"
#include "clutter-enum-types.h"
#include "clutter-main.h"
#include "clutter-debug.h"
#include "clutter-private.h"

/**
 * SECTION:clutter-behaviour-depth
 * @Title: ClutterBehaviourDepth
 * @short_description: A behaviour controlling the Z position
 * @Deprecated: 1.6: Use clutter_actor_animate() instead
 *
 * #ClutterBehaviourDepth is a simple #ClutterBehaviour controlling the
 * depth of a set of actors between a start and end depth.
 *
 * #ClutterBehaviourDepth is available since Clutter 0.4.
 *
 * Deprecated: 1.6: Use the #ClutterActor:depth property and
 *   clutter_actor_animate(), or #ClutterAnimator, or #ClutterState
 *   instead.
 */

struct _ClutterBehaviourDepthPrivate
{
  gint depth_start;
  gint depth_end;
};

enum
{
  PROP_0,

  PROP_DEPTH_START,
  PROP_DEPTH_END
};

G_DEFINE_TYPE_WITH_PRIVATE (ClutterBehaviourDepth,
                            clutter_behaviour_depth,
                            CLUTTER_TYPE_BEHAVIOUR)

static void
alpha_notify_foreach (ClutterBehaviour *behaviour,
                      ClutterActor     *actor,
                      gpointer          user_data)
{
  clutter_actor_set_depth (actor, GPOINTER_TO_INT (user_data));
}

static void
clutter_behaviour_depth_alpha_notify (ClutterBehaviour *behaviour,
                                      gdouble           alpha_value)
{
  ClutterBehaviourDepthPrivate *priv;
  gint depth;

  priv = CLUTTER_BEHAVIOUR_DEPTH (behaviour)->priv;

  /* Need to create factor as to avoid borking signedness */
  depth = (alpha_value * (priv->depth_end - priv->depth_start))
        + priv->depth_start;

  CLUTTER_NOTE (ANIMATION, "alpha: %.4f, depth: %d", alpha_value, depth);

  clutter_behaviour_actors_foreach (behaviour,
                                    alpha_notify_foreach,
                                    GINT_TO_POINTER (depth));
}

static void
clutter_behaviour_depth_applied (ClutterBehaviour *behaviour,
                                 ClutterActor     *actor)
{
  ClutterBehaviourDepth *depth = CLUTTER_BEHAVIOUR_DEPTH (behaviour);

  clutter_actor_set_depth (actor, depth->priv->depth_start);
}

static void
clutter_behaviour_depth_set_property (GObject      *gobject,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  ClutterBehaviourDepth *depth = CLUTTER_BEHAVIOUR_DEPTH (gobject);

  switch (prop_id)
    {
    case PROP_DEPTH_START:
      depth->priv->depth_start = g_value_get_int (value);
      break;
    case PROP_DEPTH_END:
      depth->priv->depth_end = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_behaviour_depth_get_property (GObject    *gobject,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  ClutterBehaviourDepth *depth = CLUTTER_BEHAVIOUR_DEPTH (gobject);

  switch (prop_id)
    {
    case PROP_DEPTH_START:
      g_value_set_int (value, depth->priv->depth_start);
      break;
    case PROP_DEPTH_END:
      g_value_set_int (value, depth->priv->depth_end);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_behaviour_depth_class_init (ClutterBehaviourDepthClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterBehaviourClass *behaviour_class = CLUTTER_BEHAVIOUR_CLASS (klass);

  gobject_class->set_property = clutter_behaviour_depth_set_property;
  gobject_class->get_property = clutter_behaviour_depth_get_property;

  behaviour_class->alpha_notify = clutter_behaviour_depth_alpha_notify;
  behaviour_class->applied = clutter_behaviour_depth_applied;

  /**
   * ClutterBehaviourDepth:depth-start:
   *
   * Start depth level to apply to the actors.
   *
   * Since: 0.4
   *
   * Deprecated: 1.6
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DEPTH_START,
                                   g_param_spec_int ("depth-start",
                                                     P_("Start Depth"),
                                                     P_("Initial depth to apply"),
                                                     G_MININT, G_MAXINT, 0,
                                                     CLUTTER_PARAM_READWRITE));
  /**
   * ClutterBehaviourDepth:depth-end:
   *
   * End depth level to apply to the actors.
   *
   * Since: 0.4
   *
   * Deprecated: 1.6
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DEPTH_END,
                                   g_param_spec_int ("depth-end",
                                                     P_("End Depth"),
                                                     P_("Final depth to apply"),
                                                     G_MININT, G_MAXINT, 0,
                                                     CLUTTER_PARAM_READWRITE));
}

static void
clutter_behaviour_depth_init (ClutterBehaviourDepth *depth)
{
  depth->priv = clutter_behaviour_depth_get_instance_private (depth);
}

/**
 * clutter_behaviour_depth_new:
 * @alpha: (allow-none): a #ClutterAlpha instance, or %NULL
 * @depth_start: initial value of the depth
 * @depth_end: final value of the depth
 *
 * Creates a new #ClutterBehaviourDepth which can be used to control
 * the ClutterActor:depth property of a set of #ClutterActor<!-- -->s.
 *
 * If @alpha is not %NULL, the #ClutterBehaviour will take ownership
 * of the #ClutterAlpha instance. In the case when @alpha is %NULL,
 * it can be set later with clutter_behaviour_set_alpha().
 *
 * Return value: (transfer full): the newly created behaviour
 *
 * Since: 0.4
 *
 * Deprecated: 1.6
 */
ClutterBehaviour *
clutter_behaviour_depth_new (ClutterAlpha *alpha,
                             gint          depth_start,
                             gint          depth_end)
{
  g_return_val_if_fail (alpha == NULL || CLUTTER_IS_ALPHA (alpha), NULL);

  return g_object_new (CLUTTER_TYPE_BEHAVIOUR_DEPTH,
                       "alpha", alpha,
                       "depth-start", depth_start,
                       "depth-end", depth_end,
                       NULL);
}

/**
 * clutter_behaviour_depth_set_bounds:
 * @behaviour: a #ClutterBehaviourDepth
 * @depth_start: initial value of the depth
 * @depth_end: final value of the depth
 *
 * Sets the boundaries of the @behaviour.
 *
 * Since: 0.6
 *
 * Deprecated: 1.6
 */
void
clutter_behaviour_depth_set_bounds (ClutterBehaviourDepth *behaviour,
                                    gint                   depth_start,
                                    gint                   depth_end)
{
  ClutterBehaviourDepthPrivate *priv;

  g_return_if_fail (CLUTTER_IS_BEHAVIOUR_DEPTH (behaviour));

  priv = behaviour->priv;

  g_object_freeze_notify (G_OBJECT (behaviour));

  if (priv->depth_start != depth_start)
    {
      priv->depth_start = depth_start;
      g_object_notify (G_OBJECT (behaviour), "depth-start");
    }

  if (priv->depth_end != depth_end)
    {
      priv->depth_end = depth_end;
      g_object_notify (G_OBJECT (behaviour), "depth-end");
    }

  g_object_thaw_notify (G_OBJECT (behaviour));
}

/**
 * clutter_behaviour_depth_get_bounds:
 * @behaviour: a #ClutterBehaviourDepth
 * @depth_start: (out): return location for the initial depth value, or %NULL
 * @depth_end: (out): return location for the final depth value, or %NULL
 *
 * Gets the boundaries of the @behaviour
 *
 * Since: 0.6
 *
 * Deprecated: 1.6
 */
void
clutter_behaviour_depth_get_bounds (ClutterBehaviourDepth *behaviour,
                                    gint                  *depth_start,
                                    gint                  *depth_end)
{
  g_return_if_fail (CLUTTER_IS_BEHAVIOUR_DEPTH (behaviour));

  if (depth_start)
    *depth_start = behaviour->priv->depth_start;

  if (depth_end)
    *depth_end = behaviour->priv->depth_end;
}
