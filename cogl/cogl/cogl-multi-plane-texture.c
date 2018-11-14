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

static const gchar *nv12_to_rgba_shader =
    "vec4\n"
    "cogl_nv12_to_rgba (vec2 UV)\n"
    "{\n"
    "  vec4 color;\n"

    "  float y = 1.1640625 * (texture2D (cogl_sampler0, UV).x - 0.0625);\n"
    "  vec2 uv = texture2D (cogl_sampler1, UV).rg;\n"
    "  uv -= 0.5;\n"
    "  float u = uv.x;\n"
    "  float v = uv.y;\n"

    "  color.r = y + 1.59765625 * v;\n"
    "  color.g = y - 0.390625 * u - 0.8125 * v;\n"
    "  color.b = y + 2.015625 * u;\n"
    "  color.a = 1.0;\n"

    "  return color;\n"
    "}\n";

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

void
cogl_multi_plane_texture_create_color_conversion_snippets (CoglMultiPlaneTexture *self,
                                                           CoglSnippet **vertex_snippet_out,
                                                           CoglSnippet **fragment_snippet_out,
                                                           CoglSnippet **layer_snippet_out)
{
  const gchar *global_hook;
  const gchar *layer_hook;

  switch (self->format)
    {
    case COGL_PIXEL_FORMAT_NV12:
      /* XXX are we using Y_UV or Y_xUxV? Maybe check for RG support? */
      global_hook = nv12_to_rgba_shader;
      layer_hook =  "cogl_layer = cogl_nv12_to_rgba(cogl_tex_coord0_in.st);\n";
      break;
    default:
      *vertex_snippet_out = NULL;
      *fragment_snippet_out = NULL;
      *layer_snippet_out = NULL;
      return;
    }

    *vertex_snippet_out = cogl_snippet_new (COGL_SNIPPET_HOOK_VERTEX_GLOBALS,
                                            global_hook,
                                            NULL);

    *fragment_snippet_out = cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT_GLOBALS,
                                              global_hook,
                                              NULL);

    *layer_snippet_out = cogl_snippet_new (COGL_SNIPPET_HOOK_LAYER_FRAGMENT,
                                           NULL,
                                           layer_hook);
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

guint
cogl_pixel_format_get_n_planes (CoglPixelFormat format)
{
  switch (format)
    {
    case COGL_PIXEL_FORMAT_NV12:
    case COGL_PIXEL_FORMAT_NV21:
    case COGL_PIXEL_FORMAT_NV16:
    case COGL_PIXEL_FORMAT_NV61:
    case COGL_PIXEL_FORMAT_NV24:
    case COGL_PIXEL_FORMAT_NV42:
      return 2;
    case COGL_PIXEL_FORMAT_YUV410:
    case COGL_PIXEL_FORMAT_YVU410:
    case COGL_PIXEL_FORMAT_YUV411:
    case COGL_PIXEL_FORMAT_YVU411:
    case COGL_PIXEL_FORMAT_YUV420:
    case COGL_PIXEL_FORMAT_YVU420:
    case COGL_PIXEL_FORMAT_YUV422:
    case COGL_PIXEL_FORMAT_YVU422:
    case COGL_PIXEL_FORMAT_YUV444:
    case COGL_PIXEL_FORMAT_YVU444:
      return 3;
    default:
      return 1;
    }

  g_assert_not_reached ();
}

void
cogl_pixel_format_get_subsampling_parameters (CoglPixelFormat format,
                                              guint *horizontal_params,
                                              guint *vertical_params)
{
  switch (format)
    {
    case COGL_PIXEL_FORMAT_NV12:
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
