/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2024 Red Hat.
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
 */

#pragma once

#include "cogl/cogl-gl-header.h"
#include "cogl/cogl-texture-driver.h"

G_DECLARE_DERIVABLE_TYPE (CoglTextureDriverGL,
                          cogl_texture_driver_gl,
                          COGL,
                          TEXTURE_DRIVER_GL,
                          CoglTextureDriver)

struct _CoglTextureDriverGLClass
{
  CoglTextureDriverClass parent_class;

  /*
   * A very small wrapper around glGenTextures() that ensures we default to
   * non-mipmap filters when creating textures. This is to save some memory as
   * the driver will not allocate room for the mipmap tree.
   */
  GLuint (* gen) (CoglTextureDriverGL *driver,
                  CoglContext         *ctx,
                  GLenum               gl_target,
                  CoglPixelFormat      internal_format);

  /*
   * This uploads a sub-region from source_bmp to a single GL texture
   * handle (i.e a single CoglTexture slice)
   *
   * It also updates the array of tex->first_pixels[slice_index] if
   * dst_{x,y} == 0
   *
   * The driver abstraction is in place because GLES doesn't support the pixel
   * store options required to source from a subregion, so for GLES we have
   * to manually create a transient source bitmap.
   *
   * XXX: sorry for the ridiculous number of arguments :-(
   */
  gboolean (* upload_subregion_to_gl) (CoglTextureDriverGL *driver,
                                       CoglContext         *ctx,
                                       CoglTexture         *texture,
                                       int                  src_x,
                                       int                  src_y,
                                       int                  dst_x,
                                       int                  dst_y,
                                       int                  width,
                                       int                  height,
                                       int                  level,
                                       CoglBitmap          *source_bmp,
                                       GLuint               source_gl_format,
                                       GLuint               source_gl_type,
                                       GError             **error);

  /*
   * Replaces the contents of the GL texture with the entire bitmap. On
   * GL this just directly calls glTexImage2D, but under GLES it needs
   * to copy the bitmap if the rowstride is not a multiple of a possible
   * alignment value because there is no GL_UNPACK_ROW_LENGTH
   */
  gboolean (* upload_to_gl) (CoglTextureDriverGL *driver,
                             CoglContext         *ctx,
                             GLenum               gl_target,
                             GLuint               gl_handle,
                             CoglBitmap          *source_bmp,
                             GLint                internal_gl_format,
                             GLuint               source_gl_format,
                             GLuint               source_gl_type,
                             GError             **error);

  /*
   * This driver abstraction is needed because GLES doesn't support
   * glGetTexImage (). On GLES this currently just returns FALSE which
   * will lead to a generic fallback path being used that simply
   * renders the texture and reads it back from the framebuffer. (See
   * _cogl_texture_draw_and_read () )
   */
  gboolean (* gl_get_tex_image) (CoglTextureDriverGL *driver,
                                 CoglContext         *ctx,
                                 GLenum               gl_target,
                                 GLenum               dest_gl_format,
                                 GLenum               dest_gl_type,
                                 uint8_t             *dest);

  /*
   * The driver may impose constraints on what formats can be used to store
   * texture data read from textures. For example GLES currently only supports
   * RGBA_8888, and so we need to manually convert the data if the final
   * destination has another format.
   */
  CoglPixelFormat (* find_best_gl_get_data_format) (CoglTextureDriverGL *driver,
                                                    CoglContext         *context,
                                                    CoglPixelFormat      format,
                                                    GLenum              *closest_gl_format,
                                                    GLenum              *closest_gl_type);
};

#define COGL_TYPE_TEXTURE_DRIVER_GL (cogl_texture_driver_gl_get_type ())

/* GL and GLES3 have this by default, but GLES2 does not except via extension.
 * So really it's probably always available. Even if we used it and it wasn't
 * available in some driver then there are no adverse consequences to the
 * command simply being ignored...
 */
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif
