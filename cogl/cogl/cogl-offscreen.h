/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009,2012 Intel Corporation.
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

#include "cogl/cogl-types.h"
#include "cogl/cogl-texture.h"

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * CoglOffscreen:
 *
 * Functions for creating and manipulating offscreen framebuffers.
 */

/* Offscreen api */

#define COGL_TYPE_OFFSCREEN (cogl_offscreen_get_type ())
COGL_EXPORT
G_DECLARE_FINAL_TYPE (CoglOffscreen, cogl_offscreen,
                      COGL, OFFSCREEN,
                      CoglFramebuffer)

/**
 * cogl_offscreen_new_with_texture:
 * @texture: A #CoglTexture pointer
 *
 * This creates an offscreen framebuffer object using the given
 * @texture as the primary color buffer. It doesn't just initialize
 * the contents of the offscreen buffer with the @texture; they are
 * tightly bound so that drawing to the offscreen buffer effectively
 * updates the contents of the given texture. You don't need to
 * destroy the offscreen buffer before you can use the @texture again.
 *
 * This api only works with low-level #CoglTexture types such as
 * #CoglTexture2D and not with meta-texture types such as
 * #CoglTexture2DSliced.
 *
 * The storage for the framebuffer is actually allocated lazily
 * so this function will never return %NULL to indicate a runtime
 * error. This means it is still possible to configure the framebuffer
 * before it is really allocated.
 *
 * Simple applications without full error handling can simply rely on
 * Cogl to lazily allocate the storage of framebuffers but you should
 * be aware that if Cogl encounters an error (such as running out of
 * GPU memory) then your application will simply abort with an error
 * message. If you need to be able to catch such exceptions at runtime
 * then you can explicitly allocate your framebuffer when you have
 * finished configuring it by calling cogl_framebuffer_allocate() and
 * passing in a #GError argument to catch any exceptions.
 *
 * Return value: (transfer full): a newly instantiated #CoglOffscreen
 *   framebuffer.
 */
COGL_EXPORT CoglOffscreen *
cogl_offscreen_new_with_texture (CoglTexture *texture);

/**
 * cogl_offscreen_get_texture:
 *
 * Returns: (transfer none): a #CoglTexture
 */
COGL_EXPORT CoglTexture *
cogl_offscreen_get_texture (CoglOffscreen *offscreen);

G_END_DECLS
