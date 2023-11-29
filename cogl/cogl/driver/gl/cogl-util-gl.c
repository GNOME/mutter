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
#include "cogl/driver/gl/cogl-pipeline-opengl-private.h"
#include "cogl/driver/gl/cogl-util-gl-private.h"

/* This is a relatively new extension */
#ifndef GL_PURGED_CONTEXT_RESET_NV
#define GL_PURGED_CONTEXT_RESET_NV 0x92BB
#endif

/* These aren't defined in the GLES2 headers */
#ifndef GL_GUILTY_CONTEXT_RESET_ARB
#define GL_GUILTY_CONTEXT_RESET_ARB 0x8253
#endif

#ifndef GL_INNOCENT_CONTEXT_RESET_ARB
#define GL_INNOCENT_CONTEXT_RESET_ARB 0x8254
#endif

#ifndef GL_UNKNOWN_CONTEXT_RESET_ARB
#define GL_UNKNOWN_CONTEXT_RESET_ARB 0x8255
#endif

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
#endif /* COGL_GL_DEBUG */

CoglGLContext *
_cogl_driver_gl_context (CoglContext *context)
{
  return context->driver_context;
}

gboolean
_cogl_driver_gl_context_init (CoglContext *context)
{
  CoglGLContext *gl_context;

  if (!context->driver_context)
    context->driver_context = g_new0 (CoglGLContext, 1);

  gl_context = _cogl_driver_gl_context (context);
  if (!gl_context)
    return FALSE;

  gl_context->next_fake_sampler_object_number = 1;
  gl_context->texture_units =
    g_array_new (FALSE, FALSE, sizeof (CoglTextureUnit));

  /* See cogl-pipeline.c for more details about why we leave texture unit 1
   * active by default... */
  gl_context->active_texture_unit = 1;
  GE (context, glActiveTexture (GL_TEXTURE1));

  return TRUE;
}

void
_cogl_driver_gl_context_deinit (CoglContext *context)
{
  _cogl_destroy_texture_units (context);
  g_free (context->driver_context);
}

CoglFramebufferDriver *
_cogl_driver_gl_create_framebuffer_driver (CoglContext                        *context,
                                           CoglFramebuffer                    *framebuffer,
                                           const CoglFramebufferDriverConfig  *driver_config,
                                           GError                            **error)
{
  g_return_val_if_fail (driver_config, NULL);

  switch (driver_config->type)
    {
    case COGL_FRAMEBUFFER_DRIVER_TYPE_FBO:
      {
        CoglGlFramebufferFbo *gl_framebuffer_fbo;

        gl_framebuffer_fbo = cogl_gl_framebuffer_fbo_new (framebuffer,
                                                          driver_config,
                                                          error);
        if (!gl_framebuffer_fbo)
          return NULL;

        return COGL_FRAMEBUFFER_DRIVER (gl_framebuffer_fbo);
      }
    case COGL_FRAMEBUFFER_DRIVER_TYPE_BACK:
      {
        CoglGlFramebufferBack *gl_framebuffer_back;

        gl_framebuffer_back = cogl_gl_framebuffer_back_new (framebuffer,
                                                            driver_config,
                                                            error);
        if (!gl_framebuffer_back)
          return NULL;

        return COGL_FRAMEBUFFER_DRIVER (gl_framebuffer_back);
      }
    }

  g_assert_not_reached ();
  return NULL;
}

void
_cogl_driver_gl_flush_framebuffer_state (CoglContext          *ctx,
                                         CoglFramebuffer      *draw_buffer,
                                         CoglFramebuffer      *read_buffer,
                                         CoglFramebufferState  state)
{
  CoglGlFramebuffer *draw_gl_framebuffer;
  CoglGlFramebuffer *read_gl_framebuffer;
  unsigned long differences;

  /* We can assume that any state that has changed for the current
   * framebuffer is different to the currently flushed value. */
  differences = ctx->current_draw_buffer_changes;

  /* Any state of the current framebuffer that hasn't already been
   * flushed is assumed to be unknown so we will always flush that
   * state if asked. */
  differences |= ~ctx->current_draw_buffer_state_flushed;

  /* We only need to consider the state we've been asked to flush */
  differences &= state;

  if (ctx->current_draw_buffer != draw_buffer)
    {
      /* If the previous draw buffer is NULL then we'll assume
         everything has changed. This can happen if a framebuffer is
         destroyed while it is the last flushed draw buffer. In that
         case the framebuffer destructor will set
         ctx->current_draw_buffer to NULL */
      if (ctx->current_draw_buffer == NULL)
        differences |= state;
      else
        /* NB: we only need to compare the state we're being asked to flush
         * and we don't need to compare the state we've already decided
         * we will definitely flush... */
        differences |= _cogl_framebuffer_compare (ctx->current_draw_buffer,
                                                  draw_buffer,
                                                  state & ~differences);

      /* NB: we don't take a reference here, to avoid a circular
       * reference. */
      ctx->current_draw_buffer = draw_buffer;
      ctx->current_draw_buffer_state_flushed = 0;
    }

  if (ctx->current_read_buffer != read_buffer &&
      state & COGL_FRAMEBUFFER_STATE_BIND)
    {
      differences |= COGL_FRAMEBUFFER_STATE_BIND;
      /* NB: we don't take a reference here, to avoid a circular
       * reference. */
      ctx->current_read_buffer = read_buffer;
    }

  if (!differences)
    return;

  /* Lazily ensure the framebuffers have been allocated */
  if (G_UNLIKELY (!cogl_framebuffer_is_allocated (draw_buffer)))
    cogl_framebuffer_allocate (draw_buffer, NULL);
  if (G_UNLIKELY (!cogl_framebuffer_is_allocated (read_buffer)))
    cogl_framebuffer_allocate (read_buffer, NULL);

  draw_gl_framebuffer =
    COGL_GL_FRAMEBUFFER (cogl_framebuffer_get_driver (draw_buffer));
  read_gl_framebuffer =
    COGL_GL_FRAMEBUFFER (cogl_framebuffer_get_driver (read_buffer));

  /* We handle buffer binding separately since the method depends on whether
   * we are binding the same buffer for read and write or not unlike all
   * other state that only relates to the draw_buffer. */
  if (differences & COGL_FRAMEBUFFER_STATE_BIND)
    {
      if (draw_buffer == read_buffer)
        {
          cogl_gl_framebuffer_bind (draw_gl_framebuffer, GL_FRAMEBUFFER);
        }
      else
        {
          /* NB: Currently we only take advantage of binding separate
           * read/write buffers for framebuffer blit purposes. */
          g_return_if_fail (cogl_has_feature
                            (ctx, COGL_FEATURE_ID_BLIT_FRAMEBUFFER));

          cogl_gl_framebuffer_bind (draw_gl_framebuffer, GL_DRAW_FRAMEBUFFER);
          cogl_gl_framebuffer_bind (read_gl_framebuffer, GL_READ_FRAMEBUFFER);
        }

      differences &= ~COGL_FRAMEBUFFER_STATE_BIND;
    }

  cogl_gl_framebuffer_flush_state_differences (draw_gl_framebuffer,
                                               differences);

  ctx->current_draw_buffer_state_flushed |= state;
  ctx->current_draw_buffer_changes &= ~state;
}

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
  const char *env_disabled_extensions;
  char **ret;

  /* In GL 3, querying GL_EXTENSIONS is deprecated so we have to build
   * the array using glGetStringi instead */
#ifdef HAVE_GL
  if (context->driver == COGL_DRIVER_GL3)
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

/*
 * This should arguably use something like GLX_MESA_query_renderer, but
 * a) that's GLX-only, and you could add it to EGL too but
 * b) that'd make this a winsys query when really it's not a property of
 *    the winsys but the renderer, and
 * c) only Mesa really supports it anyway, and
 * d) Mesa is the only software renderer of interest.
 *
 * So instead just check a list of known software renderer strings.
 */
gboolean
_cogl_driver_gl_is_hardware_accelerated (CoglContext *ctx)
{
  const char *renderer = (const char *) ctx->glGetString (GL_RENDERER);
  gboolean software;

  if (!renderer)
    {
      g_warning ("OpenGL driver returned NULL as the renderer, "
                 "something is wrong");
      return TRUE;
    }

  software = strstr (renderer, "llvmpipe") != NULL ||
             strstr (renderer, "softpipe") != NULL ||
             strstr (renderer, "software rasterizer") != NULL ||
             strstr (renderer, "Software Rasterizer") != NULL ||
             strstr (renderer, "SWR");

  return !software;
}

CoglGraphicsResetStatus
_cogl_gl_get_graphics_reset_status (CoglContext *context)
{
  if (!context->glGetGraphicsResetStatus)
    return COGL_GRAPHICS_RESET_STATUS_NO_ERROR;

  switch (context->glGetGraphicsResetStatus ())
    {
    case GL_GUILTY_CONTEXT_RESET_ARB:
      return COGL_GRAPHICS_RESET_STATUS_GUILTY_CONTEXT_RESET;

    case GL_INNOCENT_CONTEXT_RESET_ARB:
      return COGL_GRAPHICS_RESET_STATUS_INNOCENT_CONTEXT_RESET;

    case GL_UNKNOWN_CONTEXT_RESET_ARB:
      return COGL_GRAPHICS_RESET_STATUS_UNKNOWN_CONTEXT_RESET;

    case GL_PURGED_CONTEXT_RESET_NV:
      return COGL_GRAPHICS_RESET_STATUS_PURGED_CONTEXT_RESET;

    default:
      return COGL_GRAPHICS_RESET_STATUS_NO_ERROR;
    }
}

CoglTimestampQuery *
cogl_gl_create_timestamp_query (CoglContext *context)
{
  CoglTimestampQuery *query;

  g_return_val_if_fail (cogl_has_feature (context,
                                          COGL_FEATURE_ID_TIMESTAMP_QUERY),
                        NULL);

  query = g_new0 (CoglTimestampQuery, 1);

  GE (context, glGenQueries (1, &query->id));
  GE (context, glQueryCounter (query->id, GL_TIMESTAMP));

  /* Flush right away so GL knows about our timestamp query.
   *
   * E.g. the direct scanout path doesn't call SwapBuffers or any other
   * glFlush-inducing operation, and skipping explicit glFlush here results in
   * the timestamp query being placed at the point of glGetQueryObject much
   * later, resulting in a GPU timestamp much later on in time.
   */
  context->glFlush ();

  return query;
}

void
cogl_gl_free_timestamp_query (CoglContext        *context,
                              CoglTimestampQuery *query)
{
  GE (context, glDeleteQueries (1, &query->id));
  g_free (query);
}

int64_t
cogl_gl_timestamp_query_get_time_ns (CoglContext        *context,
                                     CoglTimestampQuery *query)
{
  int64_t query_time_ns;

  GE (context, glGetQueryObjecti64v (query->id,
                                     GL_QUERY_RESULT,
                                     &query_time_ns));

  return query_time_ns;
}

int64_t
cogl_gl_get_gpu_time_ns (CoglContext *context)
{
  int64_t gpu_time_ns;

  g_return_val_if_fail (cogl_has_feature (context,
                                          COGL_FEATURE_ID_TIMESTAMP_QUERY),
                        0);

  GE (context, glGetInteger64v (GL_TIMESTAMP, &gpu_time_ns));
  return gpu_time_ns;
}
