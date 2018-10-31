/*
 * shaped texture
 *
 * An actor to draw a texture clipped to a list of rectangles
 *
 * Authored By Neil Roberts  <neil@linux.intel.com>
 *
 * Copyright (C) 2008 Intel Corporation
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

#ifndef __META_SHAPED_TEXTURE_H__
#define __META_SHAPED_TEXTURE_H__

#include <X11/Xlib.h>

#include "clutter/clutter.h"

G_BEGIN_DECLS

#define META_TYPE_SHAPED_TEXTURE (meta_shaped_texture_get_type ())
G_DECLARE_FINAL_TYPE (MetaShapedTexture, meta_shaped_texture, META, SHAPED_TEXTURE, ClutterActor)


void meta_shaped_texture_set_create_mipmaps (MetaShapedTexture *self,
					     gboolean           create_mipmaps);

gboolean meta_shaped_texture_update_area (MetaShapedTexture *self,
                                          int                x,
                                          int                y,
                                          int                width,
                                          int                height);

CoglTexture * meta_shaped_texture_get_texture (MetaShapedTexture *self);

void meta_shaped_texture_set_mask_texture (MetaShapedTexture *self,
                                           CoglTexture       *mask_texture);
void meta_shaped_texture_set_opaque_region (MetaShapedTexture *self,
                                            cairo_region_t    *opaque_region);

cairo_surface_t * meta_shaped_texture_get_image (MetaShapedTexture     *self,
                                                 cairo_rectangle_int_t *clip);

G_END_DECLS

#endif /* __META_SHAPED_TEXTURE_H__ */
