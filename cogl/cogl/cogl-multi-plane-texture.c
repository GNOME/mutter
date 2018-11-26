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
  guint i = 0;

  for (i = 0; i < self->n_planes; i++)
    cogl_object_unref (self->planes[i]);

  g_free (self->planes);
}

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

CoglMultiPlaneTexture *
cogl_multi_plane_texture_new_single_plane (CoglPixelFormat format,
                                           CoglTexture *plane)
{
  CoglMultiPlaneTexture *self = g_slice_new0 (CoglMultiPlaneTexture);

  _cogl_multi_plane_texture_object_new (self);

  self->format = format;
  self->n_planes = 1;
  self->planes = g_malloc (sizeof (CoglTexture *));
  self->planes[0] = plane;

  return self;
}
