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

#include "cogl/cogl-driver-private.h"

typedef struct _CoglDriverGLPrivate
{
  int glsl_major;
  int glsl_minor;
  gboolean glsl_es;

  GArray *texture_units;
  int active_texture_unit;

  /* Cached values for GL_MAX_TEXTURE_[IMAGE_]UNITS to avoid calling
     glGetInteger too often */
  GLint max_activateable_texture_units;

  /* This is used for generated fake unique sampler object numbers
   when the sampler object extension is not supported */
  GLuint next_fake_sampler_object_number;

  /* This defines a list of function pointers that Cogl uses from
     either GL or GLES. All functions are accessed indirectly through
     these pointers rather than linking to them directly */
#ifndef APIENTRY
#define APIENTRY
#endif

#define COGL_EXT_BEGIN(name, \
                       min_gl_major, min_gl_minor, \
                       gles_availability, \
                       extension_suffixes, extension_names)
#define COGL_EXT_FUNCTION(ret, name, args) \
  ret (APIENTRY * name) args;
#define COGL_EXT_END()

#include "cogl/gl-prototypes/cogl-all-functions.h"

#undef COGL_EXT_BEGIN
#undef COGL_EXT_FUNCTION
#undef COGL_EXT_END
} CoglDriverGLPrivate;


G_DECLARE_DERIVABLE_TYPE (CoglDriverGL,
                          cogl_driver_gl,
                          COGL,
                          DRIVER_GL,
                          CoglDriver);

struct _CoglDriverGLClass
{
  CoglDriverClass parent_class;

  CoglPixelFormat (* pixel_format_to_gl) (CoglDriverGL    *driver,
                                          CoglContext     *context,
                                          CoglPixelFormat  format,
                                          GLenum          *out_glintformat,
                                          GLenum          *out_glformat,
                                          GLenum          *out_gltype);

  CoglPixelFormat (* get_read_pixels_format) (CoglDriverGL    *driver,
                                              CoglContext     *context,
                                              CoglPixelFormat  from,
                                              CoglPixelFormat  to,
                                              GLenum          *gl_format_out,
                                              GLenum          *gl_type_out);
  /*
   * This sets up the glPixelStore state for an download to a destination with
   * the same size, and with no offset.
   */
  /* NB: GLES can't download pixel data into a sub region of a larger
   * destination buffer, the GL driver has a more flexible version of
   * this function that it uses internally. */
  void (* prep_gl_for_pixels_download) (CoglDriverGL *driver,
                                        CoglContext  *ctx,
                                        int           image_width,
                                        int           pixels_rowstride,
                                        int           pixels_bpp);
  /*
   * It may depend on the driver as to what texture sizes are supported...
   */
  gboolean (* texture_size_supported) (CoglDriverGL *driver,
                                       CoglContext  *ctx,
                                       GLenum        gl_target,
                                       GLenum        gl_intformat,
                                       GLenum        gl_format,
                                       GLenum        gl_type,
                                       int           width,
                                       int           height);

  void (* query_max_texture_units) (CoglDriverGL *driver,
                                    GLint        *values,
                                    int          *n_values);
};

#define COGL_TYPE_DRIVER_GL (cogl_driver_gl_get_type ())

CoglDriverGLPrivate * cogl_driver_gl_get_private (CoglDriverGL *driver);

gboolean cogl_driver_gl_is_es (CoglDriverGL *driver);

void cogl_driver_gl_get_glsl_version (CoglDriverGL *driver,
                                      int          *major,
                                      int          *minor);

void cogl_driver_gl_clear_gl_errors (CoglDriverGL *driver);

gboolean cogl_driver_gl_catch_out_of_memory (CoglDriverGL *driver,
                                             GError      **error);

const char * cogl_driver_gl_get_gl_string (CoglDriverGL  *driver,
                                           GLenum         name);

/*
 * cogl_driver_gl_get_gl_extensions:
 * @driver: A CoglDriverGL
 * @renderer: A CoglRenderer
 *
 * Return value: a NULL-terminated array of strings representing the
 *   supported extensions by the current driver. This array is owned
 *   by the caller and should be freed with g_strfreev().
 */
char ** cogl_driver_gl_get_gl_extensions (CoglDriverGL *driver,
                                          CoglRenderer *renderer);

const char * cogl_driver_gl_get_gl_version (CoglDriverGL *driver);

GLenum cogl_driver_gl_get_gl_error (CoglDriverGL *driver);

GLint cogl_driver_gl_get_max_activateable_texture_units (CoglDriverGL *driver);

gboolean cogl_parse_gl_version (const char *version_string,
                                int        *major_out,
                                int        *minor_out);

#ifdef COGL_ENABLE_DEBUG
/* GL error to string conversion */
static const struct {
  GLuint error_code;
  const char *error_string;
} gl_errors[] = {
  { GL_NO_ERROR,          "No error" },
  { GL_INVALID_ENUM,      "Invalid enumeration value" },
  { GL_INVALID_VALUE,     "Invalid value" },
  { GL_INVALID_OPERATION, "Invalid operation" },
#ifdef HAVE_GL
  { GL_STACK_OVERFLOW,    "Stack overflow" },
  { GL_STACK_UNDERFLOW,   "Stack underflow" },
#endif
  { GL_OUT_OF_MEMORY,     "Out of memory" },

#ifdef GL_INVALID_FRAMEBUFFER_OPERATION_EXT
  { GL_INVALID_FRAMEBUFFER_OPERATION_EXT, "Invalid framebuffer operation" }
#endif
};

static const unsigned int n_gl_errors = G_N_ELEMENTS (gl_errors);

static inline const char *
cogl_gl_error_to_string (GLenum error_code)
{
  int i;

  for (i = 0; i < n_gl_errors; i++)
    {
      if (gl_errors[i].error_code == error_code)
        return gl_errors[i].error_string;
    }

  return "Unknown GL error";
}

#define GE(driver, x)                      G_STMT_START {  \
  GLenum __err;                                         \
  CoglDriverGL *_driver_gl = COGL_DRIVER_GL (driver);   \
  CoglDriverGLPrivate *_driver_gl_private = cogl_driver_gl_get_private (_driver_gl);   \
  (_driver_gl_private)->x;                                             \
  while ((__err = (_driver_gl_private)->glGetError ()) != GL_NO_ERROR && __err != GL_CONTEXT_LOST) \
    {                                                   \
      g_warning ("%s: GL error (%d): %s\n",             \
                 G_STRLOC,                              \
                 __err,                                 \
                 cogl_gl_error_to_string (__err));     \
    }                                   } G_STMT_END

#define GE_RET(ret, driver, x)             G_STMT_START {  \
  GLenum __err;                                         \
  CoglDriverGL *_driver_gl = COGL_DRIVER_GL (driver);   \
  CoglDriverGLPrivate *_driver_gl_private = cogl_driver_gl_get_private (_driver_gl);   \
  ret = (_driver_gl_private)->x;                                       \
  while ((__err = (_driver_gl_private)->glGetError ()) != GL_NO_ERROR && __err != GL_CONTEXT_LOST) \
    {                                                   \
      g_warning ("%s: GL error (%d): %s\n",             \
                 G_STRLOC,                              \
                 __err,                                 \
                 cogl_gl_error_to_string (__err));     \
    }                                   } G_STMT_END

#else /* !COGL_ENABLE_DEBUG */

#define GE(driver, x)                      G_STMT_START {  \
  CoglDriverGL *_driver_gl = COGL_DRIVER_GL (driver);   \
  CoglDriverGLPrivate *_driver_gl_private = cogl_driver_gl_get_private (_driver_gl);   \
  (_driver_gl_private)->x;                                             \
                                  } G_STMT_END

#define GE_RET(ret, driver, x)             G_STMT_START {  \
  CoglDriverGL *_driver_gl = COGL_DRIVER_GL (driver);   \
  CoglDriverGLPrivate *_driver_gl_private = cogl_driver_gl_get_private (_driver_gl);   \
  ret = (_driver_gl_private)->x;                                       \
                                   } G_STMT_END
#endif /* COGL_ENABLE_DEBUG */

static inline void *
cogl_gl_get_proc (CoglDriver *driver,
                  size_t      offset)
{
  CoglDriverGL *_driver_gl = COGL_DRIVER_GL (driver);
  CoglDriverGLPrivate *_driver_gl_private =
    cogl_driver_gl_get_private (_driver_gl);

  return *(void **)(((char *)_driver_gl_private) + offset);
}

#define GE_HAS(driver, member) \
  (cogl_gl_get_proc ((driver), G_STRUCT_OFFSET (CoglDriverGLPrivate, member)))

/* In OpenGL ES context, GL_CONTEXT_LOST has a _KHR prefix */
#ifndef GL_CONTEXT_LOST
#define GL_CONTEXT_LOST GL_CONTEXT_LOST_KHR
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_RENDERBUFFER
#define GL_RENDERBUFFER	0x8D41
#endif
#ifndef GL_STENCIL_ATTACHMENT
#define GL_STENCIL_ATTACHMENT 0x8D00
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_STENCIL_INDEX8
#define GL_STENCIL_INDEX8 0x8D48
#endif
#ifndef GL_DEPTH_STENCIL
#define GL_DEPTH_STENCIL 0x84F9
#endif
#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8 0x88F0
#endif
#ifndef GL_DEPTH_ATTACHMENT
#define GL_DEPTH_ATTACHMENT 0x8D00
#endif
#ifndef GL_DEPTH_STENCIL_ATTACHMENT
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#endif
#ifndef GL_DEPTH_COMPONENT16
#define GL_DEPTH_COMPONENT16 0x81A5
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE 0x8212
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE 0x8213
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE 0x8214
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE 0x8215
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE 0x8216
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE 0x8217
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_TEXTURE_SAMPLES_IMG
#define GL_TEXTURE_SAMPLES_IMG 0x9136
#endif
#ifndef GL_PACK_INVERT_MESA
#define GL_PACK_INVERT_MESA 0x8758
#endif
#ifndef GL_PACK_REVERSE_ROW_ORDER_ANGLE
#define GL_PACK_REVERSE_ROW_ORDER_ANGLE 0x93A4
#endif
#ifndef GL_BACK_LEFT
#define GL_BACK_LEFT 0x0402
#endif
#ifndef GL_BACK_RIGHT
#define GL_BACK_RIGHT 0x0403
#endif

#ifndef GL_COLOR
#define GL_COLOR 0x1800
#endif
#ifndef GL_DEPTH
#define GL_DEPTH 0x1801
#endif
#ifndef GL_STENCIL
#define GL_STENCIL 0x1802
#endif

#ifndef GL_TIMESTAMP
#define GL_TIMESTAMP 0x8E28
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif

#ifndef GL_TEXTURE_LOD_BIAS
#define GL_TEXTURE_LOD_BIAS 0x8501
#endif

/* In OpenGL ES context, GL_CONTEXT_LOST has a _KHR prefix */
#ifndef GL_CONTEXT_LOST
#define GL_CONTEXT_LOST GL_CONTEXT_LOST_KHR
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_RENDERBUFFER
#define GL_RENDERBUFFER 0x8D41
#endif
#ifndef GL_STENCIL_ATTACHMENT
#define GL_STENCIL_ATTACHMENT 0x8D00
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_STENCIL_INDEX8
#define GL_STENCIL_INDEX8 0x8D48
#endif
#ifndef GL_DEPTH_STENCIL
#define GL_DEPTH_STENCIL 0x84F9
#endif
#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8 0x88F0
#endif
#ifndef GL_DEPTH_ATTACHMENT
#define GL_DEPTH_ATTACHMENT 0x8D00
#endif
#ifndef GL_DEPTH_STENCIL_ATTACHMENT
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#endif
#ifndef GL_DEPTH_COMPONENT16
#define GL_DEPTH_COMPONENT16 0x81A5
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE 0x8212
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE 0x8213
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE 0x8214
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE 0x8215
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE 0x8216
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE 0x8217
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_TEXTURE_SAMPLES_IMG
#define GL_TEXTURE_SAMPLES_IMG 0x9136
#endif
#ifndef GL_PACK_INVERT_MESA
#define GL_PACK_INVERT_MESA 0x8758
#endif
#ifndef GL_PACK_REVERSE_ROW_ORDER_ANGLE
#define GL_PACK_REVERSE_ROW_ORDER_ANGLE 0x93A4
#endif
#ifndef GL_BACK_LEFT
#define GL_BACK_LEFT 0x0402
#endif
#ifndef GL_BACK_RIGHT
#define GL_BACK_RIGHT 0x0403
#endif

#ifndef GL_COLOR
#define GL_COLOR 0x1800
#endif
#ifndef GL_DEPTH
#define GL_DEPTH 0x1801
#endif
#ifndef GL_STENCIL
#define GL_STENCIL 0x1802
#endif

#ifndef GL_TIMESTAMP
#define GL_TIMESTAMP 0x8E28
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif

#ifndef GL_TEXTURE_LOD_BIAS
#define GL_TEXTURE_LOD_BIAS 0x8501
#endif
