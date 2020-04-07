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
 * SECTION:meta-multi-texture
 * @title: MetaMultiTexture
 * @short_description: A texture that can have multiple subtextures.
 *
 * #MetaMultiTexture allows one to deal with non-trivial formats that
 * have multiple planes, requires subsampling and/or aren't in RGB. A common
 * example of this are decoded video frames, which often use something in the
 * YUV colorspace, combined with subsampling.
 *
 * The basic idea of a #MetaMultiTexture is the following:
 * - Each subtextures is represented by a separate #CoglTexture. That means
 *   that you should add each of these planes as a layer to your #CoglPipeline.
 * - When dealing with a color space that is not RGB, you can ask the
 *   #MetaMultiTexture to create a shader for you that does the conversion
 *   in the GPU.
 * - In case you need to deal with memory access in a format with subsampling,
 *   you can use meta_multi_texture_get_width() and its analogous version
 *   for the height to get the correct size of the texture.
 */

#include "meta/meta-multi-texture.h"
#include "meta/meta-enum-types.h"

struct _MetaMultiTexture
{
  GObject parent_instance;

  MetaMultiTextureFormat format;

  guint n_subtextures;
  CoglTexture **subtextures;
};

G_DEFINE_TYPE (MetaMultiTexture, meta_multi_texture, G_TYPE_OBJECT);

/**
 * meta_multi_texture_get_format:
 * @self: a #MetaMultiTexture
 *
 * Returns the #MetaMultiTextureFormat that is used by this texture.
 *
 * Returns: The texture format that is used by this #MetaMultiTexture.
 */
MetaMultiTextureFormat
meta_multi_texture_get_format (MetaMultiTexture *self)
{
  g_return_val_if_fail (META_IS_MULTI_TEXTURE (self), META_MULTI_TEXTURE_FORMAT_SIMPLE);

  return self->format;
}

/**
 * meta_multi_texture_is_simple:
 * @self: a #MetaMultiTexture
 *
 * A small function that checks whether the given multi texture uses a "simple"
 * format, i.e. one that can be represented by a #CoglPixelFormat.
 *
 * Returns: Whether the texture format is #META_MULTI_TEXTURE_FORMAT_SIMPLE
 */
gboolean
meta_multi_texture_is_simple (MetaMultiTexture *self)
{
  g_return_val_if_fail (META_IS_MULTI_TEXTURE (self), FALSE);

  return self->format == META_MULTI_TEXTURE_FORMAT_SIMPLE;
}

/**
 * meta_multi_texture_get_n_subtexture:
 * @self: a #MetaMultiTexture
 *
 * Returns the number of subtextures for this texture. For example, simple RGB
 * textures will have a single subtexture, while some more convoluted formats
 * like NV12 and YUV 4:4:4 can have 2 and 3 subtextures respectively.
 *
 * Returns: The number of subtextures in this #MetaMultiTexture.
 */
guint
meta_multi_texture_get_n_subtextures (MetaMultiTexture *self)
{
  g_return_val_if_fail (META_IS_MULTI_TEXTURE (self), 0);

  return self->n_subtextures;
}

/**
 * meta_multi_texture_get_subtexture:
 * @self: a #MetaMultiTexture
 * @index: the index of the subtexture
 *
 * Returns the n'th subtexture of the #MetaMultiTexture. Note that it's a
 * programming error to use with an index larger than
 * meta_multi_texture_get_n_subtextures().
 *
 * Returns: (transfer none): The subtexture at the given @index.
 */
CoglTexture *
meta_multi_texture_get_subtexture (MetaMultiTexture *self, guint index)
{
  g_return_val_if_fail (META_IS_MULTI_TEXTURE (self), 0);
  g_return_val_if_fail (index < self->n_subtextures, NULL);

  return self->subtextures[index];
}

/**
 * meta_multi_texture_get_width:
 * @self: a #MetaMultiTexture
 *
 * Returns the width of the #MetaMultiTexture. Prefer this over calling
 * cogl_texture_get_width() on one of the textures, as that might give a
 * different size when dealing with subsampling.
 *
 * Returns: The width of the texture.
 */
int
meta_multi_texture_get_width (MetaMultiTexture *self)
{
  g_return_val_if_fail (META_IS_MULTI_TEXTURE (self), 0);

  return cogl_texture_get_width (self->subtextures[0]);
}

/**
 * meta_multi_texture_get_height:
 * @self: a #MetaMultiTexture
 *
 * Returns the height of the #MetaMultiTexture. Prefer this over calling
 * cogl_texture_get_height() on one of the textures, as that might give a
 * different size when dealing with subsampling.
 *
 * Returns: The height of the texture.
 */
int
meta_multi_texture_get_height (MetaMultiTexture *self)
{
  g_return_val_if_fail (META_IS_MULTI_TEXTURE (self), 0);

  return cogl_texture_get_height (self->subtextures[0]);
}

static void
meta_multi_texture_finalize (GObject *object)
{
  MetaMultiTexture *self = META_MULTI_TEXTURE (object);
  int i;

  for (i = 0; i < self->n_subtextures; i++)
    cogl_clear_object (&self->subtextures[i]);

  g_free (self->subtextures);

  G_OBJECT_CLASS (meta_multi_texture_parent_class)->finalize (object);
}

static void
meta_multi_texture_init (MetaMultiTexture *self)
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
 * @subtextures: (transfer full): The actual subtextures of the texture
 * @n_subtextures: The number of subtextures
 *
 * Creates a #MetaMultiTexture with the given @format. Each of the
 * #CoglTexture<!-- -->s represents a subtexture.
 *
 * Returns: (transfer full): A new #MetaMultiTexture. Use g_object_unref() when
 * you're done with it.
 */
MetaMultiTexture *
meta_multi_texture_new (MetaMultiTextureFormat format,
                        CoglTexture          **subtextures,
                        guint                  n_subtextures)
{
  MetaMultiTexture *self;

  g_return_val_if_fail (subtextures != NULL, NULL);
  g_return_val_if_fail (n_subtextures > 0, NULL);

  self = g_object_new (META_TYPE_MULTI_TEXTURE, NULL);
  self->format = format;
  self->n_subtextures = n_subtextures;
  self->subtextures = subtextures;

  return self;
}

/**
 * meta_multi_texture_new_simple:
 * @subtexture: (transfer full): The single subtexture of the texture
 *
 * Creates a #MetaMultiTexture for a "simple" texture, i.e. with only one
 * subtexture, in a format that can be represented using #CoglPixelFormat.
 *
 * Returns: (transfer full): A new #MetaMultiTexture. Use g_object_unref() when
 * you're done with it.
 */
MetaMultiTexture *
meta_multi_texture_new_simple (CoglTexture *subtexture)
{
  MetaMultiTexture *self;

  g_return_val_if_fail (subtexture != NULL, NULL);

  self = g_object_new (META_TYPE_MULTI_TEXTURE, NULL);
  self->format = META_MULTI_TEXTURE_FORMAT_SIMPLE;
  self->n_subtextures = 1;
  self->subtextures = g_malloc (sizeof (CoglTexture *));
  self->subtextures[0] = subtexture;

  return self;
}

/**
 * meta_multi_texture_to_string:
 * @self: a #MetaMultiTexture
 *
 * Returns a string representation of @self, useful for debugging purposes.
 *
 * Returns: (transfer full): A string representation of @self. Use g_free() when
 * done with it.
 */
char *
meta_multi_texture_to_string (MetaMultiTexture *self)
{
  g_autoptr(GString) str = NULL;
  g_autofree char *format_str = NULL;
  g_autofree char *ret = NULL;
  uint8_t i;

  str = g_string_new ("");
  g_string_append_printf (str, "MetaMultiTexture (%p) {\n", self);
  format_str = g_enum_to_string (META_TYPE_MULTI_TEXTURE_FORMAT, self->format);
  g_string_append_printf (str, "  .format   =  %s;\n", format_str);
  g_string_append_printf (str, "  .n_subtextures =  %u;\n", self->n_subtextures);
  g_string_append (str, "  .subtextures   =  {\n");

  for (i = 0; i < self->n_subtextures; i++)
    {
      CoglTexture *subtexture = self->subtextures[i];
      CoglPixelFormat subtexture_format = _cogl_texture_get_format (subtexture);

      g_string_append_printf (str, "    (%p) { .format = %s },\n",
                              subtexture,
                              cogl_pixel_format_to_string (subtexture_format));
    }

  g_string_append (str, "  }\n");
  g_string_append (str, "}");

  ret = g_string_free (g_steal_pointer (&str), FALSE);
  return g_steal_pointer (&ret);
}
