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

#define UNIFORM_NAME_LUMINANCE_MAPPING "luminance_mapping"
#define UNIFORM_NAME_COLOR_SPACE_MAPPING "color_space_mapping"

enum
{
  PROP_0,

  PROP_CONTEXT,
  PROP_COLORSPACE,
  PROP_PRIMARIES,
  PROP_TRANSFER_FUNCTION,
  PROP_MIN_LUMINANCE,
  PROP_MAX_LUMINANCE,
  PROP_REF_LUMINANCE,

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
  ClutterPrimaries *primaries;
  ClutterTransferFunction transfer_function;
  float min_lum;
  float max_lum;
  float ref_lum;
} ClutterColorStatePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (ClutterColorState,
                            clutter_color_state,
                            G_TYPE_OBJECT)

guint
clutter_color_transform_key_hash (gconstpointer data)
{
  const ClutterColorTransformKey *key = data;

  return (key->source.transfer_function ^
          key->target.transfer_function);
}

gboolean
clutter_color_transform_key_equal (gconstpointer data1,
                                   gconstpointer data2)
{
  const ClutterColorTransformKey *key1 = data1;
  const ClutterColorTransformKey *key2 = data2;

  return (key1->source.transfer_function == key2->source.transfer_function &&
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

  key->source.transfer_function = priv->transfer_function;
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

void
clutter_transfer_function_get_default_luminances (ClutterTransferFunction  transfer_function,
                                                  float                   *min_lum_out,
                                                  float                   *max_lum_out,
                                                  float                   *ref_lum_out)
{
  float min_lum = -1.0f, max_lum = -1.0f, ref_lum = -1.0f;

  switch (transfer_function)
    {
    case CLUTTER_TRANSFER_FUNCTION_DEFAULT:
    case CLUTTER_TRANSFER_FUNCTION_SRGB:
    case CLUTTER_TRANSFER_FUNCTION_LINEAR:
      min_lum = 0.2f;
      max_lum = 80.0f;
      ref_lum = 80.0f;
      break;
    case CLUTTER_TRANSFER_FUNCTION_PQ:
      min_lum = 0.005f;
      max_lum = 10000.0f;
      ref_lum = 203.0f;
      break;
    }

  if (min_lum_out)
    *min_lum_out = min_lum;
  if (max_lum_out)
    *max_lum_out = max_lum;
  if (ref_lum_out)
    *ref_lum_out = ref_lum;
}

void
clutter_color_state_get_luminances (ClutterColorState *color_state,
                                    float             *min_lum_out,
                                    float             *max_lum_out,
                                    float             *ref_lum_out)
{
  ClutterColorStatePrivate *priv;
  float min_lum, max_lum, ref_lum;

  g_return_if_fail (CLUTTER_IS_COLOR_STATE (color_state));

  priv = clutter_color_state_get_instance_private (color_state);

  clutter_transfer_function_get_default_luminances (priv->transfer_function,
                                                    &min_lum,
                                                    &max_lum,
                                                    &ref_lum);

  if (priv->min_lum >= 0.0f)
    min_lum = priv->min_lum;
  if (priv->max_lum >= 0.0f)
    max_lum = priv->max_lum;
  if (priv->ref_lum >= 0.0f)
    ref_lum = priv->ref_lum;

  if (min_lum_out)
    *min_lum_out = min_lum;
  if (max_lum_out)
    *max_lum_out = max_lum;
  if (ref_lum_out)
    *ref_lum_out = ref_lum;
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
clutter_color_state_finalize (GObject *object)
{
  ClutterColorState *color_state = CLUTTER_COLOR_STATE (object);
  ClutterColorStatePrivate *priv =
    clutter_color_state_get_instance_private (color_state);

  g_clear_pointer (&priv->primaries, g_free);

  G_OBJECT_CLASS (clutter_color_state_parent_class)->finalize (object);
}

static void
clutter_color_state_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  ClutterColorState *color_state = CLUTTER_COLOR_STATE (object);
  ClutterColorStatePrivate *priv;
  ClutterPrimaries *primaries;

  priv = clutter_color_state_get_instance_private (color_state);

  switch (prop_id)
    {
    case PROP_CONTEXT:
      priv->context = g_value_get_object (value);
      break;

    case PROP_COLORSPACE:
      priv->colorspace = g_value_get_enum (value);
      break;

    case PROP_PRIMARIES:
      primaries = g_value_get_pointer (value);
      g_clear_pointer (&priv->primaries, g_free);
      priv->primaries = g_memdup2 (primaries, sizeof (ClutterPrimaries));
      break;

    case PROP_TRANSFER_FUNCTION:
      priv->transfer_function = g_value_get_enum (value);
      break;

    case PROP_MIN_LUMINANCE:
      priv->min_lum = g_value_get_float (value);
      break;

    case PROP_MAX_LUMINANCE:
      priv->max_lum = g_value_get_float (value);
      break;

    case PROP_REF_LUMINANCE:
      priv->ref_lum = g_value_get_float (value);
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
      g_value_set_enum (value, priv->colorspace);
      break;

    case PROP_TRANSFER_FUNCTION:
      g_value_set_enum (value, priv->transfer_function);
      break;

    case PROP_MIN_LUMINANCE:
      g_value_set_float (value, priv->min_lum);
      break;

    case PROP_MAX_LUMINANCE:
      g_value_set_float (value, priv->max_lum);
      break;

    case PROP_REF_LUMINANCE:
      g_value_set_float (value, priv->ref_lum);
      break;

    case PROP_PRIMARIES:
      g_value_set_pointer (value, priv->primaries);
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
  gobject_class->finalize = clutter_color_state_finalize;
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
   * ClutterColorState:primaries:
   *
   * Explicit set of primaries defined as chromaticity coordinates
   */
  obj_props[PROP_PRIMARIES] =
    g_param_spec_pointer ("primaries", NULL, NULL,
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

  /**
   * ClutterColorState:min-luminance:
   *
   * Minimum luminance.
   */
  obj_props[PROP_MIN_LUMINANCE] =
    g_param_spec_float ("min-luminance", NULL, NULL,
                        -1.0f, 10000.0f, 0.0f,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_CONSTRUCT_ONLY);

  /**
   * ClutterColorState:max-luminance:
   *
   * Maximum luminance.
   */
  obj_props[PROP_MAX_LUMINANCE] =
    g_param_spec_float ("max-luminance", NULL, NULL,
                        -1.0f, 10000.0f, 0.0f,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_CONSTRUCT_ONLY);

  /**
   * ClutterColorState:ref-luminance:
   *
   * Reference luminance.
   */
  obj_props[PROP_REF_LUMINANCE] =
    g_param_spec_float ("ref-luminance", NULL, NULL,
                        -1.0f, 10000.0f, 0.0f,
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
  return clutter_color_state_new_full (context,
                                       colorspace, transfer_function, NULL,
                                       -1.0f, -1.0f, -1.0f);
}

/**
 * clutter_color_state_new_full:
 *
 * Create a new ClutterColorState object with all possible parameters. Some
 * arguments might not be valid to set with other arguments.
 *
 * Return value: A new ClutterColorState object.
 **/
ClutterColorState *
clutter_color_state_new_full (ClutterContext          *context,
                              ClutterColorspace        colorspace,
                              ClutterTransferFunction  transfer_function,
                              ClutterPrimaries        *primaries,
                              float                    min_lum,
                              float                    max_lum,
                              float                    ref_lum)
{
  return g_object_new (CLUTTER_TYPE_COLOR_STATE,
                       "context", context,
                       "colorspace", colorspace,
                       "transfer-function", transfer_function,
                       "primaries", primaries,
                       "min-luminance", min_lum,
                       "max-luminance", max_lum,
                       "ref-luminance", ref_lum,
                       NULL);
}

static const char pq_eotf_source[] =
  "// pq_eotf:\n"
  "// @color: Normalized ([0,1]) electrical signal value\n"
  "// Returns: tristimulus values ([0,1])\n"
  "vec3 pq_eotf (vec3 color)\n"
  "{\n"
  "  const float c1 = 0.8359375;\n"
  "  const float c2 = 18.8515625;\n"
  "  const float c3 = 18.6875;\n"
  "\n"
  "  const float oo_m1 = 1.0 / 0.1593017578125;\n"
  "  const float oo_m2 = 1.0 / 78.84375;\n"
  "\n"
  "  vec3 num = max (pow (color, vec3 (oo_m2)) - c1, vec3 (0.0));\n"
  "  vec3 den = c2 - c3 * pow (color, vec3 (oo_m2));\n"
  "\n"
  "  return pow (num / den, vec3 (oo_m1));\n"
  "}\n"
  "\n"
  "vec4 pq_eotf (vec4 color)\n"
  "{\n"
  "  return vec4 (pq_eotf (color.rgb), color.a);\n"
  "}\n";

static const char pq_inv_eotf_source[] =
  "// pq_inv_eotf:\n"
  "// @color: Normalized tristimulus values ([0,1])"
  "// Returns: Normalized ([0,1]) electrical signal value\n"
  "vec3 pq_inv_eotf (vec3 color)\n"
  "{\n"
  "  float m1 = 0.1593017578125;\n"
  "  float m2 = 78.84375;\n"
  "  float c1 = 0.8359375;\n"
  "  float c2 = 18.8515625;\n"
  "  float c3 = 18.6875;\n"
  "  vec3 color_pow_m1 = pow (color, vec3 (m1));\n"
  "  vec3 num = vec3 (c1) + c2 * color_pow_m1;\n"
  "  vec3 denum = vec3 (1.0) + c3 * color_pow_m1;\n"
  "  return pow (num / denum, vec3 (m2));\n"
  "}\n"
  "\n"
  "vec4 pq_inv_eotf (vec4 color)\n"
  "{\n"
  "  return vec4 (pq_inv_eotf (color.rgb), color.a);\n"
  "}\n";

static const char srgb_eotf_source[] =
  "// srgb_eotf:\n"
  "// @color: Normalized ([0,1]) electrical signal value.\n"
  "// Returns: Normalized tristimulus values ([0,1])\n"
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
  "// @color: Normalized ([0,1]) tristimulus values\n"
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

typedef struct _TransferFunction
{
  const char *source;
  const char *name;
} TransferFunction;

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

static void
append_shader_description (GString           *snippet_source,
                           ClutterColorState *color_state,
                           ClutterColorState *target_color_state)
{
  ClutterColorStatePrivate *priv =
    clutter_color_state_get_instance_private (color_state);
  ClutterColorStatePrivate *target_priv =
    clutter_color_state_get_instance_private (target_color_state);
  const char *transfer_function =
    clutter_transfer_function_to_string (priv->transfer_function);
  const char *target_transfer_function =
    clutter_transfer_function_to_string (target_priv->transfer_function);

  g_string_append_printf (snippet_source,
                          "  // %s to %s\n",
                          transfer_function,
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

static void
get_transfer_functions (ClutterColorState       *color_state,
                        ClutterColorState       *target_color_state,
                        const TransferFunction **pre_transfer_function,
                        const TransferFunction **post_transfer_function)
{
  if (clutter_color_state_equals (color_state, target_color_state))
    return;

  *pre_transfer_function = get_eotf (color_state);
  *post_transfer_function = get_inv_eotf (target_color_state);
}

/* Primaries and white point retrieved from:
 * https://www.color.org */
static const ClutterPrimaries srgb_primaries = {
  .r_x = 0.64f, .r_y = 0.33f,
  .g_x = 0.30f, .g_y = 0.60f,
  .b_x = 0.15f, .b_y = 0.06f,
  .w_x = 0.3127f, .w_y = 0.3290f,
};

static const ClutterPrimaries bt2020_primaries = {
  .r_x = 0.708f, .r_y = 0.292f,
  .g_x = 0.170f, .g_y = 0.797f,
  .b_x = 0.131f, .b_y = 0.046f,
  .w_x = 0.3127f, .w_y = 0.3290f,
};

const ClutterPrimaries *
clutter_color_state_get_primaries (ClutterColorState *color_state)
{
  ClutterColorStatePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE (color_state),
                        NULL);

  priv = clutter_color_state_get_instance_private (color_state);

  if (priv->primaries)
    return priv->primaries;

  switch (priv->colorspace)
    {
    case CLUTTER_COLORSPACE_DEFAULT:
    case CLUTTER_COLORSPACE_SRGB:
      return &srgb_primaries;
    case CLUTTER_COLORSPACE_BT2020:
      return &bt2020_primaries;
    }

  g_warning ("Unhandled colorspace %s",
             clutter_colorspace_to_string (priv->colorspace));

  return &srgb_primaries;
}

static gboolean
chromaticity_equal (float x1,
                    float y1,
                    float x2,
                    float y2)

{
  return G_APPROX_VALUE (x1, x2, 0.000001f) &&
         G_APPROX_VALUE (y1, y2, 0.000001f);
}

static gboolean
primaries_white_point_equal (ClutterColorState *color_state,
                             ClutterColorState *other_color_state)
{
  const ClutterPrimaries *primaries;
  const ClutterPrimaries *other_primaries;

  primaries = clutter_color_state_get_primaries (color_state);
  other_primaries = clutter_color_state_get_primaries (other_color_state);

  return chromaticity_equal (primaries->w_x, primaries->w_y,
                             other_primaries->w_x, other_primaries->w_y);
}

static void
get_white_chromaticity_in_XYZ (float            x,
                               float            y,
                               graphene_vec3_t *chromaticity_XYZ)
{
  if (y == 0.0f)
    {
      /* Avoid a division by 0 */
      y = FLT_EPSILON;
      g_warning ("White point y coordinate is 0, something is probably wrong");
    }

  graphene_vec3_init (chromaticity_XYZ,
                      x / y,
                      1,
                      (1 - x - y) / y);
}

/*
 * Get the matrix rgb_to_xyz that makes:
 *
 *   color_XYZ = rgb_to_xyz * color_RGB
 *
 * Steps:
 *
 *   (1) white_point_XYZ = rgb_to_xyz * white_point_RGB
 *
 * Breaking down rgb_to_xyz: rgb_to_xyz = primaries_mat * coefficients_mat
 *
 *   (2) white_point_XYZ = primaries_mat * coefficients_mat * white_point_RGB
 *
 * white_point_RGB is (1, 1, 1) and coefficients_mat is a diagonal matrix:
 * coefficients_vec = coefficients_mat * white_point_RGB
 *
 *   (3) white_point_XYZ = primaries_mat * coefficients_vec
 *
 *   (4) primaries_mat^-1 * white_point_XYZ = coefficients_vec
 *
 * When coefficients_vec is calculated, coefficients_mat can be composed to
 * finally solve:
 *
 *  (5) rgb_to_xyz = primaries_mat * coefficients_mat
 *
 * Notes:
 *
 *   white_point_XYZ: xy white point coordinates transformed to XYZ space
 *                    using the maximum luminance: Y = 1
 *
 *   primaries_mat: matrix made from xy chromaticities transformed to xyz
 *                  considering x + y + z = 1
 *
 *   xyz_to_rgb = rgb_to_xyz^-1
 *
 * Reference:
 *   https://www.ryanjuckett.com/rgb-color-space-conversion/
 */
static gboolean
get_color_space_trans_matrices (ClutterColorState *color_state,
                                graphene_matrix_t *rgb_to_xyz,
                                graphene_matrix_t *xyz_to_rgb)
{
  const ClutterPrimaries *primaries =
    clutter_color_state_get_primaries (color_state);
  graphene_matrix_t coefficients_mat;
  graphene_matrix_t inv_primaries_mat;
  graphene_matrix_t primaries_mat;
  graphene_vec3_t white_point_XYZ;
  graphene_vec3_t coefficients;

  graphene_matrix_init_from_float (
    &primaries_mat,
    (float [16]) {
    primaries->r_x, primaries->r_y, 1 - primaries->r_x - primaries->r_y, 0,
    primaries->g_x, primaries->g_y, 1 - primaries->g_x - primaries->g_y, 0,
    primaries->b_x, primaries->b_y, 1 - primaries->b_x - primaries->b_y, 0,
    0, 0, 0, 1,
  });

  if (!graphene_matrix_inverse (&primaries_mat, &inv_primaries_mat))
    return FALSE;

  get_white_chromaticity_in_XYZ (primaries->w_x, primaries->w_y, &white_point_XYZ);

  graphene_matrix_transform_vec3 (&inv_primaries_mat, &white_point_XYZ, &coefficients);

  graphene_matrix_init_from_float (
    &coefficients_mat,
    (float [16]) {
    graphene_vec3_get_x (&coefficients), 0, 0, 0,
    0, graphene_vec3_get_y (&coefficients), 0, 0,
    0, 0, graphene_vec3_get_z (&coefficients), 0,
    0, 0, 0, 1,
  });

  graphene_matrix_multiply (&coefficients_mat, &primaries_mat, rgb_to_xyz);

  if (!graphene_matrix_inverse (rgb_to_xyz, xyz_to_rgb))
    return FALSE;

  return TRUE;
}

/*
 * Get the matrix that converts XYZ chromaticity relative to src_white_point
 * to XYZ chromaticity relative to dst_white_point:
 *
 *   dst_XYZ = chromatic_adaptation * src_XYZ
 *
 * Steps:
 *
 *   chromatic_adaptation = bradford_mat^-1 * coefficients_mat * bradford_mat
 *
 *   coefficients_mat = diag (coefficients)
 *
 *   coefficients = dst_white_LMS / src_white_LMS
 *
 *   dst_white_LMS = bradford_mat * dst_white_XYZ
 *   src_white_LMS = bradford_mat * src_white_XYZ
 *
 * Notes:
 *
 *   *_white_XYZ: xy white point coordinates transformed to XYZ space
 *                using the maximum luminance: Y = 1
 *
 * Bradford matrix and reference:
 *   http://www.brucelindbloom.com/index.html?Eqn_ChromAdapt.html
 */
static void
get_chromatic_adaptation (ClutterColorState *color_state,
                          ClutterColorState *target_color_state,
                          graphene_matrix_t *chromatic_adaptation)
{
  const ClutterPrimaries *source_primaries =
    clutter_color_state_get_primaries (color_state);
  const ClutterPrimaries *target_primaries =
    clutter_color_state_get_primaries (target_color_state);
  graphene_matrix_t coefficients_mat;
  graphene_matrix_t bradford_mat, inv_bradford_mat;
  graphene_vec3_t src_white_point_XYZ, dst_white_point_XYZ;
  graphene_vec3_t src_white_point_LMS, dst_white_point_LMS;
  graphene_vec3_t coefficients;

  graphene_matrix_init_from_float (
    &bradford_mat,
    (float [16]) {
    0.8951f, -0.7502f, 0.0389f, 0,
    0.2664f, 1.7135f, -0.0685f, 0,
    -0.1614f, 0.0367f, 1.0296f, 0,
    0, 0, 0, 1,
  });

  graphene_matrix_init_from_float (
    &inv_bradford_mat,
    (float [16]) {
    0.9869929f, 0.4323053f, -0.0085287f, 0,
    -0.1470543f, 0.5183603f, 0.0400428f, 0,
    0.1599627f, 0.0492912f, 0.9684867f, 0,
    0, 0, 0, 1,
  });

  get_white_chromaticity_in_XYZ (source_primaries->w_x, source_primaries->w_y,
                                 &src_white_point_XYZ);
  get_white_chromaticity_in_XYZ (target_primaries->w_x, target_primaries->w_y,
                                 &dst_white_point_XYZ);

  graphene_matrix_transform_vec3 (&bradford_mat, &src_white_point_XYZ,
                                  &src_white_point_LMS);
  graphene_matrix_transform_vec3 (&bradford_mat, &dst_white_point_XYZ,
                                  &dst_white_point_LMS);

  graphene_vec3_divide (&dst_white_point_LMS, &src_white_point_LMS,
                        &coefficients);

  graphene_matrix_init_from_float (
    &coefficients_mat,
    (float [16]) {
    graphene_vec3_get_x (&coefficients), 0, 0, 0,
    0, graphene_vec3_get_y (&coefficients), 0, 0,
    0, 0, graphene_vec3_get_z (&coefficients), 0,
    0, 0, 0, 1,
  });

  graphene_matrix_multiply (&bradford_mat, &coefficients_mat,
                            chromatic_adaptation);
  graphene_matrix_multiply (chromatic_adaptation, &inv_bradford_mat,
                            chromatic_adaptation);
}

static void
get_color_space_mapping_matrix (ClutterColorState *color_state,
                                ClutterColorState *target_color_state,
                                float              out_color_space_mapping[9])
{
  graphene_matrix_t matrix;
  graphene_matrix_t src_rgb_to_xyz, src_xyz_to_rgb;
  graphene_matrix_t target_rgb_to_xyz, target_xyz_to_rgb;
  graphene_matrix_t chromatic_adaptation;

  if (!get_color_space_trans_matrices (color_state,
                                       &src_rgb_to_xyz,
                                       &src_xyz_to_rgb) ||
      !get_color_space_trans_matrices (target_color_state,
                                       &target_rgb_to_xyz,
                                       &target_xyz_to_rgb))
    {
      graphene_matrix_init_identity (&matrix);
    }
  else
    {
      if (!primaries_white_point_equal (color_state, target_color_state))
        {
          get_chromatic_adaptation (color_state, target_color_state,
                                    &chromatic_adaptation);
          graphene_matrix_multiply (&src_rgb_to_xyz, &chromatic_adaptation,
                                    &matrix);
          graphene_matrix_multiply (&matrix, &target_xyz_to_rgb,
                                    &matrix);
        }
      else
        {
          graphene_matrix_multiply (&src_rgb_to_xyz, &target_xyz_to_rgb, &matrix);
        }
    }

  out_color_space_mapping[0] = graphene_matrix_get_value (&matrix, 0, 0);
  out_color_space_mapping[1] = graphene_matrix_get_value (&matrix, 0, 1);
  out_color_space_mapping[2] = graphene_matrix_get_value (&matrix, 0, 2);
  out_color_space_mapping[3] = graphene_matrix_get_value (&matrix, 1, 0);
  out_color_space_mapping[4] = graphene_matrix_get_value (&matrix, 1, 1);
  out_color_space_mapping[5] = graphene_matrix_get_value (&matrix, 1, 2);
  out_color_space_mapping[6] = graphene_matrix_get_value (&matrix, 2, 0);
  out_color_space_mapping[7] = graphene_matrix_get_value (&matrix, 2, 1);
  out_color_space_mapping[8] = graphene_matrix_get_value (&matrix, 2, 2);
}

static CoglSnippet *
clutter_color_state_get_transform_snippet (ClutterColorState *color_state,
                                           ClutterColorState *target_color_state)
{
  ClutterColorStatePrivate *priv;
  ClutterColorManager *color_manager;
  ClutterColorTransformKey transform_key;
  CoglSnippet *snippet;
  const TransferFunction *pre_transfer_function = NULL;
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

  get_transfer_functions (color_state, target_color_state,
                          &pre_transfer_function,
                          &post_transfer_function);

  globals_source = g_string_new (NULL);
  if (pre_transfer_function)
    g_string_append_printf (globals_source, "%s\n", pre_transfer_function->source);
  if (post_transfer_function)
    g_string_append_printf (globals_source, "%s\n", post_transfer_function->source);

  g_string_append (globals_source,
                   "uniform float " UNIFORM_NAME_LUMINANCE_MAPPING ";\n");
  g_string_append (globals_source,
                   "uniform mat3 " UNIFORM_NAME_COLOR_SPACE_MAPPING ";\n");

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

  g_string_append (snippet_source,
                   "  color_state_color = "
                   UNIFORM_NAME_LUMINANCE_MAPPING " * color_state_color;\n");

  g_string_append (snippet_source,
                   "  color_state_color = "
                   UNIFORM_NAME_COLOR_SPACE_MAPPING " * color_state_color;\n");

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

static float
get_luminance_mapping (ClutterColorState *color_state,
                       ClutterColorState *target_color_state)
{
  float min_lum, max_lum, ref_lum;
  float target_min_lum, target_max_lum, target_ref_lum;

  clutter_color_state_get_luminances (color_state,
                                      &min_lum, &max_lum, &ref_lum);

  clutter_color_state_get_luminances (target_color_state,
                                      &target_min_lum,
                                      &target_max_lum,
                                      &target_ref_lum);

  /* this is a very basic, non-contrast preserving way of matching the reference
   * luminance level */
  return (target_ref_lum / ref_lum) * (max_lum / target_max_lum);
}

void
clutter_color_state_update_uniforms (ClutterColorState *color_state,
                                     ClutterColorState *target_color_state,
                                     CoglPipeline      *pipeline)
{
  float luminance_mapping;
  float color_space_mapping[9] = { 0 };
  int uniform_location_luminance_mapping;
  int uniform_location_color_space_mapping;

  luminance_mapping = get_luminance_mapping (color_state, target_color_state);

  uniform_location_luminance_mapping =
    cogl_pipeline_get_uniform_location (pipeline,
                                        UNIFORM_NAME_LUMINANCE_MAPPING);

  cogl_pipeline_set_uniform_1f (pipeline,
                                uniform_location_luminance_mapping,
                                luminance_mapping);

  get_color_space_mapping_matrix (color_state, target_color_state,
                                  color_space_mapping);

  uniform_location_color_space_mapping =
    cogl_pipeline_get_uniform_location (pipeline,
                                        UNIFORM_NAME_COLOR_SPACE_MAPPING);

  cogl_pipeline_set_uniform_matrix (pipeline,
                                    uniform_location_color_space_mapping,
                                    3,
                                    1,
                                    FALSE,
                                    color_space_mapping);
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

  clutter_color_state_update_uniforms (color_state,
                                       target_color_state,
                                       pipeline);
}

static gboolean
luminance_value_approx_equal (float lum,
                              float other_lum,
                              float epsilon)
{
  if (lum == 0.0f || other_lum == 0.0f)
    return lum == other_lum;

  return G_APPROX_VALUE (lum / other_lum, 1.0f, epsilon);
}

static gboolean
luminances_equal (ClutterColorState *color_state,
                  ClutterColorState *other_color_state)
{
  float min_lum, max_lum, ref_lum;
  float other_min_lum, other_max_lum, other_ref_lum;

  clutter_color_state_get_luminances (color_state,
                                      &min_lum, &max_lum, &ref_lum);

  clutter_color_state_get_luminances (other_color_state,
                                      &other_min_lum,
                                      &other_max_lum,
                                      &other_ref_lum);

  return luminance_value_approx_equal (min_lum, other_min_lum, 0.1f) &&
         luminance_value_approx_equal (max_lum, other_max_lum, 0.1f) &&
         luminance_value_approx_equal (ref_lum, other_ref_lum, 0.1f);
}

static gboolean
primaries_equal (ClutterColorState *color_state,
                 ClutterColorState *other_color_state)
{
  ClutterColorStatePrivate *priv;
  ClutterColorStatePrivate *other_priv;
  const ClutterPrimaries *primaries;
  const ClutterPrimaries *other_primaries;

  priv = clutter_color_state_get_instance_private (color_state);
  other_priv = clutter_color_state_get_instance_private (other_color_state);

  if (priv->colorspace != CLUTTER_COLORSPACE_DEFAULT &&
      other_priv->colorspace != CLUTTER_COLORSPACE_DEFAULT)
    return priv->colorspace == other_priv->colorspace;

  primaries = clutter_color_state_get_primaries (color_state);
  other_primaries = clutter_color_state_get_primaries (other_color_state);

  return chromaticity_equal (primaries->r_x, primaries->r_y,
                             other_primaries->r_x, other_primaries->r_y) &&
         chromaticity_equal (primaries->g_x, primaries->g_y,
                             other_primaries->g_x, other_primaries->g_y) &&
         chromaticity_equal (primaries->b_x, primaries->b_y,
                             other_primaries->b_x, other_primaries->b_y) &&
         chromaticity_equal (primaries->w_x, primaries->w_y,
                             other_primaries->w_x, other_primaries->w_y);
}

gboolean
clutter_color_state_equals (ClutterColorState *color_state,
                            ClutterColorState *other_color_state)
{
  ClutterColorStatePrivate *priv;
  ClutterColorStatePrivate *other_priv;

  if (color_state == other_color_state)
    return TRUE;

  if (color_state == NULL || other_color_state == NULL)
    return FALSE;

  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE (color_state), FALSE);
  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE (other_color_state), FALSE);

  priv = clutter_color_state_get_instance_private (color_state);
  other_priv = clutter_color_state_get_instance_private (other_color_state);

  return priv->transfer_function == other_priv->transfer_function &&
         primaries_equal (color_state, other_color_state) &&
         luminances_equal (color_state, other_color_state);
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

static char *
primaries_to_string (ClutterColorState *color_state)
{
  ClutterColorStatePrivate *priv;
  const ClutterPrimaries *primaries;

  priv = clutter_color_state_get_instance_private (color_state);

  if (priv->colorspace != CLUTTER_COLORSPACE_DEFAULT)
    return enum_to_string (CLUTTER_TYPE_COLORSPACE, priv->colorspace);

  primaries = clutter_color_state_get_primaries (color_state);
  return g_strdup_printf ("[R: %f, %f G: %f, %f B: %f, %f W: %f, %f]",
                          primaries->r_x, primaries->r_y,
                          primaries->g_x, primaries->g_y,
                          primaries->b_x, primaries->b_y,
                          primaries->w_x, primaries->w_y);
}

char *
clutter_color_state_to_string (ClutterColorState *color_state)
{
  ClutterColorStatePrivate *priv;
  g_autofree char *primaries_name = NULL;
  g_autofree char *transfer_function_name = NULL;
  float min_lum, max_lum, ref_lum;

  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE (color_state), FALSE);

  priv = clutter_color_state_get_instance_private (color_state);

  primaries_name = primaries_to_string (color_state);

  transfer_function_name = enum_to_string (CLUTTER_TYPE_TRANSFER_FUNCTION,
                                           priv->transfer_function);

  clutter_color_state_get_luminances (color_state, &min_lum, &max_lum, &ref_lum);

  return g_strdup_printf ("ClutterColorState %d "
                          "(primaries: %s, transfer function: %s, "
                          "min lum: %f, max lum: %f, ref lum: %f)",
                          priv->id,
                          primaries_name,
                          transfer_function_name,
                          min_lum,
                          max_lum,
                          ref_lum);
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

  return clutter_color_state_new_full (priv->context,
                                       priv->colorspace,
                                       blending_tf,
                                       priv->primaries,
                                       priv->min_lum,
                                       priv->max_lum,
                                       priv->ref_lum);
}
