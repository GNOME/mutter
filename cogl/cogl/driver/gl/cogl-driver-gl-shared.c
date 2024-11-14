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

#include "cogl/driver/gl/cogl-driver-gl-shared.h"
#include "cogl/driver/gl/cogl-pipeline-opengl-private.h"
#include "cogl/driver/gl/cogl-buffer-gl-private.h"
#include "cogl/driver/gl/cogl-clip-stack-gl-private.h"
#include "cogl/driver/gl/cogl-attribute-gl-private.h"
#include "cogl/driver/gl/cogl-texture-2d-gl-private.h"
#include "cogl/driver/gl/cogl-util-gl-private.h"

G_DEFINE_TYPE_WITH_PRIVATE (CoglGLSharedDriver, cogl_gl_shared_driver, COGL_TYPE_DRIVER);

static void
cogl_gl_shared_driver_dispose (GObject *object)
{
  CoglGLSharedDriver *driver = COGL_GL_SHARED_DRIVER (object);
  CoglGLSharedDriverPrivate *priv =
    cogl_gl_shared_driver_get_instance_private (driver);
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

  G_OBJECT_CLASS (cogl_gl_shared_driver_parent_class)->dispose (object);
}

static gboolean
cogl_gl_shared_driver_context_init (CoglDriver  *driver,
                                    CoglContext *context)
{
  /* See cogl-pipeline.c for more details about why we leave texture unit 1
   * active by default... */
  GE (context, glActiveTexture (GL_TEXTURE1));

  return TRUE;
}

static const char *
cogl_gl_shared_driver_get_gl_vendor (CoglDriver  *driver,
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
cogl_gl_shared_driver_is_hardware_accelerated (CoglDriver  *driver,
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
cogl_gl_shared_driver_get_graphics_reset_status (CoglDriver  *driver,
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

static CoglTimestampQuery *
cogl_gl_shared_driver_create_timestamp_query (CoglDriver  *driver,
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
cogl_gl_shared_driver_free_timestamp_query (CoglDriver         *driver,
                                            CoglContext        *context,
                                            CoglTimestampQuery *query)
{
  GE (context, glDeleteQueries (1, &query->id));
  g_free (query);
}

static int64_t
cogl_gl_shared_driver_timestamp_query_get_time_ns (CoglDriver         *driver,
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
cogl_gl_shared_driver_get_gpu_time_ns (CoglDriver  *driver,
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
cogl_gl_shared_driver_class_init (CoglGLSharedDriverClass *klass)
{
  CoglDriverClass *driver_klass = COGL_DRIVER_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = cogl_gl_shared_driver_dispose;

  driver_klass->context_init = cogl_gl_shared_driver_context_init;
  driver_klass->get_vendor = cogl_gl_shared_driver_get_gl_vendor;
  driver_klass->is_hardware_accelerated = cogl_gl_shared_driver_is_hardware_accelerated;
  driver_klass->get_graphics_reset_status = cogl_gl_shared_driver_get_graphics_reset_status;
  driver_klass->create_framebuffer_driver = _cogl_driver_gl_create_framebuffer_driver;
  driver_klass->flush_framebuffer_state = _cogl_driver_gl_flush_framebuffer_state;
  driver_klass->texture_2d_free = _cogl_texture_2d_gl_free;
  driver_klass->texture_2d_can_create = _cogl_texture_2d_gl_can_create;
  driver_klass->texture_2d_init = _cogl_texture_2d_gl_init;
  driver_klass->texture_2d_allocate = _cogl_texture_2d_gl_allocate;
  driver_klass->texture_2d_copy_from_framebuffer = _cogl_texture_2d_gl_copy_from_framebuffer;
  driver_klass->texture_2d_get_gl_handle = _cogl_texture_2d_gl_get_gl_handle;
  driver_klass->texture_2d_generate_mipmap = _cogl_texture_2d_gl_generate_mipmap;
  driver_klass->texture_2d_copy_from_bitmap = _cogl_texture_2d_gl_copy_from_bitmap;
  driver_klass->texture_2d_is_get_data_supported = _cogl_texture_2d_gl_is_get_data_supported;
  driver_klass->texture_2d_get_data = _cogl_texture_2d_gl_get_data;
  driver_klass->flush_attributes_state = _cogl_gl_flush_attributes_state;
  driver_klass->clip_stack_flush = _cogl_clip_stack_gl_flush;
  driver_klass->buffer_create = _cogl_buffer_gl_create;
  driver_klass->buffer_destroy = _cogl_buffer_gl_destroy;
  driver_klass->buffer_map_range = _cogl_buffer_gl_map_range;
  driver_klass->buffer_unmap = _cogl_buffer_gl_unmap;
  driver_klass->buffer_set_data = _cogl_buffer_gl_set_data;
  driver_klass->sampler_init = _cogl_sampler_gl_init;
  driver_klass->sampler_free = _cogl_sampler_gl_free;
  driver_klass->set_uniform = _cogl_gl_set_uniform; /* XXX name is weird... */
  driver_klass->create_timestamp_query = cogl_gl_shared_driver_create_timestamp_query;
  driver_klass->free_timestamp_query = cogl_gl_shared_driver_free_timestamp_query;
  driver_klass->timestamp_query_get_time_ns = cogl_gl_shared_driver_timestamp_query_get_time_ns;
  driver_klass->get_gpu_time_ns = cogl_gl_shared_driver_get_gpu_time_ns;
}

static void
cogl_gl_shared_driver_init (CoglGLSharedDriver *driver)
{
  CoglGLSharedDriverPrivate *priv =
    cogl_gl_shared_driver_get_instance_private (driver);

  priv->next_fake_sampler_object_number = 1;
  priv->texture_units =
    g_array_new (FALSE, FALSE, sizeof (CoglTextureUnit));

  /* See cogl-pipeline.c for more details about why we leave texture unit 1
   * active by default... */
  priv->active_texture_unit = 1;
}

CoglGLSharedDriverPrivate *
cogl_gl_shared_driver_get_private (CoglGLSharedDriver *driver)
{
  return cogl_gl_shared_driver_get_instance_private (driver);
}
