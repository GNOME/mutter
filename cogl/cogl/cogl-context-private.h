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

#include "cogl/cogl-display-private.h"
#include "cogl/cogl-clip-stack.h"
#include "cogl/cogl-matrix-stack.h"
#include "cogl/cogl-pipeline-private.h"
#include "cogl/cogl-buffer-private.h"
#include "cogl/cogl-bitmask.h"
#include "cogl/cogl-atlas.h"
#include "cogl/cogl-driver-private.h"
#include "cogl/cogl-texture-driver.h"
#include "cogl/cogl-pipeline-cache.h"
#include "cogl/cogl-texture-2d.h"
#include "cogl/cogl-sampler-cache-private.h"
#include "cogl/cogl-framebuffer-private.h"
#include "cogl/cogl-offscreen-private.h"
#include "cogl/cogl-onscreen-private.h"
#include "cogl/cogl-private.h"
#include "cogl/winsys/cogl-winsys-private.h"

typedef struct
{
  GLfloat v[3];
  GLfloat t[2];
  GLubyte c[4];
} CoglTextureGLVertex;

struct _CoglTimestampQuery
{
  unsigned int id;
};

struct _CoglContext
{
  GObject parent_instance;

  CoglDisplay *display;

  /* Features cache */
  unsigned long features[COGL_FLAGS_N_LONGS_FOR_SIZE (_COGL_N_FEATURE_IDS)];
  unsigned long private_features
    [COGL_FLAGS_N_LONGS_FOR_SIZE (COGL_N_PRIVATE_FEATURES)];

  CoglPipeline *default_pipeline;
  CoglPipelineLayer *default_layer_0;
  CoglPipelineLayer *default_layer_n;
  CoglPipelineLayer *dummy_layer_dependant;

  GHashTable *attribute_name_states_hash;
  GArray *attribute_name_index_map;
  int n_attribute_names;

  CoglBitmask       enabled_custom_attributes;

  /* These are temporary bitmasks that are used when disabling
   * builtin and custom attribute arrays. They are here just
   * to avoid allocating new ones each time */
  CoglBitmask       enable_custom_attributes_tmp;
  CoglBitmask       changed_bits_tmp;

  /* A few handy matrix constants */
  graphene_matrix_t identity_matrix;
  graphene_matrix_t y_flip_matrix;

  /* The matrix stack entries that should be flushed during the next
   * pipeline state flush */
  CoglMatrixEntry *current_projection_entry;
  CoglMatrixEntry *current_modelview_entry;

  CoglMatrixEntry identity_entry;

  /* Only used for comparing other pipelines when reading pixels. */
  CoglPipeline     *opaque_color_pipeline;

  GString          *codegen_header_buffer;
  GString          *codegen_source_buffer;

  CoglPipelineCache *pipeline_cache;

  /* Textures */
  CoglTexture *default_gl_texture_2d_tex;

  /* Central list of all framebuffers so all journals can be flushed
   * at any time. */
  GList            *framebuffers;

  /* Global journal buffers */
  GArray           *journal_flush_attributes_array;
  GArray           *journal_clip_bounds;

  /* Some simple caching, to minimize state changes... */
  CoglPipeline     *current_pipeline;
  unsigned long     current_pipeline_changes_since_flush;
  gboolean          current_pipeline_with_color_attrib;
  gboolean          current_pipeline_unknown_color_alpha;
  unsigned long     current_pipeline_age;

  gboolean          gl_blend_enable_cache;

  gboolean              depth_test_enabled_cache;
  CoglDepthTestFunction depth_test_function_cache;
  gboolean              depth_writing_enabled_cache;
  float                 depth_range_near_cache;
  float                 depth_range_far_cache;

  CoglBuffer       *current_buffer[COGL_BUFFER_BIND_TARGET_COUNT];

  /* Framebuffers */
  unsigned long     current_draw_buffer_state_flushed;
  unsigned long     current_draw_buffer_changes;
  CoglFramebuffer  *current_draw_buffer;
  CoglFramebuffer  *current_read_buffer;

  gboolean have_last_offscreen_allocate_flags;
  CoglOffscreenAllocateFlags last_offscreen_allocate_flags;

  GHashTable *swap_callback_closures;
  int next_swap_callback_id;

  CoglList onscreen_events_queue;
  CoglList onscreen_dirty_queue;
  CoglClosure *onscreen_dispatch_idle;

  /* This becomes TRUE the first time the context is bound to an
   * onscreen buffer. This is used by cogl-framebuffer-gl to determine
   * when to initialise the glDrawBuffer state */
  gboolean was_bound_to_onscreen;

  /* Primitives */
  CoglPipeline     *stencil_pipeline;

  CoglIndices      *rectangle_byte_indices;
  CoglIndices      *rectangle_short_indices;
  int               rectangle_short_indices_len;

  CoglPipeline     *blit_texture_pipeline;

  GSList           *atlases;
  GHookList         atlas_reorganize_callbacks;

  /* This debugging variable is used to pick a colour for visually
     displaying the quad batches. It needs to be global so that it can
     be reset by cogl_clear. It needs to be reset to increase the
     chances of getting the same colour during an animation */
  uint8_t            journal_rectangles_color;

  /* Cached values for GL_MAX_TEXTURE_[IMAGE_]UNITS to avoid calling
     glGetInteger too often */
  GLint             max_texture_units;
  GLint             max_texture_image_units;
  GLint             max_activateable_texture_units;

  /* Fragment processing programs */
  GLuint                  current_gl_program;

  gboolean current_gl_dither_enabled;

  /* Clipping */
  /* TRUE if we have a valid clipping stack flushed. In that case
     current_clip_stack will describe what the current state is. If
     this is FALSE then the current clip stack is completely unknown
     so it will need to be reflushed. In that case current_clip_stack
     doesn't need to be a valid pointer. We can't just use NULL in
     current_clip_stack to mark a dirty state because NULL is a valid
     stack (meaning no clipping) */
  gboolean          current_clip_stack_valid;
  /* The clip state that was flushed. This isn't intended to be used
     as a stack to push and pop new entries. Instead the current stack
     that the user wants is part of the framebuffer state. This is
     just used to record the flush state so we can avoid flushing the
     same state multiple times. When the clip state is flushed this
     will hold a reference */
  CoglClipStack    *current_clip_stack;

  /* This is used as a temporary buffer to fill a CoglBuffer when
     cogl_buffer_map fails and we only want to map to fill it with new
     data */
  GByteArray       *buffer_map_fallback_array;
  gboolean          buffer_map_fallback_in_use;
  size_t            buffer_map_fallback_offset;

  CoglSamplerCache *sampler_cache;

  unsigned long winsys_features
    [COGL_FLAGS_N_LONGS_FOR_SIZE (COGL_WINSYS_FEATURE_N_FEATURES)];
  void *winsys;

  /* Array of names of uniforms. These are used like quarks to give a
     unique number to each uniform name except that we ensure that
     they increase sequentially so that we can use the id as an index
     into a bitfield representing the uniforms that a pipeline
     overrides from its parent. */
  GPtrArray *uniform_names;
  /* A hash table to quickly get an index given an existing name. The
     name strings are owned by the uniform_names array. The values are
     the uniform location cast to a pointer. */
  GHashTable *uniform_name_hash;
  int n_uniform_names;

  GHashTable *named_pipelines;

  /* This defines a list of function pointers that Cogl uses from
     either GL or GLES. All functions are accessed indirectly through
     these pointers rather than linking to them directly */
#ifndef APIENTRY
#define APIENTRY
#endif

#define COGL_EXT_BEGIN(name, \
                       min_gl_major, min_gl_minor, \
                       gles_availability, \
                       extension_suffixes, extension_names)
#define COGL_EXT_FUNCTION(ret, name, args) \
  ret (APIENTRY * name) args;
#define COGL_EXT_END()

#include "gl-prototypes/cogl-all-functions.h"

#undef COGL_EXT_BEGIN
#undef COGL_EXT_FUNCTION
#undef COGL_EXT_END
};

const CoglWinsysVtable *
_cogl_context_get_winsys (CoglContext *context);

/* Query the GL extensions and lookup the corresponding function
 * pointers. Theoretically the list of extensions can change for
 * different GL contexts so it is the winsys backend's responsibility
 * to know when to re-query the GL extensions. The backend should also
 * check whether the GL context is supported by Cogl. If not it should
 * return FALSE and set @error */
gboolean
_cogl_context_update_features (CoglContext *context,
                               GError **error);

void
_cogl_context_set_current_projection_entry (CoglContext *context,
                                            CoglMatrixEntry *entry);

void
_cogl_context_set_current_modelview_entry (CoglContext *context,
                                           CoglMatrixEntry *entry);

void
_cogl_context_update_sync (CoglContext *context);

CoglDriver * cogl_context_get_driver (CoglContext *context);
