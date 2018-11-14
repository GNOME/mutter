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

#ifndef __META_PLANAR_TEXTURE_H__
#define __META_PLANAR_TEXTURE_H__

#include "cogl/cogl.h"

G_BEGIN_DECLS

#define META_TYPE_PLANAR_TEXTURE (meta_planar_texture_get_type())
G_DECLARE_FINAL_TYPE (MetaPlanarTexture,
                      meta_planar_texture,
                      META, PLANAR_TEXTURE,
                      GObject)


MetaPlanarTexture * meta_planar_texture_new   (CoglPixelFormat format,
                                               CoglTexture **planes,
                                               guint n_planes);


CoglPixelFormat meta_planar_texture_get_format   (MetaPlanarTexture *self);

guint           meta_planar_texture_get_n_planes (MetaPlanarTexture *self);

CoglTexture *   meta_planar_texture_get_plane    (MetaPlanarTexture *self,
                                                  guint index);

CoglTexture **  meta_planar_texture_get_planes   (MetaPlanarTexture *self);

guint           meta_planar_texture_get_width    (MetaPlanarTexture *self);

guint           meta_planar_texture_get_height   (MetaPlanarTexture *self);

/**
 * _cogl_pixel_format_get_n_planes:
 * @format: a #CoglPixelFormat
 *
 * Returns the number of planes the given CoglPixelFormat specifies.
 */
guint
_cogl_pixel_format_get_n_planes (CoglPixelFormat format);


G_END_DECLS

#endif
