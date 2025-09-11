/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009,2010,2011,2012 Intel Corporation.
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
 *
 * Authors:
 *  Neil Roberts   <neil@linux.intel.com>
 *  Robert Bragg   <robert@linux.intel.com>
 */


#include "config.h"

#include "cogl/cogl-context-private.h"
#include "cogl/cogl-graphene.h"
#include "cogl/cogl-primitives-private.h"
#include "cogl/driver/gl/cogl-driver-gl-private.h"
#include "cogl/driver/gl/cogl-util-gl-private.h"
#include "cogl/driver/gl/cogl-pipeline-gl-private.h"
#include "cogl/driver/gl/cogl-clip-stack-gl-private.h"
#include "mtk/mtk.h"

static void
add_stencil_clip_rectangle (CoglFramebuffer *framebuffer,
                            CoglMatrixEntry *modelview_entry,
                            float x_1,
                            float y_1,
                            float x_2,
                            float y_2,
                            gboolean merge)
{
  CoglMatrixStack *projection_stack =
    _cogl_framebuffer_get_projection_stack (framebuffer);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  CoglDriver *driver = cogl_context_get_driver (ctx);
  CoglMatrixEntry *old_projection_entry, *old_modelview_entry;

  /* NB: This can be called while flushing the journal so we need
   * to be very conservative with what state we change.
   */
  old_projection_entry = g_steal_pointer (&ctx->current_projection_entry);
  old_modelview_entry = g_steal_pointer (&ctx->current_modelview_entry);

  ctx->current_projection_entry = projection_stack->last_entry;
  ctx->current_modelview_entry = modelview_entry;

  GE (driver, glColorMask (FALSE, FALSE, FALSE, FALSE));
  GE (driver, glDepthMask (FALSE));
  GE (driver, glStencilMask (0x3));

  if (merge)
    {
      /* Add one to every pixel of the stencil buffer in the
	 rectangle */
      GE (driver, glStencilFunc (GL_NEVER, 0x1, 0x3));
      GE (driver, glStencilOp (GL_INCR, GL_INCR, GL_INCR));
      _cogl_rectangle_immediate (framebuffer,
                                 ctx->stencil_pipeline,
                                 x_1, y_1, x_2, y_2);

      /* Subtract one from all pixels in the stencil buffer so that
	 only pixels where both the original stencil buffer and the
	 rectangle are set will be valid */
      GE (driver, glStencilOp (GL_DECR, GL_DECR, GL_DECR));

      ctx->current_projection_entry = &ctx->identity_entry;
      ctx->current_modelview_entry = &ctx->identity_entry;

      _cogl_rectangle_immediate (framebuffer,
                                 ctx->stencil_pipeline,
                                 -1.0, -1.0, 1.0, 1.0);
    }
  else
    {
      GE (driver, glEnable (GL_STENCIL_TEST));

      /* Initially disallow everything */
      GE (driver, glClearStencil (0));
      GE (driver, glClear (GL_STENCIL_BUFFER_BIT));

      /* Punch out a hole to allow the rectangle */
      GE (driver, glStencilFunc (GL_ALWAYS, 0x1, 0x1));
      GE (driver, glStencilOp (GL_KEEP, GL_KEEP, GL_REPLACE));
      _cogl_rectangle_immediate (framebuffer,
                                 ctx->stencil_pipeline,
                                 x_1, y_1, x_2, y_2);
    }

  ctx->current_projection_entry = old_projection_entry;
  ctx->current_modelview_entry = old_modelview_entry;

  /* Restore the stencil mode */
  GE (driver, glDepthMask (TRUE));
  GE (driver, glColorMask (TRUE, TRUE, TRUE, TRUE));
  GE (driver, glStencilMask (0x0));
  GE (driver, glStencilFunc (GL_EQUAL, 0x1, 0x1));
  GE (driver, glStencilOp (GL_KEEP, GL_KEEP, GL_KEEP));
}

static void
add_stencil_clip_region (CoglFramebuffer *framebuffer,
                         MtkRegion       *region,
                         gboolean         merge)
{
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  CoglDriver *driver = cogl_context_get_driver (ctx);
  CoglMatrixEntry *old_projection_entry, *old_modelview_entry;
  graphene_matrix_t matrix;
  int num_rectangles = mtk_region_num_rectangles (region);
  int i;
  CoglVertexP2 *vertices;
  graphene_point3d_t p;

  /* NB: This can be called while flushing the journal so we need
   * to be very conservative with what state we change.
   */
  old_projection_entry = g_steal_pointer (&ctx->current_projection_entry);
  old_modelview_entry = g_steal_pointer (&ctx->current_modelview_entry);

  ctx->current_projection_entry = &ctx->identity_entry;
  ctx->current_modelview_entry = &ctx->identity_entry;

  /* The coordinates in the region are meant to be window coordinates,
   * make a matrix that translates those across the viewport, and into
   * the default [-1, -1, 1, 1] range.
   */
  graphene_point3d_init (&p,
                         - cogl_framebuffer_get_viewport_x (framebuffer),
                         - cogl_framebuffer_get_viewport_y (framebuffer),
                         0);

  graphene_matrix_init_translate (&matrix, &p);
  graphene_matrix_scale (&matrix,
                         2.0f / cogl_framebuffer_get_viewport_width (framebuffer),
                         - 2.0f / cogl_framebuffer_get_viewport_height (framebuffer),
                         1.0);
  graphene_matrix_translate (&matrix, &GRAPHENE_POINT3D_INIT (-1.f, 1.f, 0.f));

  GE (driver, glColorMask (FALSE, FALSE, FALSE, FALSE));
  GE (driver, glDepthMask (FALSE));
  GE (driver, glStencilMask (0x3));

  if (merge)
    {
      GE (driver, glStencilFunc (GL_ALWAYS, 0x1, 0x3));
      GE (driver, glStencilOp (GL_KEEP, GL_KEEP, GL_INCR));
    }
  else
    {
      GE (driver, glEnable (GL_STENCIL_TEST));

      /* Initially disallow everything */
      GE (driver, glClearStencil (0));
      GE (driver, glClear (GL_STENCIL_BUFFER_BIT));

      /* Punch out holes to allow the rectangles */
      GE (driver, glStencilFunc (GL_ALWAYS, 0x1, 0x1));
      GE (driver, glStencilOp (GL_KEEP, GL_KEEP, GL_REPLACE));
    }

  vertices = g_alloca (sizeof (CoglVertexP2) * num_rectangles * 6);

  for (i = 0; i < num_rectangles; i++)
    {
      MtkRectangle rect;
      float x1, y1, z1, w1;
      float x2, y2, z2, w2;
      CoglVertexP2 *v = vertices + i * 6;

      rect = mtk_region_get_rectangle (region, i);

      x1 = rect.x;
      y1 = rect.y;
      z1 = 0.f;
      w1 = 1.f;

      x2 = rect.x + rect.width;
      y2 = rect.y + rect.height;
      z2 = 0.f;
      w2 = 1.f;

      cogl_graphene_matrix_project_point (&matrix, &x1, &y1, &z1, &w1);
      cogl_graphene_matrix_project_point (&matrix, &x2, &y2, &z2, &w2);

      v[0].x = x1;
      v[0].y = y1;
      v[1].x = x1;
      v[1].y = y2;
      v[2].x = x2;
      v[2].y = y1;
      v[3].x = x1;
      v[3].y = y2;
      v[4].x = x2;
      v[4].y = y2;
      v[5].x = x2;
      v[5].y = y1;
    }

  cogl_2d_primitives_immediate (framebuffer,
                                ctx->stencil_pipeline,
                                COGL_VERTICES_MODE_TRIANGLES,
                                vertices,
                                6 * num_rectangles);

  if (merge)
    {
      /* Subtract one from all pixels in the stencil buffer so that
       * only pixels where both the original stencil buffer and the
       * region are set will be valid
       */
      GE (driver, glStencilOp (GL_KEEP, GL_KEEP, GL_DECR));
      _cogl_rectangle_immediate (framebuffer,
                                 ctx->stencil_pipeline,
                                 -1.0, -1.0, 1.0, 1.0);
    }

  ctx->current_projection_entry = old_projection_entry;
  ctx->current_modelview_entry = old_modelview_entry;

  /* Restore the stencil mode */
  GE (driver, glDepthMask (TRUE));
  GE (driver, glColorMask (TRUE, TRUE, TRUE, TRUE));
  GE (driver, glStencilMask (0x0));
  GE (driver, glStencilFunc (GL_EQUAL, 0x1, 0x1));
  GE (driver, glStencilOp (GL_KEEP, GL_KEEP, GL_KEEP));
}

void
_cogl_clip_stack_gl_flush (CoglDriver      *driver,
                           CoglClipStack   *stack,
                           CoglFramebuffer *framebuffer)
{
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  gboolean using_stencil_buffer = FALSE;
  int scissor_x0;
  int scissor_y0;
  int scissor_x1;
  int scissor_y1;
  CoglClipStack *entry;
  int scissor_y_start;

  /* If we have already flushed this state then we don't need to do
     anything */
  if (ctx->current_clip_stack_valid)
    {
      if (ctx->current_clip_stack == stack)
        return;

      _cogl_clip_stack_unref (ctx->current_clip_stack);
    }

  ctx->current_clip_stack_valid = TRUE;
  ctx->current_clip_stack = _cogl_clip_stack_ref (stack);

  GE (driver, glDisable (GL_STENCIL_TEST));

  /* If the stack is empty then there's nothing else to do
   */
  if (stack == NULL)
    {
      COGL_NOTE (CLIPPING, "Flushed empty clip stack");

      GE (driver, glDisable (GL_SCISSOR_TEST));
      return;
    }

  /* Calculate the scissor rect first so that if we eventually have to
     clear the stencil buffer then the clear will be clipped to the
     intersection of all of the bounding boxes. This saves having to
     clear the whole stencil buffer */
  _cogl_clip_stack_get_bounds (stack,
                               &scissor_x0, &scissor_y0,
                               &scissor_x1, &scissor_y1);

  /* Enable scissoring as soon as possible */
  if (scissor_x0 >= scissor_x1 || scissor_y0 >= scissor_y1)
    scissor_x0 = scissor_y0 = scissor_x1 = scissor_y1 = scissor_y_start = 0;
  else
    {
      /* We store the entry coordinates in Cogl coordinate space
       * but OpenGL requires the window origin to be the bottom
       * left so we may need to convert the incoming coordinates.
       *
       * NB: Cogl forces all offscreen rendering to be done upside
       * down so in this case no conversion is needed.
       */

      if (cogl_framebuffer_is_y_flipped (framebuffer))
        {
          scissor_y_start = scissor_y0;
        }
      else
        {
          int framebuffer_height =
            cogl_framebuffer_get_height (framebuffer);

          scissor_y_start = framebuffer_height - scissor_y1;
        }
    }

  COGL_NOTE (CLIPPING, "Flushing scissor to (%i, %i, %i, %i)",
             scissor_x0, scissor_y0,
             scissor_x1, scissor_y1);

  GE (driver, glEnable (GL_SCISSOR_TEST));
  GE (driver, glScissor (scissor_x0, scissor_y_start,
                         scissor_x1 - scissor_x0,
                         scissor_y1 - scissor_y0));

  /* Add all of the entries. This will end up adding them in the
     reverse order that they were specified but as all of the clips
     are intersecting it should work out the same regardless of the
     order */
  for (entry = stack; entry; entry = entry->parent)
    {
      switch (entry->type)
        {
        case COGL_CLIP_STACK_RECT:
            {
              CoglClipStackRect *rect = (CoglClipStackRect *) entry;

              /* We don't need to do anything extra if the clip for this
                 rectangle was entirely described by its scissor bounds */
              if (!rect->can_be_scissor ||
                  G_UNLIKELY (COGL_DEBUG_ENABLED (COGL_DEBUG_STENCILLING)))
                {
                  COGL_NOTE (CLIPPING, "Adding stencil clip for rectangle");

                  add_stencil_clip_rectangle (framebuffer,
                                              rect->matrix_entry,
                                              rect->x0,
                                              rect->y0,
                                              rect->x1,
                                              rect->y1,
                                              using_stencil_buffer);
                  using_stencil_buffer = TRUE;
                }
              break;
            }
        case COGL_CLIP_STACK_REGION:
            {
              CoglClipStackRegion *region = (CoglClipStackRegion *) entry;

              /* If nrectangles <= 1, it can be fully represented with the
               * scissor clip.
               */
              if (mtk_region_num_rectangles (region->region) > 1 ||
                  G_UNLIKELY (COGL_DEBUG_ENABLED (COGL_DEBUG_STENCILLING)))
                {
                  COGL_NOTE (CLIPPING, "Adding stencil clip for region");

                  add_stencil_clip_region (framebuffer, region->region,
                                           using_stencil_buffer);
                  using_stencil_buffer = TRUE;
                }
              break;
            }
        }
    }
}
