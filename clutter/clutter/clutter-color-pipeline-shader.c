/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2026 Red Hat, Inc.
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

#include "config.h"

#include "clutter/clutter-color-pipeline-shader.h"

#include <math.h>

#include "clutter/clutter-backend-private.h"
#include "clutter/clutter-color-op.h"
#include "clutter/clutter-color-pipeline.h"
#include "clutter/clutter-color-state-private.h"
#include "clutter/clutter-color-transform-private.h"
#include "clutter/clutter-main.h"
#include "cogl-half-float.h"

/* Layer offset to avoid conflict with base texture on layer 0 */
#define PIPELINE_LAYER_OFFSET 10

typedef struct _SnippetCacheOpKey
{
  GType type;
  uint8_t has_alpha;
} SnippetCacheOpKey;

typedef struct _SnippetCacheKey
{
  size_t n_ops;
  SnippetCacheOpKey op_keys[];
} SnippetCacheKey;

static unsigned int
snippet_cache_key_hash (gconstpointer data)
{
  const SnippetCacheKey *key = data;
  unsigned int hash = key->n_ops;

  for (size_t i = 0; i < key->n_ops; i++)
    {
      hash = hash * 31 + (unsigned int) key->op_keys[i].type;
      hash = hash * 31 + key->op_keys[i].has_alpha;
    }

  return hash;
}

static gboolean
snippet_cache_key_equal (gconstpointer a,
                         gconstpointer b)
{
  const SnippetCacheKey *ka = a;
  const SnippetCacheKey *kb = b;

  if (ka->n_ops != kb->n_ops)
    return FALSE;

  for (size_t i = 0; i < ka->n_ops; i++)
    {
      if (ka->op_keys[i].type != kb->op_keys[i].type ||
          ka->op_keys[i].has_alpha != kb->op_keys[i].has_alpha)
        return FALSE;
    }

  return TRUE;
}

static SnippetCacheKey *
build_snippet_cache_key (ClutterColorPipeline *color_pipeline)
{
  const GList *ops = clutter_color_pipeline_get_ops (color_pipeline);
  g_autofree SnippetCacheKey *key = NULL;
  size_t n_ops, i = 0;

  n_ops = g_list_length ((GList *) ops);
  key = g_malloc (sizeof (SnippetCacheKey) +
                  n_ops * sizeof (SnippetCacheOpKey));
  key->n_ops = n_ops;

  for (const GList *l = ops; l != NULL; l = l->next, i++)
    {
      ClutterColorOp *op = l->data;

      key->op_keys[i].type = G_OBJECT_TYPE (op);
      key->op_keys[i].has_alpha = FALSE;

      if (CLUTTER_IS_COLOR_OP_CURVE_1D (op))
        {
          const float *a_data;

          clutter_color_op_curve_1d_get_data (op, NULL, NULL, NULL, NULL, &a_data);
          key->op_keys[i].has_alpha = a_data != NULL;
        }
    }

  return g_steal_pointer (&key);
}

static GHashTable *
get_snippet_cache (void)
{
  ClutterBackend *backend = clutter_get_default_backend ();
  ClutterContext *context = backend->context;
  GHashTable *cache;

  cache = g_object_get_data (G_OBJECT (context),
                             "color-pipeline-snippet-cache");
  if (!cache)
    {
      g_autoptr (GHashTable) owned_cache = NULL;

      owned_cache = cache = g_hash_table_new_full (snippet_cache_key_hash,
                                                   snippet_cache_key_equal,
                                                   g_free,
                                                   g_object_unref);
      g_object_set_data_full (G_OBJECT (context),
                              "color-pipeline-snippet-cache",
                              g_steal_pointer (&owned_cache),
                              (GDestroyNotify) g_hash_table_unref);
    }

  return cache;
}

static const char clamp_unit_source[] =
  "vec3 clamp_unit (vec3 color)\n"
  "{\n"
  "  return clamp (color, 0.0, 1.0);\n"
  "}\n"
  "\n"
  "vec4 clamp_unit (vec4 color)\n"
  "{\n"
  "  return clamp (color, 0.0, 1.0);\n"
  "}\n";

static const char srgb_piecewise_eotf_source[] =
  "vec3 srgb_piecewise_eotf (vec3 color)\n"
  "{\n"
  "  vec3 vsign = sign (color);\n"
  "  color = abs (color);\n"
  "  vec3 is_low = vec3 (lessThanEqual (color, vec3 (0.04045)));\n"
  "  vec3 lo_part = color / 12.92;\n"
  "  vec3 hi_part = pow ((color + 0.055) / 1.055, vec3 (12.0 / 5.0));\n"
  "  return vsign * mix (hi_part, lo_part, is_low);\n"
  "}\n"
  "\n"
  "vec4 srgb_piecewise_eotf (vec4 color)\n"
  "{\n"
  "  return vec4 (srgb_piecewise_eotf (color.rgb), color.a);\n"
  "}\n";

static const char srgb_piecewise_inv_eotf_source[] =
  "vec3 srgb_piecewise_inv_eotf (vec3 color)\n"
  "{\n"
  "  vec3 vsign = sign (color);\n"
  "  color = abs (color);\n"
  "  vec3 is_lo = vec3 (lessThanEqual (color, vec3 (0.0031308)));\n"
  "\n"
  "  vec3 lo_part = color * 12.92;\n"
  "  vec3 hi_part = pow (color, vec3 (5.0 / 12.0)) * 1.055 - 0.055;\n"
  "  return vsign * mix (hi_part, lo_part, is_lo);\n"
  "}\n"
  "\n"
  "vec4 srgb_piecewise_inv_eotf (vec4 color)\n"
  "{\n"
  "  return vec4 (srgb_piecewise_inv_eotf (color.rgb), color.a);\n"
  "}\n";

static const char pq_eotf_source[] =
  "vec3 pq_eotf (vec3 color)\n"
  "{\n"
  "  const float c1 = 0.8359375;\n"
  "  const float c2 = 18.8515625;\n"
  "  const float c3 = 18.6875;\n"
  "  const float oo_m1 = 1.0 / 0.1593017;\n"
  "  const float oo_m2 = 1.0 / 78.84375;\n"
  "\n"
  "  color = clamp (color, vec3 (0.0), vec3 (1.0));\n"
  "  vec3 color_pow = pow (color, vec3 (oo_m2));\n"
  "  vec3 num = max (color_pow - c1, vec3 (0.0));\n"
  "  vec3 den = c2 - c3 * color_pow;\n"
  "  return pow (num / den, vec3 (oo_m1));\n"
  "}\n";

static const char pq_inv_eotf_source[] =
  "vec3 pq_inv_eotf (vec3 color)\n"
  "{\n"
  "  const float c1 = 0.8359375;\n"
  "  const float c2 = 18.8515625;\n"
  "  const float c3 = 18.6875;\n"
  "  const float m1 = 0.1593017;\n"
  "  const float m2 = 78.84375;\n"
  "\n"
  "  color = clamp (color, vec3 (0.0), vec3 (1.0));\n"
  "  vec3 in_pow_m1 = pow (color, vec3 (m1));\n"
  "  vec3 num = c1 + c2 * in_pow_m1;\n"
  "  vec3 den = vec3 (1.0) + c3 * in_pow_m1;\n"
  "  return pow (num / den, vec3 (m2));\n"
  "}\n";

static const char lut_3d_source[] =
  "// Tetrahedral interpolation\n"
  "vec3 sample_3d_lut_SIZE (vec3 color, sampler2D lut_texture, float lut_size)\n"
  "{\n"
  "  vec3 scaled_color = color * (lut_size - 1.0);\n"
  "  vec3 index_low = floor (scaled_color);\n"
  "  vec3 index_high = min (index_low + 1.0, lut_size - 1.0);\n"
  "  vec3 t = scaled_color - index_low;\n"
  "\n"
  "  index_low.z *= lut_size;\n"
  "  index_high.z *= lut_size;\n"
  "  float normalize_v = 1.0 / ((lut_size * lut_size) - 1.0);\n"
  "  index_low.x /= lut_size - 1.0;\n"
  "  index_high.x /= lut_size - 1.0;\n"
  "\n"
  "  vec2 coord000 = vec2 (index_low.x, (index_low.y + index_low.z) * normalize_v);\n"
  "  vec2 coord111 = vec2 (index_high.x, (index_high.y + index_high.z) * normalize_v);\n"
  "  vec3 v000 = texture (lut_texture, coord000).rgb;\n"
  "  vec3 v111 = texture (lut_texture, coord111).rgb;\n"
  "\n"
  "  if (t.x > t.y)\n"
  "    {\n"
  "      if (t.y > t.z)\n"
  "        {\n"
  "          vec2 coord100 = vec2 (index_high.x, (index_low.y + index_low.z) * normalize_v);\n"
  "          vec2 coord110 = vec2 (index_high.x, (index_high.y + index_low.z) * normalize_v);\n"
  "          vec3 v100 = texture (lut_texture, coord100).rgb;\n"
  "          vec3 v110 = texture (lut_texture, coord110).rgb;\n"
  "          return v000 + t.x * (v100 - v000) + t.y * (v110 - v100) + t.z * (v111 - v110);\n"
  "        }\n"
  "      else if (t.x > t.z)\n"
  "        {\n"
  "          vec2 coord100 = vec2 (index_high.x, (index_low.y + index_low.z) * normalize_v);\n"
  "          vec2 coord101 = vec2 (index_high.x, (index_low.y + index_high.z) * normalize_v);\n"
  "          vec3 v100 = texture (lut_texture, coord100).rgb;\n"
  "          vec3 v101 = texture (lut_texture, coord101).rgb;\n"
  "          return v000 + t.x * (v100 - v000) + t.y * (v111 - v101) + t.z * (v101 - v100);\n"
  "        }\n"
  "      else\n"
  "        {\n"
  "          vec2 coord001 = vec2 (index_low.x, (index_low.y + index_high.z) * normalize_v);\n"
  "          vec2 coord101 = vec2 (index_high.x, (index_low.y + index_high.z) * normalize_v);\n"
  "          vec3 v001 = texture (lut_texture, coord001).rgb;\n"
  "          vec3 v101 = texture (lut_texture, coord101).rgb;\n"
  "          return v000 + t.x * (v101 - v001) + t.y * (v111 - v101) + t.z * (v001 - v000);\n"
  "        }\n"
  "    }\n"
  "  else\n"
  "    {\n"
  "      if (t.z > t.y)\n"
  "        {\n"
  "          vec2 coord001 = vec2 (index_low.x, (index_low.y + index_high.z) * normalize_v);\n"
  "          vec2 coord011 = vec2 (index_low.x, (index_high.y + index_high.z) * normalize_v);\n"
  "          vec3 v001 = texture (lut_texture, coord001).rgb;\n"
  "          vec3 v011 = texture (lut_texture, coord011).rgb;\n"
  "          return v000 + t.x * (v111 - v011) + t.y * (v011 - v001) + t.z * (v001 - v000);\n"
  "        }\n"
  "      else if (t.z > t.x)\n"
  "        {\n"
  "          vec2 coord010 = vec2 (index_low.x, (index_high.y + index_low.z) * normalize_v);\n"
  "          vec2 coord011 = vec2 (index_low.x, (index_high.y + index_high.z) * normalize_v);\n"
  "          vec3 v010 = texture (lut_texture, coord010).rgb;\n"
  "          vec3 v011 = texture (lut_texture, coord011).rgb;\n"
  "          return v000 + t.x * (v111 - v011) + t.y * (v010 - v000) + t.z * (v011 - v010);\n"
  "        }\n"
  "      else\n"
  "        {\n"
  "          vec2 coord010 = vec2 (index_low.x, (index_high.y + index_low.z) * normalize_v);\n"
  "          vec2 coord110 = vec2 (index_high.x, (index_high.y + index_low.z) * normalize_v);\n"
  "          vec3 v010 = texture (lut_texture, coord010).rgb;\n"
  "          vec3 v110 = texture (lut_texture, coord110).rgb;\n"
  "          return v000 + t.x * (v110 - v010) + t.y * (v010 - v000) + t.z * (v111 - v110);\n"
  "        }\n"
  "    }\n"
  "}\n";

static const char gamma_power_source[] =
  "vec3 gamma_power (vec3 color, float power)\n"
  "{\n"
  "  vec3 is_negative = vec3 (lessThan (color, vec3 (0.0)));\n"
  "  vec3 positive = pow (abs (color), vec3 (power));\n"
  "  vec3 negative = -positive;\n"
  "  return mix (positive, negative, is_negative);\n"
  "}\n"
  "\n"
  "vec4 gamma_power (vec4 color, float power)\n"
  "{\n"
  "  return vec4 (gamma_power (color.rgb, power), color.a);\n"
  "}\n";

static const char curve_1d_source[] =
  "// samples from a xsize by 4 texture,"
  "// where the y-direction contains the different channels\n"
  "vec4 curve_1d (vec4 color, in sampler2D tex, float xsize)\n"
  "{\n"
  "  float xoff = (1.0 / (xsize * 2.0));\n"
  "  float yoff = (1.0 / (4.0 * 2.0));\n"
  "  return vec4 (texture (tex, vec2 (color.r + xoff, 0.00 + yoff)).r,\n"
  "               texture (tex, vec2 (color.g + xoff, 0.25 + yoff)).r,\n"
  "               texture (tex, vec2 (color.b + xoff, 0.50 + yoff)).r,\n"
  "               texture (tex, vec2 (color.a + xoff, 0.75 + yoff)).r);\n"
  "}\n";

static const char multiply_source[] =
  "vec3 multiply (vec3 color, float value)\n"
  "{\n"
  "  return color * value;\n"
  "}\n"
  "\n"
  "vec4 multiply (vec4 color, float value)\n"
  "{\n"
  "  return vec4 (color.rgb * value, color.a);\n"
  "}\n";

static const char matrix_4x4_source[] =
  "vec4 matrix_4x4 (vec4 color, mat4 matrix)\n"
  "{\n"
  "  return matrix * color;\n"
  "}\n";

static const char unpremultiply_source[] =
  "vec4 unpremultiply (vec4 color)\n"
  "{\n"
  "  if (color.a > 0.0)\n"
  "    return vec4 (color.rgb / color.a, color.a);\n"
  "  return color;\n"
  "}\n";

static const char premultiply_source[] =
  "vec4 premultiply (vec4 color)\n"
  "{\n"
  "  return vec4 (color.rgb * color.a, color.a);\n"
  "}\n";

static char *
get_3d_lut_declarations (ClutterColorOp *op,
                         size_t          op_id)
{
  size_t layer_id;

  layer_id = op_id + PIPELINE_LAYER_OFFSET;
  return g_strdup_printf ("uniform float lut_3d_size_%zu;\n", layer_id);
}

static char *
get_3d_lut_invocation (ClutterColorOp *op,
                       size_t          op_id)
{
  size_t layer_id;

  layer_id = op_id + PIPELINE_LAYER_OFFSET;
  return g_strdup_printf ("vec4 (sample_3d_lut_SIZE (color.rgb, cogl_sampler%zu, lut_3d_size_%zu), color.a)",
                          layer_id, layer_id);
}

static void
update_3d_lut_state (ClutterColorOp *op,
                     size_t          op_id,
                     CoglPipeline   *pipeline)
{
  uint32_t size;
  const float *data;
  CoglContext *cogl_context;
  g_autoptr (CoglTexture) texture = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree float *texture_data = NULL;
  g_autofree char *uniform_name = NULL;
  int rowstride, uniform_location_size;
  size_t n_pixels, layer_id;

  layer_id = op_id + PIPELINE_LAYER_OFFSET;
  clutter_color_op_3d_lut_get_data (op, &size, &data);
  cogl_context = clutter_backend_get_cogl_context (clutter_get_default_backend ());

  n_pixels = (size_t) size * size * size;
  texture_data = g_new (float, n_pixels * 4);
  for (size_t i = 0; i < n_pixels; i++)
    {
      texture_data[i * 4 + 0] = data[i * 3 + 0];
      texture_data[i * 4 + 1] = data[i * 3 + 1];
      texture_data[i * 4 + 2] = data[i * 3 + 2];
      texture_data[i * 4 + 3] = 1.0f;
    }

  rowstride = size * 4 * sizeof (float);
  texture = cogl_texture_2d_new_from_data (cogl_context,
                                           size,
                                           size * size,
                                           COGL_PIXEL_FORMAT_RGBA_FP_32323232,
                                           rowstride,
                                           (uint8_t *) texture_data,
                                           &error);
  if (!texture)
    {
      g_warning ("Failed creating 3D LUT texture: %s", error->message);
      return;
    }

  cogl_pipeline_set_layer_texture (pipeline, layer_id, texture);
  cogl_pipeline_set_layer_combine (pipeline, layer_id,
                                   "RGBA = REPLACE(PREVIOUS)", NULL);
  cogl_pipeline_set_layer_wrap_mode_s (pipeline, layer_id,
                                       COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);
  cogl_pipeline_set_layer_wrap_mode_t (pipeline, layer_id,
                                       COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);
  cogl_pipeline_set_layer_filters (pipeline, layer_id,
                                   COGL_PIPELINE_FILTER_NEAREST,
                                   COGL_PIPELINE_FILTER_NEAREST);

  uniform_name = g_strdup_printf ("lut_3d_size_%zu", layer_id);
  uniform_location_size = cogl_pipeline_get_uniform_location (pipeline,
                                                              uniform_name);
  cogl_pipeline_set_uniform_1f (pipeline,
                                uniform_location_size,
                                size);
}

static char *
get_gamma_power_declarations (ClutterColorOp *op,
                              size_t          op_id)
{
  return g_strdup_printf ("uniform float gamma_power_param_%zu;\n", op_id);
}

static char *
get_gamma_power_invocation (ClutterColorOp *op,
                            size_t          op_id)
{
  return g_strdup_printf ("vec4 (gamma_power (color.rgb, gamma_power_param_%zu), color.a)", op_id);
}

static void
update_gamma_power_state (ClutterColorOp *op,
                          size_t          op_id,
                          CoglPipeline   *pipeline)
{
  float power;
  g_autofree char *uniform_name = NULL;
  int uniform_location;

  power = clutter_color_op_gamma_power_get_power (op);
  uniform_name = g_strdup_printf ("gamma_power_param_%zu", op_id);
  uniform_location = cogl_pipeline_get_uniform_location (pipeline,
                                                         uniform_name);
  cogl_pipeline_set_uniform_1f (pipeline, uniform_location, power);
}

static char *
get_curve_1d_declarations (ClutterColorOp *op,
                           size_t          op_id)
{
  size_t layer_id;
  const float *a_data;

  layer_id = op_id + PIPELINE_LAYER_OFFSET;
  clutter_color_op_curve_1d_get_data (op, NULL, NULL, NULL, NULL, &a_data);

  if (a_data)
    {
      return g_strdup_printf (
        "uniform float curve_1d_size_%zu;\n"
        "vec4 curve_1d_%zu (vec4 color)\n"
        "{\n"
        "  float xscale = (curve_1d_size_%zu - 1.0) / curve_1d_size_%zu;\n"
        "  float xoff = 0.5 / curve_1d_size_%zu;\n"
        "  vec4 r_sample = texture (cogl_sampler%zu, vec2 (color.r * xscale + xoff, 0.5));\n"
        "  vec4 g_sample = texture (cogl_sampler%zu, vec2 (color.g * xscale + xoff, 0.5));\n"
        "  vec4 b_sample = texture (cogl_sampler%zu, vec2 (color.b * xscale + xoff, 0.5));\n"
        "  vec4 a_sample = texture (cogl_sampler%zu, vec2 (color.a * xscale + xoff, 0.5));\n"
        "  return vec4 (r_sample.r, g_sample.g, b_sample.b, a_sample.a);\n"
        "}\n",
        layer_id, op_id, layer_id, layer_id, layer_id,
        layer_id, layer_id, layer_id, layer_id);
    }

  return g_strdup_printf (
    "uniform float curve_1d_size_%zu;\n"
    "vec4 curve_1d_%zu (vec4 color)\n"
    "{\n"
    "  float xscale = (curve_1d_size_%zu - 1.0) / curve_1d_size_%zu;\n"
    "  float xoff = 0.5 / curve_1d_size_%zu;\n"
    "  vec4 r_sample = texture (cogl_sampler%zu, vec2 (color.r * xscale + xoff, 0.5));\n"
    "  vec4 g_sample = texture (cogl_sampler%zu, vec2 (color.g * xscale + xoff, 0.5));\n"
    "  vec4 b_sample = texture (cogl_sampler%zu, vec2 (color.b * xscale + xoff, 0.5));\n"
    "  return vec4 (r_sample.r, g_sample.g, b_sample.b, color.a);\n"
    "}\n",
    layer_id, op_id, layer_id, layer_id, layer_id,
    layer_id, layer_id, layer_id);
}

static char *
get_curve_1d_invocation (ClutterColorOp *op,
                         size_t          op_id)
{
  return g_strdup_printf ("curve_1d_%zu (color)", op_id);
}

static void
update_curve_1d_state (ClutterColorOp *op,
                       size_t          op_id,
                       CoglPipeline   *pipeline)
{
  size_t size, rowstride, layer_id;
  const float *r, *g, *b, *a;
  CoglContext *context;
  g_autoptr (GError) error = NULL;
  g_autoptr (CoglTexture) texture = NULL;
  g_autofree float *texture_data = NULL;
  g_autofree char *uniform_name = NULL;
  int uniform_location;

  layer_id = op_id + PIPELINE_LAYER_OFFSET;
  clutter_color_op_curve_1d_get_data (op, &size, &r, &g, &b, &a);
  context = clutter_backend_get_cogl_context (clutter_get_default_backend ());

  texture_data = g_malloc0 (size * 4 * sizeof (float));
  for (size_t i = 0; i < size; i++)
    {
      texture_data[i * 4 + 0] = r ? r[i] : (float) i / (float) (size - 1);
      texture_data[i * 4 + 1] = g ? g[i] : (float) i / (float) (size - 1);
      texture_data[i * 4 + 2] = b ? b[i] : (float) i / (float) (size - 1);
      texture_data[i * 4 + 3] = a ? a[i] : (float) i / (float) (size - 1);
    }

  rowstride = size * 4 * sizeof (float);

  texture = cogl_texture_2d_new_from_data (context,
                                           size,
                                           1,
                                           COGL_PIXEL_FORMAT_RGBA_FP_32323232,
                                           rowstride,
                                           (uint8_t *) texture_data,
                                           &error);
  if (!texture)
    {
      g_warning ("Failed creating curve_1d texture: %s", error->message);
      return;
    }

  cogl_pipeline_set_layer_texture (pipeline, layer_id, texture);
  cogl_pipeline_set_layer_combine (pipeline, layer_id,
                                   "RGBA = REPLACE(PREVIOUS)", NULL);
  cogl_pipeline_set_layer_wrap_mode_s (pipeline, layer_id,
                                       COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);
  cogl_pipeline_set_layer_wrap_mode_t (pipeline, layer_id,
                                       COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);
  cogl_pipeline_set_layer_filters (pipeline, layer_id,
                                   COGL_PIPELINE_FILTER_LINEAR,
                                   COGL_PIPELINE_FILTER_LINEAR);

  uniform_name = g_strdup_printf ("curve_1d_size_%zu", layer_id);
  uniform_location = cogl_pipeline_get_uniform_location (pipeline,
                                                         uniform_name);
  cogl_pipeline_set_uniform_1f (pipeline, uniform_location, (float) size);
}

static char *
get_matrix_declarations (ClutterColorOp *op,
                         size_t          op_id)
{
  return g_strdup_printf ("uniform mat4 matrix_4x4_param_%zu;\n", op_id);
}

static char *
get_matrix_invocation (ClutterColorOp *op,
                       size_t          op_id)
{
  return g_strdup_printf ("matrix_4x4 (color, matrix_4x4_param_%zu)", op_id);
}

static void
update_matrix_4x4_state (ClutterColorOp *op,
                         size_t          op_id,
                         CoglPipeline   *pipeline)
{
  const graphene_matrix_t *matrix;
  g_autofree char *uniform_name = NULL;
  int uniform_location;
  float matrix_floats[16];

  matrix = clutter_color_op_matrix_4x4_get_matrix (op);
  uniform_name = g_strdup_printf ("matrix_4x4_param_%zu", op_id);
  uniform_location = cogl_pipeline_get_uniform_location (pipeline,
                                                         uniform_name);
  graphene_matrix_to_float (matrix, matrix_floats);
  cogl_pipeline_set_uniform_matrix (pipeline, uniform_location,
                                    4, 1, FALSE,
                                    matrix_floats);
}

static void
update_ycbcr_matrix_state (ClutterColorOp *op,
                           size_t          op_id,
                           CoglPipeline   *pipeline)
{
  const graphene_matrix_t *matrix;
  g_autofree char *uniform_name = NULL;
  int uniform_location;
  float matrix_floats[16];

  matrix = clutter_color_op_ycbcr_matrix_get_matrix (op);
  uniform_name = g_strdup_printf ("matrix_4x4_param_%zu", op_id);
  uniform_location = cogl_pipeline_get_uniform_location (pipeline,
                                                         uniform_name);
  graphene_matrix_to_float (matrix, matrix_floats);
  cogl_pipeline_set_uniform_matrix (pipeline, uniform_location,
                                    4, 1, FALSE,
                                    matrix_floats);
}

static char *
get_multiply_declarations (ClutterColorOp *op,
                           size_t          op_id)
{
  return g_strdup_printf ("uniform float multiply_param_%zu;\n", op_id);
}

static char *
get_multiply_invocation (ClutterColorOp *op,
                         size_t          op_id)
{
  return g_strdup_printf ("vec4 (multiply (color.rgb, multiply_param_%zu), color.a)", op_id);
}

static void
update_multiply_state (ClutterColorOp *op,
                       size_t          op_id,
                       CoglPipeline   *pipeline)
{
  float value;
  g_autofree char *uniform_name = NULL;
  int uniform_location;

  value = clutter_color_op_multiply_get_value (op);
  uniform_name = g_strdup_printf ("multiply_param_%zu", op_id);
  uniform_location = cogl_pipeline_get_uniform_location (pipeline,
                                                         uniform_name);
  cogl_pipeline_set_uniform_1f (pipeline, uniform_location, value);
}

typedef struct _ShaderOpInfo
{
  GType (*get_type) (void);
  const char *shader_source;
  const char *invocation;
  char * (*get_declarations) (ClutterColorOp *op,
                              size_t          op_id);
  char * (*get_invocation) (ClutterColorOp *op,
                            size_t          op_id);
  void (*update_state) (ClutterColorOp *op,
                        size_t          op_id,
                        CoglPipeline   *pipeline);
} ShaderOpInfo;

static const ShaderOpInfo shader_op_infos[] = {
  {
    .get_type = clutter_color_op_clamp_unit_get_type,
    .shader_source = clamp_unit_source,
    .invocation = "vec4 (clamp_unit (color.rgb), clamp (color.a, 0.0, 1.0))",
  },
  {
    .get_type = clutter_color_op_srgb_piecewise_eotf_get_type,
    .shader_source = srgb_piecewise_eotf_source,
    .invocation = "vec4 (srgb_piecewise_eotf (color.rgb), color.a)",
  },
  {
    .get_type = clutter_color_op_srgb_piecewise_inv_eotf_get_type,
    .shader_source = srgb_piecewise_inv_eotf_source,
    .invocation = "vec4 (srgb_piecewise_inv_eotf (color.rgb), color.a)",
  },
  {
    .get_type = clutter_color_op_pq_eotf_get_type,
    .shader_source = pq_eotf_source,
    .invocation = "vec4 (pq_eotf (color.rgb), color.a)",
  },
  {
    .get_type = clutter_color_op_pq_inv_eotf_get_type,
    .shader_source = pq_inv_eotf_source,
    .invocation = "vec4 (pq_inv_eotf (color.rgb), color.a)",
  },
  {
    .get_type = clutter_color_op_unpremultiply_get_type,
    .shader_source = unpremultiply_source,
    .invocation = "unpremultiply (color)",
  },
  {
    .get_type = clutter_color_op_premultiply_get_type,
    .shader_source = premultiply_source,
    .invocation = "premultiply (color)",
  },
  {
    .get_type = clutter_color_op_3d_lut_get_type,
    .shader_source = lut_3d_source,
    .get_declarations = get_3d_lut_declarations,
    .get_invocation = get_3d_lut_invocation,
    .update_state = update_3d_lut_state,
  },
  {
    .get_type = clutter_color_op_gamma_power_get_type,
    .shader_source = gamma_power_source,
    .get_declarations = get_gamma_power_declarations,
    .get_invocation = get_gamma_power_invocation,
    .update_state = update_gamma_power_state,
  },
  {
    .get_type = clutter_color_op_curve_1d_get_type,
    .shader_source = curve_1d_source,
    .get_declarations = get_curve_1d_declarations,
    .get_invocation = get_curve_1d_invocation,
    .update_state = update_curve_1d_state,
  },
  {
    .get_type = clutter_color_op_matrix_4x4_get_type,
    .shader_source = matrix_4x4_source,
    .get_declarations = get_matrix_declarations,
    .get_invocation = get_matrix_invocation,
    .update_state = update_matrix_4x4_state,
  },
  {
    .get_type = clutter_color_op_ycbcr_matrix_get_type,
    .shader_source = matrix_4x4_source,
    .get_declarations = get_matrix_declarations,
    .get_invocation = get_matrix_invocation,
    .update_state = update_ycbcr_matrix_state,
  },
  {
    .get_type = clutter_color_op_multiply_get_type,
    .shader_source = multiply_source,
    .get_declarations = get_multiply_declarations,
    .get_invocation = get_multiply_invocation,
    .update_state = update_multiply_state,
  },
};

static gsize shader_op_infos_initialized;
static GQuark shader_op_info_quark;

static const ShaderOpInfo *
get_op_info (ClutterColorOp *op)
{
  if (g_once_init_enter (&shader_op_infos_initialized))
    {
      shader_op_info_quark =
        g_quark_from_static_string ("ClutterShaderOpInfo");

      for (size_t i = 0; i < G_N_ELEMENTS (shader_op_infos); i++)
        {
          GType type = shader_op_infos[i].get_type ();

          g_type_set_qdata (type,
                            shader_op_info_quark,
                            (gpointer) &shader_op_infos[i]);
        }

      g_once_init_leave (&shader_op_infos_initialized, 1);
    }

  return g_type_get_qdata (G_OBJECT_TYPE (op), shader_op_info_quark);
}

static const char *
get_shader_source (ClutterColorOp *op)
{
  const ShaderOpInfo *info = get_op_info (op);

  return info ? info->shader_source : NULL;
}

static char *
get_shader_declarations (ClutterColorOp *op,
                         size_t          op_id)
{
  const ShaderOpInfo *info = get_op_info (op);

  if (!info || !info->get_declarations)
    return NULL;

  return info->get_declarations (op, op_id);
}

static char *
get_shader_invocation (ClutterColorOp *op,
                       size_t          op_id)
{
  const ShaderOpInfo *info = get_op_info (op);

  if (info && info->invocation)
    return g_strdup (info->invocation);
  if (info && info->get_invocation)
    return info->get_invocation (op, op_id);

  return g_strdup_printf ("%s (color)", G_OBJECT_TYPE_NAME (op));
}

static void
update_shader_state (ClutterColorOp *op,
                     size_t          op_id,
                     CoglPipeline   *pipeline)
{
  const ShaderOpInfo *info = get_op_info (op);

  if (info && info->update_state)
    info->update_state (op, op_id, pipeline);
}

static CoglSnippet *
build_snippet (ClutterColorPipeline *color_pipeline)
{
  g_autoptr (GString) snippet_globals = NULL;
  g_autoptr (GString) snippet_source = NULL;
  g_autoptr (GHashTable) added_sources = NULL;
  const GList *ops;
  size_t op_id;

  ops = clutter_color_pipeline_get_ops (color_pipeline);

  added_sources = g_hash_table_new (NULL, NULL);
  snippet_globals = g_string_new (NULL);
  snippet_source = g_string_new (NULL);

  for (const GList *l = ops; l != NULL; l = l->next)
    {
      ClutterColorOp *op = l->data;
      const char *source;

      source = get_shader_source (op);
      if (source && !g_hash_table_contains (added_sources, (gpointer) source))
        {
          g_string_append_printf (snippet_globals, "%s\n", source);
          g_hash_table_add (added_sources, (gpointer) source);
        }
    }

  op_id = 0;
  for (const GList *l = ops; l != NULL; l = l->next, op_id++)
    {
      ClutterColorOp *op = l->data;
      g_autofree char *declarations = NULL;

      declarations = get_shader_declarations (op, op_id);
      if (declarations)
        g_string_append (snippet_globals, declarations);
    }

  /* Build the color_pipeline_transform() function */
  g_string_append (snippet_globals,
                   "vec4 color_pipeline_transform (vec4 color)\n"
                   "{\n");

  op_id = 0;
  for (const GList *l = ops; l != NULL; l = l->next, op_id++)
    {
      ClutterColorOp *op = l->data;
      g_autofree char *invocation = NULL;

      invocation = get_shader_invocation (op, op_id);
      g_string_append_printf (snippet_globals, "  color = %s;\n", invocation);
    }

  g_string_append (snippet_globals, "  return color;\n}\n");

  /* Fragment hook: apply the transform to cogl_color_out */
  g_string_append (snippet_source,
                   "  cogl_color_out = color_pipeline_transform (cogl_color_out);\n");

  return cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                           snippet_globals->str,
                           snippet_source->str);
}

void
clutter_color_pipeline_shader_add_transform (ClutterColorPipeline *color_pipeline,
                                             CoglPipeline         *cogl_pipeline)
{
  g_autofree SnippetCacheKey *key = NULL;
  GHashTable *snippet_cache;
  CoglSnippet *snippet;
  const GList *ops;
  size_t op_id;

  if (clutter_color_pipeline_is_empty (color_pipeline))
    return;

  snippet_cache = get_snippet_cache ();
  key = build_snippet_cache_key (color_pipeline);
  snippet = g_hash_table_lookup (snippet_cache, key);

  if (!snippet)
    {
      g_autoptr (CoglSnippet) owned_snippet = NULL;

      owned_snippet = snippet = build_snippet (color_pipeline);
      g_hash_table_insert (snippet_cache,
                           g_steal_pointer (&key),
                           g_steal_pointer (&owned_snippet));
    }

  cogl_pipeline_add_snippet (cogl_pipeline, snippet);

  /* Set up uniforms and texture bindings */
  ops = clutter_color_pipeline_get_ops (color_pipeline);
  op_id = 0;
  for (const GList *l = ops; l != NULL; l = l->next, op_id++)
    {
      ClutterColorOp *op = l->data;
      update_shader_state (op, op_id, cogl_pipeline);
    }
}

void
clutter_color_pipeline_shader_set_color_state (CoglPipeline                    *cogl_pipeline,
                                               ClutterColorState               *source_color_state,
                                               ClutterColorState               *target_color_state,
                                               ClutterColorStateTransformFlags  flags)
{
  ClutterContext *context;
  ClutterColorTransform *transform;

  if (cogl_pipeline_has_capability (cogl_pipeline,
                                    CLUTTER_PIPELINE_CAPABILITY,
                                    CLUTTER_PIPELINE_CAPABILITY_COLOR_STATE))
    return;

  context = clutter_color_state_get_context (source_color_state);
  transform = clutter_color_transform_from_color_states (context,
                                                         source_color_state,
                                                         target_color_state,
                                                         flags);
  clutter_color_pipeline_shader_add_transform (
    clutter_color_transform_get_pipeline (transform),
    cogl_pipeline);
  cogl_pipeline_add_capability (cogl_pipeline,
                                CLUTTER_PIPELINE_CAPABILITY,
                                CLUTTER_PIPELINE_CAPABILITY_COLOR_STATE);
}

gboolean
clutter_color_pipeline_shader_needs_color_state (ClutterColorState               *source_color_state,
                                                 ClutterColorState               *target_color_state,
                                                 ClutterColorStateTransformFlags  flags)
{
  ClutterContext *context;
  ClutterColorTransform *transform;
  ClutterColorPipeline *pipeline;

  if (source_color_state == target_color_state)
    return FALSE;

  if (source_color_state == NULL || target_color_state == NULL)
    return TRUE;

  context = clutter_color_state_get_context (source_color_state);
  transform = clutter_color_transform_from_color_states (context,
                                                         source_color_state,
                                                         target_color_state,
                                                         flags);
  pipeline = clutter_color_transform_get_pipeline (transform);
  return !clutter_color_pipeline_is_empty (pipeline);
}
