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

#include "cogl/driver/gl/gl3/cogl-driver-gl3-private.h"
#include "cogl/driver/gl/gl3/cogl-texture-driver-gl3-private.h"
#include "cogl/cogl-private.h"
#include "cogl/cogl-feature-private.h"
#include "cogl/driver/gl/cogl-util-gl-private.h"
#include "cogl/driver/gl/cogl-texture-2d-gl-private.h"
#include "cogl/driver/gl/cogl-texture-gl-private.h"

G_DEFINE_FINAL_TYPE (CoglDriverGL3, cogl_driver_gl3, COGL_TYPE_DRIVER_GL);

static gboolean
cogl_driver_gl3_context_init (CoglDriver  *driver,
                              CoglContext *context)
{
  GLuint vertex_array;

  COGL_DRIVER_CLASS (cogl_driver_gl3_parent_class)->context_init (driver, context);

  /* In a forward compatible context, GL 3 doesn't support rendering
   * using the default vertex array object. Cogl doesn't use vertex
   * array objects yet so for now we just create a dummy array
   * object that we will use as our own default object. Eventually
   * it could be good to attach the vertex array objects to
   * CoglPrimitives */
  context->glGenVertexArrays (1, &vertex_array);
  context->glBindVertexArray (vertex_array);

  /* There's no enable for this in GLES2, it's always on */
  GE (context, glEnable (GL_PROGRAM_POINT_SIZE) );

  return TRUE;
}

static CoglPixelFormat
cogl_driver_gl3_pixel_format_to_gl (CoglDriverGL    *driver,
                                    CoglContext     *context,
                                    CoglPixelFormat  format,
                                    GLenum          *out_glintformat,
                                    GLenum          *out_glformat,
                                    GLenum          *out_gltype)
{
  CoglPixelFormat required_format;
  GLenum glintformat = 0;
  GLenum glformat = 0;
  GLenum gltype = 0;

  required_format = format;

  /* For a pixel format to be used as a framebuffer attachment the corresponding
   * GL internal format must be color-renderable.
   *
   * GL core 3.1
   * The following base internal formats from table 3.11 are color-renderable:
   * RED, RG, RGB, and RGBA. The sized internal formats from table 3.12 that
   * have a color-renderable base internal format are also color-renderable. No
   * other formats, including compressed internal formats, are color-renderable.
   *
   * All sized formats from table 3.12 have a color-renderable base internal
   * format and are therefore color-renderable.
   *
   * Only a subset of those formats are required to be supported as
   * color-renderable (3.8.1 Required Texture Formats). Notably absent from the
   * required renderbuffer color formats are RGB8, RGB16F and GL_RGB10. They are
   * required to be supported as texture-renderable though, so using those
   * internal formats is okay but allocating a framebuffer with those formats
   * might fail.
   */

  /* Find GL equivalents */
  switch (format)
    {
    case COGL_PIXEL_FORMAT_A_8:
      /* The driver doesn't natively support alpha textures so we
       * will use a red component texture with a swizzle to implement
       * the texture */
      glintformat = GL_R8;
      glformat = GL_RED;
      gltype = GL_UNSIGNED_BYTE;
      break;
    case COGL_PIXEL_FORMAT_R_8:
      glintformat = GL_R8;
      glformat = GL_RED;
      gltype = GL_UNSIGNED_BYTE;
      break;

    case COGL_PIXEL_FORMAT_RG_88:
      glintformat = GL_RG8;
      glformat = GL_RG;
      gltype = GL_UNSIGNED_BYTE;
      break;

    case COGL_PIXEL_FORMAT_RGB_888:
      glintformat = GL_RGB8;
      glformat = GL_RGB;
      gltype = GL_UNSIGNED_BYTE;
      break;
    case COGL_PIXEL_FORMAT_BGR_888:
      glintformat = GL_RGB8;
      glformat = GL_BGR;
      gltype = GL_UNSIGNED_BYTE;
      break;
    case COGL_PIXEL_FORMAT_RGBX_8888:
      glintformat = GL_RGB8;
      glformat = GL_RGBA;
      gltype = GL_UNSIGNED_BYTE;
      break;
    case COGL_PIXEL_FORMAT_RGBA_8888:
    case COGL_PIXEL_FORMAT_RGBA_8888_PRE:
      glintformat = GL_RGBA8;
      glformat = GL_RGBA;
      gltype = GL_UNSIGNED_BYTE;
      break;
    case COGL_PIXEL_FORMAT_BGRX_8888:
      glintformat = GL_RGB8;
      glformat = GL_BGRA;
      gltype = GL_UNSIGNED_BYTE;
      break;
    case COGL_PIXEL_FORMAT_BGRA_8888:
    case COGL_PIXEL_FORMAT_BGRA_8888_PRE:
      glintformat = GL_RGBA8;
      glformat = GL_BGRA;
      gltype = GL_UNSIGNED_BYTE;
      break;

      /* The following two types of channel ordering
       * have no GL equivalent unless defined using
       * system word byte ordering */
    case COGL_PIXEL_FORMAT_XRGB_8888:
      glintformat = GL_RGB8;
      glformat = GL_BGRA;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      gltype = GL_UNSIGNED_INT_8_8_8_8;
#else
      gltype = GL_UNSIGNED_INT_8_8_8_8_REV;
#endif
      break;
    case COGL_PIXEL_FORMAT_ARGB_8888:
    case COGL_PIXEL_FORMAT_ARGB_8888_PRE:
      glintformat = GL_RGBA8;
      glformat = GL_BGRA;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      gltype = GL_UNSIGNED_INT_8_8_8_8;
#else
      gltype = GL_UNSIGNED_INT_8_8_8_8_REV;
#endif
      break;

    case COGL_PIXEL_FORMAT_XBGR_8888:
      glintformat = GL_RGB8;
      glformat = GL_RGBA;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      gltype = GL_UNSIGNED_INT_8_8_8_8;
#else
      gltype = GL_UNSIGNED_INT_8_8_8_8_REV;
#endif
      break;
    case COGL_PIXEL_FORMAT_ABGR_8888:
    case COGL_PIXEL_FORMAT_ABGR_8888_PRE:
      glintformat = GL_RGBA8;
      glformat = GL_RGBA;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      gltype = GL_UNSIGNED_INT_8_8_8_8;
#else
      gltype = GL_UNSIGNED_INT_8_8_8_8_REV;
#endif
      break;

    case COGL_PIXEL_FORMAT_RGBA_1010102:
    case COGL_PIXEL_FORMAT_RGBA_1010102_PRE:
      glintformat = GL_RGB10_A2;
      glformat = GL_RGBA;
      gltype = GL_UNSIGNED_INT_10_10_10_2;
      break;

    case COGL_PIXEL_FORMAT_BGRA_1010102:
    case COGL_PIXEL_FORMAT_BGRA_1010102_PRE:
      glintformat = GL_RGB10_A2;
      glformat = GL_BGRA;
      gltype = GL_UNSIGNED_INT_10_10_10_2;
      break;

    case COGL_PIXEL_FORMAT_XBGR_2101010:
      glintformat = GL_RGB10;
      glformat = GL_RGBA;
      gltype = GL_UNSIGNED_INT_2_10_10_10_REV;
      break;
    case COGL_PIXEL_FORMAT_ABGR_2101010:
    case COGL_PIXEL_FORMAT_ABGR_2101010_PRE:
      glintformat = GL_RGB10_A2;
      glformat = GL_RGBA;
      gltype = GL_UNSIGNED_INT_2_10_10_10_REV;
      break;

    case COGL_PIXEL_FORMAT_XRGB_2101010:
      glintformat = GL_RGB10;
      glformat = GL_BGRA;
      gltype = GL_UNSIGNED_INT_2_10_10_10_REV;
      break;
    case COGL_PIXEL_FORMAT_ARGB_2101010:
    case COGL_PIXEL_FORMAT_ARGB_2101010_PRE:
      glintformat = GL_RGB10_A2;
      glformat = GL_BGRA;
      gltype = GL_UNSIGNED_INT_2_10_10_10_REV;
      break;

      /* The following three types of channel ordering
       * are always defined using system word byte
       * ordering (even according to GLES spec) */
    case COGL_PIXEL_FORMAT_RGB_565:
      glintformat = GL_RGB;
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

    case COGL_PIXEL_FORMAT_RGBX_FP_16161616:
      glintformat = GL_RGB16F;
      glformat = GL_RGBA;
      gltype = GL_HALF_FLOAT;
      break;
    case COGL_PIXEL_FORMAT_RGBA_FP_16161616:
    case COGL_PIXEL_FORMAT_RGBA_FP_16161616_PRE:
      glintformat = GL_RGBA16F;
      glformat = GL_RGBA;
      gltype = GL_HALF_FLOAT;
      break;
    case COGL_PIXEL_FORMAT_BGRX_FP_16161616:
      glintformat = GL_RGB16F;
      glformat = GL_BGRA;
      gltype = GL_HALF_FLOAT;
      break;
    case COGL_PIXEL_FORMAT_BGRA_FP_16161616:
    case COGL_PIXEL_FORMAT_BGRA_FP_16161616_PRE:
      glintformat = GL_RGBA16F;
      glformat = GL_BGRA;
      gltype = GL_HALF_FLOAT;
      break;
    case COGL_PIXEL_FORMAT_ARGB_FP_16161616:
    case COGL_PIXEL_FORMAT_ARGB_FP_16161616_PRE:
    case COGL_PIXEL_FORMAT_ABGR_FP_16161616:
    case COGL_PIXEL_FORMAT_ABGR_FP_16161616_PRE:
      required_format =
        cogl_driver_gl3_pixel_format_to_gl (driver,
                                            context,
                                            COGL_PIXEL_FORMAT_RGBA_FP_16161616 |
                                            (format & COGL_PREMULT_BIT),
                                            &glintformat,
                                            &glformat,
                                            &gltype);
      break;
    case COGL_PIXEL_FORMAT_XRGB_FP_16161616:
    case COGL_PIXEL_FORMAT_XBGR_FP_16161616:
      required_format =
        cogl_driver_gl3_pixel_format_to_gl (driver,
                                            context,
                                            COGL_PIXEL_FORMAT_RGBX_FP_16161616,
                                            &glintformat,
                                            &glformat,
                                            &gltype);
      break;

    case COGL_PIXEL_FORMAT_RGBA_FP_32323232:
    case COGL_PIXEL_FORMAT_RGBA_FP_32323232_PRE:
      glintformat = GL_RGBA32F;
      glformat = GL_RGBA;
      gltype = GL_FLOAT;
      break;

    case COGL_PIXEL_FORMAT_R_16:
      glintformat = GL_R16;
      glformat = GL_RED;
      gltype = GL_UNSIGNED_SHORT;
      break;
    case COGL_PIXEL_FORMAT_RG_1616:
      glintformat = GL_RG16;
      glformat = GL_RG;
      gltype = GL_UNSIGNED_SHORT;
      break;
    case COGL_PIXEL_FORMAT_RGBA_16161616:
    case COGL_PIXEL_FORMAT_RGBA_16161616_PRE:
      glintformat = GL_RGBA16;
      glformat = GL_RGBA;
      gltype = GL_UNSIGNED_SHORT;
      break;

    case COGL_PIXEL_FORMAT_DEPTH_16:
      glintformat = GL_DEPTH_COMPONENT16;
      glformat = GL_DEPTH_COMPONENT;
      gltype = GL_UNSIGNED_SHORT;
      break;

    case COGL_PIXEL_FORMAT_DEPTH_24_STENCIL_8:
      glintformat = GL_DEPTH24_STENCIL8;
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

/* OpenGL - unlike GLES - can download pixel data into a sub region of
 * a larger destination buffer */
static void
prep_gl_for_pixels_download_full (CoglContext *ctx,
                                  int image_width,
                                  int pixels_rowstride,
                                  int image_height,
                                  int pixels_src_x,
                                  int pixels_src_y,
                                  int pixels_bpp)
{
  GE( ctx, glPixelStorei (GL_PACK_ROW_LENGTH, pixels_rowstride / pixels_bpp) );

  GE( ctx, glPixelStorei (GL_PACK_SKIP_PIXELS, pixels_src_x) );
  GE( ctx, glPixelStorei (GL_PACK_SKIP_ROWS, pixels_src_y) );

  _cogl_texture_gl_prep_alignment_for_pixels_download (ctx,
                                                       pixels_bpp,
                                                       image_width,
                                                       pixels_rowstride);
}
static void
cogl_driver_gl3_prep_gl_for_pixels_download (CoglDriverGL *driver,
                                             CoglContext  *ctx,
                                             int           image_width,
                                             int           pixels_rowstride,
                                             int           pixels_bpp)
{
  prep_gl_for_pixels_download_full (ctx,
                                    image_width,
                                    pixels_rowstride,
                                    0 /* image height */,
                                    0, 0, /* pixels_src_x/y */
                                    pixels_bpp);
}

static gboolean
cogl_driver_gl3_texture_size_supported (CoglDriverGL *driver,
                                        CoglContext  *ctx,
                                        GLenum        gl_target,
                                        GLenum        gl_intformat,
                                        GLenum        gl_format,
                                        GLenum        gl_type,
                                        int           width,
                                        int           height)
{
  GLenum proxy_target;
  GLint new_width = 0;

  if (gl_target == GL_TEXTURE_2D)
    proxy_target = GL_PROXY_TEXTURE_2D;
  else if (gl_target == GL_TEXTURE_RECTANGLE_ARB)
    proxy_target = GL_PROXY_TEXTURE_RECTANGLE_ARB;
  else
    /* Unknown target, assume it's not supported */
    return FALSE;

  /* Proxy texture allows for a quick check for supported size */
  GE( ctx, glTexImage2D (proxy_target, 0, gl_intformat,
                         width, height, 0 /* border */,
                         gl_format, gl_type, NULL) );

  GE( ctx, glGetTexLevelParameteriv (proxy_target, 0,
                                     GL_TEXTURE_WIDTH, &new_width) );

  return new_width != 0;
}

static CoglPixelFormat
cogl_driver_gl3_get_read_pixels_format (CoglDriverGL    *driver,
                                        CoglContext     *context,
                                        CoglPixelFormat  from,
                                        CoglPixelFormat  to,
                                        GLenum          *gl_format_out,
                                        GLenum          *gl_type_out)
{
  return cogl_driver_gl3_pixel_format_to_gl (driver,
                                             context,
                                             to,
                                             NULL,
                                             gl_format_out,
                                             gl_type_out);
}

static gboolean
_cogl_get_gl_version (CoglContext *ctx,
                      int         *major_out,
                      int         *minor_out)
{
  const char *version_string;

  /* Get the OpenGL version number */
  if ((version_string = _cogl_context_get_gl_version (ctx)) == NULL)
    return FALSE;

  return _cogl_gl_util_parse_gl_version (version_string, major_out, minor_out);
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
                   "The OpenGL version could not be determined");
      return FALSE;
    }

  if (!COGL_CHECK_GL_VERSION (major, minor, 3, 1))
    {
      g_set_error (error,
                   COGL_DRIVER_ERROR,
                   COGL_DRIVER_ERROR_INVALID_VERSION,
                   "OpenGL 3.1 or better is required");
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
  return _cogl_gl_util_parse_gl_version (version_string, major_out, minor_out);
}

static gboolean
check_glsl_version (CoglContext  *ctx,
                    GError      **error)
{
  CoglDriver *driver = cogl_context_get_driver (ctx);
  int major, minor, driver_major, driver_minor;

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
                   "GLSL %d%d0 or better is required",
                   driver_major, driver_minor);
      return FALSE;
    }

  return TRUE;
}

static gboolean
cogl_driver_gl3_update_features (CoglDriver   *driver,
                                 CoglContext  *ctx,
                                 GError      **error)
{
  unsigned long private_features
    [COGL_FLAGS_N_LONGS_FOR_SIZE (COGL_N_PRIVATE_FEATURES)] = { 0 };
  g_auto (GStrv) gl_extensions = 0;
  int gl_major = 0, gl_minor = 0;
  int i;

  /* We have to special case getting the pointer to the glGetString*
     functions because we need to use them to determine what functions
     we can expect */
  ctx->glGetString =
    (void *) cogl_renderer_get_proc_address (ctx->display->renderer,
                                             "glGetString");

  if (!check_gl_version (ctx, error))
    return FALSE;

  if (!check_glsl_version (ctx, error))
    return FALSE;

  /* These are only used in _cogl_context_get_gl_extensions for GL 3.0
   * so don't look them up before check_gl_version()
   */
  ctx->glGetStringi =
    (void *) cogl_renderer_get_proc_address (ctx->display->renderer,
                                             "glGetStringi");
  ctx->glGetIntegerv =
    (void *) cogl_renderer_get_proc_address (ctx->display->renderer,
                                             "glGetIntegerv");

  gl_extensions = _cogl_context_get_gl_extensions (ctx);

  if (G_UNLIKELY (COGL_DEBUG_ENABLED (COGL_DEBUG_WINSYS)))
    {
      g_autofree char *all_extensions = g_strjoinv (" ", gl_extensions);

      COGL_NOTE (WINSYS,
                 "Checking features\n"
                 "  GL_VENDOR: %s\n"
                 "  GL_RENDERER: %s\n"
                 "  GL_VERSION: %s\n"
                 "  GL_EXTENSIONS: %s",
                 ctx->glGetString (GL_VENDOR),
                 ctx->glGetString (GL_RENDERER),
                 _cogl_context_get_gl_version (ctx),
                 all_extensions);
    }

  _cogl_get_gl_version (ctx, &gl_major, &gl_minor);

  COGL_FLAGS_SET (ctx->features,
                  COGL_FEATURE_ID_UNSIGNED_INT_INDICES, TRUE);

  _cogl_feature_check_ext_functions (ctx,
                                     gl_major,
                                     gl_minor,
                                     gl_extensions);

  if (_cogl_check_extension ("GL_MESA_pack_invert", gl_extensions))
    COGL_FLAGS_SET (private_features,
                    COGL_PRIVATE_FEATURE_MESA_PACK_INVERT, TRUE);

  COGL_FLAGS_SET (private_features,
                  COGL_PRIVATE_FEATURE_QUERY_FRAMEBUFFER_BITS,
                  TRUE);

  COGL_FLAGS_SET (ctx->features, COGL_FEATURE_ID_BLIT_FRAMEBUFFER, TRUE);

  COGL_FLAGS_SET (private_features, COGL_PRIVATE_FEATURE_PBOS, TRUE);

  COGL_FLAGS_SET (ctx->features, COGL_FEATURE_ID_MAP_BUFFER_FOR_READ, TRUE);
  COGL_FLAGS_SET (ctx->features, COGL_FEATURE_ID_MAP_BUFFER_FOR_WRITE, TRUE);

  if (ctx->glEGLImageTargetTexture2D)
    COGL_FLAGS_SET (private_features,
                    COGL_PRIVATE_FEATURE_TEXTURE_2D_FROM_EGL_IMAGE, TRUE);

  COGL_FLAGS_SET (private_features,
                  COGL_PRIVATE_FEATURE_EXT_PACKED_DEPTH_STENCIL, TRUE);

  if (ctx->glGenSamplers)
    COGL_FLAGS_SET (private_features,
                    COGL_PRIVATE_FEATURE_SAMPLER_OBJECTS, TRUE);

  if (COGL_CHECK_GL_VERSION (gl_major, gl_minor, 3, 3) ||
      _cogl_check_extension ("GL_ARB_texture_swizzle", gl_extensions) ||
      _cogl_check_extension ("GL_EXT_texture_swizzle", gl_extensions))
    COGL_FLAGS_SET (private_features,
                    COGL_PRIVATE_FEATURE_TEXTURE_SWIZZLE, TRUE);

  COGL_FLAGS_SET (private_features,
                  COGL_PRIVATE_FEATURE_READ_PIXELS_ANY_STRIDE, TRUE);
  COGL_FLAGS_SET (private_features, COGL_PRIVATE_FEATURE_ANY_GL, TRUE);
  COGL_FLAGS_SET (private_features,
                  COGL_PRIVATE_FEATURE_FORMAT_CONVERSION, TRUE);
  COGL_FLAGS_SET (private_features,
                  COGL_PRIVATE_FEATURE_QUERY_TEXTURE_PARAMETERS, TRUE);
  COGL_FLAGS_SET (private_features,
                  COGL_PRIVATE_FEATURE_TEXTURE_MAX_LEVEL, TRUE);

  COGL_FLAGS_SET (private_features,
                  COGL_PRIVATE_FEATURE_TEXTURE_LOD_BIAS, TRUE);

  if (ctx->glFenceSync)
    COGL_FLAGS_SET (ctx->features, COGL_FEATURE_ID_FENCE, TRUE);

  COGL_FLAGS_SET (ctx->features, COGL_FEATURE_ID_TEXTURE_RG, TRUE);

  COGL_FLAGS_SET (ctx->features, COGL_FEATURE_ID_TEXTURE_RGBA1010102, TRUE);

  COGL_FLAGS_SET (ctx->features, COGL_FEATURE_ID_TEXTURE_HALF_FLOAT, TRUE);

  COGL_FLAGS_SET (ctx->features, COGL_FEATURE_ID_TEXTURE_NORM16, TRUE);

  if (ctx->glGenQueries && ctx->glQueryCounter && ctx->glGetInteger64v)
    COGL_FLAGS_SET (ctx->features, COGL_FEATURE_ID_TIMESTAMP_QUERY, TRUE);

  /* Cache features */
  for (i = 0; i < G_N_ELEMENTS (private_features); i++)
    ctx->private_features[i] |= private_features[i];

  if (!COGL_FLAGS_GET (private_features, COGL_PRIVATE_FEATURE_TEXTURE_SWIZZLE))
    {
      g_set_error (error,
                   COGL_DRIVER_ERROR,
                   COGL_DRIVER_ERROR_NO_SUITABLE_DRIVER_FOUND,
                   "The GL_ARB_texture_swizzle extension is required "
                   "to use the GL3 driver");
      return FALSE;
    }

  return TRUE;
}

static gboolean
cogl_driver_gl3_format_supports_upload (CoglDriver      *driver,
                                        CoglContext     *ctx,
                                        CoglPixelFormat  format)
{
  switch (format)
    {
    case COGL_PIXEL_FORMAT_A_8:
    case COGL_PIXEL_FORMAT_R_8:
    case COGL_PIXEL_FORMAT_RG_88:
    case COGL_PIXEL_FORMAT_BGRX_8888:
    case COGL_PIXEL_FORMAT_BGRA_8888:
    case COGL_PIXEL_FORMAT_BGRA_8888_PRE:
    case COGL_PIXEL_FORMAT_RGB_888:
    case COGL_PIXEL_FORMAT_BGR_888:
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
    case COGL_PIXEL_FORMAT_BGRX_FP_16161616:
    case COGL_PIXEL_FORMAT_BGRA_FP_16161616:
    case COGL_PIXEL_FORMAT_XRGB_FP_16161616:
    case COGL_PIXEL_FORMAT_ARGB_FP_16161616:
    case COGL_PIXEL_FORMAT_XBGR_FP_16161616:
    case COGL_PIXEL_FORMAT_ABGR_FP_16161616:
    case COGL_PIXEL_FORMAT_BGRA_FP_16161616_PRE:
    case COGL_PIXEL_FORMAT_ARGB_FP_16161616_PRE:
    case COGL_PIXEL_FORMAT_ABGR_FP_16161616_PRE:
    case COGL_PIXEL_FORMAT_RGBX_FP_16161616:
    case COGL_PIXEL_FORMAT_RGBA_FP_16161616:
    case COGL_PIXEL_FORMAT_RGBA_FP_16161616_PRE:
    case COGL_PIXEL_FORMAT_RGBA_FP_32323232:
    case COGL_PIXEL_FORMAT_RGBA_FP_32323232_PRE:
    case COGL_PIXEL_FORMAT_R_16:
    case COGL_PIXEL_FORMAT_RG_1616:
    case COGL_PIXEL_FORMAT_RGBA_16161616:
    case COGL_PIXEL_FORMAT_RGBA_16161616_PRE:
      return TRUE;
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
cogl_driver_gl3_create_texture_driver (CoglDriver *driver)
{
  return g_object_new (COGL_TYPE_TEXTURE_DRIVER_GL3,
                       "driver", driver,
                       NULL);
}

static void
cogl_driver_gl3_class_init (CoglDriverGL3Class *klass)
{
  CoglDriverClass *driver_klass = COGL_DRIVER_CLASS (klass);
  CoglDriverGLClass *driver_gl_klass = COGL_DRIVER_GL_CLASS (klass);

  driver_klass->context_init = cogl_driver_gl3_context_init;
  driver_klass->update_features = cogl_driver_gl3_update_features;
  driver_klass->format_supports_upload = cogl_driver_gl3_format_supports_upload;
  driver_klass->create_texture_driver = cogl_driver_gl3_create_texture_driver;

  driver_gl_klass->get_read_pixels_format = cogl_driver_gl3_get_read_pixels_format;
  driver_gl_klass->pixel_format_to_gl = cogl_driver_gl3_pixel_format_to_gl;
  driver_gl_klass->prep_gl_for_pixels_download = cogl_driver_gl3_prep_gl_for_pixels_download;
  driver_gl_klass->texture_size_supported = cogl_driver_gl3_texture_size_supported;
}

static void
cogl_driver_gl3_init (CoglDriverGL3 *driver)
{
  CoglDriverGLPrivate *priv =
    cogl_driver_gl_get_private (COGL_DRIVER_GL (driver));

  priv->glsl_major = 1;
  priv->glsl_minor = 40;
  priv->glsl_es = FALSE;
}
