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

/**
 * ClutterImage:
 * 
 * Image data content
 *
 * #ClutterImage is a #ClutterContent implementation that displays
 * image data inside a [class@Actor].
 */

#include "config.h"

#include "clutter/clutter-image.h"

#include "clutter/clutter-actor-private.h"
#include "clutter/clutter-color.h"
#include "clutter/clutter-content-private.h"
#include "clutter/clutter-debug.h"
#include "clutter/clutter-paint-node.h"
#include "clutter/clutter-paint-nodes.h"
#include "clutter/clutter-private.h"

typedef struct
{
  CoglTexture *texture;
  gint width;
  gint height;
} ClutterImagePrivate;

static void clutter_content_iface_init (ClutterContentInterface *iface);

G_DEFINE_TYPE_WITH_CODE (ClutterImage, clutter_image, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (ClutterImage)
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_CONTENT,
                                                clutter_content_iface_init))

static CoglTexture *
create_texture_from_data (unsigned int      width,
                          unsigned int      height,
                          CoglPixelFormat   pixel_format,
                          unsigned int      row_stride,
                          const uint8_t    *data,
                          GError          **error)
{
  CoglContext *ctx =
    clutter_backend_get_cogl_context (clutter_get_default_backend ());
  CoglTexture *texture_2d;

  texture_2d = cogl_texture_2d_new_from_data (ctx,
                                              width,
                                              height,
                                              pixel_format,
                                              row_stride,
                                              data,
                                              error);

  return texture_2d;
}

static void
update_image_size (ClutterImage *self)
{
  ClutterImagePrivate *priv = clutter_image_get_instance_private (self);
  gint width, height;

  if (priv->texture == NULL)
    return;

  width = cogl_texture_get_width (priv->texture);
  height = cogl_texture_get_height (priv->texture);

  if (priv->width == width &&
      priv->height == height)
    return;

  priv->width = width;
  priv->height = height;

  clutter_content_invalidate_size (CLUTTER_CONTENT (self));
}

static void
clutter_image_finalize (GObject *gobject)
{
  ClutterImage *image = CLUTTER_IMAGE (gobject);
  ClutterImagePrivate *priv = clutter_image_get_instance_private (image);

  g_clear_object (&priv->texture);

  G_OBJECT_CLASS (clutter_image_parent_class)->finalize (gobject);
}

static void
clutter_image_class_init (ClutterImageClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = clutter_image_finalize;
}

static void
clutter_image_init (ClutterImage *self)
{
}

static void
clutter_image_paint_content (ClutterContent      *content,
                             ClutterActor        *actor,
                             ClutterPaintNode    *root,
                             ClutterPaintContext *paint_context)
{
  ClutterImage *image = CLUTTER_IMAGE (content);
  ClutterImagePrivate *priv = clutter_image_get_instance_private (image);
  ClutterPaintNode *node;

  if (priv->texture == NULL)
    return;

  node = clutter_actor_create_texture_paint_node (actor, priv->texture);
  clutter_paint_node_set_static_name (node, "Image Content");
  clutter_paint_node_add_child (root, node);
  clutter_paint_node_unref (node);
}

static gboolean
clutter_image_get_preferred_size (ClutterContent *content,
                                  gfloat         *width,
                                  gfloat         *height)
{
  ClutterImage *image = CLUTTER_IMAGE (content);
  ClutterImagePrivate *priv = clutter_image_get_instance_private (image);

  if (priv->texture == NULL)
    return FALSE;

  if (width != NULL)
    *width = cogl_texture_get_width (priv->texture);

  if (height != NULL)
    *height = cogl_texture_get_height (priv->texture);

  return TRUE;
}

static void
clutter_content_iface_init (ClutterContentInterface *iface)
{
  iface->get_preferred_size = clutter_image_get_preferred_size;
  iface->paint_content = clutter_image_paint_content;
}

/**
 * clutter_image_new:
 *
 * Creates a new #ClutterImage instance.
 *
 * Return value: (transfer full): the newly created #ClutterImage instance.
 *   Use g_object_unref() when done.
 */
ClutterContent *
clutter_image_new (void)
{
  return g_object_new (CLUTTER_TYPE_IMAGE, NULL);
}

/**
 * clutter_image_set_data:
 * @image: a #ClutterImage
 * @data: (array): the image data, as an array of bytes
 * @pixel_format: the Cogl pixel format of the image data
 * @width: the width of the image data
 * @height: the height of the image data
 * @row_stride: the length of each row inside @data
 * @error: return location for a #GError, or %NULL
 *
 * Sets the image data to be displayed by @image.
 *
 * If the image data was successfully loaded, the @image will be invalidated.
 *
 * In case of error, the @error value will be set, and this function will
 * return %FALSE.
 *
 * The image data is copied in texture memory.
 *
 * The image data is expected to be a linear array of RGBA or RGB pixel data;
 * how to retrieve that data is left to platform specific image loaders. For
 * instance, if you use the GdkPixbuf library:
 *
 * ```c
 *   ClutterContent *image = clutter_image_new ();
 *
 *   GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file (filename, NULL);
 *
 *   clutter_image_set_data (CLUTTER_IMAGE (image),
 *                           gdk_pixbuf_get_pixels (pixbuf),
 *                           gdk_pixbuf_get_has_alpha (pixbuf)
 *                             ? COGL_PIXEL_FORMAT_RGBA_8888
 *                             : COGL_PIXEL_FORMAT_RGB_888,
 *                           gdk_pixbuf_get_width (pixbuf),
 *                           gdk_pixbuf_get_height (pixbuf),
 *                           gdk_pixbuf_get_rowstride (pixbuf),
 *                           &error);
 *
 *   g_object_unref (pixbuf);
 * ```
 *
 * Return value: %TRUE if the image data was successfully loaded,
 *   and %FALSE otherwise.
 */
gboolean
clutter_image_set_data (ClutterImage     *image,
                        const guint8     *data,
                        CoglPixelFormat   pixel_format,
                        guint             width,
                        guint             height,
                        guint             row_stride,
                        GError          **error)
{
  ClutterImagePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_IMAGE (image), FALSE);
  g_return_val_if_fail (data != NULL, FALSE);

  priv = clutter_image_get_instance_private (image);

  if (priv->texture != NULL)
    g_object_unref (priv->texture);

  priv->texture = create_texture_from_data (width,
                                            height,
                                            pixel_format,
                                            row_stride,
                                            data,
                                            error);

  if (priv->texture == NULL)
    return FALSE;

  clutter_content_invalidate (CLUTTER_CONTENT (image));
  update_image_size (image);

  return TRUE;
}

/**
 * clutter_image_set_bytes:
 * @image: a #ClutterImage
 * @data: the image data, as a #GBytes
 * @pixel_format: the Cogl pixel format of the image data
 * @width: the width of the image data
 * @height: the height of the image data
 * @row_stride: the length of each row inside @data
 * @error: return location for a #GError, or %NULL
 *
 * Sets the image data stored inside a #GBytes to be displayed by @image.
 *
 * If the image data was successfully loaded, the @image will be invalidated.
 *
 * In case of error, the @error value will be set, and this function will
 * return %FALSE.
 *
 * The image data contained inside the #GBytes is copied in texture memory,
 * and no additional reference is acquired on the @data.
 *
 * Return value: %TRUE if the image data was successfully loaded,
 *   and %FALSE otherwise.
 */
gboolean
clutter_image_set_bytes (ClutterImage     *image,
                         GBytes           *data,
                         CoglPixelFormat   pixel_format,
                         guint             width,
                         guint             height,
                         guint             row_stride,
                         GError          **error)
{
  ClutterImagePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_IMAGE (image), FALSE);
  g_return_val_if_fail (data != NULL, FALSE);

  priv = clutter_image_get_instance_private (image);

  if (priv->texture != NULL)
    g_object_unref (priv->texture);

  priv->texture = create_texture_from_data (width,
                                            height,
                                            pixel_format,
                                            row_stride,
                                            g_bytes_get_data (data, NULL),
                                            error);

  if (priv->texture == NULL)
    return FALSE;

  clutter_content_invalidate (CLUTTER_CONTENT (image));
  update_image_size (image);

  return TRUE;
}

/**
 * clutter_image_set_area:
 * @image: a #ClutterImage
 * @data: (array): the image data, as an array of bytes
 * @pixel_format: the Cogl pixel format of the image data
 * @rect: a rectangle indicating the area that should be set
 * @row_stride: the length of each row inside @data
 * @error: return location for a #GError, or %NULL
 *
 * Sets the image data to be display by @image, using @rect to indicate
 * the position and size of the image data to be set.
 *
 * If the @image does not have any image data set when this function is
 * called, a new texture will be created with the size of the width and
 * height of the rectangle, i.e. calling this function on a newly created
 * #ClutterImage will be the equivalent of calling [method@Clutter.Image.set_data].
 *
 * If the image data was successfully loaded, the @image will be invalidated.
 *
 * In case of error, the @error value will be set, and this function will
 * return %FALSE.
 *
 * The image data is copied in texture memory.
 *
 * Return value: %TRUE if the image data was successfully loaded,
 *   and %FALSE otherwise.
 */
gboolean
clutter_image_set_area (ClutterImage        *image,
                        const guint8        *data,
                        CoglPixelFormat      pixel_format,
                        const MtkRectangle  *area,
                        guint                row_stride,
                        GError             **error)
{
  ClutterImagePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_IMAGE (image), FALSE);
  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (area != NULL, FALSE);

  priv = clutter_image_get_instance_private (image);

  if (priv->texture == NULL)
    {
      priv->texture = create_texture_from_data (area->width,
                                                area->height,
                                                pixel_format,
                                                row_stride,
                                                data,
                                                error);
    }
  else
    {
      gboolean res;

      res = cogl_texture_set_region (priv->texture,
                                     0, 0,
                                     area->x, area->y,
                                     area->width, area->height,
                                     area->width, area->height,
                                     pixel_format,
                                     row_stride,
                                     data);

      if (!res)
        {
          g_clear_object (&priv->texture);
        }
    }

  if (priv->texture == NULL)
    return FALSE;

  clutter_content_invalidate (CLUTTER_CONTENT (image));
  update_image_size (image);

  return TRUE;
}

/**
 * clutter_image_get_texture:
 * @image: a #ClutterImage
 *
 * Retrieves a pointer to the Cogl texture used by @image.
 *
 * If you change the contents of the returned Cogl texture you will need
 * to manually invalidate the @image with [method@Clutter.Content.invalidate]
 * in order to update the actors using @image as their content.
 *
 * Return value: (transfer none): a pointer to the Cogl texture, or %NULL
 */
CoglTexture *
clutter_image_get_texture (ClutterImage *image)
{
  ClutterImagePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_IMAGE (image), NULL);

  priv = clutter_image_get_instance_private (image);
  return priv->texture;
}
