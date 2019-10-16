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

#ifndef META_MULTI_TEXTURE_FORMAT_PRIVATE_H
#define META_MULTI_TEXTURE_FORMAT_PRIVATE_H

#include <cogl/cogl.h>

#include "meta/meta-multi-texture-format.h"

G_BEGIN_DECLS

const char * meta_multi_texture_format_to_string (MetaMultiTextureFormat format);

int meta_multi_texture_format_get_n_planes (MetaMultiTextureFormat format);

void meta_multi_texture_format_get_subformats (MetaMultiTextureFormat  format,
                                               CoglPixelFormat        *formats_out);

void meta_multi_texture_format_get_plane_indices (MetaMultiTextureFormat  format,
                                                  uint8_t                *plane_indices);

void meta_multi_texture_format_get_subsampling_factors (MetaMultiTextureFormat  format,
                                                        uint8_t                *horizontal_factors,
                                                        uint8_t                *vertical_factors);

gboolean meta_multi_texture_format_get_snippets (MetaMultiTextureFormat   format,
                                                 CoglSnippet            **fragment_globals_snippet,
                                                 CoglSnippet            **fragment_snippet);

G_END_DECLS

#endif /* META_MULTI_TEXTURE_FORMAT_PRIVATE_H */
