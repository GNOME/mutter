/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009 Intel Corporation.
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

#include <glib-object.h>

#include "cogl/cogl-gl-header.h"
#include "cogl/cogl-pixel-format.h"
#include "cogl/cogl-types.h"

G_DECLARE_DERIVABLE_TYPE (CoglTextureDriver,
                          cogl_texture_driver,
                          COGL,
                          TEXTURE_DRIVER,
                          GObject)

#define COGL_TYPE_TEXTURE_DRIVER (cogl_texture_driver_get_type ())

struct _CoglTextureDriverClass
{
  GObjectClass parent_class;

  /*
   * A very small wrapper around glGenTextures() that ensures we default to
   * non-mipmap filters when creating textures. This is to save some memory as
   * the driver will not allocate room for the mipmap tree.
   */
  GLuint (* gen) (CoglTextureDriver *driver,
                  CoglContext       *ctx,
                  GLenum             gl_target,
                  CoglPixelFormat    internal_format);

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
  gboolean (* upload_subregion_to_gl) (CoglTextureDriver *driver,
                                       CoglContext       *ctx,
                                       CoglTexture       *texture,
                                       int                src_x,
                                       int                src_y,
                                       int                dst_x,
                                       int                dst_y,
                                       int                width,
                                       int                height,
                                       int                level,
                                       CoglBitmap        *source_bmp,
                                       GLuint             source_gl_format,
                                       GLuint             source_gl_type,
                                       GError           **error);

  /*
   * Replaces the contents of the GL texture with the entire bitmap. On
   * GL this just directly calls glTexImage2D, but under GLES it needs
   * to copy the bitmap if the rowstride is not a multiple of a possible
   * alignment value because there is no GL_UNPACK_ROW_LENGTH
   */
  gboolean (* upload_to_gl) (CoglTextureDriver *driver,
                             CoglContext       *ctx,
                             GLenum             gl_target,
                             GLuint             gl_handle,
                             CoglBitmap        *source_bmp,
                             GLint              internal_gl_format,
                             GLuint             source_gl_format,
                             GLuint             source_gl_type,
                             GError           **error);

  /*
   * This sets up the glPixelStore state for an download to a destination with
   * the same size, and with no offset.
   */
  /* NB: GLES can't download pixel data into a sub region of a larger
   * destination buffer, the GL driver has a more flexible version of
   * this function that it uses internally. */
  void (* prep_gl_for_pixels_download) (CoglTextureDriver *driver,
                                        CoglContext       *ctx,
                                        int                image_width,
                                        int                pixels_rowstride,
                                        int                pixels_bpp);

  /*
   * This driver abstraction is needed because GLES doesn't support
   * glGetTexImage (). On GLES this currently just returns FALSE which
   * will lead to a generic fallback path being used that simply
   * renders the texture and reads it back from the framebuffer. (See
   * _cogl_texture_draw_and_read () )
   */
  gboolean (* gl_get_tex_image) (CoglTextureDriver *driver,
                                 CoglContext       *ctx,
                                 GLenum             gl_target,
                                 GLenum             dest_gl_format,
                                 GLenum             dest_gl_type,
                                 uint8_t           *dest);

  /*
   * It may depend on the driver as to what texture sizes are supported...
   */
  gboolean (* size_supported) (CoglTextureDriver *driver,
                               CoglContext       *ctx,
                               GLenum             gl_target,
                               GLenum             gl_intformat,
                               GLenum             gl_format,
                               GLenum             gl_type,
                               int                width,
                               int                height);


  gboolean (* format_supports_upload) (CoglTextureDriver *driver,
                                       CoglContext       *ctx,
                                       CoglPixelFormat    format);

  /*
   * The driver may impose constraints on what formats can be used to store
   * texture data read from textures. For example GLES currently only supports
   * RGBA_8888, and so we need to manually convert the data if the final
   * destination has another format.
   */
  CoglPixelFormat (* find_best_gl_get_data_format) (CoglTextureDriver *driver,
                                                    CoglContext       *context,
                                                    CoglPixelFormat    format,
                                                    GLenum            *closest_gl_format,
                                                    GLenum            *closest_gl_type);


  /* Destroys any driver specific resources associated with the given
  * 2D texture. */
  void (* texture_2d_free) (CoglTextureDriver *driver,
                            CoglTexture2D     *tex_2d);

  /* Returns TRUE if the driver can support creating a 2D texture with
  * the given geometry and specified internal format.
  */
  gboolean (* texture_2d_can_create) (CoglTextureDriver *driver,
                                      CoglContext       *ctx,
                                      int                width,
                                      int                height,
                                      CoglPixelFormat    internal_format);

  /* Initializes driver private state before allocating any specific
  * storage for a 2D texture, where base texture and texture 2D
  * members will already be initialized before passing control to
  * the driver.
  */
  void (* texture_2d_init) (CoglTextureDriver *driver,
                            CoglTexture2D     *tex_2d);

  /* Allocates (uninitialized) storage for the given texture according
  * to the configured size and format of the texture */
  gboolean (* texture_2d_allocate) (CoglTextureDriver *driver,
                                    CoglTexture       *tex,
                                    GError           **error);

  /* Initialize the specified region of storage of the given texture
  * with the contents of the specified framebuffer region
  */
  void (* texture_2d_copy_from_framebuffer) (CoglTextureDriver *driver,
                                             CoglTexture2D     *tex_2d,
                                             int                src_x,
                                             int                src_y,
                                             int                width,
                                             int                height,
                                             CoglFramebuffer   *src_fb,
                                             int                dst_x,
                                             int                dst_y,
                                             int                level);

  /* If the given texture has a corresponding OpenGL texture handle
  * then return that.
  *
  * This is optional
  */
  unsigned int (* texture_2d_get_gl_handle) (CoglTextureDriver *driver,
                                             CoglTexture2D     *tex_2d);

  /* Update all mipmap levels > 0 */
  void (* texture_2d_generate_mipmap) (CoglTextureDriver *driver,
                                       CoglTexture2D     *tex_2d);

  /* Initialize the specified region of storage of the given texture
  * with the contents of the specified bitmap region
  *
  * Since this may need to create the underlying storage first
  * it may throw a NO_MEMORY error.
  */
  gboolean (* texture_2d_copy_from_bitmap) (CoglTextureDriver *driver,
                                            CoglTexture2D     *tex_2d,
                                            int                src_x,
                                            int                src_y,
                                            int                width,
                                            int                height,
                                            CoglBitmap        *bitmap,
                                            int                dst_x,
                                            int                dst_y,
                                            int                level,
                                            GError           **error);

  gboolean (* texture_2d_is_get_data_supported) (CoglTextureDriver *driver,
                                                 CoglTexture2D     *tex_2d);

  /* Reads back the full contents of the given texture and write it to
  * @data in the given @format and with the given @rowstride.
  *
  * This is optional
  */
  void (* texture_2d_get_data) (CoglTextureDriver *driver,
                                CoglTexture2D     *tex_2d,
                                CoglPixelFormat    format,
                                int                rowstride,
                                uint8_t           *data);
};
