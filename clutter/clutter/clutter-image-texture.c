/*
 * Clutter.
 *
 * An OpenGL based 'interactive image' library.
 *
 * Copyright (C) 2012  Intel Corporation.
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

#include "clutter-build-config.h"

#define CLUTTER_ENABLE_EXPERIMENTAL_API

#include "clutter-image-texture.h"

#include "clutter-actor-private.h"
#include "clutter-color.h"
#include "clutter-content-private.h"
#include "clutter-debug.h"
#include "clutter-paint-node.h"
#include "clutter-paint-nodes.h"
#include "clutter-private.h"

struct _ClutterImageTexturePrivate
{
  CoglTexture *texture;
};

static void clutter_content_iface_init (ClutterContentIface *iface);

G_DEFINE_TYPE_WITH_CODE (ClutterImageTexture, clutter_image_texture, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (ClutterImageTexture)
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_CONTENT,
                                                clutter_content_iface_init))

GQuark
clutter_image_texture_error_quark (void)
{
  return g_quark_from_static_string ("clutter-image-error-quark");
}

static void
clutter_image_texture_finalize (GObject *gobject)
{
  ClutterImageTexturePrivate *priv = CLUTTER_IMAGE_TEXTURE (gobject)->priv;

  if (priv->texture != NULL)
    {
      cogl_object_unref (priv->texture);
      priv->texture = NULL;
    }

  G_OBJECT_CLASS (clutter_image_texture_parent_class)->finalize (gobject);
}

static void
clutter_image_texture_class_init (ClutterImageTextureClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = clutter_image_texture_finalize;
}

static void
clutter_image_texture_init (ClutterImageTexture *self)
{
  self->priv = clutter_image_texture_get_instance_private (self);
}

static void
clutter_image_texture_paint_content (ClutterContent   *content,
                                     ClutterActor     *actor,
                                     ClutterPaintNode *root)
{
  ClutterImageTexturePrivate *priv = CLUTTER_IMAGE_TEXTURE (content)->priv;
  ClutterPaintNode *node;

  if (priv->texture == NULL)
    return;

  node = clutter_actor_create_texture_paint_node (actor, priv->texture);
  clutter_paint_node_set_name (node, "Image Content");
  clutter_paint_node_add_child (root, node);
  clutter_paint_node_unref (node);
}

static gboolean
clutter_image_texture_get_preferred_size (ClutterContent *content,
                                          gfloat         *width,
                                          gfloat         *height)
{
  ClutterImageTexturePrivate *priv = CLUTTER_IMAGE_TEXTURE (content)->priv;

  if (priv->texture == NULL)
    return FALSE;

  if (width != NULL)
    *width = cogl_texture_get_width (priv->texture);

  if (height != NULL)
    *height = cogl_texture_get_height (priv->texture);

  return TRUE;
}

static void
clutter_content_iface_init (ClutterContentIface *iface)
{
  iface->get_preferred_size = clutter_image_texture_get_preferred_size;
  iface->paint_content = clutter_image_texture_paint_content;
}

ClutterContent *
clutter_image_texture_new_from_texture (CoglTexture *texture)
{
  ClutterImageTexture *image;

  g_return_val_if_fail (texture != NULL, FALSE);

  image = g_object_new (CLUTTER_TYPE_IMAGE_TEXTURE, NULL);
  image->priv->texture = texture;

  return CLUTTER_CONTENT (image);
}

CoglTexture *
clutter_image_texture_get_texture (ClutterImageTexture *image)
{
  g_return_val_if_fail (CLUTTER_IS_IMAGE_TEXTURE (image), NULL);

  return image->priv->texture;
}
