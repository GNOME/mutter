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

#include "cogl-config.h"

#include "cogl-object-private.h"
#include "cogl-multi-plane-texture.h"
#include "cogl-gtype-private.h"

struct _CoglMultiPlaneTexture
{
  CoglObject _parent;

  CoglPixelFormat format;

  guint n_planes;
  CoglTexture **planes;
};

static void
_cogl_multi_plane_texture_free (CoglMultiPlaneTexture *self);

COGL_OBJECT_DEFINE (MultiPlaneTexture, multi_plane_texture);
COGL_GTYPE_DEFINE_CLASS (MultiPlaneTexture, multi_plane_texture);


CoglPixelFormat
cogl_multi_plane_texture_get_format (CoglMultiPlaneTexture *self)
{
  return self->format;
}

guint
cogl_multi_plane_texture_get_n_planes (CoglMultiPlaneTexture *self)
{
  return self->n_planes;
}

CoglTexture *
cogl_multi_plane_texture_get_plane (CoglMultiPlaneTexture *self, guint index)
{
  g_return_val_if_fail (self->n_planes > 0, NULL);
  g_return_val_if_fail (index < self->n_planes, NULL);

  return self->planes[index];
}

CoglTexture **
cogl_multi_plane_texture_get_planes (CoglMultiPlaneTexture *self)
{
  return self->planes;
}

guint
cogl_multi_plane_texture_get_width (CoglMultiPlaneTexture *self)
{
  g_return_val_if_fail (self->n_planes > 0, 0);

  return cogl_texture_get_width (self->planes[0]);
}

guint
cogl_multi_plane_texture_get_height (CoglMultiPlaneTexture *self)
{
  g_return_val_if_fail (self->n_planes > 0, 0);

  return cogl_texture_get_height (self->planes[0]);
}

static void
_cogl_multi_plane_texture_free (CoglMultiPlaneTexture *self)
{
  g_free (self->planes);
  /* XXX do we need to unref the CoglTextures as well here? */
}

/**
 * cogl_multi_plane_texture_new:
 * @format: The format of the #CoglMultiPlaneTexture
 * @planes: (transfer full): The actual planes of the texture
 * @n_planes: The number of planes
 *
 * Creates a #CoglMultiPlaneTexture with the given @format. Each of the
 * #CoglTexture<!-- -->s represents a plane.
 */
CoglMultiPlaneTexture *
cogl_multi_plane_texture_new (CoglPixelFormat format,
                              CoglTexture **planes, guint n_planes)
{
  CoglMultiPlaneTexture *self = g_slice_new0 (CoglMultiPlaneTexture);

  _cogl_multi_plane_texture_object_new (self);

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
cogl_pixel_format_get_n_planes (CoglPixelFormat format)
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
cogl_pixel_format_get_subsampling_parameters (CoglPixelFormat format,
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
