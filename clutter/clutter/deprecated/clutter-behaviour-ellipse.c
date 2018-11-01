
/* -*- mode:C; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Tomas Frydrych <tf@openedhand.com>
 *
 * Copyright (C) 2007 OpenedHand Ltd
 * Copyright (C) 2009 Intel Corp.
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
 * SECTION:clutter-behaviour-ellipse
 * @Title: ClutterBehaviourEllipse
 * @short_description: A behaviour interpolating position along an ellipse
 * @Deprecated: 1.6: Use clutter_actor_animate() instead
 *
 * #ClutterBehaviourEllipse interpolates actors along a path defined by
 *  an ellipse.
 *
 * When applying an ellipse behaviour to an actor, the
 * behaviour will update the actor's position and depth and set them
 * to what is dictated by the ellipses initial position.
 *
 * Deprecated: 1.6: Use clutter_actor_animate(), #ClutterPath and a
 *   #ClutterPathConstraint instead.
 *
 * Since: 0.4
 */

#include "clutter-build-config.h"

#include <math.h>
#include <stdlib.h>

#define CLUTTER_DISABLE_DEPRECATION_WARNINGS
#include "deprecated/clutter-actor.h"

#include "clutter-alpha.h"
#include "clutter-behaviour.h"
#include "clutter-behaviour-ellipse.h"
#include "clutter-debug.h"
#include "clutter-enum-types.h"
#include "clutter-private.h"

enum
{
  PROP_0,

  PROP_CENTER,
  PROP_WIDTH,
  PROP_HEIGHT,
  PROP_ANGLE_START,
  PROP_ANGLE_END,
  PROP_ANGLE_TILT_X,
  PROP_ANGLE_TILT_Y,
  PROP_ANGLE_TILT_Z,
  PROP_DIRECTION,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

struct _ClutterBehaviourEllipsePrivate
{
  ClutterKnot  center;

  /* a = width / 2 */
  gint a;

  /* b = height / 2 */
  gint b;

  gdouble angle_start;
  gdouble angle_end;

  gdouble angle_tilt_x;
  gdouble angle_tilt_y;
  gdouble angle_tilt_z;

  ClutterRotateDirection direction;
};

G_DEFINE_TYPE_WITH_PRIVATE (ClutterBehaviourEllipse,
                            clutter_behaviour_ellipse,
                            CLUTTER_TYPE_BEHAVIOUR)

typedef struct _knot3d
{
  gint x;
  gint y;
  gint z;
} knot3d;

static void
clutter_behaviour_ellipse_advance (ClutterBehaviourEllipse *e,
                                   float                    angle,
                                   knot3d                  *knot)
{
  ClutterBehaviourEllipsePrivate *priv = e->priv;
  gint x, y, z;

  x = priv->a * cosf (angle * (G_PI / 180.0));
  y = priv->b * sinf (angle * (G_PI / 180.0));
  z = 0;

  if (priv->angle_tilt_z)
    {
      /*
       * x2 = r * cos (angle + tilt_z)
       * y2 = r * sin (angle + tilt_z)
       *
       * These can be trasformed to the formulas below using properties of
       * sin (a + b) and cos (a + b)
       *
       */
      gfloat x2, y2;

      x2 = x * cosf (priv->angle_tilt_z * (G_PI / 180.0))
         - y * sinf (priv->angle_tilt_z * (G_PI / 180.0));

      y2 = y * cosf (priv->angle_tilt_z * (G_PI / 180.0))
         + x * sinf (priv->angle_tilt_z * (G_PI / 180.0));

      x =  (x2);
      y =  (y2);
    }

  if (priv->angle_tilt_x)
    {
      gfloat z2, y2;

      z2 = - y * sinf (priv->angle_tilt_x * (G_PI / 180.0));
      y2 =   y * cosf (priv->angle_tilt_x * (G_PI / 180.0));

      z = z2;
      y = y2;
    }

  if (priv->angle_tilt_y)
    {
      gfloat x2, z2;

      x2 = x * cosf (priv->angle_tilt_y * (G_PI / 180.0))
         - z * sinf (priv->angle_tilt_y * (G_PI / 180.0));

      z2 = z * cosf (priv->angle_tilt_y * (G_PI / 180.0))
         + x * sinf (priv->angle_tilt_y * (G_PI / 180.0));

      x = x2;
      z = z2;
    }

  knot->x = x;
  knot->y = y;
  knot->z = z;

  CLUTTER_NOTE (ANIMATION, "advancing to angle %.2f [%d, %d] (a: %d, b: %d)",
                angle,
                knot->x, knot->y,
                priv->a, priv->b);
}


static void
actor_apply_knot_foreach (ClutterBehaviour *behave,
                          ClutterActor     *actor,
                          gpointer          data)
{
  ClutterBehaviourEllipsePrivate *priv;
  knot3d *knot = data;

  priv = ((ClutterBehaviourEllipse *) behave)->priv;

  clutter_actor_set_position (actor, knot->x, knot->y);

  if (priv->angle_tilt_x != 0 || priv->angle_tilt_y != 0)
    clutter_actor_set_depth (actor, knot->z);
}

static inline float
clamp_angle (float a)
{
  gint rounds;

  rounds = a / 360;
  if (a < 0)
    rounds--;

  return a - 360 * rounds;
}

static void
clutter_behaviour_ellipse_alpha_notify (ClutterBehaviour *behave,
                                        gdouble           alpha)
{
  ClutterBehaviourEllipse *self = CLUTTER_BEHAVIOUR_ELLIPSE (behave);
  ClutterBehaviourEllipsePrivate *priv = self->priv;
  gfloat start, end;
  gfloat angle = 0;
  knot3d knot;

  /* we do everything in single precision because it's easier, even
   * though all the parameters are stored in double precision for
   * consistency with the equivalent ClutterActor API
   */
  start = priv->angle_start;
  end   = priv->angle_end;

  if (priv->direction == CLUTTER_ROTATE_CW && start >= end)
    end += 360;
  else if (priv->direction == CLUTTER_ROTATE_CCW && start <= end)
    end -= 360;

  angle = (end - start) * alpha + start;

  clutter_behaviour_ellipse_advance (self, angle, &knot);

  knot.x += priv->center.x;
  knot.y += priv->center.y;

  clutter_behaviour_actors_foreach (behave, actor_apply_knot_foreach, &knot);
}

static void
clutter_behaviour_ellipse_set_property (GObject      *gobject,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  ClutterBehaviourEllipse *el = CLUTTER_BEHAVIOUR_ELLIPSE (gobject);
  ClutterBehaviourEllipsePrivate *priv = el->priv;

  switch (prop_id)
    {
    case PROP_ANGLE_START:
      priv->angle_start = g_value_get_double (value);
      break;

    case PROP_ANGLE_END:
      priv->angle_end = g_value_get_double (value);
      break;

    case PROP_ANGLE_TILT_X:
      priv->angle_tilt_x = g_value_get_double (value);
      break;

    case PROP_ANGLE_TILT_Y:
      priv->angle_tilt_y = g_value_get_double (value);
      break;

    case PROP_ANGLE_TILT_Z:
      priv->angle_tilt_z = g_value_get_double (value);
      break;

    case PROP_WIDTH:
      clutter_behaviour_ellipse_set_width (el, g_value_get_int (value));
      break;

    case PROP_HEIGHT:
      clutter_behaviour_ellipse_set_height (el, g_value_get_int (value));
      break;

    case PROP_CENTER:
      {
        ClutterKnot *knot = g_value_get_boxed (value);
        if (knot)
          clutter_behaviour_ellipse_set_center (el, knot->x, knot->y);
      }
      break;

    case PROP_DIRECTION:
      priv->direction = g_value_get_enum (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_behaviour_ellipse_get_property (GObject    *gobject,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  ClutterBehaviourEllipsePrivate *priv;

  priv = CLUTTER_BEHAVIOUR_ELLIPSE (gobject)->priv;

  switch (prop_id)
    {
    case PROP_ANGLE_START:
      g_value_set_double (value, priv->angle_start);
      break;

    case PROP_ANGLE_END:
      g_value_set_double (value, priv->angle_end);
      break;

    case PROP_ANGLE_TILT_X:
      g_value_set_double (value, priv->angle_tilt_x);
      break;

    case PROP_ANGLE_TILT_Y:
      g_value_set_double (value, priv->angle_tilt_y);
      break;

    case PROP_ANGLE_TILT_Z:
      g_value_set_double (value, priv->angle_tilt_z);
      break;

    case PROP_WIDTH:
      g_value_set_int (value, (priv->a * 2));
      break;

    case PROP_HEIGHT:
      g_value_set_int (value, (priv->b * 2));
      break;

    case PROP_CENTER:
      g_value_set_boxed (value, &priv->center);
      break;

    case PROP_DIRECTION:
      g_value_set_enum (value, priv->direction);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_behaviour_ellipse_applied (ClutterBehaviour *behave,
                                   ClutterActor     *actor)
{
  ClutterBehaviourEllipse *e = CLUTTER_BEHAVIOUR_ELLIPSE (behave);
  ClutterBehaviourEllipsePrivate *priv = e->priv;
  knot3d knot = { 0, };

  clutter_behaviour_ellipse_advance (e, priv->angle_start, &knot);

  clutter_actor_set_position (actor, knot.x, knot.y);

  /* the depth should be changed only if there is a tilt on
   * any of the X or the Y axis
   */
  if (priv->angle_tilt_x != 0 || priv->angle_tilt_y != 0)
    clutter_actor_set_depth (actor, knot.z);
}

static void
clutter_behaviour_ellipse_class_init (ClutterBehaviourEllipseClass *klass)
{
  GObjectClass          *object_class = G_OBJECT_CLASS (klass);
  ClutterBehaviourClass *behave_class = CLUTTER_BEHAVIOUR_CLASS (klass);
  GParamSpec            *pspec        = NULL;

  object_class->set_property = clutter_behaviour_ellipse_set_property;
  object_class->get_property = clutter_behaviour_ellipse_get_property;

  behave_class->alpha_notify = clutter_behaviour_ellipse_alpha_notify;
  behave_class->applied = clutter_behaviour_ellipse_applied;

  /**
   * ClutterBehaviourEllipse:angle-start:
   *
   * The initial angle from where the rotation should start.
   *
   * Since: 0.4
   */
  pspec = g_param_spec_double ("angle-start",
                               P_("Start Angle"),
                               P_("Initial angle"),
                               0.0, 360.0,
                               0.0,
                               CLUTTER_PARAM_READWRITE);
  obj_props[PROP_ANGLE_START] = pspec;
  g_object_class_install_property (object_class, PROP_ANGLE_START, pspec);

  /**
   * ClutterBehaviourEllipse:angle-end:
   *
   * The final angle to where the rotation should end.
   *
   * Since: 0.4
   */
  pspec = g_param_spec_double ("angle-end",
                               P_("End Angle"),
                               P_("Final angle"),
                               0.0, 360.0,
                               0.0,
                               CLUTTER_PARAM_READWRITE);
  obj_props[PROP_ANGLE_END] = pspec;
  g_object_class_install_property (object_class, PROP_ANGLE_END, pspec);

  /**
   * ClutterBehaviourEllipse:angle-tilt-x:
   *
   * The tilt angle for the rotation around center in X axis
   *
   * Since: 0.4
   */
  pspec = g_param_spec_double ("angle-tilt-x",
                               P_("Angle x tilt"),
                               P_("Tilt of the ellipse around x axis"),
                               0.0, 360.0,
                               360.0,
                               CLUTTER_PARAM_READWRITE);
  obj_props[PROP_ANGLE_TILT_X] = pspec;
  g_object_class_install_property (object_class, PROP_ANGLE_TILT_X, pspec);

  /**
   * ClutterBehaviourEllipse:angle-tilt-y:
   *
   * The tilt angle for the rotation around center in Y axis
   *
   * Since: 0.4
   */
  pspec = g_param_spec_double ("angle-tilt-y",
                               P_("Angle y tilt"),
                               P_("Tilt of the ellipse around y axis"),
                               0.0, 360.0,
                               360.0,
                               CLUTTER_PARAM_READWRITE);
  obj_props[PROP_ANGLE_TILT_Y] = pspec;
  g_object_class_install_property (object_class, PROP_ANGLE_TILT_Y, pspec);

  /**
   * ClutterBehaviourEllipse:angle-tilt-z:
   *
   * The tilt angle for the rotation on the Z axis
   *
   * Since: 0.4
   */
  pspec = g_param_spec_double ("angle-tilt-z",
                               P_("Angle z tilt"),
                               P_("Tilt of the ellipse around z axis"),
                               0.0, 360.0,
                               360.0,
                               CLUTTER_PARAM_READWRITE);
  obj_props[PROP_ANGLE_TILT_Z] = pspec;
  g_object_class_install_property (object_class, PROP_ANGLE_TILT_Z, pspec);

  /**
   * ClutterBehaviourEllipse:width:
   *
   * Width of the ellipse, in pixels
   *
   * Since: 0.4
   */
  pspec = g_param_spec_int ("width",
                            P_("Width"),
                            P_("Width of the ellipse"),
                            0, G_MAXINT,
                            100,
                            CLUTTER_PARAM_READWRITE);
  obj_props[PROP_WIDTH] = pspec;
  g_object_class_install_property (object_class, PROP_WIDTH, pspec);

  /**
   * ClutterBehaviourEllipse:height:
   *
   * Height of the ellipse, in pixels
   *
   * Since: 0.4
   */
  pspec = g_param_spec_int ("height",
                            P_("Height"),
                            P_("Height of ellipse"),
                            0, G_MAXINT,
                            50,
                            CLUTTER_PARAM_READWRITE);
  obj_props[PROP_HEIGHT] = pspec;
  g_object_class_install_property (object_class, PROP_HEIGHT, pspec);

  /**
   * ClutterBehaviourEllipse:center:
   *
   * The center of the ellipse.
   *
   * Since: 0.4
   */
  pspec = g_param_spec_boxed ("center",
                              P_("Center"),
                              P_("Center of ellipse"),
                              CLUTTER_TYPE_KNOT,
                              CLUTTER_PARAM_READWRITE);
  obj_props[PROP_CENTER] = pspec;
  g_object_class_install_property (object_class, PROP_CENTER, pspec);

  /**
   * ClutterBehaviourEllipse:direction:
   *
   * The direction of the rotation.
   *
   * Since: 0.4
   */
  pspec = g_param_spec_enum ("direction",
                             P_("Direction"),
                             P_("Direction of rotation"),
                             CLUTTER_TYPE_ROTATE_DIRECTION,
                             CLUTTER_ROTATE_CW,
                             CLUTTER_PARAM_READWRITE);
  obj_props[PROP_DIRECTION] = pspec;
  g_object_class_install_property (object_class, PROP_DIRECTION, pspec);
}

static void
clutter_behaviour_ellipse_init (ClutterBehaviourEllipse * self)
{
  ClutterBehaviourEllipsePrivate *priv;

  self->priv = priv = clutter_behaviour_ellipse_get_instance_private (self);

  priv->direction = CLUTTER_ROTATE_CW;

  priv->angle_start = 0;
  priv->angle_end   = 0;

  priv->a = 50;
  priv->b = 25;

  priv->angle_tilt_x = 360;
  priv->angle_tilt_y = 360;
  priv->angle_tilt_z = 360;
}

/**
 * clutter_behaviour_ellipse_new:
 * @alpha: (allow-none): a #ClutterAlpha instance, or %NULL
 * @x: x coordinace of the center
 * @y: y coordiance of the center
 * @width: width of the ellipse
 * @height: height of the ellipse
 * @direction: #ClutterRotateDirection of rotation
 * @start: angle in degrees at which movement starts, between 0 and 360
 * @end: angle in degrees at which movement ends, between 0 and 360
 *
 * Creates a behaviour that drives actors along an elliptical path with
 * given center, width and height; the movement starts at @start
 * degrees (with 0 corresponding to 12 o'clock) and ends at @end
 * degrees. Angles greated than 360 degrees get clamped to the canonical
 * interval <0, 360); if @start is equal to @end, the behaviour will
 * rotate by exacly 360 degrees.
 *
 * If @alpha is not %NULL, the #ClutterBehaviour will take ownership
 * of the #ClutterAlpha instance. In the case when @alpha is %NULL,
 * it can be set later with clutter_behaviour_set_alpha().
 *
 * Return value: the newly created #ClutterBehaviourEllipse
 *
 * Since: 0.4
 */
ClutterBehaviour *
clutter_behaviour_ellipse_new (ClutterAlpha           *alpha,
                               gint                    x,
                               gint                    y,
                               gint                    width,
                               gint                    height,
                               ClutterRotateDirection  direction,
                               gdouble                 start,
                               gdouble                 end)
{
  ClutterKnot center;

  g_return_val_if_fail (alpha == NULL || CLUTTER_IS_ALPHA (alpha), NULL);

  center.x = x;
  center.y = y;

  return g_object_new (CLUTTER_TYPE_BEHAVIOUR_ELLIPSE,
                       "alpha", alpha,
                       "center", &center,
                       "width", width,
                       "height", height,
                       "direction", direction,
                       "angle-start", start,
                       "angle-end", end,
                       NULL);
}

/**
 * clutter_behaviour_ellipse_set_center:
 * @self: a #ClutterBehaviourEllipse
 * @x: x coordinace of centre
 * @y: y coordinace of centre
 *
 * Sets the center of the elliptical path to the point represented by knot.
 *
 * Since: 0.4
 */
void
clutter_behaviour_ellipse_set_center (ClutterBehaviourEllipse *self,
                                      gint                     x,
                                      gint                     y)
{
  ClutterBehaviourEllipsePrivate *priv;

  g_return_if_fail (CLUTTER_IS_BEHAVIOUR_ELLIPSE (self));

  priv = self->priv;

  if (priv->center.x != x || priv->center.y != y)
    {
      priv->center.x = x;
      priv->center.y = y;

      g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_CENTER]);
    }
}

/**
 * clutter_behaviour_ellipse_get_center:
 * @self: a #ClutterBehaviourEllipse
 * @x: (out): return location for the X coordinate of the center, or %NULL
 * @y: (out): return location for the Y coordinate of the center, or %NULL
 *
 * Gets the center of the elliptical path path.
 *
 * Since: 0.4
 */
void
clutter_behaviour_ellipse_get_center (ClutterBehaviourEllipse  *self,
                                      gint                     *x,
                                      gint                     *y)
{
  ClutterBehaviourEllipsePrivate *priv;

  g_return_if_fail (CLUTTER_IS_BEHAVIOUR_ELLIPSE (self));

  priv = self->priv;

  if (x)
    *x = priv->center.x;

  if (y)
    *y = priv->center.y;
}


/**
 * clutter_behaviour_ellipse_set_width:
 * @self: a #ClutterBehaviourEllipse
 * @width: width of the ellipse
 *
 * Sets the width of the elliptical path.
 *
 * Since: 0.4
 */
void
clutter_behaviour_ellipse_set_width (ClutterBehaviourEllipse *self,
                                     gint                     width)
{
  ClutterBehaviourEllipsePrivate *priv;

  g_return_if_fail (CLUTTER_IS_BEHAVIOUR_ELLIPSE (self));

  priv = self->priv;

  if (priv->a != width / 2)
    {
      priv->a = width / 2;

      g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_WIDTH]);
    }
}

/**
 * clutter_behaviour_ellipse_get_width:
 * @self: a #ClutterBehaviourEllipse
 *
 * Gets the width of the elliptical path.
 *
 * Return value: the width of the path
 *
 * Since: 0.4
 */
gint
clutter_behaviour_ellipse_get_width (ClutterBehaviourEllipse *self)
{
  g_return_val_if_fail (CLUTTER_IS_BEHAVIOUR_ELLIPSE (self), 0);

  return self->priv->a * 2;
}

/**
 * clutter_behaviour_ellipse_set_height:
 * @self: a #ClutterBehaviourEllipse
 * @height: height of the ellipse
 *
 * Sets the height of the elliptical path.
 *
 * Since: 0.4
 */
void
clutter_behaviour_ellipse_set_height (ClutterBehaviourEllipse *self,
                                      gint                     height)
{
  ClutterBehaviourEllipsePrivate *priv;

  g_return_if_fail (CLUTTER_IS_BEHAVIOUR_ELLIPSE (self));

  priv = self->priv;

  if (priv->b != height / 2)
    {
      priv->b = height / 2;

      g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_HEIGHT]);
    }
}

/**
 * clutter_behaviour_ellipse_get_height:
 * @self: a #ClutterBehaviourEllipse
 *
 * Gets the height of the elliptical path.
 *
 * Return value: the height of the path
 *
 * Since: 0.4
 */
gint
clutter_behaviour_ellipse_get_height (ClutterBehaviourEllipse *self)
{
  g_return_val_if_fail (CLUTTER_IS_BEHAVIOUR_ELLIPSE (self), 0);

  return self->priv->b * 2;
}

/**
 * clutter_behaviour_ellipse_set_angle_start:
 * @self: a #ClutterBehaviourEllipse
 * @angle_start: angle at which movement starts in degrees, between 0 and 360.
 *
 * Sets the angle at which movement starts; angles >= 360 degress get clamped
 * to the canonical interval <0, 360).
 *
 * Since: 0.6
 */
void
clutter_behaviour_ellipse_set_angle_start (ClutterBehaviourEllipse *self,
                                           gdouble                  angle_start)
{
  ClutterBehaviourEllipsePrivate *priv;
  gdouble new_angle;

  g_return_if_fail (CLUTTER_IS_BEHAVIOUR_ELLIPSE (self));

  new_angle = clamp_angle (angle_start);

  priv = self->priv;

  if (priv->angle_start != new_angle)
    {
      priv->angle_start = new_angle;
      g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_ANGLE_START]);
    }
}

/**
 * clutter_behaviour_ellipse_get_angle_start:
 * @self: a #ClutterBehaviourEllipse
 *
 * Gets the angle at which movements starts.
 *
 * Return value: angle in degrees
 *
 * Since: 0.6
 */
gdouble
clutter_behaviour_ellipse_get_angle_start (ClutterBehaviourEllipse *self)
{
  g_return_val_if_fail (CLUTTER_IS_BEHAVIOUR_ELLIPSE (self), 0.0);

  return self->priv->angle_start;
}

/**
 * clutter_behaviour_ellipse_set_angle_end:
 * @self: a #ClutterBehaviourEllipse
 * @angle_end: angle at which movement ends in degrees, between 0 and 360.
 *
 * Sets the angle at which movement ends; angles >= 360 degress get clamped
 * to the canonical interval <0, 360).
 *
 * Since: 0.4
 */
void
clutter_behaviour_ellipse_set_angle_end (ClutterBehaviourEllipse *self,
                                         gdouble                  angle_end)
{
  ClutterBehaviourEllipsePrivate *priv;
  gdouble new_angle;

  g_return_if_fail (CLUTTER_IS_BEHAVIOUR_ELLIPSE (self));

  new_angle = clamp_angle (angle_end);

  priv = self->priv;

  if (priv->angle_end != new_angle)
    {
      priv->angle_end = new_angle;

      g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_ANGLE_END]);
    }
}

/**
 * clutter_behaviour_ellipse_get_angle_end:
 * @self: a #ClutterBehaviourEllipse
 *
 * Gets the at which movements ends.
 *
 * Return value: angle in degrees
 *
 * Since: 0.4
 */
gdouble
clutter_behaviour_ellipse_get_angle_end (ClutterBehaviourEllipse *self)
{
  g_return_val_if_fail (CLUTTER_IS_BEHAVIOUR_ELLIPSE (self), 0.0);

  return self->priv->angle_end;
}

/**
 * clutter_behaviour_ellipse_set_angle_tilt:
 * @self: a #ClutterBehaviourEllipse
 * @axis: a #ClutterRotateAxis
 * @angle_tilt: tilt of the elipse around the center in the given axis in
 * degrees.
 *
 * Sets the angle at which the ellipse should be tilted around it's center.
 *
 * Since: 0.4
 */
void
clutter_behaviour_ellipse_set_angle_tilt (ClutterBehaviourEllipse *self,
                                          ClutterRotateAxis        axis,
                                          gdouble                  angle_tilt)
{
  ClutterBehaviourEllipsePrivate *priv;

  g_return_if_fail (CLUTTER_IS_BEHAVIOUR_ELLIPSE (self));

  priv = self->priv;

  switch (axis)
    {
    case CLUTTER_X_AXIS:
      if (priv->angle_tilt_x != angle_tilt)
        {
          priv->angle_tilt_x = angle_tilt;

          g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_ANGLE_TILT_X]);
        }
      break;

    case CLUTTER_Y_AXIS:
      if (priv->angle_tilt_y != angle_tilt)
        {
          priv->angle_tilt_y = angle_tilt;

          g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_ANGLE_TILT_Y]);
        }
      break;

    case CLUTTER_Z_AXIS:
      if (priv->angle_tilt_z != angle_tilt)
        {
          priv->angle_tilt_z = angle_tilt;

          g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_ANGLE_TILT_Z]);
        }
      break;
    }
}

/**
 * clutter_behaviour_ellipse_get_angle_tilt:
 * @self: a #ClutterBehaviourEllipse
 * @axis: a #ClutterRotateAxis
 *
 * Gets the tilt of the ellipse around the center in the given axis.
 *
 * Return value: angle in degrees.
 *
 * Since: 0.4
 */
gdouble
clutter_behaviour_ellipse_get_angle_tilt (ClutterBehaviourEllipse *self,
                                          ClutterRotateAxis        axis)
{
  g_return_val_if_fail (CLUTTER_IS_BEHAVIOUR_ELLIPSE (self), 0.0);

  switch (axis)
    {
    case CLUTTER_X_AXIS:
      return self->priv->angle_tilt_x;

    case CLUTTER_Y_AXIS:
      return self->priv->angle_tilt_y;

    case CLUTTER_Z_AXIS:
      return self->priv->angle_tilt_z;
    }

  return 0.0;
}

/**
 * clutter_behaviour_ellipse_set_tilt:
 * @self: a #ClutterBehaviourEllipse
 * @angle_tilt_x: tilt of the elipse around the center in X axis in degrees.
 * @angle_tilt_y: tilt of the elipse around the center in Y axis in degrees.
 * @angle_tilt_z: tilt of the elipse around the center in Z axis in degrees.
 *
 * Sets the angles at which the ellipse should be tilted around it's center.
 *
 * Since: 0.4
 */
void
clutter_behaviour_ellipse_set_tilt (ClutterBehaviourEllipse *self,
                                    gdouble                  angle_tilt_x,
                                    gdouble                  angle_tilt_y,
                                    gdouble                  angle_tilt_z)
{
  ClutterBehaviourEllipsePrivate *priv;

  g_return_if_fail (CLUTTER_IS_BEHAVIOUR_ELLIPSE (self));

  priv = self->priv;

  g_object_freeze_notify (G_OBJECT (self));

  if (priv->angle_tilt_x != angle_tilt_x)
    {
      priv->angle_tilt_x = angle_tilt_x;

      g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_ANGLE_TILT_X]);
    }

  if (priv->angle_tilt_y != angle_tilt_y)
    {
      priv->angle_tilt_y = angle_tilt_y;

      g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_ANGLE_TILT_Y]);
    }

  if (priv->angle_tilt_z != angle_tilt_z)
    {
      priv->angle_tilt_z = angle_tilt_z;

      g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_ANGLE_TILT_Z]);
    }

  g_object_thaw_notify (G_OBJECT (self));
}

/**
 * clutter_behaviour_ellipse_get_tilt:
 * @self: a #ClutterBehaviourEllipse
 * @angle_tilt_x: (out): return location for tilt angle on the X axis, or %NULL.
 * @angle_tilt_y: (out): return location for tilt angle on the Y axis, or %NULL.
 * @angle_tilt_z: (out): return location for tilt angle on the Z axis, or %NULL.
 *
 * Gets the tilt of the ellipse around the center in Y axis.
 *
 * Since: 0.4
 */
void
clutter_behaviour_ellipse_get_tilt (ClutterBehaviourEllipse *self,
                                    gdouble                 *angle_tilt_x,
                                    gdouble                 *angle_tilt_y,
                                    gdouble                 *angle_tilt_z)
{
  ClutterBehaviourEllipsePrivate *priv;

  g_return_if_fail (CLUTTER_IS_BEHAVIOUR_ELLIPSE (self));

  priv = self->priv;

  if (angle_tilt_x)
    *angle_tilt_x = priv->angle_tilt_x;

  if (angle_tilt_y)
    *angle_tilt_y = priv->angle_tilt_y;

  if (angle_tilt_z)
    *angle_tilt_z = priv->angle_tilt_z;
}

/**
 * clutter_behaviour_ellipse_get_direction:
 * @self: a #ClutterBehaviourEllipse
 *
 * Retrieves the #ClutterRotateDirection used by the ellipse behaviour.
 *
 * Return value: the rotation direction
 *
 * Since: 0.4
 */
ClutterRotateDirection
clutter_behaviour_ellipse_get_direction (ClutterBehaviourEllipse *self)
{
  g_return_val_if_fail (CLUTTER_IS_BEHAVIOUR_ELLIPSE (self),
                        CLUTTER_ROTATE_CW);

  return self->priv->direction;
}

/**
 * clutter_behaviour_ellipse_set_direction:
 * @self: a #ClutterBehaviourEllipse
 * @direction: the rotation direction
 *
 * Sets the rotation direction used by the ellipse behaviour.
 *
 * Since: 0.4
 */
void
clutter_behaviour_ellipse_set_direction (ClutterBehaviourEllipse *self,
                                         ClutterRotateDirection  direction)
{
  ClutterBehaviourEllipsePrivate *priv;

  g_return_if_fail (CLUTTER_IS_BEHAVIOUR_ELLIPSE (self));

  priv = self->priv;

  if (priv->direction != direction)
    {
      priv->direction = direction;

      g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_DIRECTION]);
    }
}
