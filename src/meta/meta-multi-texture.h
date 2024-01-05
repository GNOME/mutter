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

#include <glib-object.h>

#include "cogl/cogl.h"
#include "meta/common.h"
#include "meta/meta-multi-texture-format.h"

G_BEGIN_DECLS

#define META_TYPE_MULTI_TEXTURE (meta_multi_texture_get_type ())
META_EXPORT
G_DECLARE_FINAL_TYPE (MetaMultiTexture,
                      meta_multi_texture,
                      META,
                      MULTI_TEXTURE,
                      GObject)

META_EXPORT
MetaMultiTexture * meta_multi_texture_new (MetaMultiTextureFormat   format,
                                           CoglTexture            **planes,
                                           int                      n_planes);

META_EXPORT
MetaMultiTexture * meta_multi_texture_new_simple (CoglTexture *plane);

META_EXPORT
MetaMultiTextureFormat meta_multi_texture_get_format (MetaMultiTexture *multi_texture);

META_EXPORT
gboolean meta_multi_texture_is_simple (MetaMultiTexture *multi_texture);

META_EXPORT
int meta_multi_texture_get_n_planes (MetaMultiTexture *multi_texture);

META_EXPORT
CoglTexture * meta_multi_texture_get_plane (MetaMultiTexture *multi_texture,
                                            int               index);

META_EXPORT
int meta_multi_texture_get_width (MetaMultiTexture *multi_texture);

META_EXPORT
int meta_multi_texture_get_height (MetaMultiTexture *multi_texture);

META_EXPORT
char * meta_multi_texture_to_string (MetaMultiTexture *multi_texture);

G_END_DECLS
