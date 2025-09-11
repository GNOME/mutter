/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2010,2011,2012 Intel Corporation.
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

#include <string.h>

#include "cogl/cogl-private.h"
#include "cogl/cogl-context-private.h"
#include "cogl/cogl-attribute.h"
#include "cogl/cogl-attribute-private.h"
#include "cogl/driver/gl/cogl-attribute-gl-private.h"
#include "cogl/driver/gl/cogl-buffer-impl-gl-private.h"
#include "cogl/driver/gl/cogl-driver-gl-private.h"
#include "cogl/driver/gl/cogl-pipeline-gl-private.h"
#include "cogl/driver/gl/cogl-pipeline-progend-glsl-private.h"

typedef struct _ForeachChangedBitState
{
  CoglContext *context;
  const CoglBitmask *new_bits;
  CoglPipeline *pipeline;
} ForeachChangedBitState;

static gboolean
toggle_custom_attribute_enabled_cb (int bit_num, void *user_data)
{
  ForeachChangedBitState *state = user_data;
  gboolean enabled = _cogl_bitmask_get (state->new_bits, bit_num);
  CoglContext *context = state->context;
  CoglDriver *driver = cogl_context_get_driver (context);

  if (enabled)
    GE (driver, glEnableVertexAttribArray (bit_num));
  else
    GE (driver, glDisableVertexAttribArray (bit_num));

  return TRUE;
}

static void
foreach_changed_bit_and_save (CoglContext *context,
                              CoglBitmask *current_bits,
                              const CoglBitmask *new_bits,
                              CoglBitmaskForeachFunc callback,
                              ForeachChangedBitState *state)
{
  /* Get the list of bits that are different */
  _cogl_bitmask_clear_all (&context->changed_bits_tmp);
  _cogl_bitmask_set_bits (&context->changed_bits_tmp, current_bits);
  _cogl_bitmask_xor_bits (&context->changed_bits_tmp, new_bits);

  /* Iterate over each bit to change */
  state->new_bits = new_bits;
  _cogl_bitmask_foreach (&context->changed_bits_tmp,
                         callback,
                         state);

  /* Store the new values */
  _cogl_bitmask_clear_all (current_bits);
  _cogl_bitmask_set_bits (current_bits, new_bits);
}

static void
setup_generic_buffered_attribute (CoglContext *context,
                                  CoglPipeline *pipeline,
                                  CoglAttribute *attribute,
                                  uint8_t *base)
{
  CoglDriver *driver = cogl_context_get_driver (context);
  int name_index = attribute->name_state->name_index;
  int attrib_location =
    _cogl_pipeline_progend_glsl_get_attrib_location (pipeline, name_index);

  if (attrib_location == -1)
    return;

  GE (driver, glVertexAttribPointer (attrib_location,
                                     attribute->n_components,
                                     attribute->type,
                                     attribute->normalized,
                                     attribute->stride,
                                     base + attribute->offset));
  _cogl_bitmask_set (&context->enable_custom_attributes_tmp,
                     attrib_location, TRUE);
}

static void
apply_attribute_enable_updates (CoglContext *context,
                                CoglPipeline *pipeline)
{
  ForeachChangedBitState changed_bits_state;

  changed_bits_state.context = context;
  changed_bits_state.pipeline = pipeline;
  changed_bits_state.new_bits = &context->enable_custom_attributes_tmp;
  foreach_changed_bit_and_save (context,
                                &context->enabled_custom_attributes,
                                &context->enable_custom_attributes_tmp,
                                toggle_custom_attribute_enabled_cb,
                                &changed_bits_state);
}

void
_cogl_gl_flush_attributes_state (CoglDriver           *driver,
                                 CoglFramebuffer      *framebuffer,
                                 CoglPipeline         *pipeline,
                                 CoglFlushLayerState  *layers_state,
                                 CoglDrawFlags         flags,
                                 CoglAttribute       **attributes,
                                 int                   n_attributes)
{
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  int i;
  gboolean with_color_attrib = FALSE;
  gboolean unknown_color_alpha = FALSE;
  CoglPipeline *copy = NULL;

  /* Iterate the attributes to see if we have a color attribute which
   * may affect our decision to enable blending or not.
   *
   * We need to do this before flushing the pipeline. */
  for (i = 0; i < n_attributes; i++)
    switch (attributes[i]->name_state->name_id)
      {
      case COGL_ATTRIBUTE_NAME_ID_COLOR_ARRAY:
        if ((flags & COGL_DRAW_COLOR_ATTRIBUTE_IS_OPAQUE) == 0 &&
            _cogl_attribute_get_n_components (attributes[i]) == 4)
          unknown_color_alpha = TRUE;
        with_color_attrib = TRUE;
        break;

      default:
        break;
      }

  if (G_UNLIKELY (layers_state->options.flags))
    {
      /* If we haven't already created a derived pipeline... */
      if (!copy)
        {
          copy = cogl_pipeline_copy (pipeline);
          pipeline = copy;
        }
      _cogl_pipeline_apply_overrides (pipeline, &layers_state->options);

      /* TODO:
       * overrides = cogl_pipeline_get_data (pipeline,
       *                                     last_overrides_key);
       * if (overrides)
       *   {
       *     age = cogl_pipeline_get_age (pipeline);
       *     XXX: actually we also need to check for legacy_state
       *     if (overrides->ags != age ||
       *         memcmp (&overrides->options, &options,
       *                 sizeof (options) != 0)
       *       {
       *         g_object_unref (overrides->weak_pipeline);
       *         g_free (overrides);
       *         overrides = NULL;
       *       }
       *   }
       * if (!overrides)
       *   {
       *     overrides = g_new0 (Overrides, 1);
       *     overrides->weak_pipeline =
       *       cogl_pipeline_weak_copy (pipeline);
       *     _cogl_pipeline_apply_overrides (overrides->weak_pipeline,
       *                                     &options);
       *
       *     cogl_pipeline_set_data (pipeline, last_overrides_key,
       *                             weak_overrides,
       *                             free_overrides_cb,
       *                             NULL);
       *   }
       * pipeline = overrides->weak_pipeline;
       */
    }

  _cogl_pipeline_flush_gl_state (ctx,
                                 pipeline,
                                 framebuffer,
                                 with_color_attrib,
                                 unknown_color_alpha);

  _cogl_bitmask_clear_all (&ctx->enable_custom_attributes_tmp);

  /* Bind the attribute pointers. We need to do this after the
   * pipeline is flushed because when using GLSL that is the only
   * point when we can determine the attribute locations */

  for (i = 0; i < n_attributes; i++)
    {
      CoglAttribute *attribute = attributes[i];
      CoglAttributeBuffer *attribute_buffer;
      CoglBuffer *buffer;
      uint8_t *base;

      attribute_buffer = cogl_attribute_get_buffer (attribute);
      buffer = COGL_BUFFER (attribute_buffer);

      /* Note: we don't try and catch errors with binding buffers
        * here since OOM errors at this point indicate that nothing
        * has yet been uploaded to attribute buffer which we
        * consider to be a programmer error.
        */
      base =
        _cogl_buffer_gl_bind (buffer,
                              COGL_BUFFER_BIND_TARGET_ATTRIBUTE_BUFFER,
                              NULL);

      setup_generic_buffered_attribute (ctx, pipeline, attribute, base);

      _cogl_buffer_gl_unbind (buffer);
    }

  apply_attribute_enable_updates (ctx, pipeline);

  if (copy)
    g_object_unref (copy);
}
