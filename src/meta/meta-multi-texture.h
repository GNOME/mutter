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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef __META_MULTI_TEXTURE_H__
#define __META_MULTI_TEXTURE_H__

#include <glib-object.h>
#include <cogl/cogl.h>
#include <meta/common.h>

#include "meta/meta-multi-texture-format.h"

G_BEGIN_DECLS

#define META_TYPE_MULTI_TEXTURE (meta_multi_texture_get_type ())
META_EXPORT
G_DECLARE_FINAL_TYPE (MetaMultiTexture, meta_multi_texture,
                      META, MULTI_TEXTURE,
                      GObject)


META_EXPORT
MetaMultiTexture *     meta_multi_texture_new               (MetaMultiTextureFormat format,
                                                             CoglTexture          **subtextures,
                                                             guint                  n_subtextures);

META_EXPORT
MetaMultiTexture *     meta_multi_texture_new_simple        (CoglTexture *subtexture);

META_EXPORT
MetaMultiTextureFormat meta_multi_texture_get_format        (MetaMultiTexture *self);

META_EXPORT
gboolean               meta_multi_texture_is_simple         (MetaMultiTexture *self);

META_EXPORT
guint                  meta_multi_texture_get_n_subtextures (MetaMultiTexture *self);

META_EXPORT
CoglTexture *          meta_multi_texture_get_subtexture    (MetaMultiTexture *self,
                                                             guint index);

META_EXPORT
int                    meta_multi_texture_get_width         (MetaMultiTexture *self);

META_EXPORT
int                    meta_multi_texture_get_height        (MetaMultiTexture *self);

META_EXPORT
char *                 meta_multi_texture_to_string         (MetaMultiTexture *self);

G_END_DECLS

#endif
