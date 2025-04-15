/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * MetaTextureMipmap
 *
 * Mipmap management object using OpenGL
 *
 * Copyright (C) 2009 Red Hat, Inc.
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

#include "clutter/clutter.h"
#include "meta/meta-multi-texture.h"

G_BEGIN_DECLS

/**
 * MetaTextureMipmap:
 *
 * Mipmap handling for textures
 *
 * A #MetaTextureMipmap is used to get GL mipmaps for a texture
 */

typedef struct _MetaTextureMipmap MetaTextureMipmap;

MetaTextureMipmap *meta_texture_mipmap_new (CoglContext *cogl_context);

void meta_texture_mipmap_free (MetaTextureMipmap *mipmap);

void meta_texture_mipmap_set_base_texture (MetaTextureMipmap *mipmap,
                                           MetaMultiTexture  *texture);

void meta_texture_mipmap_set_coeffs (MetaTextureMipmap            *mipmap,
                                     MetaMultiTextureCoefficients  coeffs);

MetaMultiTexture *meta_texture_mipmap_get_paint_texture (MetaTextureMipmap *mipmap);

void meta_texture_mipmap_invalidate (MetaTextureMipmap *mipmap);

void meta_texture_mipmap_clear (MetaTextureMipmap *mipmap);

G_END_DECLS
