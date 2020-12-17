/*
 * Copyright (C) 2020 Endless OS Foundation, LLC
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

#include "clutter-blur-private.h"

#include "clutter-backend.h"

/**
 * SECTION:clutter-blur
 * @short_description: Blur textures
 *
 * #ClutterBlur is a moderately fast gaussian blur implementation.
 *
 * # Optimizations
 *
 * There are a number of optimizations in place to make this blur implementation
 * real-time. All in all, the implementation performs best when using large
 * blur-radii that allow downscaling the texture to smaller sizes, at small
 * radii where no downscaling is possible this can easily halve the framerate.
 *
 * ## Multipass
 *
 * It is implemented in 2 passes: vertical and horizontal.
 *
 * ## Downscaling
 *
 * #ClutterBlur uses dynamic downscaling to speed up blurring. Downscaling
 * happens in factors of 2 (the image is downscaled either by 2, 4, 8, 16, …)
 * and depends on the blur radius, the texture size, among others.
 *
 * The texture is drawn into a downscaled framebuffer; the blur passes are
 * applied on the downscaled texture contents; and finally, the blurred
 * contents are drawn
 * upscaled again.
 *
 * ## Hardware Interpolation
 *
 * This blur implementation cuts down the number of sampling operations by
 * exploiting the hardware interpolation that is performed when sampling between
 * pixel boundaries. This technique is described at:
 *
 * http://rastergrid.com/blog/2010/09/efficient-gaussian-blur-with-linear-sampling/
 *
 * ## Incremental gauss-factor calculation
 *
 * The kernel values for the gaussian kernel are computed incrementally instead
 * of running the expensive calculations multiple times inside the blur shader.
 * The implementation is based on the algorithm presented by K. Turkowski in
 * GPU Gems 3, chapter 40:
 *
 * https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch40.html
 *
 */

static const char *gaussian_blur_glsl_declarations =
"uniform float sigma;                                                      \n"
"uniform float pixel_step;                                                 \n"
"uniform vec2 direction;                                                   \n";

static const char *gaussian_blur_glsl =
"  vec2 uv = vec2 (cogl_tex_coord.st);                                     \n"
"                                                                          \n"
"  vec3 gauss_coefficient;                                                 \n"
"  gauss_coefficient.x = 1.0 / (sqrt (2.0 * 3.14159265) * sigma);          \n"
"  gauss_coefficient.y = exp (-0.5 / (sigma * sigma));                     \n"
"  gauss_coefficient.z = gauss_coefficient.y * gauss_coefficient.y;        \n"
"                                                                          \n"
"  float gauss_coefficient_total = gauss_coefficient.x;                    \n"
"                                                                          \n"
"  vec4 ret = texture2D (cogl_sampler, uv) * gauss_coefficient.x;          \n"
"  gauss_coefficient.xy *= gauss_coefficient.yz;                           \n"
"                                                                          \n"
"  int n_steps = int (ceil (1.5 * sigma)) * 2;                             \n"
"                                                                          \n"
"  for (int i = 1; i <= n_steps; i += 2) {                                 \n"
"    float coefficient_subtotal = gauss_coefficient.x;                     \n"
"    gauss_coefficient.xy *= gauss_coefficient.yz;                         \n"
"    coefficient_subtotal += gauss_coefficient.x;                          \n"
"                                                                          \n"
"    float gauss_ratio = gauss_coefficient.x / coefficient_subtotal;       \n"
"                                                                          \n"
"    float foffset = float (i) + gauss_ratio;                              \n"
"    vec2 offset = direction * foffset * pixel_step;                       \n"
"                                                                          \n"
"    ret += texture2D (cogl_sampler, uv + offset) * coefficient_subtotal;  \n"
"    ret += texture2D (cogl_sampler, uv - offset) * coefficient_subtotal;  \n"
"                                                                          \n"
"    gauss_coefficient_total += 2.0 * coefficient_subtotal;                \n"
"    gauss_coefficient.xy *= gauss_coefficient.yz;                         \n"
"  }                                                                       \n"
"                                                                          \n"
"  cogl_texel = ret / gauss_coefficient_total;                             \n";

#define MIN_DOWNSCALE_SIZE 256.f
#define MAX_SIGMA 6.f

enum
{
  VERTICAL,
  HORIZONTAL,
};

typedef struct
{
  CoglFramebuffer *framebuffer;
  CoglPipeline *pipeline;
  CoglTexture *texture;
  int orientation;
} BlurPass;

struct _ClutterBlur
{
  CoglTexture *source_texture;
  float sigma;
  float downscale_factor;

  BlurPass pass[2];
};

static CoglPipeline*
create_blur_pipeline (void)
{
  static CoglPipelineKey blur_pipeline_key = "clutter-blur-pipeline-private";
  CoglContext *ctx =
    clutter_backend_get_cogl_context (clutter_get_default_backend ());
  CoglPipeline *blur_pipeline;

  blur_pipeline =
    cogl_context_get_named_pipeline (ctx, &blur_pipeline_key);

  if (G_UNLIKELY (blur_pipeline == NULL))
    {
      CoglSnippet *snippet;

      blur_pipeline = cogl_pipeline_new (ctx);
      cogl_pipeline_set_layer_null_texture (blur_pipeline, 0);
      cogl_pipeline_set_layer_filters (blur_pipeline,
                                       0,
                                       COGL_PIPELINE_FILTER_LINEAR,
                                       COGL_PIPELINE_FILTER_LINEAR);
      cogl_pipeline_set_layer_wrap_mode (blur_pipeline,
                                         0,
                                         COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);

      snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_TEXTURE_LOOKUP,
                                  gaussian_blur_glsl_declarations,
                                  NULL);
      cogl_snippet_set_replace (snippet, gaussian_blur_glsl);
      cogl_pipeline_add_layer_snippet (blur_pipeline, 0, snippet);
      cogl_object_unref (snippet);

      cogl_context_set_named_pipeline (ctx, &blur_pipeline_key, blur_pipeline);
    }

  return cogl_pipeline_copy (blur_pipeline);
}

static void
update_blur_uniforms (ClutterBlur *blur,
                      BlurPass    *pass)
{
  gboolean vertical = pass->orientation == VERTICAL;
  int sigma_uniform;
  int pixel_step_uniform;
  int direction_uniform;

  pixel_step_uniform =
    cogl_pipeline_get_uniform_location (pass->pipeline, "pixel_step");
  if (pixel_step_uniform > -1)
    {
      float pixel_step;

      if (vertical)
        pixel_step = 1.f / cogl_texture_get_height (pass->texture);
      else
        pixel_step = 1.f / cogl_texture_get_width (pass->texture);

      cogl_pipeline_set_uniform_1f (pass->pipeline,
                                    pixel_step_uniform,
                                    pixel_step);
    }

  sigma_uniform = cogl_pipeline_get_uniform_location (pass->pipeline, "sigma");
  if (sigma_uniform > -1)
    {
      cogl_pipeline_set_uniform_1f (pass->pipeline,
                                    sigma_uniform,
                                    blur->sigma / blur->downscale_factor);
    }

  direction_uniform =
    cogl_pipeline_get_uniform_location (pass->pipeline, "direction");
  if (direction_uniform > -1)
    {
      gboolean horizontal = !vertical;
      float direction[2] = {
        horizontal,
        vertical,
      };

      cogl_pipeline_set_uniform_float (pass->pipeline,
                                       direction_uniform,
                                       2, 1,
                                       direction);
    }
}

static gboolean
create_fbo (ClutterBlur *blur,
            BlurPass    *pass)
{
  CoglContext *ctx =
    clutter_backend_get_cogl_context (clutter_get_default_backend ());
  float scaled_height;
  float scaled_width;
  float height;
  float width;

  g_clear_pointer (&pass->texture, cogl_object_unref);
  g_clear_object (&pass->framebuffer);

  width = cogl_texture_get_width (blur->source_texture);
  height = cogl_texture_get_height (blur->source_texture);
  scaled_width = floorf (width / blur->downscale_factor);
  scaled_height = floorf (height / blur->downscale_factor);

  pass->texture = COGL_TEXTURE (cogl_texture_2d_new_with_size (ctx,
                                                               scaled_width,
                                                               scaled_height));
  if (!pass->texture)
    return FALSE;

  pass->framebuffer =
    COGL_FRAMEBUFFER (cogl_offscreen_new_with_texture (pass->texture));
  if (!pass->framebuffer)
    {
      g_warning ("%s: Unable to create an Offscreen buffer", G_STRLOC);
      return FALSE;
    }

  cogl_framebuffer_orthographic (pass->framebuffer,
                                 0.0, 0.0,
                                 scaled_width,
                                 scaled_height,
                                 0.0, 1.0);
  return TRUE;
}

static gboolean
setup_blur_pass (ClutterBlur *blur,
                 BlurPass    *pass,
                 int          orientation,
                 CoglTexture *texture)
{
  pass->orientation = orientation;
  pass->pipeline = create_blur_pipeline ();
  cogl_pipeline_set_layer_texture (pass->pipeline, 0, texture);

  if (!create_fbo (blur, pass))
    return FALSE;

  update_blur_uniforms (blur, pass);
  return TRUE;
}

static float
calculate_downscale_factor (float width,
                            float height,
                            float sigma)
{
  float downscale_factor = 1.f;
  float scaled_width = width;
  float scaled_height = height;
  float scaled_sigma = sigma;

  /* This is the algorithm used by Firefox; keep downscaling until either the
   * blur radius is lower than the threshold, or the downscaled texture is too
   * small.
   */
  while (scaled_sigma > MAX_SIGMA &&
         scaled_width > MIN_DOWNSCALE_SIZE &&
         scaled_height > MIN_DOWNSCALE_SIZE)
    {
      downscale_factor *= 2.f;

      scaled_width = width / downscale_factor;
      scaled_height = height / downscale_factor;
      scaled_sigma = sigma / downscale_factor;
    }

  return downscale_factor;
}

static void
apply_blur_pass (BlurPass *pass)
{
  CoglColor transparent;

  cogl_color_init_from_4ub (&transparent, 0, 0, 0, 0);

  cogl_framebuffer_clear (pass->framebuffer,
                          COGL_BUFFER_BIT_COLOR,
                          &transparent);

  cogl_framebuffer_draw_rectangle (pass->framebuffer,
                                   pass->pipeline,
                                   0, 0,
                                   cogl_texture_get_width (pass->texture),
                                   cogl_texture_get_height (pass->texture));
}

static void
clear_blur_pass (BlurPass *pass)
{
  g_clear_pointer (&pass->pipeline, cogl_object_unref);
  g_clear_pointer (&pass->texture, cogl_object_unref);
  g_clear_object (&pass->framebuffer);
}

/**
 * clutter_blur_new:
 * @texture: a #CoglTexture
 * @sigma: blur sigma
 *
 * Creates a new #ClutterBlur.
 *
 * Returns: (transfer full) (nullable): A newly created #ClutterBlur
 */
ClutterBlur *
clutter_blur_new (CoglTexture *texture,
                  float        sigma)
{
  ClutterBlur *blur;
  unsigned int height;
  unsigned int width;
  BlurPass *hpass;
  BlurPass *vpass;

  g_return_val_if_fail (texture != NULL, NULL);
  g_return_val_if_fail (sigma >= 0.0, NULL);

  width = cogl_texture_get_width (texture);
  height = cogl_texture_get_height (texture);

  blur = g_new0 (ClutterBlur, 1);
  blur->sigma = sigma;
  blur->source_texture = cogl_object_ref (texture);
  blur->downscale_factor = calculate_downscale_factor (width, height, sigma);

  if (G_APPROX_VALUE (sigma, 0.0, FLT_EPSILON))
    goto out;

  vpass = &blur->pass[VERTICAL];
  hpass = &blur->pass[HORIZONTAL];

  if (!setup_blur_pass (blur, vpass, VERTICAL, texture) ||
      !setup_blur_pass (blur, hpass, HORIZONTAL, vpass->texture))
    {
      clutter_blur_free (blur);
      return NULL;
    }

out:
  return g_steal_pointer (&blur);
}

/**
 * clutter_blur_apply:
 * @blur: a #ClutterBlur
 *
 * Applies the blur. The resulting texture can be retrieved by
 * clutter_blur_get_texture().
 */
void
clutter_blur_apply (ClutterBlur *blur)
{
  if (G_APPROX_VALUE (blur->sigma, 0.0, FLT_EPSILON))
    return;

  apply_blur_pass (&blur->pass[VERTICAL]);
  apply_blur_pass (&blur->pass[HORIZONTAL]);
}

/**
 * clutter_blur_get_texture:
 * @blur: a #ClutterBlur
 *
 * Retrieves the texture where the blurred contents are stored. The
 * contents are undefined until clutter_blur_apply() is called.
 *
 * Returns: (transfer none): a #CoglTexture
 */
CoglTexture *
clutter_blur_get_texture (ClutterBlur *blur)
{
  if (G_APPROX_VALUE (blur->sigma, 0.0, FLT_EPSILON))
    return blur->source_texture;
  else
    return blur->pass[HORIZONTAL].texture;
}

/**
 * clutter_blur_free:
 * @blur: A #ClutterBlur
 *
 * Frees @blur.
 */
void
clutter_blur_free (ClutterBlur *blur)
{
  g_assert (blur);

  clear_blur_pass (&blur->pass[VERTICAL]);
  clear_blur_pass (&blur->pass[HORIZONTAL]);
  cogl_clear_object (&blur->source_texture);
  g_free (blur);
}
