/* meta-cogl-drm-formats.c
 *
 * Copyright (C) 2020 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
 * Copyright (C) 2023 Collabora Ltd.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "config.h"

#include "common/meta-cogl-drm-formats.h"

gboolean
meta_cogl_pixel_format_from_drm_format (uint32_t                drm_format,
                                        CoglPixelFormat        *out_format,
                                        MetaMultiTextureFormat *out_multi_texture_format)
{
  const size_t n = G_N_ELEMENTS (meta_cogl_drm_format_map);
  size_t i;

  for (i = 0; i < n; i++)
    {
      if (meta_cogl_drm_format_map[i].drm_format == drm_format)
        break;
    }

  if (i == n)
    return FALSE;

  if (out_format)
    *out_format = meta_cogl_drm_format_map[i].cogl_format;

  if (out_multi_texture_format)
    *out_multi_texture_format = meta_cogl_drm_format_map[i].multi_texture_format;

  return TRUE;
}
