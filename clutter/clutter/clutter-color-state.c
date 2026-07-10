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

#include "clutter/clutter-color-state.h"

#include "clutter/clutter-color-manager-private.h"

enum
{
  PROP_0,

  PROP_CONTEXT,

  N_PROPS
};

enum
{
  DESTROYED,

  N_SIGNALS,
};

static GParamSpec *obj_props[N_PROPS];

static guint signals[N_SIGNALS];

typedef struct _ClutterColorStatePrivate
{
  ClutterContext *context;

  uint64_t id;
  gboolean disposed;
} ClutterColorStatePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (ClutterColorState,
                            clutter_color_state,
                            G_TYPE_OBJECT)

uint64_t
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

  G_OBJECT_CLASS (clutter_color_state_parent_class)->constructed (object);
}

static void
clutter_color_state_dispose (GObject *object)
{
  ClutterColorState *color_state = CLUTTER_COLOR_STATE (object);
  ClutterColorStatePrivate *priv =
    clutter_color_state_get_instance_private (color_state);

  if (!priv->disposed)
    g_signal_emit (color_state, signals[DESTROYED], 0);

  priv->disposed = TRUE;

  G_OBJECT_CLASS (clutter_color_state_parent_class)->dispose (object);
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
  object_class->dispose = clutter_color_state_dispose;
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

  signals[DESTROYED] = g_signal_new ("destroyed",
                                     G_TYPE_FROM_CLASS (klass),
                                     G_SIGNAL_RUN_LAST,
                                     0,
                                     NULL, NULL, NULL,
                                     G_TYPE_NONE, 0);
}

static void
clutter_color_state_init (ClutterColorState *color_state)
{
}

/**
 * clutter_color_state_get_context:
 *
 * Returns: (transfer none): The #ClutterContext
 */
ClutterContext *
clutter_color_state_get_context (ClutterColorState *color_state)
{
  ClutterColorStatePrivate *priv =
    clutter_color_state_get_instance_private (color_state);

  return priv->context;
}

guint
clutter_color_state_hash (ClutterColorState *color_state)
{
  ClutterColorStateClass *klass;

  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE (color_state), 0);

  klass = CLUTTER_COLOR_STATE_GET_CLASS (color_state);

  return klass->hash (color_state);
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

const ClutterLuminance *
clutter_color_state_get_luminance (ClutterColorState *color_state)
{
  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE (color_state), NULL);

  return CLUTTER_COLOR_STATE_GET_CLASS (color_state)->get_luminance (color_state);
}
