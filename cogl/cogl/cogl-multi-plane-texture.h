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
 *
 * #CoglMultiPlaneTexture allows one to deal with non-trivial formats that
 * have multiple planes, requires subsampling and/or aren't in RGB. A common
 * example of this are decoded video frames, which often use something in the
 * YUV colorspace, combined with subsampling.
 *
 * The basic idea of a #CoglMultiPlaneTexture is the following:
 * - Each plane is represented by a separate #CoglTexture. That means that you
 *   should add each of these planes as a layer to your CoglPipeline.
 * - When dealing with a color space that is not RGB, you can ask the
 *   #CoglMultiPlaneTexture to create a shader for you that does the conversion
 *   in the GPU.
 * - In case you need to deal with memory access in a format with subsampling,
 *   you can use cogl_multi_plane_texture_get_width() and its analogous version
 *   for the height to get the correct size of the texture.
 */

typedef struct _CoglMultiPlaneTexture CoglMultiPlaneTexture;
#define COGL_MULTI_PLANE_TEXTURE(tex) ((CoglMultiPlaneTexture *) tex)


/**
 * cogl_multiplane_texture_get_gtype:
 *
 * Returns: a #GType that can be used with the GLib type system.
 */
GType cogl_multi_plane_texture_get_gtype (void);

/**
 * cogl_multi_plane_texture_new:
 * @format: The format of the #CoglMultiPlaneTexture
 * @planes: (transfer full): The actual planes of the texture
 * @n_planes: The number of planes
 *
 * Creates a #CoglMultiPlaneTexture with the given @format. Each of the
 * #CoglTexture<!-- -->s represents a plane.
 *
 * Returns: (transfer full): A new #CoglMultiPlaneTexture. Use
 * cogl_object_unref() when you're done with it.
 */
CoglMultiPlaneTexture * cogl_multi_plane_texture_new  (CoglPixelFormat format,
                                                       CoglTexture **planes,
                                                       guint n_planes);

/**
 * cogl_multi_plane_texture_new_single_plane:
 * @format: The format of the #CoglMultiPlaneTexture
 * @plane: (transfer full): The actual planes of the texture
 *
 * Creates a #CoglMultiPlaneTexture for a "simple" texture, i.e. with only one
 * plane.
 *
 * Returns: (transfer full): A new #CoglMultiPlaneTexture. Use
 * cogl_object_unref() when you're done with it.
 */
CoglMultiPlaneTexture * cogl_multi_plane_texture_new_single_plane (CoglPixelFormat format,
                                                                   CoglTexture *plane);

/**
 * cogl_multi_plane_texture_get_format:
 * @self: a #CoglMultiPlaneTexture
 *
 * Returns the pixel format that is used by this texture.
 *
 * Returns: The pixel format that is used by this #CoglMultiPlaneTexture.
 */
CoglPixelFormat cogl_multi_plane_texture_get_format   (CoglMultiPlaneTexture *self);

/**
 * cogl_multi_plane_texture_get_format:
 * @self: a #CoglMultiPlaneTexture
 *
 * Returns the number of planes for this texture. Note that this is entirely
 * dependent on the #CoglPixelFormat that is used. For example, simple RGB
 * textures will have a single plane, while some more convoluted formats like
 * NV12 and YUV 4:4:4 can have 2 and 3 planes respectively.
 *
 * Returns: The number of planes in this #CoglMultiPlaneTexture.
 */
guint           cogl_multi_plane_texture_get_n_planes (CoglMultiPlaneTexture *self);

/**
 * cogl_multi_plane_texture_get_plane:
 * @self: a #CoglMultiPlaneTexture
 * @index: the index of the plane
 *
 * Returns the n'th plane of the #CoglMultiPlaneTexture. Note that it is a
 * programming error to use with an index larger than
 * cogl_multi_plane_texture_get_n_planes().
 *
 * Returns: The plane at the given @index.
 */
CoglTexture *   cogl_multi_plane_texture_get_plane    (CoglMultiPlaneTexture *self,
                                                       guint index);

/**
 * cogl_multi_plane_texture_get_planes:
 * @self: a #CoglMultiPlaneTexture
 *
 * Returns all planes of the #CoglMultiPlaneTexture.
 *
 * Returns: (transfer none): The planes of this texture.
 */
CoglTexture **  cogl_multi_plane_texture_get_planes   (CoglMultiPlaneTexture *self);

/**
 * cogl_multi_plane_texture_get_width:
 * @self: a #CoglMultiPlaneTexture
 *
 * Returns the width of the #CoglMultiPlaneTexture. Prefer this over calling
 * cogl_texture_get_width() on one of the textures, as that might give a
 * different size when dealing with subsampling.
 *
 * Returns: The width of the texture.
 */
guint           cogl_multi_plane_texture_get_width    (CoglMultiPlaneTexture *self);

/**
 * cogl_multi_plane_texture_get_height:
 * @self: a #CoglMultiPlaneTexture
 *
 * Returns the height of the #CoglMultiPlaneTexture. Prefer this over calling
 * cogl_texture_get_height() on one of the textures, as that might give a
 * different size when dealing with subsampling.
 *
 * Returns: The height of the texture.
 */
guint           cogl_multi_plane_texture_get_height   (CoglMultiPlaneTexture *self);

/**
 * cogl_multi_plane_texture_create_color_conversion_snippets:
 *
 * Creates a trio of #CoglSnippets that allow you to use this texture inside
 * your pipeline. If no such shader is needed (e.g. because you already have
 * a single-plane RGBA texture), then they will be set to %NULL.
 */
void cogl_multi_plane_texture_create_color_conversion_snippets (CoglMultiPlaneTexture *self,
                                                                CoglSnippet **vertex_snippet_out,
                                                                CoglSnippet **fragment_snippet_out,
                                                                CoglSnippet **layer_snippet_out);

COGL_END_DECLS

#endif
