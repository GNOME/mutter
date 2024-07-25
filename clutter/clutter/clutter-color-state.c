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
#include "clutter/clutter-debug.h"
#include "clutter/clutter-enum-types.h"
#include "clutter/clutter-private.h"

enum
{
  PROP_0,

  PROP_CONTEXT,
  PROP_COLORSPACE,
  PROP_TRANSFER_FUNCTION,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

struct _ClutterColorState
{
  GObject parent_instance;
};

typedef struct _ClutterColorStatePrivate
{
  ClutterContext *context;

  unsigned int id;
  ClutterColorspace colorspace;
  ClutterTransferFunction transfer_function;
} ClutterColorStatePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (ClutterColorState,
                            clutter_color_state,
                            G_TYPE_OBJECT)

guint
clutter_color_transform_key_hash (gconstpointer data)
{
  const ClutterColorTransformKey *key = data;

  return (key->source.colorspace ^
          key->source.transfer_function ^
          key->target.colorspace ^
          key->target.transfer_function);
}

gboolean
clutter_color_transform_key_equal (gconstpointer data1,
                                   gconstpointer data2)
{
  const ClutterColorTransformKey *key1 = data1;
  const ClutterColorTransformKey *key2 = data2;

  return (key1->source.colorspace == key2->source.colorspace &&
          key1->source.transfer_function == key2->source.transfer_function &&
          key1->target.colorspace == key2->target.colorspace &&
          key1->target.transfer_function == key2->target.transfer_function);
}

void
clutter_color_transform_key_init (ClutterColorTransformKey *key,
                                  ClutterColorState        *color_state,
                                  ClutterColorState        *target_color_state)
{
  ClutterColorStatePrivate *priv =
    clutter_color_state_get_instance_private (color_state);
  ClutterColorStatePrivate *target_priv =
    clutter_color_state_get_instance_private (target_color_state);

  key->source.colorspace = priv->colorspace;
  key->source.transfer_function = priv->transfer_function;
  key->target.colorspace = target_priv->colorspace;
  key->target.transfer_function = target_priv->transfer_function;
}

static const char *
clutter_colorspace_to_string (ClutterColorspace colorspace)
{
  switch (colorspace)
    {
    case CLUTTER_COLORSPACE_DEFAULT:
      return "unknown";
    case CLUTTER_COLORSPACE_SRGB:
      return "sRGB";
    case CLUTTER_COLORSPACE_BT2020:
      return "BT.2020";
    }

  g_assert_not_reached ();
}

static const char *
clutter_transfer_function_to_string (ClutterTransferFunction transfer_function)
{
  switch (transfer_function)
    {
    case CLUTTER_TRANSFER_FUNCTION_DEFAULT:
      return "default";
    case CLUTTER_TRANSFER_FUNCTION_SRGB:
      return "sRGB";
    case CLUTTER_TRANSFER_FUNCTION_PQ:
      return "PQ";
    case CLUTTER_TRANSFER_FUNCTION_LINEAR:
      return "linear";
    }

  g_assert_not_reached ();
}

unsigned int
clutter_color_state_get_id (ClutterColorState *color_state)
{
  ClutterColorStatePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE (color_state), 0);

  priv = clutter_color_state_get_instance_private (color_state);

  return priv->id;
}

ClutterColorspace
clutter_color_state_get_colorspace (ClutterColorState *color_state)
{
  ClutterColorStatePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE (color_state),
                        CLUTTER_COLORSPACE_DEFAULT);

  priv = clutter_color_state_get_instance_private (color_state);

  return priv->colorspace;
}

ClutterTransferFunction
clutter_color_state_get_transfer_function (ClutterColorState *color_state)
{
  ClutterColorStatePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE (color_state),
                        CLUTTER_TRANSFER_FUNCTION_DEFAULT);

  priv = clutter_color_state_get_instance_private (color_state);

  return priv->transfer_function;
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

    case PROP_COLORSPACE:
      priv->colorspace = g_value_get_enum (value);
      break;

    case PROP_TRANSFER_FUNCTION:
      priv->transfer_function = g_value_get_enum (value);
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

    case PROP_COLORSPACE:
      g_value_set_enum (value,
                        clutter_color_state_get_colorspace (color_state));
      break;

    case PROP_TRANSFER_FUNCTION:
      g_value_set_enum (value,
                        clutter_color_state_get_transfer_function (color_state));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_color_state_class_init (ClutterColorStateClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->constructed = clutter_color_state_constructed;
  gobject_class->set_property = clutter_color_state_set_property;
  gobject_class->get_property = clutter_color_state_get_property;

  /**
   * ClutterColorState:context:
   *
   * The associated ClutterContext.
   */
  obj_props[PROP_CONTEXT] =
    g_param_spec_object ("context", NULL, NULL,
                         CLUTTER_TYPE_CONTEXT,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS |
                         G_PARAM_CONSTRUCT_ONLY);

  /**
   * ClutterColorState:colorspace:
   *
   * Colorspace information of the each color state,
   * defaults to sRGB colorspace
   */
  obj_props[PROP_COLORSPACE] =
    g_param_spec_enum ("colorspace", NULL, NULL,
                       CLUTTER_TYPE_COLORSPACE,
                       CLUTTER_COLORSPACE_SRGB,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS |
                       G_PARAM_CONSTRUCT_ONLY);

  /**
   * ClutterColorState:transfer-function:
   *
   * Transfer function.
   */
  obj_props[PROP_TRANSFER_FUNCTION] =
    g_param_spec_enum ("transfer-function", NULL, NULL,
                       CLUTTER_TYPE_TRANSFER_FUNCTION,
                       CLUTTER_TRANSFER_FUNCTION_SRGB,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS |
                       G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (gobject_class, N_PROPS, obj_props);
}

static void
clutter_color_state_init (ClutterColorState *color_state)
{
}

/**
 * clutter_color_state_new:
 *
 * Create a new ClutterColorState object.
 *
 * Return value: A new ClutterColorState object.
 **/
ClutterColorState *
clutter_color_state_new (ClutterContext          *context,
                         ClutterColorspace        colorspace,
                         ClutterTransferFunction  transfer_function)
{
  return g_object_new (CLUTTER_TYPE_COLOR_STATE,
                       "context", context,
                       "colorspace", colorspace,
                       "transfer-function", transfer_function,
                       NULL);
}

static const char pq_eotf_source[] =
  "// pq_eotf:\n"
  "// @pq: Normalized ([0,1]) electrical signal value\n"
  "// Returns: Luminance in cd/m²\n"
  "vec3 pq_eotf (vec3 pq)\n"
  "{\n"
  "  const float c1 = 0.8359375;\n"
  "  const float c2 = 18.8515625;\n"
  "  const float c3 = 18.6875;\n"
  "\n"
  "  const float oo_m1 = 1.0 / 0.1593017578125;\n"
  "  const float oo_m2 = 1.0 / 78.84375;\n"
  "\n"
  "  vec3 num = max (pow (pq, vec3 (oo_m2)) - c1, vec3 (0.0));\n"
  "  vec3 den = c2 - c3 * pow (pq, vec3 (oo_m2));\n"
  "\n"
  "  return 10000.0 * pow (num / den, vec3 (oo_m1));\n"
  "}\n"
  "\n"
  "vec4 pq_eotf (vec4 pq)\n"
  "{\n"
  "  return vec4 (pq_eotf (pq.rgb), pq.a);\n"
  "}\n";

static const char pq_inv_eotf_source[] =
  "// pq_inv_eotf:\n"
  "// @nits: Optical signal value in cd/m²\n"
  "// Returns: Normalized ([0,1]) electrical signal value\n"
  "vec3 pq_inv_eotf (vec3 nits)\n"
  "{\n"
  "  vec3 normalized = clamp (nits / 10000.0, vec3 (0), vec3 (1));\n"
  "  float m1 = 0.1593017578125;\n"
  "  float m2 = 78.84375;\n"
  "  float c1 = 0.8359375;\n"
  "  float c2 = 18.8515625;\n"
  "  float c3 = 18.6875;\n"
  "  vec3 normalized_pow_m1 = pow (normalized, vec3 (m1));\n"
  "  vec3 num = vec3 (c1) + c2 * normalized_pow_m1;\n"
  "  vec3 denum = vec3 (1.0) + c3 * normalized_pow_m1;\n"
  "  return pow (num / denum, vec3 (m2));\n"
  "}\n"
  "\n"
  "vec4 pq_inv_eotf (vec4 nits)\n"
  "{\n"
  "  return vec4 (pq_inv_eotf (nits.rgb), nits.a);\n"
  "}\n";

static const char srgb_eotf_source[] =
  "// srgb_eotf:\n"
  "// @color: Normalized ([0,1]) electrical signal value.\n"
  "// Returns: Normalized luminance ([0,1])\n"
  "vec3 srgb_eotf (vec3 color)\n"
  "{\n"
  "  bvec3 is_low = lessThanEqual (color, vec3 (0.04045));\n"
  "  vec3 lo_part = color / 12.92;\n"
  "  vec3 hi_part = pow ((color + 0.055) / 1.055, vec3 (12.0 / 5.0));\n"
  "  return mix (hi_part, lo_part, is_low);\n"
  "}\n"
  "\n"
  "vec4 srgb_eotf (vec4 color)\n"
  "{\n"
  "  return vec4 (srgb_eotf (color.rgb), color.a);\n"
  "}\n";

static const char srgb_inv_eotf_source[] =
  "// srgb_inv_eotf:\n"
  "// @color: Normalized ([0,1]) optical signal value\n"
  "// Returns: Normalized ([0,1]) electrical signal value\n"
  "vec3 srgb_inv_eotf (vec3 color)\n"
  "{\n"
  "  bvec3 is_lo = lessThanEqual (color, vec3 (0.0031308));\n"
  "\n"
  "  vec3 lo_part = color * 12.92;\n"
  "  vec3 hi_part = pow (color, vec3 (5.0 / 12.0)) * 1.055 - 0.055;\n"
  "  return mix (hi_part, lo_part, is_lo);\n"
  "}\n"
  "\n"
  "vec4 srgb_inv_eotf (vec4 color)\n"
  "{\n"
  "  return vec4 (srgb_inv_eotf (color.rgb), color.a);\n"
  "}\n";

/* Luminance gain default value (203) retrieved from
 * https://github.com/w3c/ColorWeb-CG/blob/feature/add-mastering-display-info/hdr_html_canvas_element.md#srgb-to-rec2100-pq */
static const char srgb_luminance_gain_source[] =
  "vec3 srgb_luminance_gain (vec3 value)\n"
  "{\n"
  "  return 203.0 * value;\n"
  "}\n";

static const char pq_luminance_clamp_source[] =
  "vec3 pq_luminance_clamp (vec3 value)\n"
  "{\n"
  "  return clamp (value, 0.0, 203.0) / 203.0;\n"
  "}\n";

/* Calculated using:
 *   numpy.dot(colour.models.RGB_COLOURSPACE_BT2020.matrix_XYZ_to_RGB,
 *             colour.models.RGB_COLOURSPACE_BT709.matrix_RGB_to_XYZ)
 */
static const char bt709_to_bt2020_matrix_source[] =
  "mat3 bt709_to_bt2020 =\n"
  "  mat3 (vec3 (0.6274039,  0.06909729, 0.01639144),\n"
  "        vec3 (0.32928304, 0.9195404,  0.08801331),\n"
  "        vec3 (0.04331307, 0.01136232, 0.89559525));\n";

/*
 * Calculated using:
 *  numpy.dot(colour.models.RGB_COLOURSPACE_BT709.matrix_XYZ_to_RGB,
 *            colour.models.RGB_COLOURSPACE_BT2020.matrix_RGB_to_XYZ)
 */
static const char bt2020_to_bt709_matrix_source[] =
  "mat3 bt2020_to_bt709 =\n"
  "  mat3 (vec3 (1.660491,    -0.12455047, -0.01815076),\n"
  "        vec3 (-0.58764114,  1.1328999,  -0.1005789),\n"
  "        vec3 (-0.07284986, -0.00834942,  1.11872966));\n";

typedef struct _TransferFunction
{
  const char *source;
  const char *name;
} TransferFunction;

typedef struct _MatrixMultiplication
{
  const char *source;
  const char *name;
} MatrixMultiplication;

static const TransferFunction pq_eotf = {
  .source = pq_eotf_source,
  .name = "pq_eotf",
};

static const TransferFunction pq_inv_eotf = {
  .source = pq_inv_eotf_source,
  .name = "pq_inv_eotf",
};

static const TransferFunction srgb_eotf = {
  .source = srgb_eotf_source,
  .name = "srgb_eotf",
};

static const TransferFunction srgb_inv_eotf = {
  .source = srgb_inv_eotf_source,
  .name = "srgb_inv_eotf",
};

static const TransferFunction srgb_luminance_gain = {
  .source = srgb_luminance_gain_source,
  .name = "srgb_luminance_gain",
};

static const TransferFunction pq_luminance_clamp = {
  .source = pq_luminance_clamp_source,
  .name = "pq_luminance_clamp",
};

static const MatrixMultiplication bt709_to_bt2020 = {
  .source = bt709_to_bt2020_matrix_source,
  .name = "bt709_to_bt2020",
};

static const MatrixMultiplication bt2020_to_bt709 = {
  .source = bt2020_to_bt709_matrix_source,
  .name = "bt2020_to_bt709",
};

static void
append_shader_description (GString           *snippet_source,
                           ClutterColorState *color_state,
                           ClutterColorState *target_color_state)
{
  ClutterColorStatePrivate *priv =
    clutter_color_state_get_instance_private (color_state);
  ClutterColorStatePrivate *target_priv =
    clutter_color_state_get_instance_private (target_color_state);
  const char *colorspace =
    clutter_colorspace_to_string (priv->colorspace);
  const char *transfer_function =
    clutter_transfer_function_to_string (priv->transfer_function);
  const char *target_colorspace =
    clutter_colorspace_to_string (target_priv->colorspace);
  const char *target_transfer_function =
    clutter_transfer_function_to_string (target_priv->transfer_function);

  g_string_append_printf (snippet_source,
                          "  // %s (%s) to %s (%s)\n",
                          colorspace,
                          transfer_function,
                          target_colorspace,
                          target_transfer_function);
}

static const TransferFunction *
get_eotf (ClutterColorState *color_state)
{
  ClutterColorStatePrivate *priv =
    clutter_color_state_get_instance_private (color_state);

  switch (priv->transfer_function)
    {
    case CLUTTER_TRANSFER_FUNCTION_PQ:
      return &pq_eotf;
    case CLUTTER_TRANSFER_FUNCTION_SRGB:
    case CLUTTER_TRANSFER_FUNCTION_DEFAULT:
      return &srgb_eotf;
    case CLUTTER_TRANSFER_FUNCTION_LINEAR:
      return NULL;
    }

  g_warning ("Unhandled tranfer function %s",
             clutter_transfer_function_to_string (priv->transfer_function));
  return NULL;
}

static const TransferFunction *
get_inv_eotf (ClutterColorState *color_state)
{
  ClutterColorStatePrivate *priv =
    clutter_color_state_get_instance_private (color_state);

  switch (priv->transfer_function)
    {
    case CLUTTER_TRANSFER_FUNCTION_PQ:
      return &pq_inv_eotf;
    case CLUTTER_TRANSFER_FUNCTION_SRGB:
    case CLUTTER_TRANSFER_FUNCTION_DEFAULT:
      return &srgb_inv_eotf;
    case CLUTTER_TRANSFER_FUNCTION_LINEAR:
      return NULL;
    }
  g_warning ("Unhandled tranfer function %s",
             clutter_transfer_function_to_string (priv->transfer_function));
  return NULL;
}

static const TransferFunction *
get_denormalize_function (ClutterColorState *color_state,
                          ClutterColorState *target_color_state)
{
  ClutterColorStatePrivate *priv =
    clutter_color_state_get_instance_private (color_state);
  ClutterColorStatePrivate *target_priv =
    clutter_color_state_get_instance_private (target_color_state);

  switch (priv->transfer_function)
    {
    case CLUTTER_TRANSFER_FUNCTION_SRGB:
    case CLUTTER_TRANSFER_FUNCTION_DEFAULT:
      switch (target_priv->transfer_function)
        {
        case CLUTTER_TRANSFER_FUNCTION_PQ:
        case CLUTTER_TRANSFER_FUNCTION_LINEAR:
          return &srgb_luminance_gain;
        case CLUTTER_TRANSFER_FUNCTION_SRGB:
        case CLUTTER_TRANSFER_FUNCTION_DEFAULT:
          return NULL;
        }
      break;
    case CLUTTER_TRANSFER_FUNCTION_PQ:
      switch (target_priv->transfer_function)
        {
        case CLUTTER_TRANSFER_FUNCTION_PQ:
        case CLUTTER_TRANSFER_FUNCTION_LINEAR:
          return NULL;
        case CLUTTER_TRANSFER_FUNCTION_SRGB:
        case CLUTTER_TRANSFER_FUNCTION_DEFAULT:
          return &pq_luminance_clamp;
        }
      break;
    case CLUTTER_TRANSFER_FUNCTION_LINEAR:
      return NULL;
    }

  g_return_val_if_reached (NULL);
}

static void
get_transfer_functions (ClutterColorState       *color_state,
                        ClutterColorState       *target_color_state,
                        const TransferFunction **pre_transfer_function,
                        const TransferFunction **denormalize_function,
                        const TransferFunction **post_transfer_function)
{
  ClutterColorStatePrivate *priv =
    clutter_color_state_get_instance_private (color_state);
  ClutterColorStatePrivate *target_priv =
    clutter_color_state_get_instance_private (target_color_state);

  if (priv->colorspace == target_priv->colorspace &&
      priv->transfer_function == target_priv->transfer_function)
    return;

  if (priv->transfer_function != CLUTTER_TRANSFER_FUNCTION_LINEAR &&
      target_priv->transfer_function == CLUTTER_TRANSFER_FUNCTION_LINEAR)
    {
      *pre_transfer_function = get_eotf (color_state);
      *denormalize_function = get_denormalize_function (color_state,
                                                        target_color_state);
    }
  else if (priv->transfer_function == CLUTTER_TRANSFER_FUNCTION_LINEAR &&
           target_priv->transfer_function != CLUTTER_TRANSFER_FUNCTION_LINEAR)
    {
      *denormalize_function = get_denormalize_function (color_state,
                                                        target_color_state);
      *post_transfer_function = get_inv_eotf (target_color_state);
    }
  else if (priv->transfer_function != CLUTTER_TRANSFER_FUNCTION_LINEAR &&
           target_priv->transfer_function != CLUTTER_TRANSFER_FUNCTION_LINEAR)
    {
      *pre_transfer_function = get_eotf (color_state);
      *denormalize_function = get_denormalize_function (color_state,
                                                        target_color_state);
      *post_transfer_function = get_inv_eotf (target_color_state);
    }
}

static const MatrixMultiplication *
get_color_space_mapping_matrix (ClutterColorState *color_state,
                                ClutterColorState *target_color_state)
{
  ClutterColorStatePrivate *priv =
    clutter_color_state_get_instance_private (color_state);
  ClutterColorStatePrivate *target_priv =
    clutter_color_state_get_instance_private (target_color_state);

  switch (priv->colorspace)
    {
    case CLUTTER_COLORSPACE_SRGB:
    case CLUTTER_COLORSPACE_DEFAULT:
      switch (target_priv->colorspace)
        {
        case CLUTTER_COLORSPACE_BT2020:
          return &bt709_to_bt2020;
        case CLUTTER_COLORSPACE_SRGB:
        case CLUTTER_COLORSPACE_DEFAULT:
          return NULL;
        }
      break;
    case CLUTTER_COLORSPACE_BT2020:
      switch (target_priv->colorspace)
        {
        case CLUTTER_COLORSPACE_BT2020:
          return NULL;
        case CLUTTER_COLORSPACE_SRGB:
        case CLUTTER_COLORSPACE_DEFAULT:
          return &bt2020_to_bt709;
        }
      break;
    }

  g_assert_not_reached ();
}

static CoglSnippet *
clutter_color_state_get_transform_snippet (ClutterColorState *color_state,
                                           ClutterColorState *target_color_state)
{
  ClutterColorStatePrivate *priv;
  ClutterColorManager *color_manager;
  ClutterColorTransformKey transform_key;
  CoglSnippet *snippet;
  const MatrixMultiplication *color_space_mapping = NULL;
  const TransferFunction *pre_transfer_function = NULL;
  const TransferFunction *denormalize_function = NULL;
  const TransferFunction *post_transfer_function = NULL;
  g_autoptr (GString) globals_source = NULL;
  g_autoptr (GString) snippet_source = NULL;

  priv = clutter_color_state_get_instance_private (color_state);
  color_manager = clutter_context_get_color_manager (priv->context);

  clutter_color_transform_key_init (&transform_key,
                                    color_state,
                                    target_color_state);
  snippet = clutter_color_manager_lookup_snippet (color_manager,
                                                  &transform_key);
  if (snippet)
    return g_object_ref (snippet);

  color_space_mapping = get_color_space_mapping_matrix (color_state,
                                                        target_color_state);

  get_transfer_functions (color_state, target_color_state,
                          &pre_transfer_function,
                          &denormalize_function,
                          &post_transfer_function);

  globals_source = g_string_new (NULL);
  if (pre_transfer_function)
    g_string_append_printf (globals_source, "%s\n", pre_transfer_function->source);
  if (denormalize_function)
    g_string_append_printf (globals_source, "%s\n", denormalize_function->source);
  if (post_transfer_function)
    g_string_append_printf (globals_source, "%s\n", post_transfer_function->source);
  if (color_space_mapping)
    g_string_append_printf (globals_source, "%s\n", color_space_mapping->source);

  /*
   * The following statements generate a shader snippet that transforms colors
   * from one color state (transfer function, color space, color encoding) into
   * another. When the target color state is optically encoded, we always draw
   * into an intermediate 64 bit half float typed pixel.
   *
   * The value stored in this pixel is roughly the luminance expected by the
   * target color state's transfer function.
   *
   * For sRGB that means luminance relative the reference display as defined by
   * the sRGB specification, i.e. a value typically between 0.0 and 1.0. For PQ
   * this means absolute luminance in cd/m² (nits).
   *
   * The snippet contains a pipeline that roughly looks like this:
   *
   *     color = pre_transfer_function (color)
   *     color *= luminance_gain
   *     color = color_space_mapping_matrix * color
   *     color = post_transfer_function (color)
   *
   */

  snippet_source = g_string_new (NULL);
  append_shader_description (snippet_source, color_state, target_color_state);

  g_string_append (snippet_source,
                   "  vec3 color_state_color = cogl_color_out.rgb;\n");

  if (pre_transfer_function)
    {
      g_string_append_printf (snippet_source,
                              "  color_state_color = %s (color_state_color);\n",
                              pre_transfer_function->name);
    }

  if (denormalize_function)
    {
      g_string_append_printf (snippet_source,
                              "  color_state_color = %s (color_state_color);\n",
                              denormalize_function->name);
    }
  if (color_space_mapping)
    {
      g_string_append_printf (snippet_source,
                              "  color_state_color = %s * color_state_color;\n",
                              color_space_mapping->name);
    }

  if (post_transfer_function)
    {
      g_string_append_printf (snippet_source,
                              "  // Post transfer function\n"
                              "  color_state_color = %s (color_state_color);\n",
                              post_transfer_function->name);
    }

  g_string_append (snippet_source,
                   "  cogl_color_out = vec4 (color_state_color, cogl_color_out.a);\n");

  snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                              globals_source->str,
                              snippet_source->str);
  cogl_snippet_set_capability (snippet,
                               CLUTTER_PIPELINE_CAPABILITY,
                               CLUTTER_PIPELINE_CAPABILITY_COLOR_STATE);
  clutter_color_manager_add_snippet (color_manager,
                                     &transform_key,
                                     g_object_ref (snippet));
  return snippet;
}

void
clutter_color_state_add_pipeline_transform (ClutterColorState *color_state,
                                            ClutterColorState *target_color_state,
                                            CoglPipeline      *pipeline)
{
  g_autoptr (CoglSnippet) snippet = NULL;

  g_return_if_fail (CLUTTER_IS_COLOR_STATE (color_state));
  g_return_if_fail (CLUTTER_IS_COLOR_STATE (target_color_state));

  snippet = clutter_color_state_get_transform_snippet (color_state,
                                                       target_color_state);
  cogl_pipeline_add_snippet (pipeline, snippet);
}

gboolean
clutter_color_state_equals (ClutterColorState *color_state,
                            ClutterColorState *other_color_state)
{
  ClutterColorStatePrivate *priv;
  ClutterColorStatePrivate *other_priv;

  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE (color_state), FALSE);
  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE (other_color_state), FALSE);

  if (color_state == other_color_state)
    return TRUE;

  priv = clutter_color_state_get_instance_private (color_state);
  other_priv = clutter_color_state_get_instance_private (other_color_state);

  return (priv->colorspace == other_priv->colorspace &&
          priv->transfer_function == other_priv->transfer_function);
}

static char *
enum_to_string (GType        type,
                unsigned int enum_value)
{
  GEnumClass *enum_class;
  GEnumValue *value;
  char *retval = NULL;

  enum_class = g_type_class_ref (type);

  value = g_enum_get_value (enum_class, enum_value);
  if (value)
    retval = g_strdup (value->value_nick);

  g_type_class_unref (enum_class);

  return retval;
}

char *
clutter_color_state_to_string (ClutterColorState *color_state)
{
  ClutterColorStatePrivate *priv;
  g_autofree char *colorspace_name = NULL;
  g_autofree char *transfer_function_name = NULL;

  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE (color_state), FALSE);

  priv = clutter_color_state_get_instance_private (color_state);

  colorspace_name = enum_to_string (CLUTTER_TYPE_COLORSPACE, priv->colorspace);
  transfer_function_name = enum_to_string (CLUTTER_TYPE_TRANSFER_FUNCTION,
                                           priv->transfer_function);

  return g_strdup_printf ("ClutterColorState %d "
                          "(colorspace: %s, transfer function: %s)",
                          priv->id,
                          colorspace_name,
                          transfer_function_name);
}

ClutterEncodingRequiredFormat
clutter_color_state_required_format (ClutterColorState *color_state)
{
  ClutterColorStatePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE (color_state), FALSE);

  priv = clutter_color_state_get_instance_private (color_state);

  switch (priv->transfer_function)
    {
    case CLUTTER_TRANSFER_FUNCTION_LINEAR:
      return CLUTTER_ENCODING_REQUIRED_FORMAT_FP16;
    case CLUTTER_TRANSFER_FUNCTION_PQ:
      return CLUTTER_ENCODING_REQUIRED_FORMAT_UINT10;
    case CLUTTER_TRANSFER_FUNCTION_SRGB:
    case CLUTTER_TRANSFER_FUNCTION_DEFAULT:
      return CLUTTER_ENCODING_REQUIRED_FORMAT_UINT8;
    }

  g_assert_not_reached ();
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
 * Currently sRGB content is blended with sRGB and not with linear transfer
 * characteristics.
 *
 * If @force is TRUE then linear transfer characteristics are used always.
 *
 * Returns: (transfer full): the #ClutterColorState suitable for blending
 */
ClutterColorState *
clutter_color_state_get_blending (ClutterColorState *color_state,
                                  gboolean           force)
{
  ClutterColorStatePrivate *priv;
  ClutterTransferFunction blending_tf;

  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE (color_state), FALSE);

  priv = clutter_color_state_get_instance_private (color_state);

  switch (priv->transfer_function)
    {
    case CLUTTER_TRANSFER_FUNCTION_PQ:
    case CLUTTER_TRANSFER_FUNCTION_LINEAR:
      blending_tf = CLUTTER_TRANSFER_FUNCTION_LINEAR;
      break;
    /* effectively this means we will blend sRGB content in sRGB, not linear */
    case CLUTTER_TRANSFER_FUNCTION_SRGB:
    case CLUTTER_TRANSFER_FUNCTION_DEFAULT:
      blending_tf = priv->transfer_function;
      break;
    default:
      g_assert_not_reached ();
    }

  if (force)
    blending_tf = CLUTTER_TRANSFER_FUNCTION_LINEAR;

  if (blending_tf == priv->transfer_function)
    return g_object_ref (color_state);

  return clutter_color_state_new (priv->context,
                                  priv->colorspace,
                                  blending_tf);
}
