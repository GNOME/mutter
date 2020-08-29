/* meta-cogl-utils.c
 *
 * Copyright 2020 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "backends/native/meta-cogl-utils.h"

#include <drm_fourcc.h>

typedef struct _PixelFormatMap {
  uint32_t drm_format;
  CoglPixelFormat cogl_format;
  CoglTextureComponents cogl_components;
} PixelFormatMap;

static const PixelFormatMap pixel_format_map[] = {
/* DRM formats are defined as little-endian, not machine endian. */
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  { DRM_FORMAT_RGB565,   COGL_PIXEL_FORMAT_RGB_565,       COGL_TEXTURE_COMPONENTS_RGB  },
  { DRM_FORMAT_ABGR8888, COGL_PIXEL_FORMAT_RGBA_8888_PRE, COGL_TEXTURE_COMPONENTS_RGBA },
  { DRM_FORMAT_XBGR8888, COGL_PIXEL_FORMAT_RGBA_8888_PRE, COGL_TEXTURE_COMPONENTS_RGB  },
  { DRM_FORMAT_ARGB8888, COGL_PIXEL_FORMAT_BGRA_8888_PRE, COGL_TEXTURE_COMPONENTS_RGBA },
  { DRM_FORMAT_XRGB8888, COGL_PIXEL_FORMAT_BGRA_8888_PRE, COGL_TEXTURE_COMPONENTS_RGB  },
  { DRM_FORMAT_BGRA8888, COGL_PIXEL_FORMAT_ARGB_8888_PRE, COGL_TEXTURE_COMPONENTS_RGBA },
  { DRM_FORMAT_BGRX8888, COGL_PIXEL_FORMAT_ARGB_8888_PRE, COGL_TEXTURE_COMPONENTS_RGB  },
  { DRM_FORMAT_RGBA8888, COGL_PIXEL_FORMAT_ABGR_8888_PRE, COGL_TEXTURE_COMPONENTS_RGBA },
  { DRM_FORMAT_RGBX8888, COGL_PIXEL_FORMAT_ABGR_8888_PRE, COGL_TEXTURE_COMPONENTS_RGB  },
#elif G_BYTE_ORDER == G_BIG_ENDIAN
  /* DRM_FORMAT_RGB565 cannot be expressed. */
  { DRM_FORMAT_ABGR8888, COGL_PIXEL_FORMAT_ABGR_8888_PRE, COGL_TEXTURE_COMPONENTS_RGBA },
  { DRM_FORMAT_XBGR8888, COGL_PIXEL_FORMAT_ABGR_8888_PRE, COGL_TEXTURE_COMPONENTS_RGB  },
  { DRM_FORMAT_ARGB8888, COGL_PIXEL_FORMAT_ARGB_8888_PRE, COGL_TEXTURE_COMPONENTS_RGBA },
  { DRM_FORMAT_XRGB8888, COGL_PIXEL_FORMAT_ARGB_8888_PRE, COGL_TEXTURE_COMPONENTS_RGB  },
  { DRM_FORMAT_BGRA8888, COGL_PIXEL_FORMAT_BGRA_8888_PRE, COGL_TEXTURE_COMPONENTS_RGBA },
  { DRM_FORMAT_BGRX8888, COGL_PIXEL_FORMAT_BGRA_8888_PRE, COGL_TEXTURE_COMPONENTS_RGB  },
  { DRM_FORMAT_RGBA8888, COGL_PIXEL_FORMAT_RGBA_8888_PRE, COGL_TEXTURE_COMPONENTS_RGBA },
  { DRM_FORMAT_RGBX8888, COGL_PIXEL_FORMAT_RGBA_8888_PRE, COGL_TEXTURE_COMPONENTS_RGB  },
#else
#error "unexpected G_BYTE_ORDER"
#endif
};

gboolean
meta_cogl_pixel_format_from_drm_format (uint32_t               drm_format,
                                        CoglPixelFormat       *out_format,
                                        CoglTextureComponents *out_components)
{
  const size_t n = G_N_ELEMENTS (pixel_format_map);
  size_t i;

  for (i = 0; i < n; i++)
    {
      if (pixel_format_map[i].drm_format == drm_format)
        break;
    }

  if (i == n)
    return FALSE;

  if (out_format)
    *out_format = pixel_format_map[i].cogl_format;

  if (out_components)
    *out_components = pixel_format_map[i].cogl_components;

  return TRUE;
}
