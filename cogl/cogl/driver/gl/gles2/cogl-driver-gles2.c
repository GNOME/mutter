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

#include "config.h"

#include <string.h>

#include "cogl/driver/gl/gles2/cogl-driver-gles2-private.h"
#include "cogl/driver/gl/gles2/cogl-texture-driver-gles2-private.h"
#include "cogl/cogl-context-private.h"
#include "cogl/cogl-feature-private.h"
#include "cogl/cogl-private.h"
#include "cogl/driver/gl/cogl-texture-gl-private.h"
#include "cogl/driver/gl/cogl-util-gl-private.h"

#ifndef GL_UNSIGNED_INT_24_8
#define GL_UNSIGNED_INT_24_8 0x84FA
#endif
#ifndef GL_DEPTH_STENCIL
#define GL_DEPTH_STENCIL 0x84F9
#endif
#ifndef GL_RG
#define GL_RG 0x8227
#endif
#ifndef GL_RGB8
#define GL_RGB8 0x8051
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_RGB10_A2
#define GL_RGB10_A2 0x8059
#endif
#ifndef GL_UNSIGNED_INT_2_10_10_10_REV
#define GL_UNSIGNED_INT_2_10_10_10_REV 0x8368
#endif
#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#endif
#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif
#ifndef GL_HALF_FLOAT
#define GL_HALF_FLOAT 0x140B
#endif
#ifndef GL_UNSIGNED_INT_2_10_10_10_REV
#define GL_UNSIGNED_INT_2_10_10_10_REV 0x8368
#endif
#ifndef GL_R16
#define GL_R16 0x822A
#endif
#ifndef GL_RG16
#define GL_RG16 0x822C
#endif
#ifndef GL_RED
#define GL_RED 0x1903
#endif
#ifndef GL_RGBA16
#define GL_RGBA16 0x805B
#endif
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_BGRA8
#define GL_BGRA8 0x93A1
#endif
#ifndef GL_RG8
#define GL_RG8 0x822B
#endif

G_DEFINE_FINAL_TYPE (CoglDriverGLES2, cogl_driver_gles2, COGL_TYPE_DRIVER_GL)

static CoglPixelFormat
cogl_driver_gles2_pixel_format_to_gl (CoglDriverGL    *driver,
                                      CoglContext     *context,
                                      CoglPixelFormat  format,
                                      GLenum          *out_glintformat,
                                      GLenum          *out_glformat,
                                      GLenum          *out_gltype)
{
  CoglPixelFormat required_format;
  GLenum glintformat;
  GLenum glformat = 0;
  GLenum gltype;

  required_format = format;

  /* For a pixel format to be used as a framebuffer attachment the corresponding
   * GL internal format must be color-renderable.
   *
   * GLES 3.0:
   * An internal format is color-renderable if it is one of the formats from ta-
   * ble 3.13 noted as color-renderable or if it is unsized format RGBA or RGB
   *
   * Sized formats from table 3.13:
   *   R8, RG8, RGB8, RGB565, RGBA4, RGB5_A1, RGBA8, RGB10_A2, RGB10_A2UI,
   *   SRGB8_ALPHA8, R8I, R8UI, R16I, R16UI, R32I, R32UI, RG8I, RG8UI, RG16I,
   *   RG16UI, RG32I, RG32UI, RGBA8I, RGBA8UI, RGBA16I, RGBA16UI, RGBA32I,
   *   RGBA32UI
   *
   * GLES 2.0:
   * Formats not listed in table 4.5, including compressed internal formats. are
   * not color-, depth-, or stencil-renderable, no matter which components they
   * contain.
   *
   * Sized formats from table 4.5:
   *   RGBA4, RGB5_A1, RGB565
   *
   * More color-renderable formats for glTexImage2D from extensions:
   *
   *   EXT_texture_format_BGRA8888
   *     adds BGRA_EXT as internal and external color-renderable format
   *
   *   EXT_color_buffer_half_float (requires OES_texture_half_float)
   *     adds R16F, RG16F (required EXT_texture_rg) and RGB16F, RGBA16F
   *     as internal color-renderable formats
   *
   * This means we have no way to get sized internal formats for RGB8 and RGBA8
   * in GLES 2.0 and we have to fall back to non-sized internal formats but in
   * practice this should be fine.
   */

  /* For GLES 2 (not GLES 3) the glintformat and glformat have to match:
   *
   * internalformat must match format. No conversion between formats is
   * supported during texture image processing.
   *
   *  GL_INVALID_OPERATION is generated if format does not match internalformat.
   *
   * This means for e.g. COGL_PIXEL_FORMAT_RGBX_8888 we cannot use
   * glintformat=GL_RGB8 with glformat=GL_RGBA. Using glintformat=GL_RGBA8 with
   * glformat=GL_RGBA means the alpha channel won't be ignored and using
   * glintformat=GL_RGB8 with glformat=GL_RGB means the uploading is only
   * expecting 3 channels and not 4.
   */

  /* We try to use the exact matching GL format but if that's not possible
   * because the driver doesn't support it, we fall back to the next best match
   * by calling this function again. This works for all formats which are
   * <= 8 bpc with any R, G, B, A channels because we require RGBA8888.
   */

  /* Find GL equivalents */
  switch (format)
    {
    case COGL_PIXEL_FORMAT_A_8:
      glintformat = GL_ALPHA;
      glformat = GL_ALPHA;
      gltype = GL_UNSIGNED_BYTE;
      break;

    case COGL_PIXEL_FORMAT_R_8:
      glintformat = GL_LUMINANCE;
      glformat = GL_LUMINANCE;
      gltype = GL_UNSIGNED_BYTE;
      break;

    case COGL_PIXEL_FORMAT_RG_88:
      if (cogl_context_has_feature (context, COGL_FEATURE_ID_TEXTURE_RG))
        {
          glintformat = GL_RG8;
          glformat = GL_RG;
          gltype = GL_UNSIGNED_BYTE;
        }
      else
        {
          required_format =
            cogl_driver_gles2_pixel_format_to_gl (driver,
                                                  context,
                                                  COGL_PIXEL_FORMAT_RGB_888,
                                                  &glintformat,
                                                  &glformat,
                                                  &gltype);
        }
      break;

    case COGL_PIXEL_FORMAT_RGB_888:
      if (_cogl_has_private_feature
          (context, COGL_PRIVATE_FEATURE_TEXTURE_FORMAT_SIZED_RGBA))
        glintformat = GL_RGB8;
      else
        glintformat = GL_RGB;

      glformat = GL_RGB;
      gltype = GL_UNSIGNED_BYTE;
      break;

    case COGL_PIXEL_FORMAT_BGR_888:
      required_format =
        cogl_driver_gles2_pixel_format_to_gl (driver,
                                              context,
                                              COGL_PIXEL_FORMAT_RGB_888,
                                              &glintformat,
                                              &glformat,
                                              &gltype);
      break;

    case COGL_PIXEL_FORMAT_R_16:
      if (cogl_context_has_feature (context, COGL_FEATURE_ID_TEXTURE_NORM16))
        {
          glintformat = GL_R16;
          glformat = GL_RED;
          gltype = GL_UNSIGNED_SHORT;
          break;
        }
      else
        {
          g_assert_not_reached ();
        }
      break;

    case COGL_PIXEL_FORMAT_RG_1616:
      if (cogl_context_has_feature (context, COGL_FEATURE_ID_TEXTURE_NORM16))
        {
          /* NORM16 implies RG for GLES */
          g_assert (cogl_context_has_feature (context, COGL_FEATURE_ID_TEXTURE_RG));
          glintformat = GL_RG16;
          glformat = GL_RG;
          gltype = GL_UNSIGNED_SHORT;
          break;
        }
      else
        {
          g_assert_not_reached ();
        }
      break;

    case COGL_PIXEL_FORMAT_RGBA_16161616:
    case COGL_PIXEL_FORMAT_RGBA_16161616_PRE:
      if (cogl_context_has_feature (context, COGL_FEATURE_ID_TEXTURE_NORM16))
        {
          glintformat = GL_RGBA16;
          glformat = GL_RGBA;
          gltype = GL_UNSIGNED_SHORT;
          break;
        }
      else
        {
          g_assert_not_reached ();
        }
      break;

    case COGL_PIXEL_FORMAT_BGRA_8888:
    case COGL_PIXEL_FORMAT_BGRA_8888_PRE:
      if (_cogl_has_private_feature
          (context, COGL_PRIVATE_FEATURE_TEXTURE_FORMAT_BGRA8888))
        {
          /* Using the sized internal format GL_BGRA8 only become possible on
           * 23/06/2024 (https://registry.khronos.org/OpenGL/extensions/EXT/EXT_texture_format_BGRA8888.txt).
           * When support has propagated to more drivers, we should start
           * using GL_BGRA8 again.
           */
          glintformat = GL_BGRA;
          glformat = GL_BGRA;
          gltype = GL_UNSIGNED_BYTE;
        }
      else
        {
          required_format =
            cogl_driver_gles2_pixel_format_to_gl (driver,
                                                  context,
                                                  COGL_PIXEL_FORMAT_RGBA_8888,
                                                  &glintformat,
                                                  &glformat,
                                                  &gltype);
        }
      break;

    case COGL_PIXEL_FORMAT_BGRX_8888:
    case COGL_PIXEL_FORMAT_RGBX_8888:
    case COGL_PIXEL_FORMAT_XRGB_8888:
    case COGL_PIXEL_FORMAT_XBGR_8888:
      required_format =
        cogl_driver_gles2_pixel_format_to_gl (driver,
                                              context,
                                              COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                                              &glintformat,
                                              &glformat,
                                              &gltype);
      break;

    case COGL_PIXEL_FORMAT_ARGB_8888:
    case COGL_PIXEL_FORMAT_ARGB_8888_PRE:
    case COGL_PIXEL_FORMAT_ABGR_8888:
    case COGL_PIXEL_FORMAT_ABGR_8888_PRE:
      required_format =
        cogl_driver_gles2_pixel_format_to_gl (driver,
                                              context,
                                              COGL_PIXEL_FORMAT_RGBA_8888 |
                                              (format & COGL_PREMULT_BIT),
                                              &glintformat,
                                              &glformat,
                                              &gltype);
      break;

    case COGL_PIXEL_FORMAT_RGBA_8888:
    case COGL_PIXEL_FORMAT_RGBA_8888_PRE:
      if (_cogl_has_private_feature
          (context, COGL_PRIVATE_FEATURE_TEXTURE_FORMAT_SIZED_RGBA))
        glintformat = GL_RGBA8;
      else
        glintformat = GL_RGBA;
      glformat = GL_RGBA;
      gltype = GL_UNSIGNED_BYTE;
      break;

      /* The following three types of channel ordering
       * are always defined using system word byte
       * ordering (even according to GLES spec) */
    case COGL_PIXEL_FORMAT_RGB_565:
      glintformat = GL_RGB565;
      glformat = GL_RGB;
      gltype = GL_UNSIGNED_SHORT_5_6_5;
      break;

    case COGL_PIXEL_FORMAT_RGBA_4444:
    case COGL_PIXEL_FORMAT_RGBA_4444_PRE:
      glintformat = GL_RGBA4;
      glformat = GL_RGBA;
      gltype = GL_UNSIGNED_SHORT_4_4_4_4;
      break;

    case COGL_PIXEL_FORMAT_RGBA_5551:
    case COGL_PIXEL_FORMAT_RGBA_5551_PRE:
      glintformat = GL_RGB5_A1;
      glformat = GL_RGBA;
      gltype = GL_UNSIGNED_SHORT_5_5_5_1;
      break;

    case COGL_PIXEL_FORMAT_ABGR_2101010:
    case COGL_PIXEL_FORMAT_ABGR_2101010_PRE:
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      if (cogl_context_has_feature (context, COGL_FEATURE_ID_TEXTURE_RGBA1010102))
        {
          glintformat = GL_RGB10_A2;
          glformat = GL_RGBA;
          gltype = GL_UNSIGNED_INT_2_10_10_10_REV;
          break;
        }
      else
#endif
        {
          g_assert_not_reached ();
        }
      break;

    case COGL_PIXEL_FORMAT_RGBA_1010102:
    case COGL_PIXEL_FORMAT_RGBA_1010102_PRE:
    case COGL_PIXEL_FORMAT_BGRA_1010102:
    case COGL_PIXEL_FORMAT_BGRA_1010102_PRE:
    case COGL_PIXEL_FORMAT_XBGR_2101010:
    case COGL_PIXEL_FORMAT_XRGB_2101010:
    case COGL_PIXEL_FORMAT_ARGB_2101010:
    case COGL_PIXEL_FORMAT_ARGB_2101010_PRE:
      required_format =
        cogl_driver_gles2_pixel_format_to_gl (driver,
                                              context,
                                              COGL_PIXEL_FORMAT_ABGR_2101010 |
                                              (format & COGL_PREMULT_BIT),
                                              &glintformat,
                                              &glformat,
                                              &gltype);
      break;

    case COGL_PIXEL_FORMAT_RGBA_FP_16161616:
    case COGL_PIXEL_FORMAT_RGBA_FP_16161616_PRE:
      if (cogl_context_has_feature (context, COGL_FEATURE_ID_TEXTURE_HALF_FLOAT))
        {
          glintformat = GL_RGBA16F;
          glformat = GL_RGBA;
          gltype = GL_HALF_FLOAT;
        }
      else
        {
          g_assert_not_reached ();
        }
      break;

    case COGL_PIXEL_FORMAT_RGBX_FP_16161616:
    case COGL_PIXEL_FORMAT_BGRX_FP_16161616:
    case COGL_PIXEL_FORMAT_XRGB_FP_16161616:
    case COGL_PIXEL_FORMAT_XBGR_FP_16161616:
      required_format =
        cogl_driver_gles2_pixel_format_to_gl (driver,
                                              context,
                                              COGL_PIXEL_FORMAT_RGBA_FP_16161616_PRE,
                                              &glintformat,
                                              &glformat,
                                              &gltype);
      break;

    case COGL_PIXEL_FORMAT_BGRA_FP_16161616:
    case COGL_PIXEL_FORMAT_BGRA_FP_16161616_PRE:
    case COGL_PIXEL_FORMAT_ARGB_FP_16161616:
    case COGL_PIXEL_FORMAT_ARGB_FP_16161616_PRE:
    case COGL_PIXEL_FORMAT_ABGR_FP_16161616:
    case COGL_PIXEL_FORMAT_ABGR_FP_16161616_PRE:
      required_format =
        cogl_driver_gles2_pixel_format_to_gl (driver,
                                              context,
                                              COGL_PIXEL_FORMAT_RGBA_FP_16161616 |
                                              (format & COGL_PREMULT_BIT),
                                              &glintformat,
                                              &glformat,
                                              &gltype);
      break;

    case COGL_PIXEL_FORMAT_RGBA_FP_32323232:
    case COGL_PIXEL_FORMAT_RGBA_FP_32323232_PRE:
      if (cogl_context_has_feature (context, COGL_FEATURE_ID_TEXTURE_HALF_FLOAT))
        {
          glintformat = GL_RGBA32F;
          glformat = GL_RGBA;
          gltype = GL_FLOAT;
        }
      else
        {
          g_assert_not_reached ();
        }
      break;

    case COGL_PIXEL_FORMAT_DEPTH_16:
      glintformat = GL_DEPTH_COMPONENT;
      glformat = GL_DEPTH_COMPONENT;
      gltype = GL_UNSIGNED_SHORT;
      break;

    case COGL_PIXEL_FORMAT_DEPTH_24_STENCIL_8:
      glintformat = GL_DEPTH_STENCIL;
      glformat = GL_DEPTH_STENCIL;
      gltype = GL_UNSIGNED_INT_24_8;
      break;

    case COGL_PIXEL_FORMAT_ANY:
    case COGL_PIXEL_FORMAT_YUV:
      g_assert_not_reached ();
      break;
    }

  /* All of the pixel formats are handled above so if this hits then
     we've been given an invalid pixel format */
  g_assert (glformat != 0);

  if (out_glintformat != NULL)
    *out_glintformat = glintformat;
  if (out_glformat != NULL)
    *out_glformat = glformat;
  if (out_gltype != NULL)
    *out_gltype = gltype;

  return required_format;
}


static void
cogl_driver_gles2_prep_gl_for_pixels_download (CoglDriverGL *driver,
                                               CoglContext  *ctx,
                                               int           image_width,
                                               int           pixels_rowstride,
                                               int           pixels_bpp)
{
  _cogl_texture_gl_prep_alignment_for_pixels_download (ctx,
                                                       pixels_bpp,
                                                       image_width,
                                                       pixels_rowstride);
}

static gboolean
cogl_driver_gles2_texture_size_supported (CoglDriverGL *driver,
                                          CoglContext  *ctx,
                                          GLenum        gl_target,
                                          GLenum        gl_intformat,
                                          GLenum        gl_format,
                                          GLenum        gl_type,
                                          int           width,
                                          int           height)
{
  GLint max_size;

  /* GLES doesn't support a proxy texture target so let's at least
     check whether the size is greater than GL_MAX_TEXTURE_SIZE */
  GE( ctx, glGetIntegerv (GL_MAX_TEXTURE_SIZE, &max_size) );

  return width <= max_size && height <= max_size;
}

static CoglPixelFormat
cogl_driver_gles2_get_read_pixels_format (CoglDriverGL    *driver,
                                          CoglContext     *context,
                                          CoglPixelFormat  from,
                                          CoglPixelFormat  to,
                                          GLenum          *gl_format_out,
                                          GLenum          *gl_type_out)
{
  CoglPixelFormat required_format = 0;
  GLenum required_gl_format = 0;
  GLenum required_gl_type = 0;
  CoglPixelFormat to_required_format;
  GLenum to_gl_format;
  GLenum to_gl_type;

  switch (from)
    {
    /* fixed point normalized */
    case COGL_PIXEL_FORMAT_A_8:
    case COGL_PIXEL_FORMAT_R_8:
    case COGL_PIXEL_FORMAT_RG_88:
    case COGL_PIXEL_FORMAT_RGB_888:
    case COGL_PIXEL_FORMAT_BGR_888:
    case COGL_PIXEL_FORMAT_BGRA_8888:
    case COGL_PIXEL_FORMAT_BGRA_8888_PRE:
    case COGL_PIXEL_FORMAT_BGRX_8888:
    case COGL_PIXEL_FORMAT_RGBX_8888:
    case COGL_PIXEL_FORMAT_XRGB_8888:
    case COGL_PIXEL_FORMAT_XBGR_8888:
    case COGL_PIXEL_FORMAT_ARGB_8888:
    case COGL_PIXEL_FORMAT_ARGB_8888_PRE:
    case COGL_PIXEL_FORMAT_ABGR_8888:
    case COGL_PIXEL_FORMAT_ABGR_8888_PRE:
    case COGL_PIXEL_FORMAT_RGBA_8888:
    case COGL_PIXEL_FORMAT_RGBA_8888_PRE:
    case COGL_PIXEL_FORMAT_RGB_565:
    case COGL_PIXEL_FORMAT_RGBA_4444:
    case COGL_PIXEL_FORMAT_RGBA_4444_PRE:
    case COGL_PIXEL_FORMAT_RGBA_5551:
    case COGL_PIXEL_FORMAT_RGBA_5551_PRE:
      required_gl_format = GL_RGBA;
      required_gl_type = GL_UNSIGNED_BYTE;
      required_format = COGL_PIXEL_FORMAT_RGBA_8888;
      break;

    /* fixed point normalized, 10bpc special case */
    case COGL_PIXEL_FORMAT_ABGR_2101010:
    case COGL_PIXEL_FORMAT_ABGR_2101010_PRE:
    case COGL_PIXEL_FORMAT_RGBA_1010102:
    case COGL_PIXEL_FORMAT_RGBA_1010102_PRE:
    case COGL_PIXEL_FORMAT_BGRA_1010102:
    case COGL_PIXEL_FORMAT_BGRA_1010102_PRE:
    case COGL_PIXEL_FORMAT_XBGR_2101010:
    case COGL_PIXEL_FORMAT_XRGB_2101010:
    case COGL_PIXEL_FORMAT_ARGB_2101010:
    case COGL_PIXEL_FORMAT_ARGB_2101010_PRE:
      required_gl_format = GL_RGBA;
      required_gl_type = GL_UNSIGNED_INT_2_10_10_10_REV;
      required_format = COGL_PIXEL_FORMAT_ABGR_2101010;
      break;

    /* floating point */
    case COGL_PIXEL_FORMAT_RGBX_FP_16161616:
    case COGL_PIXEL_FORMAT_RGBA_FP_16161616:
    case COGL_PIXEL_FORMAT_RGBA_FP_16161616_PRE:
    case COGL_PIXEL_FORMAT_BGRX_FP_16161616:
    case COGL_PIXEL_FORMAT_BGRA_FP_16161616:
    case COGL_PIXEL_FORMAT_XRGB_FP_16161616:
    case COGL_PIXEL_FORMAT_ARGB_FP_16161616:
    case COGL_PIXEL_FORMAT_XBGR_FP_16161616:
    case COGL_PIXEL_FORMAT_ABGR_FP_16161616:
    case COGL_PIXEL_FORMAT_BGRA_FP_16161616_PRE:
    case COGL_PIXEL_FORMAT_ARGB_FP_16161616_PRE:
    case COGL_PIXEL_FORMAT_ABGR_FP_16161616_PRE:
    case COGL_PIXEL_FORMAT_RGBA_FP_32323232:
    case COGL_PIXEL_FORMAT_RGBA_FP_32323232_PRE:
      required_gl_format = GL_RGBA;
      required_gl_type = GL_FLOAT;
      required_format = COGL_PIXEL_FORMAT_RGBA_FP_32323232;
      break;

    /* fixed point normalized 16bpc */
    case COGL_PIXEL_FORMAT_R_16:
    case COGL_PIXEL_FORMAT_RG_1616:
    case COGL_PIXEL_FORMAT_RGBA_16161616:
    case COGL_PIXEL_FORMAT_RGBA_16161616_PRE:
      required_gl_format = GL_RGBA;
      required_gl_type = GL_UNSIGNED_SHORT;
      required_format = COGL_PIXEL_FORMAT_RGBA_16161616;
      break;

    case COGL_PIXEL_FORMAT_DEPTH_16:
    case COGL_PIXEL_FORMAT_DEPTH_24_STENCIL_8:
    case COGL_PIXEL_FORMAT_ANY:
    case COGL_PIXEL_FORMAT_YUV:
      g_assert_not_reached ();
      break;
    }

  g_assert (required_format != 0);

  to_required_format = cogl_driver_gles2_pixel_format_to_gl (driver,
                                                             context,
                                                             to,
                                                             NULL,
                                                             &to_gl_format,
                                                             &to_gl_type);

  *gl_format_out = required_gl_format;
  *gl_type_out = required_gl_type;

  if (to_required_format != to ||
      to_gl_format != required_gl_format ||
      to_gl_type != required_gl_type)
    return required_format;

  return to_required_format;
}

static gboolean
_cogl_get_gl_version (CoglContext *ctx,
                      int *major_out,
                      int *minor_out)
{
  const char *version_string;

  /* Get the OpenGL version number */
  if ((version_string = _cogl_context_get_gl_version (ctx)) == NULL)
    return FALSE;

  if (!g_str_has_prefix (version_string, "OpenGL ES "))
    return FALSE;

  return _cogl_gl_util_parse_gl_version (version_string + 10,
                                         major_out,
                                         minor_out);
}

static gboolean
check_gl_version (CoglContext  *ctx,
                  GError      **error)
{
  int major, minor;

  if (!_cogl_get_gl_version (ctx, &major, &minor))
    {
      g_set_error (error,
                   COGL_DRIVER_ERROR,
                   COGL_DRIVER_ERROR_UNKNOWN_VERSION,
                   "The GLES version could not be determined");
      return FALSE;
    }

  if (!COGL_CHECK_GL_VERSION (major, minor, 2, 0))
    {
      g_set_error (error,
                   COGL_DRIVER_ERROR,
                   COGL_DRIVER_ERROR_INVALID_VERSION,
                   "OpenGL ES 2.0 or better is required");
      return FALSE;
    }

  return TRUE;
}

static gboolean
_cogl_get_glsl_version (CoglContext *ctx,
                        int         *major_out,
                        int         *minor_out)
{
  const char *version_string;

  version_string = (char *)ctx->glGetString (GL_SHADING_LANGUAGE_VERSION);

  if (!g_str_has_prefix (version_string, "OpenGL ES GLSL ES "))
    return FALSE;

  return _cogl_gl_util_parse_gl_version (version_string + 18,
                                         major_out,
                                         minor_out);
}

static gboolean
check_glsl_version (CoglContext  *ctx,
                    GError      **error)
{
  CoglDriver *driver = cogl_context_get_driver (ctx);
  int driver_major, driver_minor, major, minor;

  if (!_cogl_get_glsl_version (ctx, &major, &minor))
    {
      g_set_error (error,
                   COGL_DRIVER_ERROR,
                   COGL_DRIVER_ERROR_UNKNOWN_VERSION,
                   "The supported GLSL version could not be determined");
      return FALSE;
    }

  cogl_driver_gl_get_glsl_version (COGL_DRIVER_GL (driver),
                                   &driver_major, &driver_minor);
  if (!COGL_CHECK_GL_VERSION (major, minor, driver_major, driver_minor))
    {
      g_set_error (error,
                   COGL_DRIVER_ERROR,
                   COGL_DRIVER_ERROR_INVALID_VERSION,
                   "GLSL ES %d%d0 or better is required",
                   driver_major, driver_minor);
      return FALSE;
    }

  return TRUE;
}

static gboolean
cogl_driver_gles2_update_features (CoglDriver   *driver,
                                   CoglContext  *context,
                                   GError      **error)
{
  unsigned long private_features
    [COGL_FLAGS_N_LONGS_FOR_SIZE (COGL_N_PRIVATE_FEATURES)] = { 0 };
  g_auto (GStrv) gl_extensions = 0;
  int gl_major, gl_minor;
  int i;

  /* We have to special case getting the pointer to the glGetString
     function because we need to use it to determine what functions we
     can expect */
  context->glGetString =
    (void *) cogl_renderer_get_proc_address (context->display->renderer,
                                             "glGetString");

  if (!check_gl_version (context, error))
    return FALSE;

  if (!check_glsl_version (context, error))
    return FALSE;

  gl_extensions = _cogl_context_get_gl_extensions (context);

  if (G_UNLIKELY (COGL_DEBUG_ENABLED (COGL_DEBUG_WINSYS)))
    {
      g_autofree char *all_extensions = g_strjoinv (" ", gl_extensions);

      COGL_NOTE (WINSYS,
                 "Checking features\n"
                 "  GL_VENDOR: %s\n"
                 "  GL_RENDERER: %s\n"
                 "  GL_VERSION: %s\n"
                 "  GL_EXTENSIONS: %s",
                 context->glGetString (GL_VENDOR),
                 context->glGetString (GL_RENDERER),
                 _cogl_context_get_gl_version (context),
                 all_extensions);
    }

  _cogl_get_gl_version (context, &gl_major, &gl_minor);

  _cogl_feature_check_ext_functions (context,
                                     gl_major,
                                     gl_minor,
                                     gl_extensions);

  if (COGL_CHECK_GL_VERSION (gl_major, gl_minor, 3, 0))
    {
      /* unfortunately there is no GLES 2 ext which adds the equivalent */
      COGL_FLAGS_SET (private_features,
                      COGL_PRIVATE_FEATURE_TEXTURE_FORMAT_SIZED_RGBA, TRUE);
    }

  if (_cogl_check_extension ("GL_ANGLE_pack_reverse_row_order", gl_extensions))
    COGL_FLAGS_SET (private_features,
                    COGL_PRIVATE_FEATURE_MESA_PACK_INVERT, TRUE);

  /* Note GLES 2 core doesn't support mipmaps for npot textures or
   * repeat modes other than CLAMP_TO_EDGE. */

  COGL_FLAGS_SET (private_features, COGL_PRIVATE_FEATURE_ANY_GL, TRUE);
  COGL_FLAGS_SET (private_features, COGL_PRIVATE_FEATURE_ALPHA_TEXTURES, TRUE);

  if (context->glGenSamplers)
    COGL_FLAGS_SET (private_features, COGL_PRIVATE_FEATURE_SAMPLER_OBJECTS, TRUE);

  if (context->glBlitFramebuffer)
    COGL_FLAGS_SET (context->features,
                    COGL_FEATURE_ID_BLIT_FRAMEBUFFER, TRUE);

  if (_cogl_check_extension ("GL_OES_element_index_uint", gl_extensions))
    {
      COGL_FLAGS_SET (context->features,
                      COGL_FEATURE_ID_UNSIGNED_INT_INDICES, TRUE);
    }

  if (context->glMapBuffer)
    {
      /* The GL_OES_mapbuffer extension doesn't support mapping for
         read */
      COGL_FLAGS_SET (context->features,
                      COGL_FEATURE_ID_MAP_BUFFER_FOR_WRITE, TRUE);
    }

  if (context->glMapBufferRange)
    {
      /* MapBufferRange in ES3+ does support mapping for read */
      COGL_FLAGS_SET(context->features,
                     COGL_FEATURE_ID_MAP_BUFFER_FOR_WRITE, TRUE);
      COGL_FLAGS_SET(context->features,
                     COGL_FEATURE_ID_MAP_BUFFER_FOR_READ, TRUE);
    }

  if (context->glEGLImageTargetTexture2D)
    COGL_FLAGS_SET (private_features,
                    COGL_PRIVATE_FEATURE_TEXTURE_2D_FROM_EGL_IMAGE, TRUE);

  if (_cogl_check_extension ("GL_OES_packed_depth_stencil", gl_extensions))
    COGL_FLAGS_SET (private_features,
                    COGL_PRIVATE_FEATURE_OES_PACKED_DEPTH_STENCIL, TRUE);

  if (_cogl_check_extension ("GL_EXT_texture_format_BGRA8888", gl_extensions))
    COGL_FLAGS_SET (private_features,
                    COGL_PRIVATE_FEATURE_TEXTURE_FORMAT_BGRA8888, TRUE);

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  if (COGL_CHECK_GL_VERSION (gl_major, gl_minor, 3, 0))
    COGL_FLAGS_SET (context->features,
                    COGL_FEATURE_ID_TEXTURE_RGBA1010102, TRUE);
#endif

  if (COGL_CHECK_GL_VERSION (gl_major, gl_minor, 3, 2) ||
      (COGL_CHECK_GL_VERSION (gl_major, gl_minor, 3, 0) &&
       _cogl_check_extension ("GL_OES_texture_half_float", gl_extensions) &&
       _cogl_check_extension ("GL_EXT_color_buffer_half_float", gl_extensions)))
    COGL_FLAGS_SET (context->features,
                    COGL_FEATURE_ID_TEXTURE_HALF_FLOAT, TRUE);

  if (_cogl_check_extension ("GL_EXT_unpack_subimage", gl_extensions))
    COGL_FLAGS_SET (private_features,
                    COGL_PRIVATE_FEATURE_UNPACK_SUBIMAGE, TRUE);

  /* A nameless vendor implemented the extension, but got the case wrong
   * per the spec. */
  if (_cogl_check_extension ("GL_OES_EGL_sync", gl_extensions) ||
      _cogl_check_extension ("GL_OES_egl_sync", gl_extensions))
    COGL_FLAGS_SET (private_features, COGL_PRIVATE_FEATURE_OES_EGL_SYNC, TRUE);

#ifdef GL_ARB_sync
  if (context->glFenceSync)
    COGL_FLAGS_SET (context->features, COGL_FEATURE_ID_FENCE, TRUE);
#endif

  if (COGL_CHECK_GL_VERSION (gl_major, gl_minor, 3, 0) ||
      _cogl_check_extension ("GL_EXT_texture_rg", gl_extensions))
    COGL_FLAGS_SET (context->features,
                    COGL_FEATURE_ID_TEXTURE_RG,
                    TRUE);

  if (_cogl_check_extension ("GL_EXT_texture_lod_bias", gl_extensions))
    {
      COGL_FLAGS_SET (private_features,
                      COGL_PRIVATE_FEATURE_TEXTURE_LOD_BIAS, TRUE);
    }

  if (context->glGenQueries && context->glQueryCounter && context->glGetInteger64v)
    COGL_FLAGS_SET (context->features, COGL_FEATURE_ID_TIMESTAMP_QUERY, TRUE);

  if (!g_strcmp0 ((char *) context->glGetString (GL_RENDERER), "Mali-400 MP"))
    {
      COGL_FLAGS_SET (private_features,
                      COGL_PRIVATE_QUIRK_GENERATE_MIPMAP_NEEDS_FLUSH,
                      TRUE);
    }

  if (COGL_CHECK_GL_VERSION (gl_major, gl_minor, 3, 1) &&
      _cogl_check_extension ("GL_EXT_texture_norm16", gl_extensions))
    COGL_FLAGS_SET (context->features,
                    COGL_FEATURE_ID_TEXTURE_NORM16,
                    TRUE);

  /* Cache features */
  for (i = 0; i < G_N_ELEMENTS (private_features); i++)
    context->private_features[i] |= private_features[i];

  return TRUE;
}

static gboolean
cogl_driver_gles2_format_supports_upload (CoglDriver      *driver,
                                          CoglContext     *ctx,
                                          CoglPixelFormat  format)
{
  switch (format)
    {
    case COGL_PIXEL_FORMAT_A_8:
    case COGL_PIXEL_FORMAT_R_8:
    case COGL_PIXEL_FORMAT_RG_88:
      return TRUE;
    case COGL_PIXEL_FORMAT_BGRX_8888:
    case COGL_PIXEL_FORMAT_BGRA_8888:
    case COGL_PIXEL_FORMAT_BGRA_8888_PRE:
    case COGL_PIXEL_FORMAT_RGB_888:
    case COGL_PIXEL_FORMAT_BGR_888:
      return TRUE;
    case COGL_PIXEL_FORMAT_RGBA_1010102:
    case COGL_PIXEL_FORMAT_RGBA_1010102_PRE:
    case COGL_PIXEL_FORMAT_BGRA_1010102:
    case COGL_PIXEL_FORMAT_BGRA_1010102_PRE:
    case COGL_PIXEL_FORMAT_XBGR_2101010:
    case COGL_PIXEL_FORMAT_ABGR_2101010:
    case COGL_PIXEL_FORMAT_ABGR_2101010_PRE:
    case COGL_PIXEL_FORMAT_XRGB_2101010:
    case COGL_PIXEL_FORMAT_ARGB_2101010:
    case COGL_PIXEL_FORMAT_ARGB_2101010_PRE:
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      if (cogl_context_has_feature (ctx,  COGL_FEATURE_ID_TEXTURE_RGBA1010102))
        return TRUE;
      else
        return FALSE;
#else
      return FALSE;
#endif
    case COGL_PIXEL_FORMAT_RGBX_8888:
    case COGL_PIXEL_FORMAT_RGBA_8888:
    case COGL_PIXEL_FORMAT_RGBA_8888_PRE:
    case COGL_PIXEL_FORMAT_XRGB_8888:
    case COGL_PIXEL_FORMAT_ARGB_8888:
    case COGL_PIXEL_FORMAT_ARGB_8888_PRE:
    case COGL_PIXEL_FORMAT_XBGR_8888:
    case COGL_PIXEL_FORMAT_ABGR_8888:
    case COGL_PIXEL_FORMAT_ABGR_8888_PRE:
    case COGL_PIXEL_FORMAT_RGB_565:
    case COGL_PIXEL_FORMAT_RGBA_4444:
    case COGL_PIXEL_FORMAT_RGBA_4444_PRE:
    case COGL_PIXEL_FORMAT_RGBA_5551:
    case COGL_PIXEL_FORMAT_RGBA_5551_PRE:
      return TRUE;
    case COGL_PIXEL_FORMAT_BGRX_FP_16161616:
    case COGL_PIXEL_FORMAT_BGRA_FP_16161616:
    case COGL_PIXEL_FORMAT_XRGB_FP_16161616:
    case COGL_PIXEL_FORMAT_ARGB_FP_16161616:
    case COGL_PIXEL_FORMAT_XBGR_FP_16161616:
    case COGL_PIXEL_FORMAT_ABGR_FP_16161616:
    case COGL_PIXEL_FORMAT_BGRA_FP_16161616_PRE:
    case COGL_PIXEL_FORMAT_ARGB_FP_16161616_PRE:
    case COGL_PIXEL_FORMAT_ABGR_FP_16161616_PRE:
      return FALSE;
    case COGL_PIXEL_FORMAT_RGBX_FP_16161616:
    case COGL_PIXEL_FORMAT_RGBA_FP_16161616:
    case COGL_PIXEL_FORMAT_RGBA_FP_16161616_PRE:
    case COGL_PIXEL_FORMAT_RGBA_FP_32323232:
    case COGL_PIXEL_FORMAT_RGBA_FP_32323232_PRE:
      if (cogl_context_has_feature (ctx, COGL_FEATURE_ID_TEXTURE_HALF_FLOAT))
        return TRUE;
      else
        return FALSE;
    case COGL_PIXEL_FORMAT_R_16:
    case COGL_PIXEL_FORMAT_RG_1616:
    case COGL_PIXEL_FORMAT_RGBA_16161616:
    case COGL_PIXEL_FORMAT_RGBA_16161616_PRE:
      if (cogl_context_has_feature (ctx, COGL_FEATURE_ID_TEXTURE_NORM16))
        return TRUE;
      else
        return FALSE;
    case COGL_PIXEL_FORMAT_DEPTH_16:
    case COGL_PIXEL_FORMAT_DEPTH_24_STENCIL_8:
    case COGL_PIXEL_FORMAT_ANY:
    case COGL_PIXEL_FORMAT_YUV:
      g_assert_not_reached ();
      return FALSE;
    }

  g_assert_not_reached ();
  return FALSE;
}

static CoglTextureDriver *
cogl_driver_gles2_create_texture_driver (CoglDriver *driver)
{
  return g_object_new (COGL_TYPE_TEXTURE_DRIVER_GLES2,
                       "driver", driver,
                       NULL);
}

static void
cogl_driver_gles2_class_init (CoglDriverGLES2Class *klass)
{
  CoglDriverClass *driver_klass = COGL_DRIVER_CLASS (klass);
  CoglDriverGLClass *driver_gl_klass = COGL_DRIVER_GL_CLASS (klass);

  driver_klass->update_features = cogl_driver_gles2_update_features;
  driver_klass->format_supports_upload = cogl_driver_gles2_format_supports_upload;
  driver_klass->create_texture_driver = cogl_driver_gles2_create_texture_driver;

  driver_gl_klass->get_read_pixels_format = cogl_driver_gles2_get_read_pixels_format;
  driver_gl_klass->pixel_format_to_gl = cogl_driver_gles2_pixel_format_to_gl;
  driver_gl_klass->prep_gl_for_pixels_download = cogl_driver_gles2_prep_gl_for_pixels_download;
  driver_gl_klass->texture_size_supported = cogl_driver_gles2_texture_size_supported;
}

static void
cogl_driver_gles2_init (CoglDriverGLES2 *driver)
{
  CoglDriverGLPrivate *priv =
    cogl_driver_gl_get_private (COGL_DRIVER_GL (driver));

  priv->glsl_major = 1;
  priv->glsl_minor = 0;
  priv->glsl_es = TRUE;
}
