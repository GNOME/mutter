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

#include "config.h"

#include "cogl/cogl-types.h"
#include "cogl/cogl-context-private.h"
#include "cogl/driver/gl/cogl-framebuffer-gl-private.h"
#include "cogl/driver/gl/cogl-gl-framebuffer-fbo.h"
#include "cogl/driver/gl/cogl-gl-framebuffer-back.h"
#include "cogl/driver/gl/cogl-pipeline-gl-private.h"
#include "cogl/driver/gl/cogl-util-gl-private.h"

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
#endif /* COGL_ENABLE_DEBUG */

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
_cogl_gl_util_catch_out_of_memory (CoglContext *ctx, GError **error)
{
  GLenum gl_error;
  gboolean out_of_memory = FALSE;

  while ((gl_error = ctx->glGetError ()) != GL_NO_ERROR && gl_error != GL_CONTEXT_LOST)
    {
      if (gl_error == GL_OUT_OF_MEMORY)
        out_of_memory = TRUE;
#ifdef COGL_ENABLE_DEBUG
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
      g_set_error_literal (error, COGL_SYSTEM_ERROR,
                           COGL_SYSTEM_ERROR_NO_MEMORY,
                           "Out of memory");
      return TRUE;
    }

  return FALSE;
}

char **
_cogl_context_get_gl_extensions (CoglContext *context)
{
#ifdef HAVE_GL
  CoglRenderer *renderer = cogl_context_get_renderer (context);
#endif
  const char *env_disabled_extensions;
  char **ret;

  /* In GL 3, querying GL_EXTENSIONS is deprecated so we have to build
   * the array using glGetStringi instead */
#ifdef HAVE_GL
  if (cogl_renderer_get_driver_id (renderer) == COGL_DRIVER_ID_GL3)
    {
      int num_extensions, i;

      context->glGetIntegerv (GL_NUM_EXTENSIONS, &num_extensions);

      ret = g_malloc (sizeof (char *) * (num_extensions + 1));

      for (i = 0; i < num_extensions; i++)
        {
          const char *ext =
            (const char *) context->glGetStringi (GL_EXTENSIONS, i);
          ret[i] = g_strdup (ext);
        }

      ret[num_extensions] = NULL;
    }
  else
#endif
    {
      const char *all_extensions =
        (const char *) context->glGetString (GL_EXTENSIONS);

      ret = g_strsplit (all_extensions, " ", 0 /* max tokens */);
    }

  if ((env_disabled_extensions = g_getenv ("COGL_DISABLE_GL_EXTENSIONS")))
    {
      char **split_env_disabled_extensions;
      char **src, **dst;

      if (*env_disabled_extensions)
        {
          split_env_disabled_extensions =
            g_strsplit (env_disabled_extensions,
                        ",",
                        0 /* no max tokens */);
        }
      else
        {
          split_env_disabled_extensions = NULL;
        }

      for (dst = ret, src = ret;
           *src;
           src++)
        {
          char **d;

          if (split_env_disabled_extensions)
            for (d = split_env_disabled_extensions; *d; d++)
              if (!strcmp (*src, *d))
                goto disabled;

          *(dst++) = *src;
          continue;

        disabled:
          g_free (*src);
          continue;
        }

      *dst = NULL;

      if (split_env_disabled_extensions)
        g_strfreev (split_env_disabled_extensions);
    }

  return ret;
}

const char *
_cogl_context_get_gl_version (CoglContext *context)
{
  const char *version_override;

  if ((version_override = g_getenv ("COGL_OVERRIDE_GL_VERSION")))
    return version_override;
  else
    return (const char *) context->glGetString (GL_VERSION);

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
