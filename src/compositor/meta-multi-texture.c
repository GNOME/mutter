/*
 * Authored By Niels De Graef <niels.degraef@barco.com>
 *
 * Copyright (C) 2018 Barco NV
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/**
 * MetaMultiTexture:
 *
 * A texture that can have multiple planes.
 *
 * #MetaMultiTexture allows one to deal with non-trivial formats that
 * have multiple planes, requires subsampling and/or aren't in RGB. A common
 * example of this are decoded video frames, which often use something in the
 * YUV colorspace, combined with subsampling.
 *
 * The basic idea of a #MetaMultiTexture is the following:
 * - Each plane is represented by a separate #CoglTexture. That means that you
 *   should add each of these planes as a layer to your CoglPipeline.
 * - When dealing with a color space that is not RGB, you can ask the
 *   #MetaMultiTexture to create a shader for you that does the conversion
 *   in the GPU.
 * - In case you need to deal with memory access in a format with subsampling,
 *   you can use meta_multi_texture_get_width() and its analogous version
 *   for the height to get the correct size of the texture.
 */

#include "config.h"

#include "meta/meta-multi-texture.h"

#include "meta/meta-enum-types.h"

struct _MetaMultiTexture
{
  GObject parent_instance;

  MetaMultiTextureFormat format;

  int n_planes;
  CoglTexture **planes;
};

G_DEFINE_TYPE (MetaMultiTexture, meta_multi_texture, G_TYPE_OBJECT);

/**
 * meta_multi_texture_get_format:
 * @multi_texture: a #MetaMultiTexture
 *
 * Returns the #MetaMultiTextureFormat that is used by this texture.
 *
 * Returns: The texture format that is used by this #MetaMultiTexture.
 */
MetaMultiTextureFormat
meta_multi_texture_get_format (MetaMultiTexture *multi_texture)
{
  g_return_val_if_fail (META_IS_MULTI_TEXTURE (multi_texture), META_MULTI_TEXTURE_FORMAT_SIMPLE);

  return multi_texture->format;
}

/**
 * meta_multi_texture_is_simple:
 * @multi_texture: a #MetaMultiTexture
 *
 * A small function that checks whether the given multi texture uses a "simple"
 * format, i.e. one that can be represented by a #CoglPixelFormat.
 *
 * Returns: Whether the texture format is #META_MULTI_TEXTURE_FORMAT_SIMPLE
 */
gboolean
meta_multi_texture_is_simple (MetaMultiTexture *multi_texture)
{
  g_return_val_if_fail (META_IS_MULTI_TEXTURE (multi_texture), FALSE);

  return multi_texture->format == META_MULTI_TEXTURE_FORMAT_SIMPLE;
}

/**
 * meta_multi_texture_get_n_planes:
 * @multi_texture: a #MetaMultiTexture
 *
 * Returns the number of planes for this texture. Note that this is entirely
 * dependent on the #CoglPixelFormat that is used. For example, simple RGB
 * textures will have a single plane, while some more convoluted formats like
 * NV12 and YUV 4:4:4 can have 2 and 3 planes respectively.
 *
 * Returns: The number of planes in this #MetaMultiTexture.
 */
int
meta_multi_texture_get_n_planes (MetaMultiTexture *multi_texture)
{
  g_return_val_if_fail (META_IS_MULTI_TEXTURE (multi_texture), 0);

  return multi_texture->n_planes;
}

/**
 * meta_multi_texture_get_plane:
 * @multi_texture: a #MetaMultiTexture
 * @index: the index of the plane
 *
 * Returns the n'th plane of the #MetaMultiTexture. Note that it's a programming
 * error to use with an index larger than meta_multi_texture_get_n_planes().
 *
 * Returns: (transfer none): The plane at the given @index.
 */
CoglTexture *
meta_multi_texture_get_plane (MetaMultiTexture *multi_texture,
                              int               index)
{
  g_return_val_if_fail (META_IS_MULTI_TEXTURE (multi_texture), 0);
  g_return_val_if_fail (index < multi_texture->n_planes, NULL);

  return multi_texture->planes[index];
}

/**
 * meta_multi_texture_get_width:
 * @multi_texture: a #MetaMultiTexture
 *
 * Returns the width of the #MetaMultiTexture. Prefer this over calling
 * cogl_texture_get_width() on one of the textures, as that might give a
 * different size when dealing with subsampling.
 *
 * Returns: The width of the texture.
 */
int
meta_multi_texture_get_width (MetaMultiTexture *multi_texture)
{
  g_return_val_if_fail (META_IS_MULTI_TEXTURE (multi_texture), 0);

  return cogl_texture_get_width (multi_texture->planes[0]);
}

/**
 * meta_multi_texture_get_height:
 * @multi_texture: a #MetaMultiTexture
 *
 * Returns the height of the #MetaMultiTexture. Prefer this over calling
 * cogl_texture_get_height() on one of the textures, as that might give a
 * different size when dealing with subsampling.
 *
 * Returns: The height of the texture.
 */
int
meta_multi_texture_get_height (MetaMultiTexture *multi_texture)
{
  g_return_val_if_fail (META_IS_MULTI_TEXTURE (multi_texture), 0);

  return cogl_texture_get_height (multi_texture->planes[0]);
}

static void
meta_multi_texture_finalize (GObject *object)
{
  MetaMultiTexture *multi_texture = META_MULTI_TEXTURE (object);
  int i;

  for (i = 0; i < multi_texture->n_planes; i++)
    g_clear_object (&multi_texture->planes[i]);

  g_free (multi_texture->planes);

  G_OBJECT_CLASS (meta_multi_texture_parent_class)->finalize (object);
}

static void
meta_multi_texture_init (MetaMultiTexture *multi_texture)
{
}

static void
meta_multi_texture_class_init (MetaMultiTextureClass *klass)
{
  GObjectClass *gobj_class = G_OBJECT_CLASS (klass);

  gobj_class->finalize = meta_multi_texture_finalize;
}

/**
 * meta_multi_texture_new:
 * @format: The format of the #MetaMultiTexture
 * @planes: (transfer full): The actual planes of the texture
 * @n_planes: The number of planes
 *
 * Creates a #MetaMultiTexture with the given @format. Each of the
 * `CoglTexture`s represents a plane.
 *
 * Returns: (transfer full): A new #MetaMultiTexture. Use g_object_unref() when
 * you're done with it.
 */
MetaMultiTexture *
meta_multi_texture_new (MetaMultiTextureFormat   format,
                        CoglTexture            **planes,
                        int                      n_planes)
{
  MetaMultiTexture *multi_texture;

  g_return_val_if_fail (planes != NULL, NULL);
  g_return_val_if_fail (n_planes > 0, NULL);

  multi_texture = g_object_new (META_TYPE_MULTI_TEXTURE, NULL);
  multi_texture->format = format;
  multi_texture->n_planes = n_planes;
  multi_texture->planes = planes;

  return multi_texture;
}

/**
 * meta_multi_texture_new_simple:
 * @plane: (transfer full): The single plane of the texture
 *
 * Creates a #MetaMultiTexture for a "simple" texture, i.e. with only one
 * plane, in a format that can be represented using #CoglPixelFormat.
 *
 * Returns: (transfer full): A new #MetaMultiTexture. Use g_object_unref() when
 * you're done with it.
 */
MetaMultiTexture *
meta_multi_texture_new_simple (CoglTexture *plane)
{
  MetaMultiTexture *multi_texture;

  g_return_val_if_fail (plane != NULL, NULL);

  multi_texture = g_object_new (META_TYPE_MULTI_TEXTURE, NULL);
  multi_texture->format = META_MULTI_TEXTURE_FORMAT_SIMPLE;
  multi_texture->n_planes = 1;
  multi_texture->planes = g_malloc (sizeof (CoglTexture *));
  multi_texture->planes[0] = plane;

  return multi_texture;
}

/**
 * meta_multi_texture_to_string:
 * @multi_texture: a #MetaMultiTexture
 *
 * Returns a string representation of @multi_texture, useful for debugging
 * purposes.
 *
 * Returns: (transfer full): A string representation of @multi_texture. Use
 * g_free() when done with it.
 */
char *
meta_multi_texture_to_string (MetaMultiTexture *multi_texture)
{
  g_autoptr (GString) str = NULL;
  g_autofree char *format_str = NULL;
  g_autofree char *ret = NULL;
  uint8_t i;

  str = g_string_new ("");
  g_string_append_printf (str, "MetaMultiTexture (%p) {\n", multi_texture);
  format_str = g_enum_to_string (META_TYPE_MULTI_TEXTURE_FORMAT, multi_texture->format);
  g_string_append_printf (str, "  .format   =  %s;\n", format_str);
  g_string_append_printf (str, "  .n_planes =  %u;\n", multi_texture->n_planes);
  g_string_append (str, "  .planes   =  {\n");

  for (i = 0; i < multi_texture->n_planes; i++)
    {
      CoglTexture *plane = multi_texture->planes[i];
      CoglPixelFormat plane_format = _cogl_texture_get_format (plane);

      g_string_append_printf (str, "    (%p) { .format = %s },\n",
                              plane,
                              cogl_pixel_format_to_string (plane_format));
    }

  g_string_append (str, "  }\n");
  g_string_append (str, "}");

  ret = g_string_free (g_steal_pointer (&str), FALSE);
  return g_steal_pointer (&ret);
}
