/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009,2013 Intel Corporation.
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

#pragma once

#include "cogl/cogl-context.h"
#include "cogl/cogl-flags.h"

#include "cogl/cogl-clip-stack.h"
#include "cogl/cogl-matrix-stack.h"
#include "cogl/cogl-pipeline-private.h"
#include "cogl/cogl-buffer-private.h"
#include "cogl/cogl-atlas.h"
#include "cogl/cogl-driver-private.h"
#include "cogl/cogl-texture-driver.h"
#include "cogl/cogl-pipeline-cache.h"
#include "cogl/cogl-texture-2d.h"
#include "cogl/cogl-sampler-cache-private.h"
#include "cogl/cogl-framebuffer-private.h"
#include "cogl/cogl-onscreen-private.h"
#include "cogl/cogl-private.h"
#include "cogl/winsys/cogl-winsys.h"

void
_cogl_context_set_current_projection_entry (CoglContext *context,
                                            CoglMatrixEntry *entry);

void
_cogl_context_set_current_modelview_entry (CoglContext *context,
                                           CoglMatrixEntry *entry);


CoglPipeline *
cogl_context_get_current_pipeline (CoglContext *context);

void
cogl_context_set_current_pipeline (CoglContext  *context,
                                   CoglPipeline *pipeline);

unsigned long
cogl_context_get_current_pipeline_age (CoglContext *context);

void
cogl_context_set_current_pipeline_age (CoglContext   *context,
                                       unsigned long  age);

void
cogl_context_decrement_current_pipeline_age (CoglContext *context);

unsigned long
cogl_context_get_current_pipeline_changes_since_flush (CoglContext *context);

void
cogl_context_set_current_pipeline_changes_since_flush (CoglContext   *context,
                                                       unsigned long  changes);

void
cogl_context_add_current_pipeline_changes_since_flush (CoglContext   *context,
                                                       unsigned long  changes);

gboolean
cogl_context_get_current_pipeline_with_color_attrib (CoglContext *context);

void
cogl_context_set_current_pipeline_with_color_attrib (CoglContext *context,
                                                     gboolean     with_color_attrib);

gboolean
cogl_context_get_current_pipeline_unknown_color_alpha (CoglContext *context);

void
cogl_context_set_current_pipeline_unknown_color_alpha (CoglContext *context,
                                                       gboolean     unknown_color_alpha);

COGL_EXPORT_TEST
CoglPipelineCache * cogl_context_get_pipeline_cache (CoglContext *context);

CoglFramebuffer *
cogl_context_get_current_draw_buffer (CoglContext *context);

void
cogl_context_set_current_draw_buffer (CoglContext     *context,
                                      CoglFramebuffer *framebuffer);

CoglFramebuffer *
cogl_context_get_current_read_buffer (CoglContext *context);

void
cogl_context_set_current_read_buffer (CoglContext     *context,
                                      CoglFramebuffer *framebuffer);

unsigned long
cogl_context_get_current_draw_buffer_state_flushed (CoglContext *context);

void
cogl_context_set_current_draw_buffer_state_flushed (CoglContext   *context,
                                                    unsigned long  state);

void
cogl_context_add_current_draw_buffer_state_flushed (CoglContext   *context,
                                                    unsigned long  state);

unsigned long
cogl_context_get_current_draw_buffer_changes (CoglContext *context);

void
cogl_context_add_current_draw_buffer_changes (CoglContext   *context,
                                              unsigned long  changes);

void
cogl_context_clear_current_draw_buffer_changes (CoglContext   *context,
                                                unsigned long  changes);

CoglClipStack *
cogl_context_get_current_clip_stack (CoglContext *context);

void
cogl_context_set_current_clip_stack (CoglContext   *context,
                                     CoglClipStack *stack);

gboolean
cogl_context_get_current_clip_stack_valid (CoglContext *context);

void
cogl_context_set_current_clip_stack_valid (CoglContext *context,
                                           gboolean     valid);

CoglMatrixEntry *
cogl_context_get_current_projection_entry (CoglContext *context);

void
cogl_context_set_current_projection_entry (CoglContext     *context,
                                           CoglMatrixEntry *entry);

CoglMatrixEntry *
cogl_context_get_current_modelview_entry (CoglContext *context);

void
cogl_context_set_current_modelview_entry (CoglContext     *context,
                                          CoglMatrixEntry *entry);

CoglMatrixEntry *
cogl_context_get_identity_entry (CoglContext *context);

CoglPipeline *
cogl_context_get_stencil_pipeline (CoglContext *context);

graphene_matrix_t *
cogl_context_get_y_flip_matrix (CoglContext *context);

CoglTexture *
cogl_context_get_default_2d_texture (CoglContext *context);

void
cogl_context_set_default_2d_texture (CoglContext *context,
                                     CoglTexture *texture);

CoglBuffer *
cogl_context_get_current_buffer (CoglContext         *context,
                                 CoglBufferBindTarget target);

void
cogl_context_set_current_buffer (CoglContext         *context,
                                 CoglBufferBindTarget target,
                                 CoglBuffer          *buffer);

GArray *
cogl_context_get_attribute_name_index_map (CoglContext *context);

void
cogl_context_set_attribute_name_index_map (CoglContext *context,
                                           GArray      *array);

GPtrArray *
cogl_context_get_uniform_names (CoglContext *context);

int
cogl_context_get_n_uniform_names (CoglContext *context);

int
cogl_context_increment_n_uniform_names (CoglContext *context);

CoglPipeline *
cogl_context_get_default_pipeline (CoglContext *context);

void
cogl_context_set_default_pipeline (CoglContext  *context,
                                   CoglPipeline *pipeline);

CoglPipelineLayer *
cogl_context_get_default_layer_0 (CoglContext *context);

void
cogl_context_set_default_layer_0 (CoglContext       *context,
                                  CoglPipelineLayer *layer);

CoglPipelineLayer *
cogl_context_get_default_layer_n (CoglContext *context);

void
cogl_context_set_default_layer_n (CoglContext       *context,
                                  CoglPipelineLayer *layer);

void
cogl_context_set_dummy_layer_dependant (CoglContext       *context,
                                        CoglPipelineLayer *layer);

GHashTable *
cogl_context_get_attribute_name_states_hash (CoglContext *context);

GHashTable *
cogl_context_get_uniform_name_hash (CoglContext *context);

int
cogl_context_increment_n_attribute_names (CoglContext *context);

CoglSamplerCache *
cogl_context_get_sampler_cache (CoglContext *context);

void
cogl_context_prepend_framebuffer (CoglContext     *context,
                                  CoglFramebuffer *framebuffer);

void
cogl_context_remove_framebuffer (CoglContext     *context,
                                 CoglFramebuffer *framebuffer);

GArray *
cogl_context_get_journal_flush_attributes_array (CoglContext *context);

GArray *
cogl_context_get_journal_clip_bounds (CoglContext *context);

void
cogl_context_set_journal_clip_bounds (CoglContext *context,
                                      GArray      *array);

uint8_t
cogl_context_get_journal_rectangles_color (CoglContext *context);

void
cogl_context_set_journal_rectangles_color (CoglContext *context,
                                           uint8_t      color);

CoglPipeline *
cogl_context_get_opaque_color_pipeline (CoglContext *context);

CoglPipeline *
cogl_context_get_blit_texture_pipeline (CoglContext *context);

void
cogl_context_set_blit_texture_pipeline (CoglContext  *context,
                                        CoglPipeline *pipeline);

GSList *
cogl_context_get_atlases (CoglContext *context);

void
cogl_context_prepend_atlas (CoglContext *context,
                            CoglAtlas   *atlas);

void
cogl_context_remove_atlas (CoglContext *context,
                           CoglAtlas   *atlas);

GHookList *
cogl_context_get_atlas_reorganize_callbacks (CoglContext *context);

CoglIndices *
cogl_context_get_rectangle_byte_indices (CoglContext *context);

void
cogl_context_set_rectangle_byte_indices (CoglContext *context,
                                         CoglIndices *indices);

CoglIndices *
cogl_context_get_rectangle_short_indices (CoglContext *context);

void
cogl_context_set_rectangle_short_indices (CoglContext *context,
                                          CoglIndices *indices);

int
cogl_context_get_rectangle_short_indices_len (CoglContext *context);

void
cogl_context_set_rectangle_short_indices_len (CoglContext *context,
                                              int          len);

GByteArray *
cogl_context_get_buffer_map_fallback_array (CoglContext *context);

gboolean
cogl_context_get_buffer_map_fallback_in_use (CoglContext *context);

void
cogl_context_set_buffer_map_fallback_in_use (CoglContext *context,
                                             gboolean     in_use);

size_t
cogl_context_get_buffer_map_fallback_offset (CoglContext *context);

void
cogl_context_set_buffer_map_fallback_offset (CoglContext *context,
                                             size_t       offset);
