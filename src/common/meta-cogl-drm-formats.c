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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "config.h"

#include "common/meta-cogl-drm-formats.h"

const MetaFormatInfo *
meta_format_info_from_drm_format (uint32_t drm_format)
{
  const size_t n = G_N_ELEMENTS (meta_format_info);
  size_t i;

  for (i = 0; i < n; i++)
    {
      if (meta_format_info[i].drm_format == drm_format)
        return &meta_format_info[i];
    }

  return NULL;
}

const MetaFormatInfo *
meta_format_info_from_cogl_format (CoglPixelFormat cogl_format)
{
  const size_t n = G_N_ELEMENTS (meta_format_info);
  size_t i;

  for (i = 0; i < n; i++)
    {
      if (meta_format_info[i].cogl_format == cogl_format)
        return &meta_format_info[i];
    }

  return NULL;
}
