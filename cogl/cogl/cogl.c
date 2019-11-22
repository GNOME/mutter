/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009,2010 Intel Corporation.
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

#include "cogl-config.h"

#include <string.h>
#include <math.h>
#include <stdlib.h>

#include "cogl-i18n-private.h"
#include "cogl-debug.h"
#include "cogl-util.h"
#include "cogl-context-private.h"
#include "cogl-pipeline-private.h"
#include "cogl-framebuffer-private.h"
#include "cogl-matrix-private.h"
#include "cogl-journal-private.h"
#include "cogl-bitmap-private.h"
#include "cogl-texture-private.h"
#include "cogl-texture-driver.h"
#include "cogl-attribute-private.h"
#include "cogl-framebuffer-private.h"
#include "cogl-renderer-private.h"
#include "cogl-private.h"
#include "cogl1-context.h"
#include "cogl-offscreen.h"
#include "winsys/cogl-winsys-private.h"

#include "deprecated/cogl-framebuffer-deprecated.h"

GCallback
cogl_get_proc_address (const char* name)
{
  _COGL_GET_CONTEXT (ctx, NULL);

  return _cogl_renderer_get_proc_address (ctx->display->renderer, name, FALSE);
}

gboolean
_cogl_check_extension (const char *name, char * const *ext)
{
  while (*ext)
    if (!strcmp (name, *ext))
      return TRUE;
    else
      ext++;

  return FALSE;
}

/* XXX: This API has been deprecated */
void
cogl_set_depth_test_enabled (gboolean setting)
{
  _COGL_GET_CONTEXT (ctx, NO_RETVAL);

  if (ctx->legacy_depth_test_enabled == setting)
    return;

  ctx->legacy_depth_test_enabled = setting;
  if (ctx->legacy_depth_test_enabled)
    ctx->legacy_state_set++;
  else
    ctx->legacy_state_set--;
}

/* XXX: This API has been deprecated */
gboolean
cogl_get_depth_test_enabled (void)
{
  _COGL_GET_CONTEXT (ctx, FALSE);
  return ctx->legacy_depth_test_enabled;
}

void
cogl_set_backface_culling_enabled (gboolean setting)
{
  _COGL_GET_CONTEXT (ctx, NO_RETVAL);

  if (ctx->legacy_backface_culling_enabled == setting)
    return;

  ctx->legacy_backface_culling_enabled = setting;

  if (ctx->legacy_backface_culling_enabled)
    ctx->legacy_state_set++;
  else
    ctx->legacy_state_set--;
}

gboolean
cogl_get_backface_culling_enabled (void)
{
  _COGL_GET_CONTEXT (ctx, FALSE);

  return ctx->legacy_backface_culling_enabled;
}

gboolean
cogl_has_feature (CoglContext *ctx, CoglFeatureID feature)
{
  return COGL_FLAGS_GET (ctx->features, feature);
}

gboolean
cogl_has_features (CoglContext *ctx, ...)
{
  va_list args;
  CoglFeatureID feature;

  va_start (args, ctx);
  while ((feature = va_arg (args, CoglFeatureID)))
    if (!cogl_has_feature (ctx, feature))
      return FALSE;
  va_end (args);

  return TRUE;
}

void
cogl_foreach_feature (CoglContext *ctx,
                      CoglFeatureCallback callback,
                      void *user_data)
{
  int i;
  for (i = 0; i < _COGL_N_FEATURE_IDS; i++)
    if (COGL_FLAGS_GET (ctx->features, i))
      callback (i, user_data);
}

void
cogl_flush (void)
{
  GList *l;

  _COGL_GET_CONTEXT (ctx, NO_RETVAL);

  for (l = ctx->framebuffers; l; l = l->next)
    _cogl_framebuffer_flush_journal (l->data);
}

uint32_t
_cogl_driver_error_quark (void)
{
  return g_quark_from_static_string ("cogl-driver-error-quark");
}

typedef struct _CoglSourceState
{
  CoglPipeline *pipeline;
  int push_count;
  /* If this is TRUE then the pipeline will be copied and the legacy
     state will be applied whenever the pipeline is used. This is
     necessary because some internal Cogl code expects to be able to
     push a temporary pipeline to put GL into a known state. For that
     to work it also needs to prevent applying the legacy state */
  gboolean enable_legacy;
} CoglSourceState;

static void
_push_source_real (CoglPipeline *pipeline, gboolean enable_legacy)
{
  CoglSourceState *top = g_slice_new (CoglSourceState);
  _COGL_GET_CONTEXT (ctx, NO_RETVAL);

  top->pipeline = cogl_object_ref (pipeline);
  top->enable_legacy = enable_legacy;
  top->push_count = 1;

  ctx->source_stack = g_list_prepend (ctx->source_stack, top);
}

/* FIXME: This should take a context pointer for Cogl 2.0 Technically
 * we could make it so we can retrieve a context reference from the
 * pipeline, but this would not by symmetric with cogl_pop_source. */
void
cogl_push_source (void *material_or_pipeline)
{
  CoglPipeline *pipeline = COGL_PIPELINE (material_or_pipeline);

  g_return_if_fail (cogl_is_pipeline (pipeline));

  _cogl_push_source (pipeline, TRUE);
}

/* This internal version of cogl_push_source is the same except it
   never applies the legacy state. Some parts of Cogl use this
   internally to set a temporary pipeline with a known state */
void
_cogl_push_source (CoglPipeline *pipeline, gboolean enable_legacy)
{
  CoglSourceState *top;

  _COGL_GET_CONTEXT (ctx, NO_RETVAL);

  g_return_if_fail (cogl_is_pipeline (pipeline));

  if (ctx->source_stack)
    {
      top = ctx->source_stack->data;
      if (top->pipeline == pipeline && top->enable_legacy == enable_legacy)
        {
          top->push_count++;
          return;
        }
      else
        _push_source_real (pipeline, enable_legacy);
    }
  else
    _push_source_real (pipeline, enable_legacy);
}

/* FIXME: This needs to take a context pointer for Cogl 2.0 */
void
cogl_pop_source (void)
{
  CoglSourceState *top;

  _COGL_GET_CONTEXT (ctx, NO_RETVAL);

  g_return_if_fail (ctx->source_stack);

  top = ctx->source_stack->data;
  top->push_count--;
  if (top->push_count == 0)
    {
      cogl_object_unref (top->pipeline);
      g_slice_free (CoglSourceState, top);
      ctx->source_stack = g_list_delete_link (ctx->source_stack,
                                              ctx->source_stack);
    }
}

/* FIXME: This needs to take a context pointer for Cogl 2.0 */
void *
cogl_get_source (void)
{
  CoglSourceState *top;

  _COGL_GET_CONTEXT (ctx, NULL);

  g_return_val_if_fail (ctx->source_stack, NULL);

  top = ctx->source_stack->data;
  return top->pipeline;
}

gboolean
_cogl_get_enable_legacy_state (void)
{
  CoglSourceState *top;

  _COGL_GET_CONTEXT (ctx, FALSE);

  g_return_val_if_fail (ctx->source_stack, FALSE);

  top = ctx->source_stack->data;
  return top->enable_legacy;
}

void
cogl_set_source (void *material_or_pipeline)
{
  CoglSourceState *top;
  CoglPipeline *pipeline = COGL_PIPELINE (material_or_pipeline);

  _COGL_GET_CONTEXT (ctx, NO_RETVAL);

  g_return_if_fail (cogl_is_pipeline (pipeline));
  g_return_if_fail (ctx->source_stack);

  top = ctx->source_stack->data;
  if (top->pipeline == pipeline && top->enable_legacy)
    return;

  if (top->push_count == 1)
    {
      /* NB: top->pipeline may be only thing keeping pipeline
       * alive currently so ref pipeline first... */
      cogl_object_ref (pipeline);
      cogl_object_unref (top->pipeline);
      top->pipeline = pipeline;
      top->enable_legacy = TRUE;
    }
  else
    {
      top->push_count--;
      cogl_push_source (pipeline);
    }
}

/* Scale from OpenGL normalized device coordinates (ranging from -1 to 1)
 * to Cogl window/framebuffer coordinates (ranging from 0 to buffer-size) with
 * (0,0) being top left. */
#define VIEWPORT_TRANSFORM_X(x, vp_origin_x, vp_width) \
    (  ( ((x) + 1.0) * ((vp_width) / 2.0) ) + (vp_origin_x)  )
/* Note: for Y we first flip all coordinates around the X axis while in
 * normalized device coodinates */
#define VIEWPORT_TRANSFORM_Y(y, vp_origin_y, vp_height) \
    (  ( ((-(y)) + 1.0) * ((vp_height) / 2.0) ) + (vp_origin_y)  )

/* Transform a homogeneous vertex position from model space to Cogl
 * window coordinates (with 0,0 being top left) */
void
_cogl_transform_point (const CoglMatrix *matrix_mv,
                       const CoglMatrix *matrix_p,
                       const float *viewport,
                       float *x,
                       float *y)
{
  float z = 0;
  float w = 1;

  /* Apply the modelview matrix transform */
  cogl_matrix_transform_point (matrix_mv, x, y, &z, &w);

  /* Apply the projection matrix transform */
  cogl_matrix_transform_point (matrix_p, x, y, &z, &w);

  /* Perform perspective division */
  *x /= w;
  *y /= w;

  /* Apply viewport transform */
  *x = VIEWPORT_TRANSFORM_X (*x, viewport[0], viewport[2]);
  *y = VIEWPORT_TRANSFORM_Y (*y, viewport[1], viewport[3]);
}

#undef VIEWPORT_TRANSFORM_X
#undef VIEWPORT_TRANSFORM_Y

uint32_t
_cogl_system_error_quark (void)
{
  return g_quark_from_static_string ("cogl-system-error-quark");
}

void
_cogl_init (void)
{
  static gboolean initialized = FALSE;

  if (initialized == FALSE)
    {
#if !GLIB_CHECK_VERSION (2, 36, 0)
      g_type_init ();
#endif

      _cogl_debug_check_environment ();
      initialized = TRUE;
    }
}
