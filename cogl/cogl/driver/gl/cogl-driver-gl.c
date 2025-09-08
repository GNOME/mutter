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

#include "cogl/driver/gl/cogl-buffer-impl-gl-private.h"
#include "cogl/driver/gl/cogl-driver-gl-private.h"
#include "cogl/driver/gl/cogl-pipeline-gl-private.h"
#include "cogl/driver/gl/cogl-clip-stack-gl-private.h"
#include "cogl/driver/gl/cogl-attribute-gl-private.h"
#include "cogl/driver/gl/cogl-gl-framebuffer-fbo.h"
#include "cogl/driver/gl/cogl-gl-framebuffer-back.h"
#include "cogl/driver/gl/cogl-texture-2d-gl-private.h"
#include "cogl/driver/gl/cogl-texture-gl-private.h"

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

G_DEFINE_TYPE_WITH_PRIVATE (CoglDriverGL, cogl_driver_gl, COGL_TYPE_DRIVER);

static void
cogl_driver_gl_dispose (GObject *object)
{
  CoglDriverGL *driver = COGL_DRIVER_GL (object);
  CoglDriverGLPrivate *priv =
    cogl_driver_gl_get_instance_private (driver);
  int i;

  for (i = 0; i < priv->texture_units->len; i++)
    {
      CoglTextureUnit *unit =
        &g_array_index (priv->texture_units, CoglTextureUnit, i);

      if (unit->layer)
        g_object_unref (unit->layer);
      g_object_unref (unit->matrix_stack);
    }
  g_array_free (priv->texture_units, TRUE);

  G_OBJECT_CLASS (cogl_driver_gl_parent_class)->dispose (object);
}

static gboolean
cogl_driver_gl_context_init (CoglDriver  *driver,
                             CoglContext *context)
{
  /* See cogl-pipeline.c for more details about why we leave texture unit 1
   * active by default... */
  GE (driver, glActiveTexture (GL_TEXTURE1));

  return TRUE;
}

static const char *
cogl_driver_gl_get_gl_vendor (CoglDriver *driver)
{
  return cogl_driver_gl_get_gl_string (COGL_DRIVER_GL (driver),
                                       GL_VENDOR);
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
static gboolean
cogl_driver_gl_is_hardware_accelerated (CoglDriver *driver)
{
  const char *renderer = cogl_driver_gl_get_gl_string (COGL_DRIVER_GL (driver),
                                                       GL_RENDERER);
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

static CoglGraphicsResetStatus
cogl_driver_gl_get_graphics_reset_status (CoglDriver *driver)
{
  int status;

  if (!GE_HAS (driver, glGetGraphicsResetStatus))
    return COGL_GRAPHICS_RESET_STATUS_NO_ERROR;

  GE_RET (status, driver, glGetGraphicsResetStatus ());
  switch (status)
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

static CoglFramebufferDriver *
cogl_driver_gl_create_framebuffer_driver (CoglDriver                         *driver,
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

static void
cogl_driver_gl_flush_framebuffer_state (CoglDriver           *driver,
                                        CoglContext          *ctx,
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
          g_return_if_fail (cogl_context_has_feature
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

static CoglBufferImpl *
cogl_driver_gl_create_buffer_impl (CoglDriver *driver)
{
  return g_object_new (COGL_TYPE_BUFFER_IMPL_GL, NULL);
}

static void
cogl_driver_gl_sampler_init_init (CoglDriver            *driver,
                                  CoglContext           *context,
                                  CoglSamplerCacheEntry *entry)
{
  if (_cogl_has_private_feature (context,
                                 COGL_PRIVATE_FEATURE_SAMPLER_OBJECTS))
    {
      GE (driver, glGenSamplers (1, &entry->sampler_object));

      GE (driver, glSamplerParameteri (entry->sampler_object,
                                       GL_TEXTURE_MIN_FILTER,
                                       entry->min_filter));
      GE (driver, glSamplerParameteri (entry->sampler_object,
                                       GL_TEXTURE_MAG_FILTER,
                                       entry->mag_filter));

      GE (driver, glSamplerParameteri (entry->sampler_object,
                                       GL_TEXTURE_WRAP_S,
                                       entry->wrap_mode_s));
      GE (driver, glSamplerParameteri (entry->sampler_object,
                                       GL_TEXTURE_WRAP_T,
                                       entry->wrap_mode_t));

      /* While COGL_PRIVATE_FEATURE_SAMPLER_OBJECTS implies support for
       * GL_TEXTURE_LOD_BIAS in GL, the same is not true in GLES. So check,
       * and also only apply GL_TEXTURE_LOD_BIAS in mipmap modes:
       */
      if (_cogl_has_private_feature (context,
                                     COGL_PRIVATE_FEATURE_TEXTURE_LOD_BIAS) &&
          entry->min_filter != GL_NEAREST &&
          entry->min_filter != GL_LINEAR)
        {
          GLfloat bias = _cogl_texture_min_filter_get_lod_bias (entry->min_filter);

          GE (driver, glSamplerParameterf (entry->sampler_object,
                                           GL_TEXTURE_LOD_BIAS,
                                           bias));
        }
    }
  else
    {
      CoglDriverGL *driver_gl = COGL_DRIVER_GL (driver);
      CoglDriverGLPrivate *priv = cogl_driver_gl_get_private (driver_gl);

      /* If sampler objects aren't supported then we'll invent a
         unique number so that pipelines can still compare the
         unique state just by comparing the sampler object
         numbers */
      entry->sampler_object = priv->next_fake_sampler_object_number++;
    }
}

static void
cogl_driver_gl_sampler_free (CoglDriver            *driver,
                             CoglContext           *context,
                             CoglSamplerCacheEntry *entry)
{
  if (_cogl_has_private_feature (context,
                                 COGL_PRIVATE_FEATURE_SAMPLER_OBJECTS))
    GE (driver, glDeleteSamplers (1, &entry->sampler_object));
}

static void
cogl_driver_gl_set_uniform (CoglDriver           *driver,
                            GLint                 location,
                            const CoglBoxedValue *value)
{
  switch (value->type)
    {
    case COGL_BOXED_NONE:
      break;

    case COGL_BOXED_INT:
      {
        const int *ptr;

        if (value->count == 1)
          ptr = value->v.int_value;
        else
          ptr = value->v.int_array;

        switch (value->size)
          {
          case 1:
            GE (driver, glUniform1iv (location, value->count, ptr));
            break;
          case 2:
            GE (driver, glUniform2iv (location, value->count, ptr));
            break;
          case 3:
            GE (driver, glUniform3iv (location, value->count, ptr));
            break;
          case 4:
            GE (driver, glUniform4iv (location, value->count, ptr));
            break;
          }
      }
      break;

    case COGL_BOXED_FLOAT:
      {
        const float *ptr;

        if (value->count == 1)
          ptr = value->v.float_value;
        else
          ptr = value->v.float_array;

        switch (value->size)
          {
          case 1:
            GE (driver, glUniform1fv (location, value->count, ptr));
            break;
          case 2:
            GE (driver, glUniform2fv (location, value->count, ptr));
            break;
          case 3:
            GE (driver, glUniform3fv (location, value->count, ptr));
            break;
          case 4:
            GE (driver, glUniform4fv (location, value->count, ptr));
            break;
          }
      }
      break;

    case COGL_BOXED_MATRIX:
      {
        const float *ptr;

        if (value->count == 1)
          ptr = value->v.matrix;
        else
          ptr = value->v.float_array;

        switch (value->size)
          {
          case 2:
            GE (driver, glUniformMatrix2fv (location, value->count,
                                            FALSE, ptr));
            break;
          case 3:
            GE (driver, glUniformMatrix3fv (location, value->count,
                                            FALSE, ptr));
            break;
          case 4:
            GE (driver, glUniformMatrix4fv (location, value->count,
                                            FALSE, ptr));
            break;
          }
      }
      break;
    }
}

static void
cogl_driver_gl_class_init (CoglDriverGLClass *klass)
{
  CoglDriverClass *driver_klass = COGL_DRIVER_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = cogl_driver_gl_dispose;

  driver_klass->context_init = cogl_driver_gl_context_init;
  driver_klass->get_vendor = cogl_driver_gl_get_gl_vendor;
  driver_klass->is_hardware_accelerated = cogl_driver_gl_is_hardware_accelerated;
  driver_klass->get_graphics_reset_status = cogl_driver_gl_get_graphics_reset_status;
  driver_klass->create_framebuffer_driver = cogl_driver_gl_create_framebuffer_driver;
  driver_klass->flush_framebuffer_state = cogl_driver_gl_flush_framebuffer_state;
  driver_klass->flush_attributes_state = _cogl_gl_flush_attributes_state;
  driver_klass->clip_stack_flush = _cogl_clip_stack_gl_flush;
  driver_klass->create_buffer_impl = cogl_driver_gl_create_buffer_impl;
  driver_klass->sampler_init = cogl_driver_gl_sampler_init_init;
  driver_klass->sampler_free = cogl_driver_gl_sampler_free;
  driver_klass->set_uniform = cogl_driver_gl_set_uniform; /* XXX name is weird... */
}

static void
cogl_driver_gl_init (CoglDriverGL *driver)
{
  CoglDriverGLPrivate *priv =
    cogl_driver_gl_get_instance_private (driver);

  priv->next_fake_sampler_object_number = 1;
  priv->texture_units =
    g_array_new (FALSE, FALSE, sizeof (CoglTextureUnit));

  /* See cogl-pipeline.c for more details about why we leave texture unit 1
   * active by default... */
  priv->active_texture_unit = 1;
}

CoglDriverGLPrivate *
cogl_driver_gl_get_private (CoglDriverGL *driver)
{
  return cogl_driver_gl_get_instance_private (driver);
}

gboolean
cogl_driver_gl_is_es (CoglDriverGL *driver)
{
  CoglDriverGLPrivate *priv =
    cogl_driver_gl_get_instance_private (driver);

  return priv->glsl_es;
}

void
cogl_driver_gl_get_glsl_version (CoglDriverGL *driver,
                                 int          *major,
                                 int          *minor)
{
  CoglDriverGLPrivate *priv =
    cogl_driver_gl_get_instance_private (driver);

  *major = priv->glsl_major;
  *minor = priv->glsl_minor;
}

void
cogl_driver_gl_clear_gl_errors (CoglDriverGL *driver)
{
  CoglDriverGLPrivate *priv =
    cogl_driver_gl_get_instance_private (driver);
  GLenum gl_error;

  while ((gl_error = priv->glGetError ()) != GL_NO_ERROR && gl_error != GL_CONTEXT_LOST)
    ;
}

gboolean
cogl_driver_gl_catch_out_of_memory (CoglDriverGL *driver,
                                    GError      **error)
{
  CoglDriverGLPrivate *priv =
    cogl_driver_gl_get_instance_private (driver);
  GLenum gl_error;
  gboolean out_of_memory = FALSE;

  while ((gl_error = priv->glGetError ()) != GL_NO_ERROR && gl_error != GL_CONTEXT_LOST)
    {
      if (gl_error == GL_OUT_OF_MEMORY)
        out_of_memory = TRUE;
#ifdef COGL_ENABLE_DEBUG
      else
        {
          g_warning ("%s: GL error (%d): %s\n",
                     G_STRLOC,
                     gl_error,
                     cogl_gl_error_to_string (gl_error));
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

const char *
cogl_driver_gl_get_gl_string (CoglDriverGL  *driver,
                              GLenum         name)
{
  CoglDriverGLPrivate *priv =
    cogl_driver_gl_get_instance_private (driver);

  return (const char *) priv->glGetString (name);
}


char **
cogl_driver_gl_get_gl_extensions (CoglDriverGL *driver,
                                  CoglRenderer *renderer)
{
  const char *env_disabled_extensions;
  char **ret;

  /* In GL 3, querying GL_EXTENSIONS is deprecated so we have to build
   * the array using glGetStringi instead */
#ifdef HAVE_GL
  if (cogl_renderer_get_driver_id (renderer) == COGL_DRIVER_ID_GL3)
    {
      int num_extensions, i;

      GE (driver, glGetIntegerv (GL_NUM_EXTENSIONS, &num_extensions));

      ret = g_malloc (sizeof (char *) * (num_extensions + 1));

      for (i = 0; i < num_extensions; i++)
        {
          const GLubyte *ext;

          GE_RET (ext, driver, glGetStringi (GL_EXTENSIONS, i));

          ret[i] = g_strdup ((const char *)ext);
        }

      ret[num_extensions] = NULL;
    }
  else
#endif
    {
      const char *all_extensions = cogl_driver_gl_get_gl_string (COGL_DRIVER_GL (driver),
                                                                 GL_EXTENSIONS);

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
cogl_driver_gl_get_gl_version (CoglDriverGL *driver)
{
  const char *version_override;

  if ((version_override = g_getenv ("COGL_OVERRIDE_GL_VERSION")))
    return version_override;
  else
    return cogl_driver_gl_get_gl_string (driver, GL_VERSION);
}

GLenum
cogl_driver_gl_get_gl_error (CoglDriverGL *driver)
{
  GLenum gl_error;

  GE_RET (gl_error, driver, glGetError ());

  if (gl_error != GL_NO_ERROR && gl_error != GL_CONTEXT_LOST)
    return gl_error;
  else
    return GL_NO_ERROR;
}

/* Parses a GL version number stored in a string. @version_string must
 * point to the beginning of the version number (ie, it can't point to
 * the "OpenGL ES" part on GLES). The version number can be followed
 * by the end of the string, a space or a full stop. Anything else
 * will be treated as invalid. Returns TRUE and sets major_out and
 * minor_out if it is successfully parsed or FALSE otherwise. */
gboolean
cogl_parse_gl_version (const char *version_string,
                       int        *major_out,
                       int        *minor_out)
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
