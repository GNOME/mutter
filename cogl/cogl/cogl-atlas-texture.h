/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2013 Intel Corporation.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 */

#pragma once

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#include "cogl/cogl-context.h"

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * CoglAtlasTexture:
 *
 * Functions for managing textures in Cogl's global
 * set of texture atlases
 *
 * A texture atlas is a texture that contains many smaller images that
 * an application is interested in. These are packed together as a way
 * of optimizing drawing with those images by avoiding the costs of
 * repeatedly telling the hardware to change what texture it should
 * sample from.  This can enable more geometry to be batched together
 * into few draw calls.
 *
 * Each #CoglContext has an shared, pool of texture atlases that are
 * are managed by Cogl.
 *
 * This api lets applications upload texture data into one of Cogl's
 * shared texture atlases using a high-level #CoglAtlasTexture which
 * represents a sub-region of one of these atlases.
 *
 * A #CoglAtlasTexture is a high-level meta texture which has
 * some limitations to be aware of.
 */
#define COGL_TYPE_ATLAS_TEXTURE            (cogl_atlas_texture_get_type ())
#define COGL_ATLAS_TEXTURE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COGL_TYPE_ATLAS_TEXTURE, CoglAtlasTexture))
#define COGL_ATLAS_TEXTURE_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), COGL_TYPE_ATLAS_TEXTURE, CoglAtlasTexture const))
#define COGL_ATLAS_TEXTURE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COGL_TYPE_ATLAS_TEXTURE, CoglAtlasTextureClass))
#define COGL_IS_ATLAS_TEXTURE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COGL_TYPE_ATLAS_TEXTURE))
#define COGL_IS_ATLAS_TEXTURE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COGL_TYPE_ATLAS_TEXTURE))
#define COGL_ATLAS_TEXTURE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COGL_TYPE_ATLAS_TEXTURE, CoglAtlasTextureClass))

typedef struct _CoglAtlasTextureClass CoglAtlasTextureClass;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglAtlasTexture, g_object_unref)

COGL_EXPORT
GType               cogl_atlas_texture_get_type       (void) G_GNUC_CONST;

/**
 * cogl_atlas_texture_new_with_size:
 * @ctx: A #CoglContext
 * @width: The width of your atlased texture.
 * @height: The height of your atlased texture.
 *
 * Creates a #CoglAtlasTexture with a given @width and @height. A
 * #CoglAtlasTexture represents a sub-region within one of Cogl's
 * shared texture atlases.
 *
 * The storage for the texture is not allocated before this function
 * returns. You can call cogl_texture_allocate() to explicitly
 * allocate the underlying storage or let Cogl automatically allocate
 * storage lazily.
 *
 * The texture is still configurable until it has been allocated so
 * for example you can influence the internal format of the texture
 * using cogl_texture_set_components() and
 * cogl_texture_set_premultiplied().
 *
 * Allocate call can fail if Cogl considers the internal
 * format to be incompatible with the format of its internal
 * atlases.
 *
 * The returned #CoglAtlasTexture is a high-level meta-texture
 * with some limitations.
 *
 * Returns: (transfer full): A new #CoglAtlasTexture object.
 */
COGL_EXPORT CoglTexture *
cogl_atlas_texture_new_with_size (CoglContext *ctx,
                                  int width,
                                  int height);

/**
 * cogl_atlas_texture_new_from_bitmap:
 * @bmp: A #CoglBitmap
 *
 * Creates a new #CoglAtlasTexture texture based on data residing in a
 * @bmp. A #CoglAtlasTexture represents a sub-region within one of
 * Cogl's shared texture atlases.
 *
 * The storage for the texture is not allocated before this function
 * returns. You can call cogl_texture_allocate() to explicitly
 * allocate the underlying storage or preferably let Cogl
 * automatically allocate storage lazily when it may know more about
 * how the texture is being used and can optimize how it is allocated.
 *
 * The texture is still configurable until it has been allocated so
 * for example you can influence the internal format of the texture
 * using cogl_texture_set_components() and
 * cogl_texture_set_premultiplied().
 *
 * Allocate call can fail if Cogl considers the internal
 * format to be incompatible with the format of its internal
 * atlases.
 *
 * The returned #CoglAtlasTexture is a high-level meta-texture
 * with some limitations.
 *
 * Returns: (transfer full): A new #CoglAtlasTexture object.
 */
COGL_EXPORT CoglTexture *
cogl_atlas_texture_new_from_bitmap (CoglBitmap *bmp);

/**
 * cogl_atlas_texture_add_reorganize_callback: (skip)
 */
COGL_EXPORT void
cogl_atlas_texture_add_reorganize_callback (CoglContext *ctx,
                                            GHookFunc callback,
                                            void *user_data);

/**
 * cogl_atlas_texture_remove_reorganize_callback: (skip)
 */
COGL_EXPORT void
cogl_atlas_texture_remove_reorganize_callback (CoglContext *ctx,
                                               GHookFunc callback,
                                               void *user_data);

G_END_DECLS
