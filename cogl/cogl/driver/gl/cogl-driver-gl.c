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
  GE (context, glActiveTexture (GL_TEXTURE1));

  return TRUE;
}

static const char *
cogl_driver_gl_get_gl_vendor (CoglDriver  *driver,
                              CoglContext *context)
{
  return (const char *) context->glGetString (GL_VENDOR);
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
cogl_driver_gl_is_hardware_accelerated (CoglDriver  *driver,
                                        CoglContext *ctx)
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

static CoglGraphicsResetStatus
cogl_driver_gl_get_graphics_reset_status (CoglDriver  *driver,
                                          CoglContext *context)
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

static CoglFramebufferDriver *
cogl_driver_gl_create_framebuffer_driver (CoglDriver                         *driver,
                                          CoglContext                        *context,
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
      GE( context, glGenSamplers (1, &entry->sampler_object) );

      GE( context, glSamplerParameteri (entry->sampler_object,
                                        GL_TEXTURE_MIN_FILTER,
                                        entry->min_filter) );
      GE( context, glSamplerParameteri (entry->sampler_object,
                                        GL_TEXTURE_MAG_FILTER,
                                        entry->mag_filter) );

      GE (context, glSamplerParameteri (entry->sampler_object,
                                        GL_TEXTURE_WRAP_S,
                                        entry->wrap_mode_s) );
      GE (context, glSamplerParameteri (entry->sampler_object,
                                        GL_TEXTURE_WRAP_T,
                                        entry->wrap_mode_t) );

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

          GE (context, glSamplerParameterf (entry->sampler_object,
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
    GE( context, glDeleteSamplers (1, &entry->sampler_object) );
}

static void
cogl_driver_gl_set_uniform (CoglDriver           *driver,
                            CoglContext          *ctx,
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
            GE( ctx, glUniform1iv (location, value->count, ptr) );
            break;
          case 2:
            GE( ctx, glUniform2iv (location, value->count, ptr) );
            break;
          case 3:
            GE( ctx, glUniform3iv (location, value->count, ptr) );
            break;
          case 4:
            GE( ctx, glUniform4iv (location, value->count, ptr) );
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
            GE( ctx, glUniform1fv (location, value->count, ptr) );
            break;
          case 2:
            GE( ctx, glUniform2fv (location, value->count, ptr) );
            break;
          case 3:
            GE( ctx, glUniform3fv (location, value->count, ptr) );
            break;
          case 4:
            GE( ctx, glUniform4fv (location, value->count, ptr) );
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
            GE( ctx, glUniformMatrix2fv (location, value->count,
                                         FALSE, ptr) );
            break;
          case 3:
            GE( ctx, glUniformMatrix3fv (location, value->count,
                                         FALSE, ptr) );
            break;
          case 4:
            GE( ctx, glUniformMatrix4fv (location, value->count,
                                         FALSE, ptr) );
            break;
          }
      }
      break;
    }
}

static CoglTimestampQuery *
cogl_driver_gl_create_timestamp_query (CoglDriver  *driver,
                                       CoglContext *context)
{
  CoglTimestampQuery *query;

  g_return_val_if_fail (cogl_context_has_feature (context,
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

static void
cogl_driver_gl_free_timestamp_query (CoglDriver         *driver,
                                     CoglContext        *context,
                                     CoglTimestampQuery *query)
{
  GE (context, glDeleteQueries (1, &query->id));
  g_free (query);
}

static int64_t
cogl_driver_gl_timestamp_query_get_time_ns (CoglDriver         *driver,
                                            CoglContext        *context,
                                            CoglTimestampQuery *query)
{
  int64_t query_time_ns;

  GE (context, glGetQueryObjecti64v (query->id,
                                     GL_QUERY_RESULT,
                                     &query_time_ns));

  return query_time_ns;
}

static int64_t
cogl_driver_gl_get_gpu_time_ns (CoglDriver  *driver,
                                CoglContext *context)
{
  int64_t gpu_time_ns;

  g_return_val_if_fail (cogl_context_has_feature (context,
                                                  COGL_FEATURE_ID_TIMESTAMP_QUERY),
                        0);

  GE (context, glGetInteger64v (GL_TIMESTAMP, &gpu_time_ns));
  return gpu_time_ns;
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
  driver_klass->create_timestamp_query = cogl_driver_gl_create_timestamp_query;
  driver_klass->free_timestamp_query = cogl_driver_gl_free_timestamp_query;
  driver_klass->timestamp_query_get_time_ns = cogl_driver_gl_timestamp_query_get_time_ns;
  driver_klass->get_gpu_time_ns = cogl_driver_gl_get_gpu_time_ns;
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
