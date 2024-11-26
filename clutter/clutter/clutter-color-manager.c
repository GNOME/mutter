/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2024 Red Hat
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
 */

#include "config.h"

#include "clutter/clutter-color-manager-private.h"

#include "clutter/clutter-color-state-params.h"
#include "clutter/clutter-color-state-private.h"
#include "clutter/clutter-context.h"

enum
{
  PROP_0,

  PROP_CONTEXT,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

struct _ClutterColorManager
{
  GObject parent;

  ClutterContext *context;

  GHashTable *snippet_cache;
  unsigned int id_counter;
  ClutterColorState *default_color_state;
};

G_DEFINE_FINAL_TYPE (ClutterColorManager, clutter_color_manager, G_TYPE_OBJECT)

static void
clutter_color_manager_finalize (GObject *object)
{
  ClutterColorManager *color_manager = CLUTTER_COLOR_MANAGER (object);

  g_clear_object (&color_manager->default_color_state);

  g_clear_pointer (&color_manager->snippet_cache, g_hash_table_unref);

  G_OBJECT_CLASS (clutter_color_manager_parent_class)->finalize (object);
}

static void
clutter_color_manager_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  ClutterColorManager *color_manager = CLUTTER_COLOR_MANAGER (object);

  switch (prop_id)
    {
    case PROP_CONTEXT:
      color_manager->context = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_color_manager_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  ClutterColorManager *color_manager = CLUTTER_COLOR_MANAGER (object);

  switch (prop_id)
    {
    case PROP_CONTEXT:
      g_value_set_object (value, color_manager->context);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_color_manager_class_init (ClutterColorManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = clutter_color_manager_finalize;
  object_class->set_property = clutter_color_manager_set_property;
  object_class->get_property = clutter_color_manager_get_property;

  /**
   * ClutterColorManager:context:
   *
   * The associated ClutterContext.
   */
  obj_props[PROP_CONTEXT] =
    g_param_spec_object ("context", NULL, NULL,
                         CLUTTER_TYPE_CONTEXT,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS |
                         G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
clutter_color_manager_init (ClutterColorManager *color_manager)
{
  color_manager->snippet_cache =
    g_hash_table_new_full (clutter_color_transform_key_hash,
                           clutter_color_transform_key_equal,
                           g_free,
                           g_object_unref);
}

unsigned int
clutter_color_manager_get_next_id (ClutterColorManager *color_manager)
{
  return ++color_manager->id_counter;
}

ClutterColorState *
clutter_color_manager_get_default_color_state (ClutterColorManager *color_manager)
{
  if (!color_manager->default_color_state)
    {
      color_manager->default_color_state =
        clutter_color_state_params_new (color_manager->context,
                                        CLUTTER_COLORSPACE_SRGB,
                                        CLUTTER_TRANSFER_FUNCTION_SRGB);
    }

  return color_manager->default_color_state;
}

CoglSnippet *
clutter_color_manager_lookup_snippet (ClutterColorManager            *color_manager,
                                      const ClutterColorTransformKey *key)
{
  return g_hash_table_lookup (color_manager->snippet_cache, key);
}

void
clutter_color_manager_add_snippet (ClutterColorManager            *color_manager,
                                   const ClutterColorTransformKey *key,
                                   CoglSnippet                    *snippet)
{
  g_hash_table_insert (color_manager->snippet_cache,
                       g_memdup2 (key, sizeof (*key)),
                       g_object_ref (snippet));
}
