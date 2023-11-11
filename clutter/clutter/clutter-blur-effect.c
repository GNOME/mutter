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
 */

/**
 * ClutterBlurEffect:
 *
 * A blur effect
 *
 * #ClutterBlurEffect is a sub-class of #ClutterEffect that allows blurring a
 * actor and its contents.
 */
#include "config.h"

#include "clutter/clutter-blur-effect.h"

#include "cogl/cogl.h"

#include "clutter/clutter-debug.h"
#include "clutter/clutter-private.h"

#define BLUR_PADDING    2

/* FIXME - lame shader; we should really have a decoupled
 * horizontal/vertical two pass shader for the gaussian blur
 */
static const gchar *box_blur_glsl_declarations =
"uniform vec2 pixel_step;\n";
#define SAMPLE(offx, offy) \
  "cogl_texel += texture2D (cogl_sampler, cogl_tex_coord.st + pixel_step * " \
  "vec2 (" G_STRINGIFY (offx) ", " G_STRINGIFY (offy) "));\n"
static const gchar *box_blur_glsl_shader =
"  cogl_texel = texture2D (cogl_sampler, cogl_tex_coord.st);\n"
  SAMPLE (-1.0, -1.0)
  SAMPLE ( 0.0, -1.0)
  SAMPLE (+1.0, -1.0)
  SAMPLE (-1.0,  0.0)
  SAMPLE (+1.0,  0.0)
  SAMPLE (-1.0, +1.0)
  SAMPLE ( 0.0, +1.0)
  SAMPLE (+1.0, +1.0)
"  cogl_texel /= 9.0;\n";
#undef SAMPLE

typedef struct _ClutterBlurEffectPrivate
{
  /* a back pointer to our actor, so that we can query it */
  ClutterActor *actor;

  gint pixel_step_uniform;

  CoglPipeline *pipeline;
} ClutterBlurEffectPrivate;


G_DEFINE_TYPE_WITH_PRIVATE (ClutterBlurEffect,
                            clutter_blur_effect,
                            CLUTTER_TYPE_OFFSCREEN_EFFECT);

static CoglPipeline *
clutter_blur_effect_create_pipeline (ClutterOffscreenEffect *effect,
                                     CoglTexture            *texture)
{
  ClutterBlurEffect *blur_effect = CLUTTER_BLUR_EFFECT (effect);
  ClutterBlurEffectPrivate *priv =
    clutter_blur_effect_get_instance_private (blur_effect);

  if (priv->pixel_step_uniform > -1)
    {
      float pixel_step[2];
      int tex_width, tex_height;

      tex_width = cogl_texture_get_width (texture);
      tex_height = cogl_texture_get_height (texture);

      pixel_step[0] = 1.0f / tex_width;
      pixel_step[1] = 1.0f / tex_height;

      cogl_pipeline_set_uniform_float (priv->pipeline,
                                       priv->pixel_step_uniform,
                                       2, /* n_components */
                                       1, /* count */
                                       pixel_step);
    }

  cogl_pipeline_set_layer_texture (priv->pipeline, 0, texture);

  return g_object_ref (priv->pipeline);
}

static gboolean
clutter_blur_effect_modify_paint_volume (ClutterEffect      *effect,
                                         ClutterPaintVolume *volume)
{
  gfloat cur_width, cur_height;
  graphene_point3d_t origin;

  clutter_paint_volume_get_origin (volume, &origin);
  cur_width = clutter_paint_volume_get_width (volume);
  cur_height = clutter_paint_volume_get_height (volume);

  origin.x -= BLUR_PADDING;
  origin.y -= BLUR_PADDING;
  cur_width += 2 * BLUR_PADDING;
  cur_height += 2 * BLUR_PADDING;
  clutter_paint_volume_set_origin (volume, &origin);
  clutter_paint_volume_set_width (volume, cur_width);
  clutter_paint_volume_set_height (volume, cur_height);

  return TRUE;
}

static void
clutter_blur_effect_dispose (GObject *gobject)
{
  ClutterBlurEffect *self = CLUTTER_BLUR_EFFECT (gobject);
  ClutterBlurEffectPrivate *priv =
    clutter_blur_effect_get_instance_private (self);

  g_clear_object (&priv->pipeline);

  G_OBJECT_CLASS (clutter_blur_effect_parent_class)->dispose (gobject);
}

static void
clutter_blur_effect_class_init (ClutterBlurEffectClass *klass)
{
  ClutterEffectClass *effect_class = CLUTTER_EFFECT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterOffscreenEffectClass *offscreen_class;

  gobject_class->dispose = clutter_blur_effect_dispose;

  effect_class->modify_paint_volume = clutter_blur_effect_modify_paint_volume;

  offscreen_class = CLUTTER_OFFSCREEN_EFFECT_CLASS (klass);
  offscreen_class->create_pipeline = clutter_blur_effect_create_pipeline;
}

static void
clutter_blur_effect_init (ClutterBlurEffect *self)
{
  ClutterBlurEffectClass *klass = CLUTTER_BLUR_EFFECT_GET_CLASS (self);
  ClutterBlurEffectPrivate *priv =
    clutter_blur_effect_get_instance_private (self);

  if (G_UNLIKELY (klass->base_pipeline == NULL))
    {
      CoglSnippet *snippet;
      CoglContext *ctx =
        clutter_backend_get_cogl_context (clutter_get_default_backend ());

      klass->base_pipeline = cogl_pipeline_new (ctx);

      snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_TEXTURE_LOOKUP,
                                  box_blur_glsl_declarations,
                                  NULL);
      cogl_snippet_set_replace (snippet, box_blur_glsl_shader);
      cogl_pipeline_add_layer_snippet (klass->base_pipeline, 0, snippet);
      g_object_unref (snippet);

      cogl_pipeline_set_layer_null_texture (klass->base_pipeline, 0);
    }

  priv->pipeline = cogl_pipeline_copy (klass->base_pipeline);

  priv->pixel_step_uniform =
    cogl_pipeline_get_uniform_location (priv->pipeline, "pixel_step");
}

/**
 * clutter_blur_effect_new:
 *
 * Creates a new #ClutterBlurEffect to be used with
 * [method@Clutter.Actor.add_effect]
 *
 * Return value: the newly created #ClutterBlurEffect or %NULL
 */
ClutterEffect *
clutter_blur_effect_new (void)
{
  return g_object_new (CLUTTER_TYPE_BLUR_EFFECT, NULL);
}
