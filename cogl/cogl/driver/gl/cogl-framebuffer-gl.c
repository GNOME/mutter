/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009,2012 Intel Corporation.
 * Copyright (C) 2018 DisplayLink (UK) Ltd.
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

#include "cogl/cogl-context-private.h"
#include "cogl/cogl-framebuffer-private.h"
#include "cogl/cogl-framebuffer.h"
#include "cogl/cogl-indices-private.h"
#include "cogl/cogl-offscreen-private.h"
#include "cogl/cogl-texture-private.h"
#include "cogl/driver/gl/cogl-util-gl-private.h"
#include "cogl/driver/gl/cogl-framebuffer-gl-private.h"
#include "cogl/driver/gl/cogl-bitmap-gl-private.h"
#include "cogl/driver/gl/cogl-buffer-impl-gl-private.h"
  #include "cogl/driver/gl/cogl-driver-gl-private.h"
  #include "cogl/driver/gl/cogl-texture-driver-gl-private.h"

#include <glib.h>
#include <string.h>

G_DEFINE_ABSTRACT_TYPE (CoglGlFramebuffer, cogl_gl_framebuffer,
                        COGL_TYPE_FRAMEBUFFER_DRIVER)

static CoglContext *
context_from_driver (CoglFramebufferDriver *driver)
{
  CoglFramebuffer *framebuffer =
    cogl_framebuffer_driver_get_framebuffer (driver);

  return cogl_framebuffer_get_context (framebuffer);
}

static void
cogl_gl_framebuffer_flush_viewport_state (CoglGlFramebuffer *gl_framebuffer)
{
  CoglFramebufferDriver *driver = COGL_FRAMEBUFFER_DRIVER (gl_framebuffer);
  CoglFramebuffer *framebuffer =
    cogl_framebuffer_driver_get_framebuffer (driver);
  float viewport_x, viewport_y, viewport_width, viewport_height;
  float gl_viewport_y;

  cogl_framebuffer_get_viewport4f (framebuffer,
                                   &viewport_x,
                                   &viewport_y,
                                   &viewport_width,
                                   &viewport_height);

  g_return_if_fail (viewport_width >= 0);
  g_return_if_fail (viewport_height >= 0);

  /* Convert the Cogl viewport y offset to an OpenGL viewport y offset
   * NB: OpenGL defines its window and viewport origins to be bottom
   * left, while Cogl defines them to be top left.
   */
  if (cogl_framebuffer_is_y_flipped (framebuffer))
    gl_viewport_y = viewport_y;
  else
    gl_viewport_y =
      cogl_framebuffer_get_height (framebuffer) -
      (viewport_y + viewport_height);

  COGL_NOTE (OPENGL, "Calling glViewport(%f, %f, %f, %f)",
             viewport_x,
             gl_viewport_y,
             viewport_width,
             viewport_height);

  GE (cogl_framebuffer_get_context (framebuffer),
      glViewport ((GLint) viewport_x,
                  (GLint) gl_viewport_y,
                  (GLsizei) viewport_width,
                  (GLsizei) viewport_height));
}

static void
cogl_gl_framebuffer_flush_clip_state (CoglGlFramebuffer *gl_framebuffer)
{
  CoglFramebufferDriver *driver = COGL_FRAMEBUFFER_DRIVER (gl_framebuffer);
  CoglFramebuffer *framebuffer =
    cogl_framebuffer_driver_get_framebuffer (driver);

  _cogl_clip_stack_flush (_cogl_framebuffer_get_clip_stack (framebuffer),
                          framebuffer);
}

static void
cogl_gl_framebuffer_flush_dither_state (CoglGlFramebuffer *gl_framebuffer)
{
  CoglFramebufferDriver *driver = COGL_FRAMEBUFFER_DRIVER (gl_framebuffer);
  CoglFramebuffer *framebuffer =
    cogl_framebuffer_driver_get_framebuffer (driver);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  gboolean is_dither_enabled;

  is_dither_enabled = cogl_framebuffer_get_dither_enabled (framebuffer);
  if (ctx->current_gl_dither_enabled != is_dither_enabled)
    {
      if (is_dither_enabled)
        GE (ctx, glEnable (GL_DITHER));
      else
        GE (ctx, glDisable (GL_DITHER));
      ctx->current_gl_dither_enabled = is_dither_enabled;
    }
}

static void
cogl_gl_framebuffer_flush_modelview_state (CoglGlFramebuffer *gl_framebuffer)
{
  CoglFramebufferDriver *driver = COGL_FRAMEBUFFER_DRIVER (gl_framebuffer);
  CoglFramebuffer *framebuffer =
    cogl_framebuffer_driver_get_framebuffer (driver);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  CoglMatrixEntry *modelview_entry =
    _cogl_framebuffer_get_modelview_entry (framebuffer);

  _cogl_context_set_current_modelview_entry (ctx, modelview_entry);
}

static void
cogl_gl_framebuffer_flush_projection_state (CoglGlFramebuffer *gl_framebuffer)
{
  CoglFramebufferDriver *driver = COGL_FRAMEBUFFER_DRIVER (gl_framebuffer);
  CoglFramebuffer *framebuffer =
    cogl_framebuffer_driver_get_framebuffer (driver);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  CoglMatrixEntry *projection_entry =
    _cogl_framebuffer_get_projection_entry (framebuffer);

  _cogl_context_set_current_projection_entry (ctx, projection_entry);
}

static void
cogl_gl_framebuffer_flush_front_face_winding_state (CoglGlFramebuffer *gl_framebuffer)
{
  CoglFramebufferDriver *driver = COGL_FRAMEBUFFER_DRIVER (gl_framebuffer);
  CoglFramebuffer *framebuffer =
    cogl_framebuffer_driver_get_framebuffer (driver);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglPipelineCullFaceMode mode;

  /* NB: The face winding state is actually owned by the current
   * CoglPipeline.
   *
   * If we don't have a current pipeline then we can just assume that
   * when we later do flush a pipeline we will check the current
   * framebuffer to know how to setup the winding */
  if (!context->current_pipeline)
    return;

  mode = cogl_pipeline_get_cull_face_mode (context->current_pipeline);

  /* If the current CoglPipeline has a culling mode that doesn't care
   * about the winding we can avoid forcing an update of the state and
   * bail out. */
  if (mode == COGL_PIPELINE_CULL_FACE_MODE_NONE ||
      mode == COGL_PIPELINE_CULL_FACE_MODE_BOTH)
    return;

  /* Since the winding state is really owned by the current pipeline
   * the way we "flush" an updated winding is to dirty the pipeline
   * state... */
  context->current_pipeline_changes_since_flush |=
    COGL_PIPELINE_STATE_CULL_FACE;
  context->current_pipeline_age--;
}

void
cogl_gl_framebuffer_flush_state_differences (CoglGlFramebuffer *gl_framebuffer,
                                             unsigned long      differences)
{
  int bit;

  COGL_FLAGS_FOREACH_START (&differences, 1, bit)
    {
      /* XXX: We considered having an array of callbacks for each state index
       * that we'd call here but decided that this way the compiler is more
       * likely going to be able to in-line the flush functions and use the
       * index to jump straight to the required code. */
      switch (bit)
        {
        case COGL_FRAMEBUFFER_STATE_INDEX_VIEWPORT:
          cogl_gl_framebuffer_flush_viewport_state (gl_framebuffer);
          break;
        case COGL_FRAMEBUFFER_STATE_INDEX_CLIP:
          cogl_gl_framebuffer_flush_clip_state (gl_framebuffer);
          break;
        case COGL_FRAMEBUFFER_STATE_INDEX_DITHER:
          cogl_gl_framebuffer_flush_dither_state (gl_framebuffer);
          break;
        case COGL_FRAMEBUFFER_STATE_INDEX_MODELVIEW:
          cogl_gl_framebuffer_flush_modelview_state (gl_framebuffer);
          break;
        case COGL_FRAMEBUFFER_STATE_INDEX_PROJECTION:
          cogl_gl_framebuffer_flush_projection_state (gl_framebuffer);
          break;
        case COGL_FRAMEBUFFER_STATE_INDEX_FRONT_FACE_WINDING:
          cogl_gl_framebuffer_flush_front_face_winding_state (gl_framebuffer);
          break;
        case COGL_FRAMEBUFFER_STATE_INDEX_DEPTH_WRITE:
          /* Nothing to do for depth write state change; the state will always
           * be taken into account when flushing the pipeline's depth state. */
          break;
        default:
          g_warn_if_reached ();
        }
    }
  COGL_FLAGS_FOREACH_END;
}

void
cogl_gl_framebuffer_bind (CoglGlFramebuffer *gl_framebuffer,
                          GLenum             target)
{
  COGL_GL_FRAMEBUFFER_GET_CLASS (gl_framebuffer)->bind (gl_framebuffer,
                                                        target);
}

static void
cogl_gl_framebuffer_clear (CoglFramebufferDriver *driver,
                           unsigned long          buffers,
                           float                  red,
                           float                  green,
                           float                  blue,
                           float                  alpha)
{
  CoglContext *ctx = context_from_driver (driver);
  GLbitfield gl_buffers = 0;

  if (buffers & COGL_BUFFER_BIT_COLOR)
    {
      GE( ctx, glClearColor (red, green, blue, alpha) );
      gl_buffers |= GL_COLOR_BUFFER_BIT;
    }

  if (buffers & COGL_BUFFER_BIT_DEPTH)
    {
      CoglFramebuffer *framebuffer =
        cogl_framebuffer_driver_get_framebuffer (driver);
      gboolean is_depth_writing_enabled;

      gl_buffers |= GL_DEPTH_BUFFER_BIT;

      is_depth_writing_enabled =
        cogl_framebuffer_get_depth_write_enabled (framebuffer);
      if (ctx->depth_writing_enabled_cache != is_depth_writing_enabled)
        {
          GE( ctx, glDepthMask (is_depth_writing_enabled));

          ctx->depth_writing_enabled_cache = is_depth_writing_enabled;

          /* Make sure the DepthMask is updated when the next primitive is drawn */
          ctx->current_pipeline_changes_since_flush |=
            COGL_PIPELINE_STATE_DEPTH;
          ctx->current_pipeline_age--;
        }
    }

  if (buffers & COGL_BUFFER_BIT_STENCIL)
    gl_buffers |= GL_STENCIL_BUFFER_BIT;


  GE (ctx, glClear (gl_buffers));
}

static void
cogl_gl_framebuffer_finish (CoglFramebufferDriver *driver)
{
  CoglContext *ctx = context_from_driver (driver);

  /* Update our "latest" sync fd to contain all previous work */
  _cogl_context_update_sync (ctx);

  ctx->glFinish ();
}

static void
cogl_gl_framebuffer_flush (CoglFramebufferDriver *driver)
{
  CoglContext *ctx = context_from_driver (driver);

  /* Update our "latest" sync fd to contain all previous work */
  _cogl_context_update_sync (ctx);

  ctx->glFlush ();
}

static void
cogl_gl_framebuffer_draw_attributes (CoglFramebufferDriver  *driver,
                                     CoglPipeline           *pipeline,
                                     CoglVerticesMode        mode,
                                     int                     first_vertex,
                                     int                     n_vertices,
                                     CoglAttribute         **attributes,
                                     int                     n_attributes,
                                     CoglDrawFlags           flags)
{
  CoglFramebuffer *framebuffer =
    cogl_framebuffer_driver_get_framebuffer (driver);

  _cogl_flush_attributes_state (framebuffer, pipeline, flags,
                                attributes, n_attributes);

  GE (cogl_framebuffer_get_context (framebuffer),
      glDrawArrays ((GLenum)mode, first_vertex, n_vertices));
}

static void
cogl_gl_framebuffer_draw_indexed_attributes (CoglFramebufferDriver  *driver,
                                             CoglPipeline           *pipeline,
                                             CoglVerticesMode        mode,
                                             int                     first_vertex,
                                             int                     n_vertices,
                                             CoglIndices            *indices,
                                             CoglAttribute         **attributes,
                                             int                     n_attributes,
                                             CoglDrawFlags           flags)
{
  CoglFramebuffer *framebuffer =
    cogl_framebuffer_driver_get_framebuffer (driver);
  CoglBuffer *buffer;
  uint8_t *base;
  size_t index_size;
  GLenum indices_gl_type = 0;

  _cogl_flush_attributes_state (framebuffer, pipeline, flags,
                                attributes, n_attributes);

  buffer = COGL_BUFFER (cogl_indices_get_buffer (indices));

  /* Note: we don't try and catch errors with binding the index buffer
   * here since OOM errors at this point indicate that nothing has yet
   * been uploaded to the indices buffer which we consider to be a
   * programmer error.
   */
  base = _cogl_buffer_gl_bind (buffer,
                               COGL_BUFFER_BIND_TARGET_INDEX_BUFFER, NULL);
  index_size = cogl_indices_type_get_size (cogl_indices_get_indices_type (indices));

  switch (cogl_indices_get_indices_type (indices))
    {
    case COGL_INDICES_TYPE_UNSIGNED_BYTE:
      indices_gl_type = GL_UNSIGNED_BYTE;
      break;
    case COGL_INDICES_TYPE_UNSIGNED_SHORT:
      indices_gl_type = GL_UNSIGNED_SHORT;
      break;
    case COGL_INDICES_TYPE_UNSIGNED_INT:
      indices_gl_type = GL_UNSIGNED_INT;
      break;
    }

  GE (cogl_framebuffer_get_context (framebuffer),
      glDrawElements ((GLenum)mode,
                      n_vertices,
                      indices_gl_type,
                      base + index_size * first_vertex));

  _cogl_buffer_gl_unbind (buffer);
}

static gboolean
cogl_gl_framebuffer_read_pixels_into_bitmap (CoglFramebufferDriver  *fb_driver,
                                             int                     x,
                                             int                     y,
                                             CoglReadPixelsFlags     source,
                                             CoglBitmap             *bitmap,
                                             GError                **error)
{
  CoglFramebuffer *framebuffer =
    cogl_framebuffer_driver_get_framebuffer (fb_driver);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  CoglDriver *driver = cogl_context_get_driver (ctx);
  int framebuffer_height = cogl_framebuffer_get_height (framebuffer);
  int width = cogl_bitmap_get_width (bitmap);
  int height = cogl_bitmap_get_height (bitmap);
  CoglPixelFormat format = cogl_bitmap_get_format (bitmap);
  CoglPixelFormat internal_format =
    cogl_framebuffer_get_internal_format (framebuffer);
  CoglDriverGLClass *driver_gl_klass = COGL_DRIVER_GL_GET_CLASS (driver);
  CoglPixelFormat read_format;
  GLenum gl_format;
  GLenum gl_type;
  GLenum gl_pack_enum = GL_FALSE;
  int bytes_per_pixel;
  gboolean format_mismatch;
  gboolean stride_mismatch;
  gboolean pack_invert_set;
  int status = FALSE;

  g_return_val_if_fail (cogl_pixel_format_get_n_planes (format) == 1, FALSE);

  cogl_context_flush_framebuffer_state (ctx,
                                        framebuffer,
                                        framebuffer,
                                        COGL_FRAMEBUFFER_STATE_BIND);

  /* The y coordinate should be given in OpenGL's coordinate system
   * so 0 is the bottom row.
   */
  if (!cogl_framebuffer_is_y_flipped (framebuffer))
    y = framebuffer_height - y - height;

  if (_cogl_has_private_feature (ctx, COGL_PRIVATE_FEATURE_MESA_PACK_INVERT) &&
      (source & COGL_READ_PIXELS_NO_FLIP) == 0 &&
      !cogl_framebuffer_is_y_flipped (framebuffer))
    {
      CoglRenderer *renderer = cogl_context_get_renderer (ctx);

      if (cogl_renderer_get_driver_id (renderer) == COGL_DRIVER_ID_GLES2)
        gl_pack_enum = GL_PACK_REVERSE_ROW_ORDER_ANGLE;
      else
        gl_pack_enum = GL_PACK_INVERT_MESA;

      GE (ctx, glPixelStorei (gl_pack_enum, TRUE));
      pack_invert_set = TRUE;
    }
  else
    pack_invert_set = FALSE;

  read_format = driver_gl_klass->get_read_pixels_format (COGL_DRIVER_GL (driver),
                                                         ctx,
                                                         internal_format,
                                                         format,
                                                         &gl_format,
                                                         &gl_type);

  format_mismatch =
    (read_format & ~COGL_PREMULT_BIT) != (format & ~COGL_PREMULT_BIT);

  bytes_per_pixel = cogl_pixel_format_get_bytes_per_pixel (format, 0);
  stride_mismatch =
    !_cogl_has_private_feature (ctx,
                                COGL_PRIVATE_FEATURE_READ_PIXELS_ANY_STRIDE) &&
    (cogl_bitmap_get_rowstride (bitmap) != bytes_per_pixel * width);

  if (format_mismatch || stride_mismatch)
    {
      CoglBitmap *tmp_bmp;
      int bpp, rowstride;
      uint8_t *tmp_data;
      gboolean succeeded;

      if (_cogl_pixel_format_can_have_premult (read_format))
        {
          read_format = ((read_format & ~COGL_PREMULT_BIT) |
                         (internal_format & COGL_PREMULT_BIT));
        }

      tmp_bmp = _cogl_bitmap_new_with_malloc_buffer (ctx,
                                                     width, height,
                                                     read_format,
                                                     error);
      if (!tmp_bmp)
        goto EXIT;

      bpp = cogl_pixel_format_get_bytes_per_pixel (read_format, 0);
      rowstride = cogl_bitmap_get_rowstride (tmp_bmp);

      driver_gl_klass->prep_gl_for_pixels_download (COGL_DRIVER_GL (driver),
                                                    ctx,
                                                    width,
                                                    rowstride,
                                                    bpp);

      /* Note: we don't worry about catching errors here since we know
       * we won't be lazily allocating storage for this buffer so it
       * won't fail due to lack of memory. */
      tmp_data = _cogl_bitmap_gl_bind (tmp_bmp,
                                       COGL_BUFFER_ACCESS_WRITE,
                                       COGL_BUFFER_MAP_HINT_DISCARD,
                                       NULL);

      GE( ctx, glReadPixels (x, y, width, height,
                             gl_format, gl_type,
                             tmp_data) );

      _cogl_bitmap_gl_unbind (tmp_bmp);

      if (!(internal_format & COGL_A_BIT))
        {
          _cogl_bitmap_set_format (tmp_bmp, read_format & ~COGL_PREMULT_BIT);
          _cogl_bitmap_set_format (bitmap, format & ~COGL_PREMULT_BIT);
        }

      succeeded = _cogl_bitmap_convert_into_bitmap (tmp_bmp, bitmap, error);

      _cogl_bitmap_set_format (bitmap, format);

      g_object_unref (tmp_bmp);

      if (!succeeded)
        goto EXIT;
    }
  else
    {
      CoglBitmap *shared_bmp;
      CoglPixelFormat bmp_format;
      int bpp, rowstride;
      gboolean succeeded = FALSE;
      uint8_t *pixels;
      GError *internal_error = NULL;

      rowstride = cogl_bitmap_get_rowstride (bitmap);

      /* We match the premultiplied state of the target buffer to the
       * premultiplied state of the framebuffer so that it will get
       * converted to the right format below */
      if (_cogl_pixel_format_can_have_premult (format))
        bmp_format = ((format & ~COGL_PREMULT_BIT) |
                      (internal_format & COGL_PREMULT_BIT));
      else
        bmp_format = format;

      if (bmp_format != format)
        shared_bmp = _cogl_bitmap_new_shared (bitmap,
                                              bmp_format,
                                              width, height,
                                              rowstride);
      else
        shared_bmp = g_object_ref (bitmap);

      bpp = cogl_pixel_format_get_bytes_per_pixel (bmp_format, 0);

      driver_gl_klass->prep_gl_for_pixels_download (COGL_DRIVER_GL (driver),
                                                    ctx,
                                                    width,
                                                    rowstride,
                                                    bpp);

      pixels = _cogl_bitmap_gl_bind (shared_bmp,
                                     COGL_BUFFER_ACCESS_WRITE,
                                     0, /* hints */
                                     &internal_error);
      /* NB: _cogl_bitmap_gl_bind() can return NULL in successful
       * cases so we have to explicitly check the cogl error pointer
       * to know if there was a problem */
      if (internal_error)
        {
          g_object_unref (shared_bmp);
          g_propagate_error (error, internal_error);
          goto EXIT;
        }

      GE( ctx, glReadPixels (x, y,
                             width, height,
                             gl_format, gl_type,
                             pixels) );

      _cogl_bitmap_gl_unbind (shared_bmp);

      /* Convert to the premult format specified by the caller
         in-place. This will do nothing if the premult status is already
         correct. */
      if (!(internal_format & COGL_A_BIT) ||
          _cogl_bitmap_convert_premult_status (shared_bmp, format, error))
        succeeded = TRUE;

      g_object_unref (shared_bmp);

      if (!succeeded)
        goto EXIT;
    }

  if (!cogl_framebuffer_is_y_flipped (framebuffer) &&
      (source & COGL_READ_PIXELS_NO_FLIP) == 0 &&
      !pack_invert_set)
    {
      uint8_t *temprow;
      int rowstride;
      uint8_t *pixels;

      rowstride = cogl_bitmap_get_rowstride (bitmap);
      pixels = _cogl_bitmap_map (bitmap,
                                 COGL_BUFFER_ACCESS_READ |
                                 COGL_BUFFER_ACCESS_WRITE,
                                 0, /* hints */
                                 error);

      if (pixels == NULL)
        goto EXIT;

      temprow = g_alloca (rowstride * sizeof (uint8_t));

      /* vertically flip the buffer in-place */
      for (y = 0; y < height / 2; y++)
        {
          if (y != height - y - 1) /* skip center row */
            {
              memcpy (temprow,
                      pixels + y * rowstride, rowstride);
              memcpy (pixels + y * rowstride,
                      pixels + (height - y - 1) * rowstride, rowstride);
              memcpy (pixels + (height - y - 1) * rowstride,
                      temprow,
                      rowstride);
            }
        }

      _cogl_bitmap_unmap (bitmap);
    }

  status = TRUE;

EXIT:

  /* Currently this function owns the pack_invert state and we don't want this
   * to interfere with other Cogl components so all other code can assume that
   * we leave the pack_invert state off. */
  if (pack_invert_set)
    GE (ctx, glPixelStorei (gl_pack_enum, FALSE));

  return status;
}

static void
cogl_gl_framebuffer_init (CoglGlFramebuffer *gl_framebuffer)
{
}

static void
cogl_gl_framebuffer_class_init (CoglGlFramebufferClass *klass)
{
  CoglFramebufferDriverClass *driver_class =
    COGL_FRAMEBUFFER_DRIVER_CLASS (klass);

  driver_class->clear = cogl_gl_framebuffer_clear;
  driver_class->finish = cogl_gl_framebuffer_finish;
  driver_class->flush = cogl_gl_framebuffer_flush;
  driver_class->draw_attributes = cogl_gl_framebuffer_draw_attributes;
  driver_class->draw_indexed_attributes =
    cogl_gl_framebuffer_draw_indexed_attributes;
  driver_class->read_pixels_into_bitmap =
    cogl_gl_framebuffer_read_pixels_into_bitmap;
}
