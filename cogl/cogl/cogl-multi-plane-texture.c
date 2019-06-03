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
#include "cogl-texture-private.h"
#include "cogl-texture-2d-sliced.h"

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

CoglMultiPlaneTexture *
cogl_multi_plane_texture_new_from_bitmaps (CoglPixelFormat format,
                                           CoglBitmap **bitmaps, guint n_planes,
                                           GError **error)
{
  guint i = 0;
  CoglMultiPlaneTexture *self = g_slice_new0 (CoglMultiPlaneTexture);

  _cogl_multi_plane_texture_object_new (self);

  self->format = format;
  self->n_planes = n_planes;
  self->planes = g_malloc (sizeof (CoglTexture *) * n_planes);

  /* XXX convert to appropriate textures here */
  for (i = 0; i < n_planes; i++)
    {
      CoglTexture *plane;

      if (format == COGL_PIXEL_FORMAT_NV12)
      {
          /* Issue here: the data is inside the A coordinate, rather than the X coordinate */
        if (i == 0)
          _cogl_bitmap_set_format (bitmaps[i], COGL_PIXEL_FORMAT_G_8);
        else
          _cogl_bitmap_set_format (bitmaps[i], COGL_PIXEL_FORMAT_RG_88);
      }

      plane = COGL_TEXTURE (cogl_texture_2d_new_from_bitmap (bitmaps[i]));

      if (format == COGL_PIXEL_FORMAT_NV12)
      {
        if (i == 0)
        {
          _cogl_texture_set_internal_format (plane, COGL_PIXEL_FORMAT_G_8);
          _cogl_bitmap_set_format (bitmaps[i], COGL_PIXEL_FORMAT_G_8);
        }
        else
        {
          _cogl_texture_set_internal_format (plane, COGL_PIXEL_FORMAT_RG_88);
          _cogl_bitmap_set_format (bitmaps[i], COGL_PIXEL_FORMAT_RG_88);
        }
      } else {
        /* XXX Let's break everyting for non RGBA */
          cogl_texture_set_components (plane, COGL_TEXTURE_COMPONENTS_RGBA);
      }

      if (!cogl_texture_allocate (plane, error))
        {
          g_clear_pointer (&plane, cogl_object_unref);

          /* There's a chance we failed due to the buffer being NPOT size.
           * If so, try again with CoglTexture2DSliced (which does support this) */
          if (g_error_matches (*error,
                               COGL_TEXTURE_ERROR,
                               COGL_TEXTURE_ERROR_SIZE))
            {
              CoglTexture2DSliced *plane_sliced;

              g_clear_error (error);

              plane_sliced =
                cogl_texture_2d_sliced_new_from_bitmap (bitmaps[i],
                                                        COGL_TEXTURE_MAX_WASTE);
              plane = COGL_TEXTURE (plane_sliced);
              if (format == COGL_PIXEL_FORMAT_NV12)
              {
                if (i == 0)
                {
                  _cogl_texture_set_internal_format (plane, COGL_PIXEL_FORMAT_G_8);
                  _cogl_bitmap_set_format (bitmaps[i], COGL_PIXEL_FORMAT_G_8);
                }
                else
                {
                  _cogl_texture_set_internal_format (plane, COGL_PIXEL_FORMAT_RG_88);
                  _cogl_bitmap_set_format (bitmaps[i], COGL_PIXEL_FORMAT_RG_88);
                }
              } else {
                /* XXX Let's break everyting for non RGBA */
                  cogl_texture_set_components (plane, COGL_TEXTURE_COMPONENTS_RGBA);
              }

              if (!cogl_texture_allocate (plane, error))
                cogl_clear_object (&plane);
            }
        }

      cogl_object_unref (bitmaps[i]);
      self->planes[i] = plane;
    }


  return self;
}

gchar *
cogl_multi_plane_texture_to_string (CoglMultiPlaneTexture *self)
{
    g_autoptr(GString) str = NULL;
    g_autofree gchar *ret = NULL;
    guint i;

    str = g_string_new ("");
    g_string_append_printf (str, "CoglMultiPlaneTexture (%p) {\n", self);
    g_string_append_printf (str, "  .format   =  %s;\n", cogl_pixel_format_to_string (self->format));
    g_string_append_printf (str, "  .n_planes =  %u;\n", self->n_planes);
    g_string_append (str, "  .planes   =  {\n");

    for (i = 0; i < self->n_planes; i++)
      {
        CoglTexture *plane = self->planes[i];

        g_string_append_printf (str, "    (%p) { .format = %s },\n",
                                plane,
                                cogl_pixel_format_to_string (_cogl_texture_get_format (plane)));
      }

    g_string_append (str, "  }\n");
    g_string_append (str, "}");

    ret = g_string_free (g_steal_pointer (&str), FALSE);
    return g_steal_pointer (&ret);
}
