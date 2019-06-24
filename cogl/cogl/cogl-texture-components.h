/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009,2010 Intel Corporation.
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

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#ifndef __COGL_TEXTURE_COMPONENTS_H__
#define __COGL_TEXTURE_COMPONENTS_H__

#include <cogl/cogl-types.h>
#include <cogl/cogl-macros.h>
#include <cogl/cogl-defines.h>

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * SECTION:cogl-texture-components
 * @short_description: Functions for creating and manipulating textures
 *
 * CoglTextureComponents can be used to specify what components of a
 * #CoglTexture can be used for sampling later. This affects how data is
 * uploaded to the GPU.
 */

/**
 * CoglTextureComponents:
 * @COGL_TEXTURE_COMPONENTS_A: Only the alpha component
 * @COGL_TEXTURE_COMPONENTS_RG: Red and green components. Note that
 *   this can only be used if the %COGL_FEATURE_ID_TEXTURE_RG feature
 *   is advertised.
 * @COGL_TEXTURE_COMPONENTS_RGB: Red, green and blue components
 * @COGL_TEXTURE_COMPONENTS_RGBA: Red, green, blue and alpha components
 * @COGL_TEXTURE_COMPONENTS_DEPTH: Only a depth component
 *
 * See cogl_texture_set_components().
 *
 * Since: 1.18
 */
typedef enum _CoglTextureComponents
{
  COGL_TEXTURE_COMPONENTS_A = 1,
  COGL_TEXTURE_COMPONENTS_RG,
  COGL_TEXTURE_COMPONENTS_RGB,
  COGL_TEXTURE_COMPONENTS_RGBA,
  COGL_TEXTURE_COMPONENTS_DEPTH
} CoglTextureComponents;

G_END_DECLS

#endif /* __COGL_TEXTURE_COMPONENTS_H__ */
