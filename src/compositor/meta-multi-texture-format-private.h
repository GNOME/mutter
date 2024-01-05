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

#pragma once

#include "cogl/cogl.h"
#include "meta/meta-multi-texture-format.h"

G_BEGIN_DECLS

typedef struct _MetaMultiTextureFormatInfo
{
  uint8_t n_planes;

  /* Per plane-information */
  CoglPixelFormat subformats[COGL_PIXEL_FORMAT_MAX_PLANES]; /* influences how we deal with it on a GL level */
  uint8_t plane_indices[COGL_PIXEL_FORMAT_MAX_PLANES]; /* source plane */
  uint8_t hsub[COGL_PIXEL_FORMAT_MAX_PLANES]; /* horizontal subsampling */
  uint8_t vsub[COGL_PIXEL_FORMAT_MAX_PLANES]; /* vertical subsampling */
} MetaMultiTextureFormatInfo;

const char * meta_multi_texture_format_to_string (MetaMultiTextureFormat format);

const MetaMultiTextureFormatInfo * meta_multi_texture_format_get_info (MetaMultiTextureFormat format);

gboolean meta_multi_texture_format_get_snippets (MetaMultiTextureFormat   format,
                                                 CoglSnippet            **fragment_globals_snippet,
                                                 CoglSnippet            **fragment_snippet);

G_END_DECLS
