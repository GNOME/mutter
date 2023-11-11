/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2010-2012 Inclusive Design Research Centre, OCAD University.
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
 *   Joseph Scheuhammer <clown@alum.mit.edu>
 */

/**
 * ClutterBrightnessContrastEffect:
 *
 * Increase/decrease brightness and/or contrast of actor.
 *
 * #ClutterBrightnessContrastEffect is a sub-class of #ClutterEffect that
 * changes the overall brightness of a #ClutterActor.
 */
#include "config.h"

#include <math.h>

#include "cogl/cogl.h"

#include "clutter/clutter-brightness-contrast-effect.h"
#include "clutter/clutter-debug.h"
#include "clutter/clutter-enum-types.h"
#include "clutter/clutter-private.h"

typedef struct _ClutterBrightnessContrastEffectPrivate
{
  /* Brightness and contrast changes. */
  gfloat brightness_red;
  gfloat brightness_green;
  gfloat brightness_blue;

  gfloat contrast_red;
  gfloat contrast_green;
  gfloat contrast_blue;

  gint brightness_multiplier_uniform;
  gint brightness_offset_uniform;
  gint contrast_uniform;

  CoglPipeline *pipeline;
} ClutterBrightnessContrastEffectPrivate;


/* Brightness effects in GLSL.
 */
static const gchar *brightness_contrast_decls =
  "uniform vec3 brightness_multiplier;\n"
  "uniform vec3 brightness_offset;\n"
  "uniform vec3 contrast;\n";

static const gchar *brightness_contrast_source =
  /* Apply the brightness. The brightness_offset is multiplied by the
     alpha to keep the color pre-multiplied */
  "cogl_color_out.rgb = (cogl_color_out.rgb * brightness_multiplier +\n"
  "                      brightness_offset * cogl_color_out.a);\n"
  /* Apply the contrast */
  "cogl_color_out.rgb = ((cogl_color_out.rgb - 0.5 * cogl_color_out.a) *\n"
  "                      contrast + 0.5 * cogl_color_out.a);\n";

static const ClutterColor no_brightness_change = { 0x7f, 0x7f, 0x7f, 0xff };
static const ClutterColor no_contrast_change = { 0x7f, 0x7f, 0x7f, 0xff };
static const gfloat no_change = 0.0f;

enum
{
  PROP_0,

  PROP_BRIGHTNESS,
  PROP_CONTRAST,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

G_DEFINE_TYPE_WITH_PRIVATE (ClutterBrightnessContrastEffect,
                            clutter_brightness_contrast_effect,
                            CLUTTER_TYPE_OFFSCREEN_EFFECT)

static gboolean
will_have_no_effect (ClutterBrightnessContrastEffect *self)
{
  ClutterBrightnessContrastEffectPrivate *priv =
    clutter_brightness_contrast_effect_get_instance_private (self);

  return (G_APPROX_VALUE (priv->brightness_red, no_change, FLT_EPSILON) &&
          G_APPROX_VALUE (priv->brightness_green, no_change, FLT_EPSILON) &&
          G_APPROX_VALUE (priv->brightness_blue, no_change, FLT_EPSILON) &&
          G_APPROX_VALUE (priv->contrast_red, no_change, FLT_EPSILON) &&
          G_APPROX_VALUE (priv->contrast_green, no_change, FLT_EPSILON) &&
          G_APPROX_VALUE (priv->contrast_blue, no_change, FLT_EPSILON));
}

static CoglPipeline *
clutter_brightness_contrast_effect_create_pipeline (ClutterOffscreenEffect *effect,
                                                    CoglTexture            *texture)
{
  ClutterBrightnessContrastEffect *self =
    CLUTTER_BRIGHTNESS_CONTRAST_EFFECT (effect);
  ClutterBrightnessContrastEffectPrivate *priv =
    clutter_brightness_contrast_effect_get_instance_private (self);

  cogl_pipeline_set_layer_texture (priv->pipeline, 0, texture);

  return g_object_ref (priv->pipeline);
}

static gboolean
clutter_brightness_contrast_effect_pre_paint (ClutterEffect       *effect,
                                              ClutterPaintNode    *node,
                                              ClutterPaintContext *paint_context)
{
  ClutterBrightnessContrastEffect *self = CLUTTER_BRIGHTNESS_CONTRAST_EFFECT (effect);
  ClutterEffectClass *parent_class;

  if (will_have_no_effect (self))
    return FALSE;

  parent_class =
    CLUTTER_EFFECT_CLASS (clutter_brightness_contrast_effect_parent_class);

  return parent_class->pre_paint (effect, node, paint_context);
}

static void
clutter_brightness_contrast_effect_dispose (GObject *gobject)
{
  ClutterBrightnessContrastEffect *self = CLUTTER_BRIGHTNESS_CONTRAST_EFFECT (gobject);
  ClutterBrightnessContrastEffectPrivate *priv =
    clutter_brightness_contrast_effect_get_instance_private (self);

  g_clear_object (&priv->pipeline);

  G_OBJECT_CLASS (clutter_brightness_contrast_effect_parent_class)->dispose (gobject);
}

static void
clutter_brightness_contrast_effect_set_property (GObject      *gobject,
                                                 guint        prop_id,
                                                 const GValue *value,
                                                 GParamSpec   *pspec)
{
  ClutterBrightnessContrastEffect *effect = CLUTTER_BRIGHTNESS_CONTRAST_EFFECT (gobject);

  switch (prop_id)
    {
    case PROP_BRIGHTNESS:
      {
        const ClutterColor *color = clutter_value_get_color (value);
        clutter_brightness_contrast_effect_set_brightness_full (effect,
                                                                color->red / 127.0f - 1.0f,
                                                                color->green / 127.0f - 1.0f,
                                                                color->blue / 127.0f - 1.0f);
      }
      break;

    case PROP_CONTRAST:
      {
        const ClutterColor *color = clutter_value_get_color (value);
        clutter_brightness_contrast_effect_set_contrast_full (effect,
                                                              color->red / 127.0f - 1.0f,
                                                              color->green / 127.0f - 1.0f,
                                                              color->blue / 127.0f - 1.0f);
      }
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_brightness_contrast_effect_get_property (GObject    *gobject,
                                                 guint      prop_id,
                                                 GValue     *value,
                                                 GParamSpec *pspec)
{
  ClutterBrightnessContrastEffect *effect = CLUTTER_BRIGHTNESS_CONTRAST_EFFECT (gobject);
  ClutterBrightnessContrastEffectPrivate *priv =
    clutter_brightness_contrast_effect_get_instance_private (effect);
  ClutterColor color;

  switch (prop_id)
    {
    case PROP_BRIGHTNESS:
      {
        color.red = (priv->brightness_red + 1.0f) * 127.0f;
        color.green = (priv->brightness_green + 1.0f) * 127.0f;
        color.blue = (priv->brightness_blue + 1.0f) * 127.0f;
        color.alpha = 0xff;

        clutter_value_set_color (value, &color);
      }
      break;

    case PROP_CONTRAST:
      {
        color.red = (priv->contrast_red + 1.0f) * 127.0f;
        color.green = (priv->contrast_green + 1.0f) * 127.0f;
        color.blue = (priv->contrast_blue + 1.0f) * 127.0f;
        color.alpha = 0xff;

        clutter_value_set_color (value, &color);
      }
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_brightness_contrast_effect_class_init (ClutterBrightnessContrastEffectClass *klass)
{
  ClutterEffectClass *effect_class = CLUTTER_EFFECT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterOffscreenEffectClass *offscreen_class;

  offscreen_class = CLUTTER_OFFSCREEN_EFFECT_CLASS (klass);
  offscreen_class->create_pipeline = clutter_brightness_contrast_effect_create_pipeline;

  effect_class->pre_paint = clutter_brightness_contrast_effect_pre_paint;

  gobject_class->set_property = clutter_brightness_contrast_effect_set_property;
  gobject_class->get_property = clutter_brightness_contrast_effect_get_property;
  gobject_class->dispose = clutter_brightness_contrast_effect_dispose;

  /**
   * ClutterBrightnessContrastEffect:brightness:
   *
   * The brightness change to apply to the effect.
   *
   * This property uses a #ClutterColor to represent the changes to each
   * color channel. The range is [ 0, 255 ], with 127 as the value used
   * to indicate no change; values smaller than 127 indicate a decrease
   * in brightness, and values larger than 127 indicate an increase in
   * brightness.
   */
  obj_props[PROP_BRIGHTNESS] =
    clutter_param_spec_color ("brightness", NULL, NULL,
                              &no_brightness_change,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS);

  /**
   * ClutterBrightnessContrastEffect:contrast:
   *
   * The contrast change to apply to the effect.
   *
   * This property uses a #ClutterColor to represent the changes to each
   * color channel. The range is [ 0, 255 ], with 127 as the value used
   * to indicate no change; values smaller than 127 indicate a decrease
   * in contrast, and values larger than 127 indicate an increase in
   * contrast.
   */
  obj_props[PROP_CONTRAST] =
    clutter_param_spec_color ("contrast", NULL, NULL,
                              &no_contrast_change,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, PROP_LAST, obj_props);
}

static void
get_brightness_values (gfloat  value,
                       gfloat *multiplier,
                       gfloat *offset)
{
  if (value < 0.0f)
    {
      *multiplier = 1.0f + value;
      *offset = 0.0f;
    }
  else
    {
      *multiplier = 1.0f - value;
      *offset = value;
    }
}

static inline void
update_uniforms (ClutterBrightnessContrastEffect *self)
{
  ClutterBrightnessContrastEffectPrivate *priv =
    clutter_brightness_contrast_effect_get_instance_private (self);

  if (priv->brightness_multiplier_uniform > -1 &&
      priv->brightness_offset_uniform > -1)
    {
      float brightness_multiplier[3];
      float brightness_offset[3];

      get_brightness_values (priv->brightness_red,
                             brightness_multiplier + 0,
                             brightness_offset + 0);
      get_brightness_values (priv->brightness_green,
                             brightness_multiplier + 1,
                             brightness_offset + 1);
      get_brightness_values (priv->brightness_blue,
                             brightness_multiplier + 2,
                             brightness_offset + 2);

      cogl_pipeline_set_uniform_float (priv->pipeline,
                                       priv->brightness_multiplier_uniform,
                                       3, /* n_components */
                                       1, /* count */
                                       brightness_multiplier);
      cogl_pipeline_set_uniform_float (priv->pipeline,
                                       priv->brightness_offset_uniform,
                                       3, /* n_components */
                                       1, /* count */
                                       brightness_offset);
    }

  if (priv->contrast_uniform > -1)
    {
      float contrast[3] = {
        tan ((priv->contrast_red + 1) * G_PI_4),
        tan ((priv->contrast_green + 1) * G_PI_4),
        tan ((priv->contrast_blue + 1) * G_PI_4)
      };

      cogl_pipeline_set_uniform_float (priv->pipeline,
                                       priv->contrast_uniform,
                                       3, /* n_components */
                                       1, /* count */
                                       contrast);
    }
}

static void
clutter_brightness_contrast_effect_init (ClutterBrightnessContrastEffect *self)
{
  ClutterBrightnessContrastEffectClass *klass;
  ClutterBrightnessContrastEffectPrivate *priv =
    clutter_brightness_contrast_effect_get_instance_private (self);

  priv->brightness_red = no_change;
  priv->brightness_green = no_change;
  priv->brightness_blue = no_change;

  priv->contrast_red = no_change;
  priv->contrast_green = no_change;
  priv->contrast_blue = no_change;

  klass = CLUTTER_BRIGHTNESS_CONTRAST_EFFECT_GET_CLASS (self);

  if (G_UNLIKELY (klass->base_pipeline == NULL))
    {
      CoglSnippet *snippet;
      CoglContext *ctx =
        clutter_backend_get_cogl_context (clutter_get_default_backend ());

      klass->base_pipeline = cogl_pipeline_new (ctx);

      snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                                  brightness_contrast_decls,
                                  brightness_contrast_source);
      cogl_pipeline_add_snippet (klass->base_pipeline, snippet);
      g_object_unref (snippet);

      cogl_pipeline_set_layer_null_texture (klass->base_pipeline, 0);
    }

  priv->pipeline = cogl_pipeline_copy (klass->base_pipeline);

  priv->brightness_multiplier_uniform =
    cogl_pipeline_get_uniform_location (priv->pipeline,
                                        "brightness_multiplier");
  priv->brightness_offset_uniform =
    cogl_pipeline_get_uniform_location (priv->pipeline,
                                        "brightness_offset");
  priv->contrast_uniform =
    cogl_pipeline_get_uniform_location (priv->pipeline, "contrast");

  update_uniforms (self);
}

/**
 * clutter_brightness_contrast_effect_new:
 *
 * Creates a new #ClutterBrightnessContrastEffect to be used with
 * [method@Clutter.Actor.add_effect]
 *
 * Return value: (transfer full): the newly created
 *   #ClutterBrightnessContrastEffect or %NULL.  Use g_object_unref() when
 *   done.
 */
ClutterEffect *
clutter_brightness_contrast_effect_new (void)
{
  return g_object_new (CLUTTER_TYPE_BRIGHTNESS_CONTRAST_EFFECT, NULL);
}

/**
 * clutter_brightness_contrast_effect_set_brightness_full:
 * @effect: a #ClutterBrightnessContrastEffect
 * @red: red component of the change in brightness
 * @green: green component of the change in brightness
 * @blue: blue component of the change in brightness
 *
 * The range for each component is [-1.0, 1.0] where 0.0 designates no change,
 * values below 0.0 mean a decrease in brightness, and values above indicate
 * an increase.
 */
void
clutter_brightness_contrast_effect_set_brightness_full (ClutterBrightnessContrastEffect *effect,
                                                        gfloat                           red,
                                                        gfloat                           green,
                                                        gfloat                           blue)
{
  ClutterBrightnessContrastEffectPrivate *priv;

  g_return_if_fail (CLUTTER_IS_BRIGHTNESS_CONTRAST_EFFECT (effect));

  priv = clutter_brightness_contrast_effect_get_instance_private (effect);
  if (G_APPROX_VALUE (red, priv->brightness_red, FLT_EPSILON) &&
      G_APPROX_VALUE (green, priv->brightness_green, FLT_EPSILON) &&
      G_APPROX_VALUE (blue, priv->brightness_blue, FLT_EPSILON))
    return;

  priv->brightness_red = red;
  priv->brightness_green = green;
  priv->brightness_blue = blue;

  update_uniforms (effect);

  clutter_effect_queue_repaint (CLUTTER_EFFECT (effect));

  g_object_notify_by_pspec (G_OBJECT (effect), obj_props[PROP_BRIGHTNESS]);
}

/**
 * clutter_brightness_contrast_effect_get_brightness:
 * @effect: a #ClutterBrightnessContrastEffect
 * @red: (out) (allow-none): return location for red component of the
 *    change in brightness
 * @green: (out) (allow-none): return location for green component of the
 *    change in brightness
 * @blue: (out) (allow-none): return location for blue component of the
 *    change in brightness
 *
 * Retrieves the change in brightness used by @effect.
 */
void
clutter_brightness_contrast_effect_get_brightness (ClutterBrightnessContrastEffect *effect,
                                                   gfloat                          *red,
                                                   gfloat                          *green,
                                                   gfloat                          *blue)
{
  ClutterBrightnessContrastEffectPrivate *priv;

  g_return_if_fail (CLUTTER_IS_BRIGHTNESS_CONTRAST_EFFECT (effect));

  priv = clutter_brightness_contrast_effect_get_instance_private (effect);
  if (red != NULL)
    *red = priv->brightness_red;

  if (green != NULL)
    *green = priv->brightness_green;

  if (blue != NULL)
    *blue = priv->brightness_blue;
}

/**
 * clutter_brightness_contrast_effect_set_brightness:
 * @effect: a #ClutterBrightnessContrastEffect
 * @brightness:  the brightness change for all three components (r, g, b)
 *
 * The range of @brightness is [-1.0, 1.0], where 0.0 designates no change;
 * a value below 0.0 indicates a decrease in brightness; and a value
 * above 0.0 indicates an increase of brightness.
 */
void
clutter_brightness_contrast_effect_set_brightness (ClutterBrightnessContrastEffect *effect,
                                                   gfloat                           brightness)
{
  clutter_brightness_contrast_effect_set_brightness_full (effect,
                                                          brightness,
                                                          brightness,
                                                          brightness);
}

/**
 * clutter_brightness_contrast_effect_set_contrast_full:
 * @effect: a #ClutterBrightnessContrastEffect
 * @red: red component of the change in contrast
 * @green: green component of the change in contrast
 * @blue: blue component of the change in contrast
 *
 * The range for each component is [-1.0, 1.0] where 0.0 designates no change,
 * values below 0.0 mean a decrease in contrast, and values above indicate
 * an increase.
 */
void
clutter_brightness_contrast_effect_set_contrast_full (ClutterBrightnessContrastEffect *effect,
                                                      gfloat                          red,
                                                      gfloat                          green,
                                                      gfloat                          blue)
{
  ClutterBrightnessContrastEffectPrivate *priv;

  g_return_if_fail (CLUTTER_IS_BRIGHTNESS_CONTRAST_EFFECT (effect));

  priv = clutter_brightness_contrast_effect_get_instance_private (effect);
  if (G_APPROX_VALUE (red, priv->contrast_red, FLT_EPSILON) &&
      G_APPROX_VALUE (green, priv->contrast_green, FLT_EPSILON) &&
      G_APPROX_VALUE (blue, priv->contrast_blue, FLT_EPSILON))
    return;

  priv->contrast_red = red;
  priv->contrast_green = green;
  priv->contrast_blue = blue;

  update_uniforms (effect);

  clutter_effect_queue_repaint (CLUTTER_EFFECT (effect));

  g_object_notify_by_pspec (G_OBJECT (effect), obj_props[PROP_CONTRAST]);
}

/**
 * clutter_brightness_contrast_effect_get_contrast:
 * @effect: a #ClutterBrightnessContrastEffect
 * @red: (out) (allow-none): return location for red component of the
 *    change in contrast
 * @green: (out) (allow-none): return location for green component of the
 *    change in contrast
 * @blue: (out) (allow-none): return location for blue component of the
 *    change in contrast
 *
 * Retrieves the contrast value used by @effect.
 */
void
clutter_brightness_contrast_effect_get_contrast (ClutterBrightnessContrastEffect *effect,
                                                 gfloat                          *red,
                                                 gfloat                          *green,
                                                 gfloat                          *blue)
{
  ClutterBrightnessContrastEffectPrivate *priv;

  g_return_if_fail (CLUTTER_IS_BRIGHTNESS_CONTRAST_EFFECT (effect));

  priv = clutter_brightness_contrast_effect_get_instance_private (effect);
  if (red != NULL)
    *red = priv->contrast_red;

  if (green != NULL)
    *green = priv->contrast_green;

  if (blue != NULL)
    *blue = priv->contrast_blue;
}

/**
 * clutter_brightness_contrast_effect_set_contrast:
 * @effect: a #ClutterBrightnessContrastEffect
 * @contrast: contrast change for all three channels
 *
 * The range for @contrast is [-1.0, 1.0], where 0.0 designates no change;
 * a value below 0.0 indicates a decrease in contrast; and a value above
 * 0.0 indicates an increase.
 */
void
clutter_brightness_contrast_effect_set_contrast (ClutterBrightnessContrastEffect *effect,
                                                 gfloat                           contrast)
{
  clutter_brightness_contrast_effect_set_contrast_full (effect,
                                                        contrast,
                                                        contrast,
                                                        contrast);
}
