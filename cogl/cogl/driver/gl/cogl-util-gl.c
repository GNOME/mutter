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

#include "cogl-config.h"

#include "cogl-types.h"
#include "cogl-context-private.h"
#include "cogl-error-private.h"
#include "driver/gl/cogl-util-gl-private.h"

#ifdef COGL_GL_DEBUG
/* GL error to string conversion */
static const struct {
  GLuint error_code;
  const char *error_string;
} gl_errors[] = {
  { GL_NO_ERROR,          "No error" },
  { GL_INVALID_ENUM,      "Invalid enumeration value" },
  { GL_INVALID_VALUE,     "Invalid value" },
  { GL_INVALID_OPERATION, "Invalid operation" },
#ifdef HAVE_COGL_GL
  { GL_STACK_OVERFLOW,    "Stack overflow" },
  { GL_STACK_UNDERFLOW,   "Stack underflow" },
#endif
  { GL_OUT_OF_MEMORY,     "Out of memory" },

#ifdef GL_INVALID_FRAMEBUFFER_OPERATION_EXT
  { GL_INVALID_FRAMEBUFFER_OPERATION_EXT, "Invalid framebuffer operation" }
#endif
};

static const unsigned int n_gl_errors = G_N_ELEMENTS (gl_errors);

const char *
_cogl_gl_error_to_string (GLenum error_code)
{
  int i;

  for (i = 0; i < n_gl_errors; i++)
    {
      if (gl_errors[i].error_code == error_code)
        return gl_errors[i].error_string;
    }

  return "Unknown GL error";
}
#endif /* COGL_GL_DEBUG */

GLenum
_cogl_gl_util_get_error (CoglContext *ctx)
{
  GLenum gl_error = ctx->glGetError ();

  if (gl_error != GL_NO_ERROR && gl_error != GL_CONTEXT_LOST)
    return gl_error;
  else
    return GL_NO_ERROR;
}

void
_cogl_gl_util_clear_gl_errors (CoglContext *ctx)
{
  GLenum gl_error;

  while ((gl_error = ctx->glGetError ()) != GL_NO_ERROR && gl_error != GL_CONTEXT_LOST)
    ;
}

gboolean
_cogl_gl_util_catch_out_of_memory (CoglContext *ctx, CoglError **error)
{
  GLenum gl_error;
  gboolean out_of_memory = FALSE;

  while ((gl_error = ctx->glGetError ()) != GL_NO_ERROR && gl_error != GL_CONTEXT_LOST)
    {
      if (gl_error == GL_OUT_OF_MEMORY)
        out_of_memory = TRUE;
#ifdef COGL_GL_DEBUG
      else
        {
          g_warning ("%s: GL error (%d): %s\n",
                     G_STRLOC,
                     gl_error,
                     _cogl_gl_error_to_string (gl_error));
        }
#endif
    }

  if (out_of_memory)
    {
      _cogl_set_error (error, COGL_SYSTEM_ERROR,
                       COGL_SYSTEM_ERROR_NO_MEMORY,
                       "Out of memory");
      return TRUE;
    }

  return FALSE;
}

void
_cogl_gl_util_get_texture_target_string (CoglTextureType texture_type,
                                         const char **target_string_out,
                                         const char **swizzle_out)
{
  const char *target_string, *tex_coord_swizzle;

  switch (texture_type)
    {
#if 0 /* TODO */
    case COGL_TEXTURE_TYPE_1D:
      target_string = "1D";
      tex_coord_swizzle = "s";
      break;
#endif

    case COGL_TEXTURE_TYPE_2D:
      target_string = "2D";
      tex_coord_swizzle = "st";
      break;

    case COGL_TEXTURE_TYPE_3D:
      target_string = "3D";
      tex_coord_swizzle = "stp";
      break;

    case COGL_TEXTURE_TYPE_RECTANGLE:
      target_string = "2DRect";
      tex_coord_swizzle = "st";
      break;

    default:
      target_string = "Unknown";
      tex_coord_swizzle = NULL;
      g_assert_not_reached ();
    }

  if (target_string_out)
    *target_string_out = target_string;
  if (swizzle_out)
    *swizzle_out = tex_coord_swizzle;
}

gboolean
_cogl_gl_util_parse_gl_version (const char *version_string,
                                int *major_out,
                                int *minor_out)
{
  const char *major_end, *minor_end;
  int major = 0, minor = 0;

  /* Extract the major number */
  for (major_end = version_string; *major_end >= '0'
         && *major_end <= '9'; major_end++)
    major = (major * 10) + *major_end - '0';
  /* If there were no digits or the major number isn't followed by a
     dot then it is invalid */
  if (major_end == version_string || *major_end != '.')
    return FALSE;

  /* Extract the minor number */
  for (minor_end = major_end + 1; *minor_end >= '0'
         && *minor_end <= '9'; minor_end++)
    minor = (minor * 10) + *minor_end - '0';
  /* If there were no digits or there is an unexpected character then
     it is invalid */
  if (minor_end == major_end + 1
      || (*minor_end && *minor_end != ' ' && *minor_end != '.'))
    return FALSE;

  *major_out = major;
  *minor_out = minor;

  return TRUE;
}
