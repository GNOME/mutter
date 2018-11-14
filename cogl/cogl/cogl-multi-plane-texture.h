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

#ifndef __COGL_MULTI_PLANE_TEXTURE_H__
#define __COGL_MULTI_PLANE_TEXTURE_H__

#include "cogl/cogl-texture.h"

COGL_BEGIN_DECLS

/**
 * SECTION:cogl-multi-plane-texture
 * @title: CoglMultiPlaneTexture
 * @short_description: A non-primitive texture that can have multiple planes.
 */

typedef struct _CoglMultiPlaneTexture CoglMultiPlaneTexture;
#define COGL_MULTI_PLANE_TEXTURE(tex) ((CoglMultiPlaneTexture *) tex)


/**
 * cogl_multiplane_texture_get_gtype:
 *
 * Returns: a #GType that can be used with the GLib type system.
 */
GType cogl_multi_plane_texture_get_gtype (void);

CoglMultiPlaneTexture * cogl_multi_plane_texture_new  (CoglPixelFormat format,
                                                       CoglTexture **planes,
                                                       guint n_planes);


CoglPixelFormat cogl_multi_plane_texture_get_format   (CoglMultiPlaneTexture *self);

guint           cogl_multi_plane_texture_get_n_planes (CoglMultiPlaneTexture *self);

CoglTexture *   cogl_multi_plane_texture_get_plane    (CoglMultiPlaneTexture *self,
                                                       guint index);

CoglTexture **  cogl_multi_plane_texture_get_planes   (CoglMultiPlaneTexture *self);

guint           cogl_multi_plane_texture_get_width    (CoglMultiPlaneTexture *self);

guint           cogl_multi_plane_texture_get_height   (CoglMultiPlaneTexture *self);

guint
cogl_pixel_format_get_n_planes (CoglPixelFormat format);

void
cogl_pixel_format_get_subsampling_parameters (CoglPixelFormat format,
                                               guint *horizontal_params,
                                               guint *vertical_params);


COGL_END_DECLS

#endif
