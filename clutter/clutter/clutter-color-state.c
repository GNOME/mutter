/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2022  Intel Corporation.
 * Copyright (C) 2023-2024 Red Hat
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
 *   Naveen Kumar <naveen1.kumar@intel.com>
 *   Jonas Ådahl <jadahl@redhat.com>
 */

/**
 * ClutterColorState:
 *
 * Color state of each ClutterActor
 *
 * The #ClutterColorState class contains the colorspace of each color
 * states (e.g. sRGB colorspace).
 *
 * Each [class@Actor] would own such an object.
 *
 * A single #ClutterColorState object can be shared by multiple [class@Actor]
 * or maybe a separate color state for each [class@Actor] (depending on whether
 * #ClutterColorState would be statefull or stateless).
 *
 * #ClutterColorState, if not set during construction, it will default to sRGB
 * color state
 *
 * The #ClutterColorState would have API to get the colorspace, whether the
 * actor content is in pq or not, and things like that
 */

#include "config.h"

#include "clutter/clutter-color-state-private.h"

#include "clutter/clutter-color-manager-private.h"
#include "clutter/clutter-color-state-params.h"
#include "clutter/clutter-main.h"
#include "cogl-half-float.h"

#define LAYER_INDEX_3D_LUT_VALUES 10
#define UNIFORM_NAME_3D_LUT_VALUES "cogl_sampler10"
#define UNIFORM_NAME_3D_LUT_SIZE "lut_3d_size"

typedef struct _Clutter3DLut
{
  uint8_t *data;
  uint32_t size;
  CoglPixelFormat format;
} Clutter3DLut;

enum
{
  PROP_0,

  PROP_CONTEXT,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _ClutterColorStatePrivate
{
  ClutterContext *context;

  unsigned int id;
} ClutterColorStatePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (ClutterColorState,
                            clutter_color_state,
                            G_TYPE_OBJECT)

guint
clutter_color_transform_key_hash (gconstpointer data)
{
  const ClutterColorTransformKey *key = data;

  return key->source_eotf_bits << 0 |
         key->target_eotf_bits << 4 |
         key->luminance_bit    << 8 |
         key->color_trans_bit  << 9 |
         key->tone_mapping_bit << 10 |
         key->lut_3d           << 11 |
         key->opaque_bit       << 12;
}

gboolean
clutter_color_transform_key_equal (gconstpointer data1,
                                   gconstpointer data2)
{
  const ClutterColorTransformKey *key1 = data1;
  const ClutterColorTransformKey *key2 = data2;

  return (key1->source_eotf_bits == key2->source_eotf_bits &&
          key1->target_eotf_bits == key2->target_eotf_bits &&
          key1->luminance_bit == key2->luminance_bit &&
          key1->color_trans_bit == key2->color_trans_bit &&
          key1->tone_mapping_bit == key2->tone_mapping_bit &&
          key1->lut_3d == key2->lut_3d &&
          key1->opaque_bit == key2->opaque_bit);
}

void
clutter_color_state_init_3d_lut_transform_key (ClutterColorState               *color_state,
                                               ClutterColorState               *target_color_state,
                                               ClutterColorStateTransformFlags  flags,
                                               ClutterColorTransformKey        *key)
{
  key->source_eotf_bits = 0;
  key->target_eotf_bits = 0;
  key->luminance_bit = 0;
  key->color_trans_bit = 0;
  key->tone_mapping_bit = 0;
  key->lut_3d = 1;
  key->opaque_bit = !!(flags & CLUTTER_COLOR_STATE_TRANSFORM_OPAQUE);
}

void
clutter_color_transform_key_init (ClutterColorTransformKey        *key,
                                  ClutterColorState               *color_state,
                                  ClutterColorState               *target_color_state,
                                  ClutterColorStateTransformFlags  flags)
{
  ClutterColorStateClass *color_state_class =
    CLUTTER_COLOR_STATE_GET_CLASS (color_state);

  g_return_if_fail (CLUTTER_IS_COLOR_STATE (color_state));
  g_return_if_fail (CLUTTER_IS_COLOR_STATE (target_color_state));

  if (G_OBJECT_TYPE (color_state) != G_OBJECT_TYPE (target_color_state))
    {
      clutter_color_state_init_3d_lut_transform_key (color_state,
                                                     target_color_state,
                                                     flags,
                                                     key);
      return;
    }

  color_state_class->init_color_transform_key (color_state,
                                               target_color_state,
                                               flags,
                                               key);
}

unsigned int
clutter_color_state_get_id (ClutterColorState *color_state)
{
  ClutterColorStatePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE (color_state), 0);

  priv = clutter_color_state_get_instance_private (color_state);

  return priv->id;
}

static void
clutter_color_state_constructed (GObject *object)
{
  ClutterColorState *color_state = CLUTTER_COLOR_STATE (object);
  ClutterColorStatePrivate *priv =
    clutter_color_state_get_instance_private (color_state);
  ClutterColorManager *color_manager;

  g_warn_if_fail (priv->context);

  color_manager = clutter_context_get_color_manager (priv->context);

  priv->id = clutter_color_manager_get_next_id (color_manager);
}

static void
clutter_color_state_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  ClutterColorState *color_state = CLUTTER_COLOR_STATE (object);
  ClutterColorStatePrivate *priv;

  priv = clutter_color_state_get_instance_private (color_state);

  switch (prop_id)
    {
    case PROP_CONTEXT:
      priv->context = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_color_state_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  ClutterColorState *color_state = CLUTTER_COLOR_STATE (object);
  ClutterColorStatePrivate *priv =
    clutter_color_state_get_instance_private (color_state);

  switch (prop_id)
    {
    case PROP_CONTEXT:
      g_value_set_object (value, priv->context);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_color_state_class_init (ClutterColorStateClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = clutter_color_state_constructed;
  object_class->set_property = clutter_color_state_set_property;
  object_class->get_property = clutter_color_state_get_property;

  /**
   * ClutterColorState:context:
   *
   * The associated ClutterContext.
   */
  obj_props[PROP_CONTEXT] = g_param_spec_object ("context", NULL, NULL,
                                                 CLUTTER_TYPE_CONTEXT,
                                                 G_PARAM_READWRITE |
                                                 G_PARAM_STATIC_STRINGS |
                                                 G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
clutter_color_state_init (ClutterColorState *color_state)
{
}

/*
* Tetrahedral interpolation implementation based on:
* https://docs.acescentral.com/specifications/clf#appendix-interpolation
*/
static const char sample_3d_lut_source[] =
  "uniform float " UNIFORM_NAME_3D_LUT_SIZE ";\n"
  "// sample_3d_lut:\n"
  "// Tetrahedral inerpolation\n"
  "// @color: Normalized ([0,1]) electrical signal value\n"
  "// Returns: tristimulus values ([0,1])\n"
  "vec3 sample_3d_lut (vec3 color)\n"
  "{\n"
  "  vec3 scaled_color = color * (" UNIFORM_NAME_3D_LUT_SIZE " - 1.0);\n"
  "  vec3 index_low = floor (scaled_color);\n"
  "  vec3 index_high = min (index_low + 1.0, " UNIFORM_NAME_3D_LUT_SIZE " - 1.0);\n"
  "  vec3 t = scaled_color - index_low;\n"
  "\n"
  "  // For accessing the y, z coordinates on texture v coord:\n"
  "  // y + (z * size) and normalize it after that\n"
  "  index_low.z *= " UNIFORM_NAME_3D_LUT_SIZE ";\n"
  "  index_high.z *= " UNIFORM_NAME_3D_LUT_SIZE ";\n"
  "  float normalize_v = 1.0 / "
       "((" UNIFORM_NAME_3D_LUT_SIZE " * " UNIFORM_NAME_3D_LUT_SIZE ") - 1.0);\n"
  "  // x can be normalized now\n"
  "  index_low.x /= " UNIFORM_NAME_3D_LUT_SIZE " - 1.0;\n"
  "  index_high.x /= " UNIFORM_NAME_3D_LUT_SIZE " - 1.0;\n"
  "\n"
  "  vec2 coord000 = vec2 (index_low.x, (index_low.y + index_low.z) * normalize_v);\n"
  "  vec2 coord111 = vec2 (index_high.x, (index_high.y + index_high.z) * normalize_v);\n"
  "  vec3 v000 = texture (" UNIFORM_NAME_3D_LUT_VALUES ", coord000).rgb;\n"
  "  vec3 v111 = texture (" UNIFORM_NAME_3D_LUT_VALUES ", coord111).rgb;\n"
  "\n"
  "  if (t.x > t.y)\n"
  "    {\n"
  "      if (t.y > t.z)\n"
  "        {\n"
  "          vec2 coord100 = vec2 (index_high.x, (index_low.y + index_low.z) * normalize_v);\n"
  "          vec2 coord110 = vec2 (index_high.x, (index_high.y + index_low.z) * normalize_v);\n"
  "\n"
  "          vec3 v100 = texture (" UNIFORM_NAME_3D_LUT_VALUES ", coord100).rgb;\n"
  "          vec3 v110 = texture (" UNIFORM_NAME_3D_LUT_VALUES ", coord110).rgb;\n"
  "\n"
  "          return v000 + t.x * (v100 - v000) + t.y * (v110 - v100) + t.z * (v111 - v110);\n"
  "        }\n"
  "      else if (t.x > t.z)\n"
  "        {\n"
  "          vec2 coord100 = vec2 (index_high.x, (index_low.y + index_low.z) * normalize_v);\n"
  "          vec2 coord101 = vec2 (index_high.x, (index_low.y + index_high.z) * normalize_v);\n"
  "\n"
  "          vec3 v100 = texture (" UNIFORM_NAME_3D_LUT_VALUES ", coord100).rgb;\n"
  "          vec3 v101 = texture (" UNIFORM_NAME_3D_LUT_VALUES ", coord101).rgb;\n"
  "\n"
  "          return v000 + t.x * (v100 - v000) + t.y * (v111 - v101) + t.z * (v101 - v100);\n"
  "        }\n"
  "      else\n"
  "        {\n"
  "          vec2 coord001 = vec2 (index_low.x, (index_low.y + index_high.z) * normalize_v);\n"
  "          vec2 coord101 = vec2 (index_high.x, (index_low.y + index_high.z) * normalize_v);\n"
  "\n"
  "          vec3 v001 = texture (" UNIFORM_NAME_3D_LUT_VALUES ", coord001).rgb;\n"
  "          vec3 v101 = texture (" UNIFORM_NAME_3D_LUT_VALUES ", coord101).rgb;\n"
  "\n"
  "          return v000 + t.x * (v101 - v001) + t.y * (v111 - v101) + t.z * (v001 - v000);\n"
  "        }\n"
  "    }\n"
  "  else\n"
  "    {\n"
  "      if (t.z > t.y)\n"
  "        {\n"
  "          vec2 coord001 = vec2 (index_low.x, (index_low.y + index_high.z) * normalize_v);\n"
  "          vec2 coord011 = vec2 (index_low.x, (index_high.y + index_high.z) * normalize_v);\n"
  "\n"
  "          vec3 v001 = texture (" UNIFORM_NAME_3D_LUT_VALUES ", coord001).rgb;\n"
  "          vec3 v011 = texture (" UNIFORM_NAME_3D_LUT_VALUES ", coord011).rgb;\n"
  "\n"
  "          return v000 + t.x * (v111 - v011) + t.y * (v011 - v001) + t.z * (v001 - v000);\n"
  "        }\n"
  "      else if (t.z > t.x)\n"
  "        {\n"
  "          vec2 coord010 = vec2 (index_low.x, (index_high.y + index_low.z) * normalize_v);\n"
  "          vec2 coord011 = vec2 (index_low.x, (index_high.y + index_high.z) * normalize_v);\n"
  "\n"
  "          vec3 v010 = texture (" UNIFORM_NAME_3D_LUT_VALUES ", coord010).rgb;\n"
  "          vec3 v011 = texture (" UNIFORM_NAME_3D_LUT_VALUES ", coord011).rgb;\n"
  "\n"
  "          return v000 + t.x * (v111 - v011) + t.y * (v010 - v000) + t.z * (v011 - v010);\n"
  "        }\n"
  "      else\n"
  "        {\n"
  "          vec2 coord010 = vec2 (index_low.x, (index_high.y + index_low.z) * normalize_v);\n"
  "          vec2 coord110 = vec2 (index_high.x, (index_high.y + index_low.z) * normalize_v);\n"
  "\n"
  "          vec3 v010 = texture (" UNIFORM_NAME_3D_LUT_VALUES ", coord010).rgb;\n"
  "          vec3 v110 = texture (" UNIFORM_NAME_3D_LUT_VALUES ", coord110).rgb;\n"
  "\n"
  "          return v000 + t.x * (v110 - v010) + t.y * (v010 - v000) + t.z * (v111 - v110);\n"
  "        }\n"
  "    }\n"
  "}\n"
  "\n"
  "vec4 sample_3d_lut (vec4 color)\n"
  "{\n"
  "  return vec4 (sample_3d_lut (color.rgb), color.a);\n"
  "}\n";

static const ClutterColorOpSnippet sample_3d_lut = {
  .source = sample_3d_lut_source,
  .name = "sample_3d_lut",
};

void
clutter_color_state_append_3d_lut_transform_snippet (ClutterColorState *color_state,
                                                     ClutterColorState *target_color_state,
                                                     GString           *snippet_globals,
                                                     GString           *snippet_source,
                                                     const char        *snippet_color_var)
{
  clutter_color_op_snippet_append_global (&sample_3d_lut,
                                          snippet_globals);

  g_string_append_printf (snippet_globals,
                          "vec3 transform_color_state (vec3 %s)\n"
                          "{\n",
                          snippet_color_var);

  clutter_color_op_snippet_append_source (&sample_3d_lut,
                                          snippet_globals,
                                          snippet_color_var);

  g_string_append_printf (snippet_globals,
                          "  return %s;\n"
                          "}\n"
                          "\n",
                          snippet_color_var);
}

static void
clutter_color_state_append_transform_snippet (ClutterColorState               *color_state,
                                              ClutterColorState               *target_color_state,
                                              GString                         *snippet_globals,
                                              GString                         *snippet_source,
                                              const char                      *snippet_color_var)
{
  ClutterColorStateClass *color_state_class =
    CLUTTER_COLOR_STATE_GET_CLASS (color_state);

  if (G_OBJECT_TYPE (color_state) != G_OBJECT_TYPE (target_color_state))
    {
      clutter_color_state_append_3d_lut_transform_snippet (color_state,
                                                           target_color_state,
                                                           snippet_globals,
                                                           snippet_source,
                                                           snippet_color_var);
      return;
    }

  color_state_class->append_transform_snippet (color_state,
                                               target_color_state,
                                               snippet_globals,
                                               snippet_source,
                                               snippet_color_var);
}

static CoglSnippet *
clutter_color_state_create_transform_snippet (ClutterColorState               *color_state,
                                              ClutterColorState               *target_color_state,
                                              ClutterColorStateTransformFlags  flags)
{
  CoglSnippet *snippet;
  const char *snippet_color_var;
  g_autoptr (GString) snippet_globals = NULL;
  g_autoptr (GString) snippet_source = NULL;

  snippet_globals = g_string_new (NULL);
  snippet_source = g_string_new (NULL);
  snippet_color_var = "color_state_color";

  clutter_color_state_append_transform_snippet (color_state,
                                                target_color_state,
                                                snippet_globals,
                                                snippet_source,
                                                snippet_color_var);

  if (flags & CLUTTER_COLOR_STATE_TRANSFORM_OPAQUE)
    {
      g_string_append_printf (snippet_source,
                              "  cogl_color_out.rgb = transform_color_state (cogl_color_out.rgb);\n");
    }
  else
    {
      g_string_append_printf (snippet_source,
                              "\n"
                              "  if (cogl_color_out.a > 0.0)\n"
                              "    {\n"
                              "      cogl_color_out.rgb =\n"
                              "        transform_color_state (cogl_color_out.rgb / cogl_color_out.a);\n"
                              "    }\n"
                              "\n"
                              "  cogl_color_out.rgb *= cogl_color_out.a;\n");
    }

  snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                              snippet_globals->str,
                              snippet_source->str);
  cogl_snippet_set_capability (snippet,
                               CLUTTER_PIPELINE_CAPABILITY,
                               CLUTTER_PIPELINE_CAPABILITY_COLOR_STATE);
  return snippet;
}

static CoglSnippet *
clutter_color_state_get_transform_snippet (ClutterColorState               *color_state,
                                           ClutterColorState               *target_color_state,
                                           ClutterColorStateTransformFlags  flags)
{
  ClutterColorStatePrivate *priv;
  ClutterColorManager *color_manager;
  ClutterColorTransformKey transform_key;
  CoglSnippet *snippet;

  priv = clutter_color_state_get_instance_private (color_state);
  color_manager = clutter_context_get_color_manager (priv->context);

  clutter_color_transform_key_init (&transform_key,
                                    color_state,
                                    target_color_state,
                                    flags);
  snippet = clutter_color_manager_lookup_snippet (color_manager,
                                                  &transform_key);
  if (snippet)
    return g_object_ref (snippet);

  snippet = clutter_color_state_create_transform_snippet (color_state,
                                                          target_color_state,
                                                          flags);

  clutter_color_manager_add_snippet (color_manager,
                                     &transform_key,
                                     snippet);
  return snippet;
}

static void
sample_3d_lut_input (float **in,
                     int     lut_size)
{
  int index;
  int i, j, k;
  float x, y, z;
  float step;
  float *sample;

  sample = *in;
  step = 1.0f / (lut_size - 1);

  /* Store the 3D LUT as a 2D texture (lut_size x (lut_size x lut_size))
   * Access the data as tex(x, y + z * lut_size) */
  index = 0;
  for (k = 0, z = 0.0f; k < lut_size; k++, z += step)
    for (j = 0, y = 0.0f; j < lut_size; j++, y += step)
      for (i = 0, x = 0.0f; i < lut_size; i++, x += step)
        {
          sample[index++] = x;
          sample[index++] = y;
          sample[index++] = z;
        }
}

static void
encode_3d_lut_output (float            *lut_output,
                      int               n_samples,
                      uint8_t         **out_encoded,
                      CoglPixelFormat  *out_format)
{
  CoglContext *context =
    clutter_backend_get_cogl_context (clutter_get_default_backend ());
  uint16_t *encoded_output_half;
  uint8_t *encoded_output;
  int i;

  if (cogl_context_has_feature (context, COGL_FEATURE_ID_TEXTURE_HALF_FLOAT))
    {
      encoded_output_half = g_malloc (n_samples * 4 * sizeof (uint16_t));
      *out_encoded = (uint8_t *) encoded_output_half;
      *out_format = COGL_PIXEL_FORMAT_RGBX_FP_16161616;

      for (i = 0; i < n_samples; i++)
        {
          encoded_output_half[0] = cogl_float_to_half (lut_output[0]);
          encoded_output_half[1] = cogl_float_to_half (lut_output[1]);
          encoded_output_half[2] = cogl_float_to_half (lut_output[2]);
          encoded_output_half[3] = cogl_float_to_half (1.0f);
          encoded_output_half += 4;
          lut_output += 3;
        }

      return;
    }

  encoded_output = g_malloc (n_samples * 4 * sizeof (uint8_t));
  *out_encoded = encoded_output;
  *out_format = COGL_PIXEL_FORMAT_RGBX_8888;

  for (i = 0; i < n_samples; i++)
    {
      encoded_output[0] = (uint8_t) (lut_output[0] * UINT8_MAX);
      encoded_output[1] = (uint8_t) (lut_output[1] * UINT8_MAX);
      encoded_output[2] = (uint8_t) (lut_output[2] * UINT8_MAX);
      encoded_output[3] = (uint8_t) UINT8_MAX;
      encoded_output += 4;
      lut_output += 3;
    }
}

static void
get_3d_lut (ClutterColorState  *color_state,
            ClutterColorState  *target_color_state,
            Clutter3DLut      **out_lut_3d)
{
  int lut_size;
  int n_samples;
  CoglPixelFormat lut_format;
  g_autofree float *data = NULL;
  g_autofree uint8_t *encoded_lut_output = NULL;
  Clutter3DLut *lut_3d;

  lut_size = 33;
  n_samples = lut_size * lut_size * lut_size;

  data = g_malloc (n_samples * 3 * sizeof (float));

  sample_3d_lut_input (&data, lut_size);

  clutter_color_state_do_transform (color_state,
                                    target_color_state,
                                    data,
                                    n_samples);

  encode_3d_lut_output (data,
                        n_samples,
                        &encoded_lut_output,
                        &lut_format);

  lut_3d = g_new (Clutter3DLut, 1);
  lut_3d->data = g_steal_pointer (&encoded_lut_output);
  lut_3d->size = lut_size;
  lut_3d->format = lut_format;

  *out_lut_3d = lut_3d;
}

static void
clutter_3d_lut_free (Clutter3DLut *lut_3d)
{
  g_clear_pointer (&lut_3d->data, g_free);
  g_free (lut_3d);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (Clutter3DLut, clutter_3d_lut_free);

void
clutter_color_state_update_3d_lut_uniforms (ClutterColorState *color_state,
                                            ClutterColorState *target_color_state,
                                            CoglPipeline      *pipeline)
{
  int rowstride;
  int uniform_location_size;
  g_autoptr (Clutter3DLut) lut_3d = NULL;
  g_autoptr (CoglTexture) lut_texture = NULL;
  g_autoptr (GError) error = NULL;
  CoglContext *context =
    clutter_backend_get_cogl_context (clutter_get_default_backend ());

  get_3d_lut (color_state, target_color_state, &lut_3d);

  switch (lut_3d->format)
    {
    case COGL_PIXEL_FORMAT_RGBX_FP_16161616:
      rowstride = lut_3d->size * 4 * sizeof (uint16_t);
      break;
    case COGL_PIXEL_FORMAT_RGBX_8888:
      rowstride = lut_3d->size * 4 * sizeof (uint8_t);
      break;
    default:
      g_warning ("Unhandled pixel format when updating the 3D LUT");
      return;
    }

  lut_texture = cogl_texture_2d_new_from_data (context,
                                               lut_3d->size,
                                               lut_3d->size * lut_3d->size,
                                               lut_3d->format,
                                               rowstride,
                                               lut_3d->data,
                                               &error);
  if (!lut_texture)
    {
      g_warning ("Failed creating 3D LUT as a texture: %s", error->message);
      return;
    }

  cogl_pipeline_set_layer_texture (pipeline,
                                   LAYER_INDEX_3D_LUT_VALUES,
                                   lut_texture);

  /* Textures are only added as layers, use this combine mode to avoid
   * this layer to modify the result, and use it as a standard texture */
  cogl_pipeline_set_layer_combine (pipeline, LAYER_INDEX_3D_LUT_VALUES,
                                   "RGBA = REPLACE(PREVIOUS)", NULL);

  cogl_pipeline_set_layer_wrap_mode_s (pipeline, LAYER_INDEX_3D_LUT_VALUES,
                                       COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);
  cogl_pipeline_set_layer_wrap_mode_t (pipeline, LAYER_INDEX_3D_LUT_VALUES,
                                       COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);
  /* Interpolation explicitly done at shader so use nearest filter */
  cogl_pipeline_set_layer_filters (pipeline, LAYER_INDEX_3D_LUT_VALUES,
                                   COGL_PIPELINE_FILTER_NEAREST,
                                   COGL_PIPELINE_FILTER_NEAREST);

  uniform_location_size =
    cogl_pipeline_get_uniform_location (pipeline,
                                        UNIFORM_NAME_3D_LUT_SIZE);
  cogl_pipeline_set_uniform_1f (pipeline,
                                uniform_location_size,
                                lut_3d->size);
}

ClutterContext *
clutter_color_state_get_context (ClutterColorState *color_state)
{
  ClutterColorStatePrivate *priv =
    clutter_color_state_get_instance_private (color_state);

  return priv->context;
}

void
clutter_color_state_update_uniforms (ClutterColorState *color_state,
                                     ClutterColorState *target_color_state,
                                     CoglPipeline      *pipeline)
{
  ClutterColorStateClass *color_state_class =
    CLUTTER_COLOR_STATE_GET_CLASS (color_state);

  g_return_if_fail (CLUTTER_IS_COLOR_STATE (color_state));
  g_return_if_fail (CLUTTER_IS_COLOR_STATE (target_color_state));

  if (G_OBJECT_TYPE (color_state) != G_OBJECT_TYPE (target_color_state))
    {
      clutter_color_state_update_3d_lut_uniforms (color_state,
                                                  target_color_state,
                                                  pipeline);
      return;
    }

  color_state_class->update_uniforms (color_state,
                                      target_color_state,
                                      pipeline);
}

/**
 * clutter_color_state_do_transform:
 * @color_state: a #ClutterColorState
 * @target_color_state: the target a #ClutterColorState
 * @data: (array): The transform data
 * @n_samples: The number of data samples
 *
 * Applies the transform to the given #ClutterColorState
 */
void
clutter_color_state_do_transform (ClutterColorState *color_state,
                                  ClutterColorState *target_color_state,
                                  float             *data,
                                  int                n_samples)
{
  ClutterColorStateClass *color_state_class =
    CLUTTER_COLOR_STATE_GET_CLASS (color_state);
  ClutterColorStateClass *target_color_state_class =
    CLUTTER_COLOR_STATE_GET_CLASS (target_color_state);

  g_return_if_fail (CLUTTER_IS_COLOR_STATE (color_state));
  g_return_if_fail (CLUTTER_IS_COLOR_STATE (target_color_state));

  color_state_class->do_transform_to_XYZ (color_state,
                                          data,
                                          n_samples);

  if (CLUTTER_IS_COLOR_STATE_PARAMS (color_state) ||
      CLUTTER_IS_COLOR_STATE_PARAMS (target_color_state))
    {
      clutter_color_state_params_do_tone_mapping (color_state,
                                                  target_color_state,
                                                  data,
                                                  n_samples);
    }

  target_color_state_class->do_transform_from_XYZ (target_color_state,
                                                   data,
                                                   n_samples);
}

void
clutter_color_state_add_pipeline_transform (ClutterColorState               *color_state,
                                            ClutterColorState               *target_color_state,
                                            CoglPipeline                    *pipeline,
                                            ClutterColorStateTransformFlags  flags)
{
  g_autoptr (CoglSnippet) snippet = NULL;

  g_return_if_fail (CLUTTER_IS_COLOR_STATE (color_state));
  g_return_if_fail (CLUTTER_IS_COLOR_STATE (target_color_state));

  if (!clutter_color_state_needs_mapping (color_state, target_color_state))
    return;

  snippet = clutter_color_state_get_transform_snippet (color_state,
                                                       target_color_state,
                                                       flags);
  cogl_pipeline_add_snippet (pipeline, snippet);

  clutter_color_state_update_uniforms (color_state,
                                       target_color_state,
                                       pipeline);
}

gboolean
clutter_color_state_equals (ClutterColorState *color_state,
                            ClutterColorState *other_color_state)
{
  ClutterColorStateClass *color_state_class =
    CLUTTER_COLOR_STATE_GET_CLASS (color_state);

  if (color_state == other_color_state)
    return TRUE;

  if (color_state == NULL || other_color_state == NULL)
    return FALSE;

  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE (color_state), FALSE);
  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE (other_color_state), FALSE);

  if (G_OBJECT_TYPE (color_state) != G_OBJECT_TYPE (other_color_state))
    return FALSE;

  return color_state_class->equals (color_state, other_color_state);
}

gboolean
clutter_color_state_needs_mapping (ClutterColorState *color_state,
                                   ClutterColorState *target_color_state)
{
  ClutterColorStateClass *color_state_class =
    CLUTTER_COLOR_STATE_GET_CLASS (color_state);

  if (color_state == target_color_state)
    return FALSE;

  if (color_state == NULL || target_color_state == NULL)
    return TRUE;

  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE (color_state), TRUE);
  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE (target_color_state), TRUE);

  if (G_OBJECT_TYPE (color_state) != G_OBJECT_TYPE (target_color_state))
    return TRUE;

  return color_state_class->needs_mapping (color_state, target_color_state);
}

char *
clutter_color_state_to_string (ClutterColorState *color_state)
{
  ClutterColorStateClass *color_state_class =
    CLUTTER_COLOR_STATE_GET_CLASS (color_state);

  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE (color_state), NULL);

  return color_state_class->to_string (color_state);
}

ClutterEncodingRequiredFormat
clutter_color_state_required_format (ClutterColorState *color_state)
{

  ClutterColorStateClass *color_state_class =
    CLUTTER_COLOR_STATE_GET_CLASS (color_state);

  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE (color_state), FALSE);

  return color_state_class->required_format (color_state);
}

/**
 * clutter_color_state_get_blending:
 * @color_state: a #ClutterColorState
 * @force: if a linear variant should be forced
 *
 * Retrieves a variant of @color_state that is suitable for blending. This
 * usually is a variant with linear transfer characteristics. If @color_state
 * already is a #ClutterColorState suitable for blending, then @color_state is
 * returned.
 *
 * If @force is TRUE then linear transfer characteristics are used always.
 *
 * Returns: (transfer full): the #ClutterColorState suitable for blending
 */
ClutterColorState *
clutter_color_state_get_blending (ClutterColorState *color_state,
                                  gboolean           force)
{
  ClutterColorStateClass *color_state_class =
    CLUTTER_COLOR_STATE_GET_CLASS (color_state);

  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE (color_state), FALSE);

  return color_state_class->get_blending (color_state, force);
}

void
clutter_color_op_snippet_append_global (const ClutterColorOpSnippet *color_snippet,
                                        GString                     *snippet_global)
{
  if (!color_snippet)
    return;

  g_string_append_printf (snippet_global, "%s\n", color_snippet->source);
}

void
clutter_color_op_snippet_append_source (const ClutterColorOpSnippet *color_snippet,
                                        GString                     *snippet_source,
                                        const char                  *snippet_color_var)
{
  if (!color_snippet)
    return;

  g_string_append_printf (snippet_source,
                          "  %s = %s (%s);\n",
                          snippet_color_var,
                          color_snippet->name,
                          snippet_color_var);
}
