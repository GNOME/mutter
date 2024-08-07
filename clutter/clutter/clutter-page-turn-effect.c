/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2010  Intel Corporation.
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
 *
 * Author:
 *   Emmanuele Bassi <ebassi@linux.intel.com>
 *
 * Based on MxDeformPageTurn, written by:
 *   Chris Lord <chris@linux.intel.com>
 */

/**
 * ClutterPageTurnEffect:
 *
 * A page turning effect
 *
 * A simple page turning effect
 */

#include "config.h"

#include <math.h>

#include "clutter/clutter-page-turn-effect.h"

#include "clutter/clutter-debug.h"
#include "clutter/clutter-private.h"

struct _ClutterPageTurnEffect
{
  ClutterDeformEffect parent_instance;

  gdouble period;
  gdouble angle;

  gfloat radius;
};

enum
{
  PROP_0,

  PROP_PERIOD,
  PROP_ANGLE,
  PROP_RADIUS,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

G_DEFINE_TYPE (ClutterPageTurnEffect,
               clutter_page_turn_effect,
               CLUTTER_TYPE_DEFORM_EFFECT);

static void
clutter_page_turn_effect_deform_vertex (ClutterDeformEffect  *effect,
                                        gfloat                width,
                                        gfloat                height,
                                        ClutterTextureVertex *vertex)
{
  ClutterPageTurnEffect *self = CLUTTER_PAGE_TURN_EFFECT (effect);
  gfloat cx, cy, rx, ry, radians, turn_angle;
  float shade;

  if (self->period == 0.0)
    return;

  radians = (float) (self->angle / (180.0f / G_PI));

  /* Rotate the point around the centre of the page-curl ray to align it with
   * the y-axis.
   */
  cx = (float) (1.0f - self->period) * width;
  cy = (float) (1.0f - self->period) * height;

  rx = ((vertex->x - cx) * cosf (- radians))
     - ((vertex->y - cy) * sinf (- radians))
     - self->radius;
  ry = ((vertex->x - cx) * sinf (- radians))
     + ((vertex->y - cy) * cosf (- radians));

  turn_angle = 0.f;
  if (rx > self->radius * -2.0f)
    {
      /* Calculate the curl angle as a function from the distance of the curl
       * ray (i.e. the page crease)
       */
      turn_angle = (float) ((rx / self->radius * G_PI_2) - G_PI_2);
      shade = ((sinf (turn_angle) * 96.0f) + 159.0f) / 255.0f;

      /* Add a gradient that makes it look like lighting and hides the switch
       * between textures.
       */
      cogl_color_init_from_4f (&vertex->color, shade, shade, shade, 1.0f);
    }

  if (rx > 0)
    {
      /* Make the curl radius smaller as more circles are formed (stops
       * z-fighting and looks cool). Note that 10 is a semi-arbitrary
       * number here - divide it by two and it's the amount of space
       * between curled layers of the texture, in pixels.
       */
      gfloat small_radius;

      small_radius = (float) (self->radius -
                              MIN (self->radius, (turn_angle * 10) / G_PI));

      /* Calculate a point on a cylinder (maybe make this a cone at some
       * point) and rotate it by the specified angle.
       */
      rx = (small_radius * cosf (turn_angle)) + self->radius;

      vertex->x = (rx * cosf (radians)) - (ry * sinf (radians)) + cx;
      vertex->y = (rx * sinf (radians)) + (ry * cosf (radians)) + cy;
      vertex->z = (small_radius * sinf (turn_angle)) + self->radius;
    }
}

static void
clutter_page_turn_effect_set_property (GObject      *gobject,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  ClutterPageTurnEffect *effect = CLUTTER_PAGE_TURN_EFFECT (gobject);

  switch (prop_id)
    {
    case PROP_PERIOD:
      clutter_page_turn_effect_set_period (effect, g_value_get_double (value));
      break;

    case PROP_ANGLE:
      clutter_page_turn_effect_set_angle (effect, g_value_get_double (value));
      break;

    case PROP_RADIUS:
      clutter_page_turn_effect_set_radius (effect, g_value_get_float (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_page_turn_effect_get_property (GObject    *gobject,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  ClutterPageTurnEffect *effect = CLUTTER_PAGE_TURN_EFFECT (gobject);

  switch (prop_id)
    {
    case PROP_PERIOD:
      g_value_set_double (value, effect->period);
      break;

    case PROP_ANGLE:
      g_value_set_double (value, effect->angle);
      break;

    case PROP_RADIUS:
      g_value_set_float (value, effect->radius);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_page_turn_effect_class_init (ClutterPageTurnEffectClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterDeformEffectClass *deform_class = CLUTTER_DEFORM_EFFECT_CLASS (klass);
  GParamSpec *pspec;

  gobject_class->set_property = clutter_page_turn_effect_set_property;
  gobject_class->get_property = clutter_page_turn_effect_get_property;

  /**
   * ClutterPageTurnEffect:period:
   *
   * The period of the page turn, between 0.0 (no curling) and
   * 1.0 (fully curled)
   */
  pspec = g_param_spec_double ("period", NULL, NULL,
                               0.0, 1.0,
                               0.0,
                               G_PARAM_READWRITE |
                               G_PARAM_STATIC_STRINGS);
  obj_props[PROP_PERIOD] = pspec;
  g_object_class_install_property (gobject_class, PROP_PERIOD, pspec);

  /**
   * ClutterPageTurnEffect:angle:
   *
   * The angle of the page rotation, in degrees, between 0.0 and 360.0
   */
  pspec = g_param_spec_double ("angle", NULL, NULL,
                               0.0, 360.0,
                               0.0,
                               G_PARAM_READWRITE |
                               G_PARAM_STATIC_STRINGS);
  obj_props[PROP_ANGLE] = pspec;
  g_object_class_install_property (gobject_class, PROP_ANGLE, pspec);

  /**
   * ClutterPageTurnEffect:radius:
   *
   * The radius of the page curl, in pixels
   */
  pspec = g_param_spec_float ("radius", NULL, NULL,
                              -G_MAXFLOAT, G_MAXFLOAT,
                              24.0,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS);
  obj_props[PROP_RADIUS] = pspec;
  g_object_class_install_property (gobject_class, PROP_RADIUS, pspec);

  deform_class->deform_vertex = clutter_page_turn_effect_deform_vertex;
}

static void
clutter_page_turn_effect_init (ClutterPageTurnEffect *self)
{
  self->period = 0.0;
  self->angle = 0.0;
  self->radius = 24.0f;
}

/**
 * clutter_page_turn_effect_new:
 * @period: the period of the page curl, between 0.0 and 1.0
 * @angle: the angle of the page curl, between 0.0 and 360.0
 * @radius: the radius of the page curl, in pixels
 *
 * Creates a new #ClutterPageTurnEffect instance with the given parameters
 *
 * Return value: the newly created #ClutterPageTurnEffect
 */
ClutterEffect *
clutter_page_turn_effect_new (gdouble period,
                              gdouble angle,
                              gfloat  radius)
{
  g_return_val_if_fail (period >= 0.0 && period <= 1.0, NULL);
  g_return_val_if_fail (angle >= 0.0 && angle <= 360.0, NULL);

  return g_object_new (CLUTTER_TYPE_PAGE_TURN_EFFECT,
                       "period", period,
                       "angle", angle,
                       "radius", radius,
                       NULL);
}

/**
 * clutter_page_turn_effect_set_period:
 * @effect: a #ClutterPageTurnEffect
 * @period: the period of the page curl, between 0.0 and 1.0
 *
 * Sets the period of the page curling, between 0.0 (no curling)
 * and 1.0 (fully curled)
 */
void
clutter_page_turn_effect_set_period (ClutterPageTurnEffect *effect,
                                     gdouble                period)
{
  g_return_if_fail (CLUTTER_IS_PAGE_TURN_EFFECT (effect));
  g_return_if_fail (period >= 0.0 && period <= 1.0);

  effect->period = period;

  clutter_deform_effect_invalidate (CLUTTER_DEFORM_EFFECT (effect));

  g_object_notify_by_pspec (G_OBJECT (effect), obj_props[PROP_PERIOD]);
}

/**
 * clutter_page_turn_effect_get_period:
 * @effect: a #ClutterPageTurnEffect
 *
 * Retrieves the value set using [method@PageTurnEffect.get_period]
 *
 * Return value: the period of the page curling
 */
gdouble
clutter_page_turn_effect_get_period (ClutterPageTurnEffect *effect)
{
  g_return_val_if_fail (CLUTTER_IS_PAGE_TURN_EFFECT (effect), 0.0);

  return effect->period;
}

/**
 * clutter_page_turn_effect_set_angle:
 * @effect: #ClutterPageTurnEffect
 * @angle: the angle of the page curl, in degrees
 *
 * Sets the angle of the page curling, in degrees
 */
void
clutter_page_turn_effect_set_angle (ClutterPageTurnEffect *effect,
                                    gdouble                angle)
{
  g_return_if_fail (CLUTTER_IS_PAGE_TURN_EFFECT (effect));
  g_return_if_fail (angle >= 0.0 && angle <= 360.0);

  effect->angle = angle;

  clutter_deform_effect_invalidate (CLUTTER_DEFORM_EFFECT (effect));

  g_object_notify_by_pspec (G_OBJECT (effect), obj_props[PROP_ANGLE]);
}

/**
 * clutter_page_turn_effect_get_angle:
 * @effect: a #ClutterPageTurnEffect:
 *
 * Retrieves the value set using [method@PageTurnEffect.get_angle]
 *
 * Return value: the angle of the page curling
 */
gdouble
clutter_page_turn_effect_get_angle (ClutterPageTurnEffect *effect)
{
  g_return_val_if_fail (CLUTTER_IS_PAGE_TURN_EFFECT (effect), 0.0);

  return effect->angle;
}

/**
 * clutter_page_turn_effect_set_radius:
 * @effect: a #ClutterPageTurnEffect:
 * @radius: the radius of the page curling, in pixels
 *
 * Sets the radius of the page curling
 */
void
clutter_page_turn_effect_set_radius (ClutterPageTurnEffect *effect,
                                     gfloat                 radius)
{
  g_return_if_fail (CLUTTER_IS_PAGE_TURN_EFFECT (effect));

  effect->radius = radius;

  clutter_deform_effect_invalidate (CLUTTER_DEFORM_EFFECT (effect));

  g_object_notify_by_pspec (G_OBJECT (effect), obj_props[PROP_RADIUS]);
}

/**
 * clutter_page_turn_effect_get_radius:
 * @effect: a #ClutterPageTurnEffect
 *
 * Retrieves the value set using [method@PageTurnEffect.set_radius]
 *
 * Return value: the radius of the page curling
 */
gfloat
clutter_page_turn_effect_get_radius (ClutterPageTurnEffect *effect)
{
  g_return_val_if_fail (CLUTTER_IS_PAGE_TURN_EFFECT (effect), 0.0);

  return effect->radius;
}
