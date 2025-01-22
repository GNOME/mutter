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

  return key->source_eotf_bits << 0 &
         key->target_eotf_bits << 4 &
         key->luminance_bit    << 8 &
         key->color_trans_bit  << 9;
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
          key1->color_trans_bit == key2->color_trans_bit);
}

void
clutter_color_transform_key_init (ClutterColorTransformKey *key,
                                  ClutterColorState        *color_state,
                                  ClutterColorState        *target_color_state)
{
  ClutterColorStateClass *color_state_class =
    CLUTTER_COLOR_STATE_GET_CLASS (color_state);

  g_return_if_fail (CLUTTER_IS_COLOR_STATE (color_state));
  g_return_if_fail (CLUTTER_IS_COLOR_STATE (target_color_state));

  color_state_class->init_color_transform_key (color_state,
                                               target_color_state,
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

static CoglSnippet *
clutter_color_state_create_transform_snippet (ClutterColorState *color_state,
                                              ClutterColorState *target_color_state)
{
  ClutterColorStateClass *color_state_class =
    CLUTTER_COLOR_STATE_GET_CLASS (color_state);

  return color_state_class->create_transform_snippet (color_state,
                                                      target_color_state);
}

static CoglSnippet *
clutter_color_state_get_transform_snippet (ClutterColorState *color_state,
                                           ClutterColorState *target_color_state)
{
  ClutterColorStatePrivate *priv;
  ClutterColorManager *color_manager;
  ClutterColorTransformKey transform_key;
  CoglSnippet *snippet;

  priv = clutter_color_state_get_instance_private (color_state);
  color_manager = clutter_context_get_color_manager (priv->context);

  clutter_color_transform_key_init (&transform_key,
                                    color_state,
                                    target_color_state);
  snippet = clutter_color_manager_lookup_snippet (color_manager,
                                                  &transform_key);
  if (snippet)
    return g_object_ref (snippet);

  snippet = clutter_color_state_create_transform_snippet (color_state,
                                                          target_color_state);

  clutter_color_manager_add_snippet (color_manager,
                                     &transform_key,
                                     g_object_ref (snippet));
  return snippet;
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

  color_state_class->update_uniforms (color_state,
                                      target_color_state,
                                      pipeline);
}

void
clutter_color_state_do_transform (ClutterColorState *color_state,
                                  ClutterColorState *target_color_state,
                                  const float       *input,
                                  float             *output,
                                  int                n_samples)
{
  ClutterColorStateClass *color_state_class =
    CLUTTER_COLOR_STATE_GET_CLASS (color_state);

  g_return_if_fail (CLUTTER_IS_COLOR_STATE (color_state));
  g_return_if_fail (CLUTTER_IS_COLOR_STATE (target_color_state));

  color_state_class->do_transform (color_state,
                                   target_color_state,
                                   input,
                                   output,
                                   n_samples);
}

void
clutter_color_state_add_pipeline_transform (ClutterColorState *color_state,
                                            ClutterColorState *target_color_state,
                                            CoglPipeline      *pipeline)
{
  g_autoptr (CoglSnippet) snippet = NULL;

  g_return_if_fail (CLUTTER_IS_COLOR_STATE (color_state));
  g_return_if_fail (CLUTTER_IS_COLOR_STATE (target_color_state));

  if (clutter_color_state_equals (color_state, target_color_state))
    return;

  snippet = clutter_color_state_get_transform_snippet (color_state,
                                                       target_color_state);
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
