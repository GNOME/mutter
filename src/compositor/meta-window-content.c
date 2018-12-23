/*
 * Copyright (C) 2018 Endless, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by:
 *     Georges Basile Stavracas Neto <gbsneto@gnome.org>
 */

#include "compositor/meta-shaped-texture-private.h"
#include "compositor/meta-surface-actor.h"
#include "compositor/meta-window-actor-private.h"
#include "compositor/meta-window-content-private.h"

struct _MetaWindowContent
{
  GObject parent;

  MetaWindowActor *window_actor;

  unsigned int attached_actors;
};

static void clutter_content_iface_init (ClutterContentInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaWindowContent, meta_window_content, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_CONTENT, clutter_content_iface_init))

/**
 * SECTION:meta-window-content
 * @title: MetaWindowContent
 * @short_description: Contents of a MetaWindowActor
 *
 * #MetaWindowContent represents the user-visible content of
 * a #MetaWindowActor. It combines the contents of all the
 * #MetaSurfaceActors that the window contains into a final
 * texture.
 *
 * It is intended to be used as follows:
 *
 * |[
 * ClutterActor *
 * create_window_clone (MetaWindowActor *window_actor)
 * {
 *   ClutterContent *window_content;
 *   ClutterActor *clone;
 *
 *   window_content = meta_window_actor_get_content (window_actor);
 *
 *   clone = clutter_actor_new ();
 *   clutter_actor_set_content (clone, window_content);
 *
 *   return clone;
 * }
 * ]|
 *
 * It is also exposed as the #MetaWindowActor.content property
 * that can be binded to other actors. Notice, however, that
 * the value of #MetaWindowActor.content cannot be modified,
 * only read.
 */

enum
{
  PROP_0,
  PROP_WINDOW_ACTOR,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

static void
add_surface_paint_nodes (MetaSurfaceActor *surface_actor,
                         ClutterActor     *actor,
                         ClutterPaintNode *root_node,
                         float             scale_h,
                         float             scale_v)
{
  MetaShapedTexture *stex;
  ClutterActorBox box;
  CoglTexture *texture;
  uint8_t opacity;

  stex = meta_surface_actor_get_texture (surface_actor);

  if (!stex)
    return;

  texture = meta_shaped_texture_get_texture (stex);

  if (!texture)
    return;

  opacity = (guint) clutter_actor_get_paint_opacity (CLUTTER_ACTOR (surface_actor)) *
            (guint) clutter_actor_get_paint_opacity (actor) /
            255;

  clutter_actor_get_content_box (CLUTTER_ACTOR (surface_actor),
                                 &box);
  box.x1 = box.x1 * scale_h;
  box.x2 = box.x2 * scale_h;
  box.y1 = box.y1 * scale_v;
  box.y2 = box.y2 * scale_v;

  meta_shaped_texture_paint_node (stex,
                                  root_node,
                                  &box,
                                  opacity);
}

static void
meta_window_content_paint_content (ClutterContent   *content,
                                   ClutterActor     *actor,
                                   ClutterPaintNode *node)
{
  MetaWindowContent *window_content = META_WINDOW_CONTENT (content);
  ClutterActor *window_actor = CLUTTER_ACTOR (window_content->window_actor);
  ClutterActor *child;
  float dst_width, dst_height;
  float scale_h, scale_v;
  float width, height;

  g_assert (!META_IS_WINDOW_ACTOR (actor));
  g_assert (!META_IS_SURFACE_ACTOR (actor));

  /* Horizontal and vertical scales */
  clutter_actor_get_size (window_actor, &width, &height);
  clutter_actor_get_size (actor, &dst_width, &dst_height);
  scale_h = dst_width / width;
  scale_v = dst_height / height;

  for (child = clutter_actor_get_first_child (window_actor);
       child != NULL;
       child = clutter_actor_get_next_sibling (child))
    {
      if (!META_IS_SURFACE_ACTOR (child))
        continue;

      add_surface_paint_nodes (META_SURFACE_ACTOR (child),
                               actor, node,
                               scale_h, scale_v);
    }
}

static gboolean
meta_window_content_get_preferred_size (ClutterContent *content,
                                        float          *width,
                                        float          *height)
{
  MetaWindowContent *window_content = META_WINDOW_CONTENT (content);

  clutter_actor_get_size (CLUTTER_ACTOR (window_content->window_actor),
                          width, height);
  return TRUE;
}

static void
meta_window_content_attached (ClutterContent *content,
                            ClutterActor   *actor)
{
  MetaWindowContent *window_content = META_WINDOW_CONTENT (content);

  window_content->attached_actors++;
}

static void
meta_window_content_detached (ClutterContent *content,
                            ClutterActor   *actor)
{
  MetaWindowContent *window_content = META_WINDOW_CONTENT (content);

  window_content->attached_actors--;
}

static void
clutter_content_iface_init (ClutterContentInterface *iface)
{
  iface->paint_content = meta_window_content_paint_content;
  iface->get_preferred_size = meta_window_content_get_preferred_size;
  iface->attached = meta_window_content_attached;
  iface->detached = meta_window_content_detached;
}

static void
meta_window_content_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  MetaWindowContent *window_content = META_WINDOW_CONTENT (object);

  switch (prop_id)
    {
    case PROP_WINDOW_ACTOR:
      g_value_set_object (value, window_content->window_actor);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_window_content_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  MetaWindowContent *window_content = META_WINDOW_CONTENT (object);

  switch (prop_id)
    {
    case PROP_WINDOW_ACTOR:
      g_assert (window_content->window_actor == NULL);

      window_content->window_actor = g_value_get_object (value);
      g_assert (window_content->window_actor != NULL);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_window_content_class_init (MetaWindowContentClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = meta_window_content_get_property;
  object_class->set_property = meta_window_content_set_property;

  properties[PROP_WINDOW_ACTOR] =
    g_param_spec_object ("window-actor",
                         "Window actor",
                         "Window actor",
                         META_TYPE_WINDOW_ACTOR,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
meta_window_content_init (MetaWindowContent *self)
{
}

MetaWindowContent *
meta_window_content_new (MetaWindowActor *window_actor)
{
  return g_object_new (META_TYPE_WINDOW_CONTENT,
                       "window-actor", window_actor,
                       NULL);
}

/**
 * meta_window_content_get_window_actor:
 * @window_content: a #MetaWindowContent
 *
 * Retrieves the window actor that @window_content represents.
 *
 * Returns: (transfer none): a #MetaWindowActor
 */
MetaWindowActor *
meta_window_content_get_window_actor (MetaWindowContent *window_content)
{
  g_return_val_if_fail (META_IS_WINDOW_CONTENT (window_content), NULL);

  return window_content->window_actor;
}
