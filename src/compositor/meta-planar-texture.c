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
 * SECTION:meta-planar-texture
 * @title: MetaPlanarTexture
 * @short_description: A texture that can have multiple planes (e.g. Y, U, V).
 */

#include "config.h"
#include "meta-planar-texture.h"

struct _MetaPlanarTexture
{
  GObject parent;

  CoglPixelFormat format;

  guint n_planes;
  CoglTexture **planes;
};

G_DEFINE_TYPE (MetaPlanarTexture, meta_planar_texture, G_TYPE_OBJECT);


CoglPixelFormat
meta_planar_texture_get_format (MetaPlanarTexture *self)
{
  return self->format;
}

guint
meta_planar_texture_get_n_planes (MetaPlanarTexture *self)
{
  return self->n_planes;
}

CoglTexture *
meta_planar_texture_get_plane (MetaPlanarTexture *self, guint index)
{
  g_return_val_if_fail (self->n_planes > 0, NULL);
  g_return_val_if_fail (index < self->n_planes, NULL);

  return self->planes[index];
}

CoglTexture **
meta_planar_texture_get_planes (MetaPlanarTexture *self)
{
  return self->planes;
}

guint
meta_planar_texture_get_width (MetaPlanarTexture *self)
{
  g_return_val_if_fail (self->n_planes > 0, 0);

  return cogl_texture_get_width (self->planes[0]);
}

guint
meta_planar_texture_get_height (MetaPlanarTexture *self)
{
  g_return_val_if_fail (self->n_planes > 0, 0);

  return cogl_texture_get_height (self->planes[0]);
}

static void
meta_planar_texture_finalize (GObject *object)
{
  MetaPlanarTexture *self = META_PLANAR_TEXTURE (object);

  g_free (self->planes);
  /* XXX do we need to unref the CoglTextures as well here? */
}

static void
meta_planar_texture_class_init (MetaPlanarTextureClass *klass)
{
  GObjectClass *gobj_class = G_OBJECT_CLASS (klass);

  gobj_class->finalize = meta_planar_texture_finalize;
}

static void
meta_planar_texture_init (MetaPlanarTexture *self)
{
  self->format = COGL_PIXEL_FORMAT_ANY;
  self->n_planes = 0;
  self->planes = NULL;
}

/**
 * meta_planar_texture_new:
 * @format: The format of the #MetaPlanarTexture
 * @planes: (transfer full): The actual planes of the texture
 * @n_planes: The number of planes
 *
 * Creates a #MetaPlanarTexture with the given @format. Each of the
 * #CoglTexture<!-- -->s represents a plane.
 */
MetaPlanarTexture *
meta_planar_texture_new (CoglPixelFormat format,
                         CoglTexture **planes, guint n_planes)
{
  MetaPlanarTexture *self = g_object_new (META_TYPE_PLANAR_TEXTURE, NULL);

  self->format = format;
  self->n_planes = n_planes;
  self->planes = planes;

  return self;
}

/**
 * _cogl_pixel_format_get_n_planes:
 *
 * Returns the number of planes the given CoglPixelFormat specifies.
 */
guint
_cogl_pixel_format_get_n_planes (CoglPixelFormat format)
{
  switch (format)
    {
    case COGL_PIXEL_FORMAT_Y_UV:
      return 2;
    default:
      return 1;
    }

  g_assert_not_reached ();
}

/**
 * _cogl_pixel_format_get_subsampling_parameters:
 *
 * Returns the subsampling in both the horizontal as the vertical direction.
 */
void
_cogl_pixel_format_get_subsampling_parameters (CoglPixelFormat format,
                                               guint *horizontal_params,
                                               guint *vertical_params)
{
  switch (format)
    {
    case COGL_PIXEL_FORMAT_Y_UV:
      horizontal_params[0] = 1;
      vertical_params[0] = 1;
      horizontal_params[1] = 2;
      vertical_params[1] = 2;
      break;
    default:
      horizontal_params[0] = 1;
      vertical_params[0] = 1;
      break;
    }
}
