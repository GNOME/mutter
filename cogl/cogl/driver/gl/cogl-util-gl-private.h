/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2012, 2013 Intel Corporation.
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
 * Authors:
 *  Robert Bragg   <robert@linux.intel.com>
 */

#pragma once

#include "cogl/cogl-driver-private.h"
#include "cogl/cogl-types.h"
#include "cogl/cogl-context.h"
#include "cogl/cogl-texture.h"

/* In OpenGL ES context, GL_CONTEXT_LOST has a _KHR prefix */
#ifndef GL_CONTEXT_LOST
#define GL_CONTEXT_LOST GL_CONTEXT_LOST_KHR
#endif

#ifdef COGL_ENABLE_DEBUG

const char *
_cogl_gl_error_to_string (GLenum error_code);

#define GE(ctx, x)                      G_STMT_START {  \
  GLenum __err;                                         \
  (ctx)->x;                                             \
  while ((__err = (ctx)->glGetError ()) != GL_NO_ERROR && __err != GL_CONTEXT_LOST) \
    {                                                   \
      g_warning ("%s: GL error (%d): %s\n",             \
                 G_STRLOC,                              \
                 __err,                                 \
                 _cogl_gl_error_to_string (__err));     \
    }                                   } G_STMT_END

#define GE_RET(ret, ctx, x)             G_STMT_START {  \
  GLenum __err;                                         \
  ret = (ctx)->x;                                       \
  while ((__err = (ctx)->glGetError ()) != GL_NO_ERROR && __err != GL_CONTEXT_LOST) \
    {                                                   \
      g_warning ("%s: GL error (%d): %s\n",             \
                 G_STRLOC,                              \
                 __err,                                 \
                 _cogl_gl_error_to_string (__err));     \
    }                                   } G_STMT_END

#else /* !COGL_ENABLE_DEBUG */

#define GE(ctx, x) ((ctx)->x)
#define GE_RET(ret, ctx, x) (ret = ((ctx)->x))

#endif /* COGL_ENABLE_DEBUG */

GLenum
_cogl_gl_util_get_error (CoglContext *ctx);

void
_cogl_gl_util_clear_gl_errors (CoglContext *ctx);

gboolean
_cogl_gl_util_catch_out_of_memory (CoglContext *ctx, GError **error);

/*
 * _cogl_context_get_gl_extensions:
 * @context: A CoglContext
 *
 * Return value: a NULL-terminated array of strings representing the
 *   supported extensions by the current driver. This array is owned
 *   by the caller and should be freed with g_strfreev().
 */
char **
_cogl_context_get_gl_extensions (CoglContext *context);

const char *
_cogl_context_get_gl_version (CoglContext *context);

/* Parses a GL version number stored in a string. @version_string must
 * point to the beginning of the version number (ie, it can't point to
 * the "OpenGL ES" part on GLES). The version number can be followed
 * by the end of the string, a space or a full stop. Anything else
 * will be treated as invalid. Returns TRUE and sets major_out and
 * minor_out if it is successfully parsed or FALSE otherwise. */
gboolean
_cogl_gl_util_parse_gl_version (const char *version_string,
                                int *major_out,
                                int *minor_out);

#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER		0x8D40
#endif
#ifndef GL_RENDERBUFFER
#define GL_RENDERBUFFER		0x8D41
#endif
#ifndef GL_STENCIL_ATTACHMENT
#define GL_STENCIL_ATTACHMENT	0x8D00
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0	0x8CE0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_STENCIL_INDEX8
#define GL_STENCIL_INDEX8       0x8D48
#endif
#ifndef GL_DEPTH_STENCIL
#define GL_DEPTH_STENCIL        0x84F9
#endif
#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8     0x88F0
#endif
#ifndef GL_DEPTH_ATTACHMENT
#define GL_DEPTH_ATTACHMENT     0x8D00
#endif
#ifndef GL_DEPTH_STENCIL_ATTACHMENT
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#endif
#ifndef GL_DEPTH_COMPONENT16
#define GL_DEPTH_COMPONENT16    0x81A5
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE      0x8212
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE    0x8213
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE     0x8214
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE    0x8215
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE    0x8216
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE  0x8217
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER               0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER               0x8CA9
#endif
#ifndef GL_TEXTURE_SAMPLES_IMG
#define GL_TEXTURE_SAMPLES_IMG            0x9136
#endif
#ifndef GL_PACK_INVERT_MESA
#define GL_PACK_INVERT_MESA 0x8758
#endif
#ifndef GL_PACK_REVERSE_ROW_ORDER_ANGLE
#define GL_PACK_REVERSE_ROW_ORDER_ANGLE 0x93A4
#endif
#ifndef GL_BACK_LEFT
#define GL_BACK_LEFT				0x0402
#endif
#ifndef GL_BACK_RIGHT
#define GL_BACK_RIGHT				0x0403
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
