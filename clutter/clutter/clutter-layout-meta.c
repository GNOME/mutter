/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2009  Intel Corporation.
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
 * ClutterLayoutMeta:
 *
 * Wrapper for actors inside a layout manager
 *
 * [type@Clutter.LayoutMeta] is a wrapper object created by
 * [class@LayoutManager] implementations in order to store child-specific data
 * and properties.
 *
 * A [type@Clutter.LayoutMeta] wraps a [class@Actor] inside a container
 * [class@Actor] using a [class@LayoutManager].
 */

#include "config.h"

#include "clutter/clutter-layout-meta.h"
#include "clutter/clutter-debug.h"
#include "clutter/clutter-private.h"

typedef struct _ClutterLayoutMetaPrivate ClutterLayoutMetaPrivate;
struct _ClutterLayoutMetaPrivate
{
  ClutterLayoutManager *manager;
  ClutterActor *container;
  ClutterActor *actor;
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (ClutterLayoutMeta,
                                     clutter_layout_meta,
                                     G_TYPE_OBJECT);

enum
{
  PROP_0,

  PROP_MANAGER,
  PROP_CONTAINER,
  PROP_ACTOR,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

static void
clutter_layout_meta_dispose (GObject *object)
{
  ClutterLayoutMeta *layout_meta = CLUTTER_LAYOUT_META (object);
  ClutterLayoutMetaPrivate *priv =
    clutter_layout_meta_get_instance_private (layout_meta);

  g_clear_weak_pointer (&priv->manager);
  g_clear_weak_pointer (&priv->container);
  g_clear_weak_pointer (&priv->actor);

  G_OBJECT_CLASS (clutter_layout_meta_parent_class)->dispose (object);
}

static void
clutter_layout_meta_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  ClutterLayoutMeta *layout_meta = CLUTTER_LAYOUT_META (object);
  ClutterLayoutMetaPrivate *priv =
    clutter_layout_meta_get_instance_private (layout_meta);

  switch (prop_id)
    {
    case PROP_MANAGER:
      g_set_weak_pointer (&priv->manager, g_value_get_object (value));
      break;

    case PROP_CONTAINER:
      g_set_weak_pointer (&priv->container, g_value_get_object (value));
      break;

    case PROP_ACTOR:
      g_set_weak_pointer (&priv->actor, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_layout_meta_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  ClutterLayoutMeta *layout_meta = CLUTTER_LAYOUT_META (object);
  ClutterLayoutMetaPrivate *priv =
    clutter_layout_meta_get_instance_private (layout_meta);

  switch (prop_id)
    {
    case PROP_MANAGER:
      g_value_set_object (value, priv->manager);
      break;

    case PROP_CONTAINER:
      g_value_set_object (value, priv->container);
      break;

    case PROP_ACTOR:
      g_value_set_object (value, priv->actor);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_layout_meta_class_init (ClutterLayoutMetaClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = clutter_layout_meta_dispose;
  gobject_class->set_property = clutter_layout_meta_set_property;
  gobject_class->get_property = clutter_layout_meta_get_property;

  /**
   * ClutterLayoutMeta:manager:
   *
   * The [class@LayoutManager] that created this [type@Clutter.LayoutMeta].
   */
  obj_props[PROP_MANAGER] =
    g_param_spec_object ("manager", NULL, NULL,
                         CLUTTER_TYPE_LAYOUT_MANAGER,
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS);

  /**
   * ClutterLayoutMeta:container:
   *
   * The [type@Clutter.Actor] containing [property@Clutter.LayoutMeta:actor]
   */
  obj_props[PROP_CONTAINER] =
    g_param_spec_object ("container", NULL, NULL,
                         CLUTTER_TYPE_ACTOR,
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS);

  /**
   * ClutterLayoutMeta:actor:
   *
   * The [type@Clutter.Actor] being wrapped by this [type@Clutter.LayoutMeta]
   */
  obj_props[PROP_ACTOR] =
    g_param_spec_object ("actor", NULL, NULL,
                         CLUTTER_TYPE_ACTOR,
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, PROP_LAST, obj_props);
}

static void
clutter_layout_meta_init (ClutterLayoutMeta *self)
{
}

/**
 * clutter_layout_meta_get_manager:
 * @data: a #ClutterLayoutMeta
 *
 * Retrieves the actor wrapped by @data
 *
 * Return value: (transfer none): a [type@Clutter.LayoutManager]
 */
ClutterLayoutManager *
clutter_layout_meta_get_manager (ClutterLayoutMeta *data)
{
  ClutterLayoutMetaPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_LAYOUT_META (data), NULL);

  priv = clutter_layout_meta_get_instance_private (data);

  return priv->manager;
}

/**
 * clutter_layout_meta_get_container:
 * @data: a #ClutterLayoutMeta
 *
 * Retrieves the container using @data
 *
 * Return value: (transfer none): a [type@Clutter.Actor]
 */
ClutterActor *
clutter_layout_meta_get_container (ClutterLayoutMeta *data)
{
  ClutterLayoutMetaPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_LAYOUT_META (data), NULL);

  priv = clutter_layout_meta_get_instance_private (data);

  return priv->container;
}

/**
 * clutter_layout_meta_get_actor:
 * @data: a #ClutterLayoutMeta
 *
 * Retrieves the actor wrapped by @data
 *
 * Return value: (transfer none): a [type@Clutter.Actor]
 */
ClutterActor *
clutter_layout_meta_get_actor (ClutterLayoutMeta *data)
{
  ClutterLayoutMetaPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_LAYOUT_META (data), NULL);

  priv = clutter_layout_meta_get_instance_private (data);

  return priv->actor;
}

gboolean
clutter_layout_meta_is_for (ClutterLayoutMeta    *data,
                            ClutterLayoutManager *manager,
                            ClutterActor         *container,
                            ClutterActor         *actor)
{
  ClutterLayoutMetaPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_LAYOUT_META (data), FALSE);

  priv = clutter_layout_meta_get_instance_private (data);

  return priv->manager == manager &&
    priv->container == container && priv->actor == actor;
}
