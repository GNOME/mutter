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

#include "config.h"

#include "cogl/cogl-mutter.h"
#include "cogl/cogl-private.h"
#include "cogl/cogl-profile.h"
#include "cogl/cogl-util.h"
#include "cogl/cogl-context-private.h"
#include "cogl/cogl-display-private.h"
#include "cogl/cogl-renderer-private.h"
#include "cogl/cogl-journal-private.h"
#include "cogl/cogl-texture-private.h"
#include "cogl/cogl-texture-2d-private.h"
#include "cogl/cogl-pipeline-private.h"
#include "cogl/cogl-framebuffer-private.h"
#include "cogl/cogl-onscreen-private.h"
#include "cogl/cogl-attribute-private.h"
#include "cogl/winsys/cogl-winsys.h"

#include <gio/gio.h>
#include <string.h>
#include <stdlib.h>

struct _CoglContext
{
  GObject parent_instance;

  CoglDisplay *display;

  CoglPipeline *default_pipeline;
  CoglPipelineLayer *default_layer_0;
  CoglPipelineLayer *default_layer_n;
  CoglPipelineLayer *dummy_layer_dependant;

  GHashTable *attribute_name_states_hash;
  GArray *attribute_name_index_map;
  int n_attribute_names;

  CoglBitmask enabled_custom_attributes;

  /* These are temporary bitmasks that are used when disabling
   * builtin and custom attribute arrays. They are here just
   * to avoid allocating new ones each time */
  CoglBitmask enable_custom_attributes_tmp;
  CoglBitmask changed_bits_tmp;

  /* A few handy matrix constants */
  graphene_matrix_t identity_matrix;
  graphene_matrix_t y_flip_matrix;

  /* The matrix stack entries that should be flushed during the next
   * pipeline state flush */
  CoglMatrixEntry *current_projection_entry;
  CoglMatrixEntry *current_modelview_entry;

  CoglMatrixEntry identity_entry;

  /* Only used for comparing other pipelines when reading pixels. */
  CoglPipeline *opaque_color_pipeline;

  GString *codegen_header_buffer;
  GString *codegen_source_buffer;

  CoglPipelineCache *pipeline_cache;

  /* Textures */
  CoglTexture *default_gl_texture_2d_tex;

  /* Central list of all framebuffers so all journals can be flushed
   * at any time. */
  GList *framebuffers;

  /* Global journal buffers */
  GArray *journal_flush_attributes_array;
  GArray *journal_clip_bounds;

  /* Some simple caching, to minimize state changes... */
  CoglPipeline *current_pipeline;
  unsigned long current_pipeline_changes_since_flush;
  gboolean current_pipeline_with_color_attrib;
  gboolean current_pipeline_unknown_color_alpha;
  unsigned long current_pipeline_age;

  gboolean gl_blend_enable_cache;

  gboolean depth_test_enabled_cache;
  CoglDepthTestFunction depth_test_function_cache;
  gboolean depth_writing_enabled_cache;
  float depth_range_near_cache;
  float depth_range_far_cache;

  CoglBuffer *current_buffer[COGL_BUFFER_BIND_TARGET_COUNT];

  /* Framebuffers */
  unsigned long current_draw_buffer_state_flushed;
  unsigned long current_draw_buffer_changes;
  CoglFramebuffer *current_draw_buffer;
  CoglFramebuffer *current_read_buffer;

  gboolean have_last_offscreen_allocate_flags;
  CoglOffscreenAllocateFlags last_offscreen_allocate_flags;

  CoglList onscreen_events_queue;
  CoglList onscreen_dirty_queue;
  CoglClosure *onscreen_dispatch_idle;

  /* This becomes TRUE the first time the context is bound to an
   * onscreen buffer. This is used by cogl-framebuffer-gl to determine
   * when to initialise the glDrawBuffer state */
  gboolean was_bound_to_onscreen;

  /* Primitives */
  CoglPipeline *stencil_pipeline;

  CoglIndices *rectangle_byte_indices;
  CoglIndices *rectangle_short_indices;
  int rectangle_short_indices_len;

  CoglPipeline *blit_texture_pipeline;

  GSList *atlases;
  GHookList atlas_reorganize_callbacks;

  /* This debugging variable is used to pick a colour for visually
     displaying the quad batches. It needs to be global so that it can
     be reset by cogl_clear. It needs to be reset to increase the
     chances of getting the same colour during an animation */
  uint8_t journal_rectangles_color;

  /* Fragment processing programs */
  GLuint current_gl_program;

  gboolean current_gl_dither_enabled;

  /* Clipping */
  /* TRUE if we have a valid clipping stack flushed. In that case
     current_clip_stack will describe what the current state is. If
     this is FALSE then the current clip stack is completely unknown
     so it will need to be reflushed. In that case current_clip_stack
     doesn't need to be a valid pointer. We can't just use NULL in
     current_clip_stack to mark a dirty state because NULL is a valid
     stack (meaning no clipping) */
  gboolean current_clip_stack_valid;
  /* The clip state that was flushed. This isn't intended to be used
     as a stack to push and pop new entries. Instead the current stack
     that the user wants is part of the framebuffer state. This is
     just used to record the flush state so we can avoid flushing the
     same state multiple times. When the clip state is flushed this
     will hold a reference */
  CoglClipStack *current_clip_stack;

  /* This is used as a temporary buffer to fill a CoglBuffer when
     cogl_buffer_map fails and we only want to map to fill it with new
     data */
  GByteArray *buffer_map_fallback_array;
  gboolean buffer_map_fallback_in_use;
  size_t buffer_map_fallback_offset;

  CoglSamplerCache *sampler_cache;

  unsigned long winsys_features
  [COGL_FLAGS_N_LONGS_FOR_SIZE (COGL_WINSYS_FEATURE_N_FEATURES)];

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
};


G_DEFINE_FINAL_TYPE (CoglContext, cogl_context, G_TYPE_OBJECT);

void
cogl_context_clear_onscreen_dirty_queue (CoglContext *context)
{
  while (!_cogl_list_empty (&context->onscreen_dirty_queue))
    {
      CoglOnscreenQueuedDirty *qe =
        _cogl_container_of (context->onscreen_dirty_queue.next,
                            CoglOnscreenQueuedDirty,
                            link);

      _cogl_list_remove (&qe->link);
      g_object_unref (qe->onscreen);

      g_free (qe);
    }
}

static void
cogl_context_dispose (GObject *object)
{
  CoglContext *context = COGL_CONTEXT (object);

  cogl_context_clear_onscreen_dirty_queue (context);

  g_clear_object (&context->default_gl_texture_2d_tex);

  g_clear_object (&context->opaque_color_pipeline);
  g_clear_object (&context->blit_texture_pipeline);

  g_clear_pointer (&context->journal_flush_attributes_array, g_array_unref);
  g_clear_pointer (&context->journal_clip_bounds, g_array_unref);

  g_clear_object (&context->rectangle_byte_indices);
  g_clear_object (&context->rectangle_short_indices);

  g_clear_object (&context->default_pipeline);

  g_clear_object (&context->dummy_layer_dependant);
  g_clear_object (&context->default_layer_n);
  g_clear_object (&context->default_layer_0);

  if (context->current_clip_stack_valid)
    g_clear_pointer (&context->current_clip_stack, _cogl_clip_stack_unref);

  g_clear_slist (&context->atlases, NULL);
  g_hook_list_clear (&context->atlas_reorganize_callbacks);

  _cogl_bitmask_destroy (&context->enabled_custom_attributes);
  _cogl_bitmask_destroy (&context->enable_custom_attributes_tmp);
  _cogl_bitmask_destroy (&context->changed_bits_tmp);

  g_clear_pointer (&context->current_modelview_entry, cogl_matrix_entry_unref);
  g_clear_pointer (&context->current_projection_entry, cogl_matrix_entry_unref);

  g_clear_pointer (&context->uniform_names, g_ptr_array_unref);
  g_clear_pointer (&context->uniform_name_hash, g_hash_table_destroy);

  g_clear_pointer (&context->attribute_name_states_hash,
                   g_hash_table_destroy);
  g_clear_pointer (&context->attribute_name_index_map, g_array_unref);

  g_clear_pointer (&context->buffer_map_fallback_array, g_byte_array_unref);

  g_clear_pointer (&context->named_pipelines, g_hash_table_destroy);

  g_clear_pointer (&context->pipeline_cache, _cogl_pipeline_cache_free);
  g_clear_pointer (&context->sampler_cache, _cogl_sampler_cache_free);

  g_clear_object (&context->display);

  G_OBJECT_CLASS (cogl_context_parent_class)->dispose (object);
}

static void
cogl_context_finalize (GObject *object)
{
  CoglContext *context = COGL_CONTEXT (object);

  g_string_free (context->codegen_header_buffer, TRUE);
  g_string_free (context->codegen_source_buffer, TRUE);

  G_OBJECT_CLASS (cogl_context_parent_class)->finalize (object);
}

static void
cogl_context_init (CoglContext *info)
{
}

static void
cogl_context_class_init (CoglContextClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->dispose = cogl_context_dispose;
  object_class->finalize = cogl_context_finalize;
}

/* For reference: There was some deliberation over whether to have a
 * constructor that could throw an exception but looking at standard
 * practices with several high level OO languages including python, C++,
 * C# Java and Ruby they all support exceptions in constructors and the
 * general consensus appears to be that throwing an exception is neater
 * than successfully constructing with an internal error status that
 * would then have to be explicitly checked via some form of ::is_ok()
 * method.
 */
CoglContext *
cogl_context_new (CoglDisplay *display,
                  GError **error)
{
  CoglDriver *driver;
  CoglWinsys *winsys;
  CoglWinsysClass *winsys_class;

  g_return_val_if_fail (display != NULL, NULL);

  CoglContext *context;
  uint8_t white_pixel[] = { 0xff, 0xff, 0xff, 0xff };
  int i;
  GError *local_error = NULL;

#ifdef COGL_ENABLE_PROFILE
  /* We need to be absolutely sure that uprof has been initialized
   * before calling _cogl_uprof_init. uprof_init (NULL, NULL)
   * will be a NOP if it has been initialized but it will also
   * mean subsequent parsing of the UProf GOptionGroup will have no
   * affect.
   *
   * Sadly GOptionGroup based library initialization is extremely
   * fragile by design because GOptionGroups have no notion of
   * dependencies and so the order things are initialized isn't
   * currently under tight control.
   */
  uprof_init (NULL, NULL);
  _cogl_uprof_init ();
#endif

  /* Allocate context memory */
  context = g_object_new (COGL_TYPE_CONTEXT, NULL);

  /* Init default values */
  memset (context->winsys_features, 0, sizeof (context->winsys_features));

  context->display = g_object_ref (display);
  /* Keep a backpointer to the context */
  display->context = context;

  winsys = cogl_renderer_get_winsys (display->renderer);
  winsys_class = COGL_WINSYS_GET_CLASS (winsys);
  if (!winsys_class->context_init (winsys, context, error))
    {
      g_object_unref (display);
      g_free (context);
      return NULL;
    }

  driver = cogl_renderer_get_driver (display->renderer);
  if (COGL_DRIVER_GET_CLASS (driver)->context_init &&
      !COGL_DRIVER_GET_CLASS (driver)->context_init (driver, context))
    {
      g_object_unref (display);
      g_object_unref (context);
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to initialize context");
      return NULL;
    }

  context->attribute_name_states_hash =
    g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

  /* The "cogl_color_in" attribute needs a deterministic name_index
   * so we make sure it's the first attribute name we register */
  _cogl_attribute_register_attribute_name (context, "cogl_color_in");


  context->uniform_names =
    g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);
  context->uniform_name_hash = g_hash_table_new (g_str_hash, g_str_equal);

  context->sampler_cache = _cogl_sampler_cache_new (context);

  _cogl_pipeline_init_default_pipeline (context);
  _cogl_pipeline_init_default_layers (context);
  _cogl_pipeline_init_state_hash_functions ();
  _cogl_pipeline_init_layer_state_hash_functions ();

  graphene_matrix_init_identity (&context->identity_matrix);
  graphene_matrix_init_identity (&context->y_flip_matrix);
  graphene_matrix_scale (&context->y_flip_matrix, 1, -1, 1);

  context->opaque_color_pipeline = cogl_pipeline_new (context);
  cogl_pipeline_set_static_name (context->opaque_color_pipeline,
                                 "CoglContext (opaque color)");

  context->codegen_header_buffer = g_string_new ("");
  context->codegen_source_buffer = g_string_new ("");

  context->current_draw_buffer_changes = COGL_FRAMEBUFFER_STATE_ALL;

  _cogl_list_init (&context->onscreen_events_queue);
  _cogl_list_init (&context->onscreen_dirty_queue);

  context->journal_flush_attributes_array =
    g_array_new (TRUE, FALSE, sizeof (CoglAttribute *));

  _cogl_bitmask_init (&context->enabled_custom_attributes);
  _cogl_bitmask_init (&context->enable_custom_attributes_tmp);
  _cogl_bitmask_init (&context->changed_bits_tmp);

  context->current_gl_dither_enabled = TRUE;

  context->depth_test_function_cache = COGL_DEPTH_TEST_FUNCTION_LESS;
  context->depth_writing_enabled_cache = TRUE;
  context->depth_range_far_cache = 1;

  context->pipeline_cache = _cogl_pipeline_cache_new (context);

  for (i = 0; i < COGL_BUFFER_BIND_TARGET_COUNT; i++)
    context->current_buffer[i] = NULL;

  context->stencil_pipeline = cogl_pipeline_new (context);
  cogl_pipeline_set_static_name (context->stencil_pipeline,
                                 "Cogl (stencil)");

  _cogl_matrix_entry_identity_init (&context->identity_entry);

  /* Create default textures used for fall backs */
  context->default_gl_texture_2d_tex =
    cogl_texture_2d_new_from_data (context,
                                   1, 1,
                                   COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                                   0, /* rowstride */
                                   white_pixel,
                                   &local_error);
  if (!context->default_gl_texture_2d_tex)
    {
      g_object_unref (display);
      g_free (context);
      g_propagate_prefixed_error (error, local_error,
                                  "Failed to create 1x1 fallback texture: ");
      return NULL;
    }

  g_hook_list_init (&context->atlas_reorganize_callbacks, sizeof (GHook));

  context->buffer_map_fallback_array = g_byte_array_new ();

  context->named_pipelines =
    g_hash_table_new_full (NULL, NULL, NULL, g_object_unref);

  return context;
}

CoglDisplay *
cogl_context_get_display (CoglContext *context)
{
  return context->display;
}

CoglRenderer *
cogl_context_get_renderer (CoglContext *context)
{
  return context->display->renderer;
}

void
cogl_context_set_winsys_feature (CoglContext      *context,
                                 CoglWinsysFeature feature,
                                 gboolean          value)
{
  COGL_FLAGS_SET (context->winsys_features, feature, value);
}

void
_cogl_context_set_current_projection_entry (CoglContext *context,
                                            CoglMatrixEntry *entry)
{
  cogl_matrix_entry_ref (entry);
  if (context->current_projection_entry)
    cogl_matrix_entry_unref (context->current_projection_entry);
  context->current_projection_entry = entry;
}

void
_cogl_context_set_current_modelview_entry (CoglContext *context,
                                           CoglMatrixEntry *entry)
{
  cogl_matrix_entry_ref (entry);
  if (context->current_modelview_entry)
    cogl_matrix_entry_unref (context->current_modelview_entry);
  context->current_modelview_entry = entry;
}

void
_cogl_context_update_sync (CoglContext *context)
{
  CoglWinsys *winsys =
    cogl_renderer_get_winsys (context->display->renderer);
  CoglWinsysClass *winsys_class = COGL_WINSYS_GET_CLASS (winsys);

  if (!winsys_class->update_sync)
    return;

  winsys_class->update_sync (winsys, context);
}

int
cogl_context_get_latest_sync_fd (CoglContext *context)
{
  CoglWinsys *winsys =
    cogl_renderer_get_winsys (context->display->renderer);
  CoglWinsysClass *winsys_class = COGL_WINSYS_GET_CLASS (winsys);

  if (!winsys_class->get_sync_fd)
    return -1;

  return winsys_class->get_sync_fd (winsys, context);
}

void
cogl_context_set_named_pipeline (CoglContext     *context,
                                 CoglPipelineKey *key,
                                 CoglPipeline    *pipeline)
{
  if (pipeline)
    {
      g_debug ("Adding named pipeline %s", *key);
      g_hash_table_insert (context->named_pipelines, (gpointer) key, pipeline);
    }
  else
    {
      g_debug ("Removing named pipeline %s", *key);
      g_hash_table_remove (context->named_pipelines, (gpointer) key);
    }
}

CoglPipeline *
cogl_context_get_named_pipeline (CoglContext     *context,
                                 CoglPipelineKey *key)
{
  return g_hash_table_lookup (context->named_pipelines, key);
}

/* FIXME: we should distinguish renderer and context features */
gboolean
cogl_context_has_winsys_feature (CoglContext       *context,
                                 CoglWinsysFeature  feature)
{
  return COGL_FLAGS_GET (context->winsys_features, feature);
}

void
cogl_context_flush (CoglContext *context)
{
  GList *l;

  for (l = context->framebuffers; l; l = l->next)
    _cogl_framebuffer_flush_journal (l->data);
}

CoglDriver *
cogl_context_get_driver (CoglContext *context)
{
  CoglRenderer *renderer = cogl_context_get_renderer (context);

  return cogl_renderer_get_driver (renderer);
}

CoglPipeline *
cogl_context_get_current_pipeline (CoglContext *context)
{
  return context->current_pipeline;
}

void
cogl_context_set_current_pipeline (CoglContext  *context,
                                   CoglPipeline *pipeline)
{
  if (context->current_pipeline != NULL)
    g_object_unref (context->current_pipeline);
  context->current_pipeline = pipeline;
}

unsigned long
cogl_context_get_current_pipeline_age (CoglContext *context)
{
  return context->current_pipeline_age;
}

void
cogl_context_set_current_pipeline_age (CoglContext   *context,
                                       unsigned long  age)
{
  context->current_pipeline_age = age;
}

void
cogl_context_decrement_current_pipeline_age (CoglContext *context)
{
  context->current_pipeline_age--;
}

unsigned long
cogl_context_get_current_pipeline_changes_since_flush (CoglContext *context)
{
  return context->current_pipeline_changes_since_flush;
}

void
cogl_context_set_current_pipeline_changes_since_flush (CoglContext   *context,
                                                       unsigned long  changes)
{
  context->current_pipeline_changes_since_flush = changes;
}

void
cogl_context_add_current_pipeline_changes_since_flush (CoglContext   *context,
                                                       unsigned long  changes)
{
  context->current_pipeline_changes_since_flush |= changes;
}

gboolean
cogl_context_get_current_pipeline_with_color_attrib (CoglContext *context)
{
  return context->current_pipeline_with_color_attrib;
}

void
cogl_context_set_current_pipeline_with_color_attrib (CoglContext *context,
                                                     gboolean     with_color_attrib)
{
  context->current_pipeline_with_color_attrib = with_color_attrib;
}

gboolean
cogl_context_get_current_pipeline_unknown_color_alpha (CoglContext *context)
{
  return context->current_pipeline_unknown_color_alpha;
}

void
cogl_context_set_current_pipeline_unknown_color_alpha (CoglContext *context,
                                                       gboolean     unknown_color_alpha)
{
  context->current_pipeline_unknown_color_alpha = unknown_color_alpha;
}

CoglPipelineCache *
cogl_context_get_pipeline_cache (CoglContext *context)
{
  return context->pipeline_cache;
}

CoglFramebuffer *
cogl_context_get_current_draw_buffer (CoglContext *context)
{
  return context->current_draw_buffer;
}

void
cogl_context_set_current_draw_buffer (CoglContext     *context,
                                      CoglFramebuffer *framebuffer)
{
  context->current_draw_buffer = framebuffer;
}

CoglFramebuffer *
cogl_context_get_current_read_buffer (CoglContext *context)
{
  return context->current_read_buffer;
}

void
cogl_context_set_current_read_buffer (CoglContext     *context,
                                      CoglFramebuffer *framebuffer)
{
  context->current_read_buffer = framebuffer;
}

unsigned long
cogl_context_get_current_draw_buffer_state_flushed (CoglContext *context)
{
  return context->current_draw_buffer_state_flushed;
}

void
cogl_context_set_current_draw_buffer_state_flushed (CoglContext   *context,
                                                    unsigned long  state)
{
  context->current_draw_buffer_state_flushed = state;
}

void
cogl_context_add_current_draw_buffer_state_flushed (CoglContext   *context,
                                                    unsigned long  state)
{
  context->current_draw_buffer_state_flushed |= state;
}

unsigned long
cogl_context_get_current_draw_buffer_changes (CoglContext *context)
{
  return context->current_draw_buffer_changes;
}

void
cogl_context_add_current_draw_buffer_changes (CoglContext   *context,
                                              unsigned long  changes)
{
  context->current_draw_buffer_changes |= changes;
}

void
cogl_context_clear_current_draw_buffer_changes (CoglContext   *context,
                                                unsigned long  changes)
{
  context->current_draw_buffer_changes &= ~changes;
}

GLuint
cogl_context_get_current_gl_program (CoglContext *context)
{
  return context->current_gl_program;
}

void
cogl_context_set_current_gl_program (CoglContext *context,
                                     GLuint       program)
{
  context->current_gl_program = program;
}

gboolean
cogl_context_get_gl_blend_enable_cache (CoglContext *context)
{
  return context->gl_blend_enable_cache;
}

void
cogl_context_set_gl_blend_enable_cache (CoglContext *context,
                                        gboolean     enabled)
{
  context->gl_blend_enable_cache = enabled;
}

gboolean
cogl_context_get_current_gl_dither_enabled (CoglContext *context)
{
  return context->current_gl_dither_enabled;
}

void
cogl_context_set_current_gl_dither_enabled (CoglContext *context,
                                            gboolean     enabled)
{
  context->current_gl_dither_enabled = enabled;
}

CoglClipStack *
cogl_context_get_current_clip_stack (CoglContext *context)
{
  return context->current_clip_stack;
}

void
cogl_context_set_current_clip_stack (CoglContext   *context,
                                     CoglClipStack *stack)
{
  context->current_clip_stack = stack;
}

gboolean
cogl_context_get_current_clip_stack_valid (CoglContext *context)
{
  return context->current_clip_stack_valid;
}

void
cogl_context_set_current_clip_stack_valid (CoglContext *context,
                                           gboolean     valid)
{
  context->current_clip_stack_valid = valid;
}

CoglMatrixEntry *
cogl_context_get_current_projection_entry (CoglContext *context)
{
  return context->current_projection_entry;
}

void
cogl_context_set_current_projection_entry (CoglContext     *context,
                                           CoglMatrixEntry *entry)
{
  context->current_projection_entry = entry;
}

CoglMatrixEntry *
cogl_context_get_current_modelview_entry (CoglContext *context)
{
  return context->current_modelview_entry;
}

void
cogl_context_set_current_modelview_entry (CoglContext     *context,
                                          CoglMatrixEntry *entry)
{
  context->current_modelview_entry = entry;
}

CoglMatrixEntry *
cogl_context_get_identity_entry (CoglContext *context)
{
  return &context->identity_entry;
}

CoglPipeline *
cogl_context_get_stencil_pipeline (CoglContext *context)
{
  return context->stencil_pipeline;
}

graphene_matrix_t *
cogl_context_get_y_flip_matrix (CoglContext *context)
{
  return &context->y_flip_matrix;
}

gboolean
cogl_context_get_depth_test_enabled_cache (CoglContext *context)
{
  return context->depth_test_enabled_cache;
}

void
cogl_context_set_depth_test_enabled_cache (CoglContext *context,
                                           gboolean     enabled)
{
  context->depth_test_enabled_cache = enabled;
}

CoglDepthTestFunction
cogl_context_get_depth_test_function_cache (CoglContext *context)
{
  return context->depth_test_function_cache;
}

void
cogl_context_set_depth_test_function_cache (CoglContext           *context,
                                            CoglDepthTestFunction  function)
{
  context->depth_test_function_cache = function;
}

gboolean
cogl_context_get_depth_writing_enabled_cache (CoglContext *context)
{
  return context->depth_writing_enabled_cache;
}

void
cogl_context_set_depth_writing_enabled_cache (CoglContext *context,
                                              gboolean     enabled)
{
  context->depth_writing_enabled_cache = enabled;
}

float
cogl_context_get_depth_range_near_cache (CoglContext *context)
{
  return context->depth_range_near_cache;
}

void
cogl_context_set_depth_range_near_cache (CoglContext *context,
                                         float        near_val)
{
  context->depth_range_near_cache = near_val;
}

float
cogl_context_get_depth_range_far_cache (CoglContext *context)
{
  return context->depth_range_far_cache;
}

void
cogl_context_set_depth_range_far_cache (CoglContext *context,
                                        float        far_val)
{
  context->depth_range_far_cache = far_val;
}

gboolean
cogl_context_get_was_bound_to_onscreen (CoglContext *context)
{
  return context->was_bound_to_onscreen;
}

void
cogl_context_set_was_bound_to_onscreen (CoglContext *context,
                                        gboolean     bound)
{
  context->was_bound_to_onscreen = bound;
}

gboolean
cogl_context_get_have_last_offscreen_allocate_flags (CoglContext *context)
{
  return context->have_last_offscreen_allocate_flags;
}

void
cogl_context_set_have_last_offscreen_allocate_flags (CoglContext *context,
                                                     gboolean     have_flags)
{
  context->have_last_offscreen_allocate_flags = have_flags;
}

CoglOffscreenAllocateFlags
cogl_context_get_last_offscreen_allocate_flags (CoglContext *context)
{
  return context->last_offscreen_allocate_flags;
}

void
cogl_context_set_last_offscreen_allocate_flags (CoglContext                *context,
                                                CoglOffscreenAllocateFlags  flags)
{
  context->last_offscreen_allocate_flags = flags;
}

CoglTexture *
cogl_context_get_default_gl_texture_2d_tex (CoglContext *context)
{
  return context->default_gl_texture_2d_tex;
}

CoglBuffer *
cogl_context_get_current_buffer (CoglContext         *context,
                                 CoglBufferBindTarget target)
{
  return context->current_buffer[target];
}

void
cogl_context_set_current_buffer (CoglContext         *context,
                                 CoglBufferBindTarget target,
                                 CoglBuffer          *buffer)
{
  context->current_buffer[target] = buffer;
}

GArray *
cogl_context_get_attribute_name_index_map (CoglContext *context)
{
  return context->attribute_name_index_map;
}

void
cogl_context_set_attribute_name_index_map (CoglContext *context,
                                           GArray      *array)
{
  context->attribute_name_index_map = array;
}

GPtrArray *
cogl_context_get_uniform_names (CoglContext *context)
{
  return context->uniform_names;
}

int
cogl_context_get_n_uniform_names (CoglContext *context)
{
  return context->n_uniform_names;
}

int
cogl_context_increment_n_uniform_names (CoglContext *context)
{
  return context->n_uniform_names++;
}

GString *
cogl_context_get_codegen_header_buffer (CoglContext *context)
{
  return context->codegen_header_buffer;
}

GString *
cogl_context_get_codegen_source_buffer (CoglContext *context)
{
  return context->codegen_source_buffer;
}

CoglBitmask *
cogl_context_get_enabled_custom_attributes (CoglContext *context)
{
  return &context->enabled_custom_attributes;
}

CoglBitmask *
cogl_context_get_enable_custom_attributes_tmp (CoglContext *context)
{
  return &context->enable_custom_attributes_tmp;
}

CoglBitmask *
cogl_context_get_changed_bits_tmp (CoglContext *context)
{
  return &context->changed_bits_tmp;
}

CoglPipeline *
cogl_context_get_default_pipeline (CoglContext *context)
{
  return context->default_pipeline;
}

void
cogl_context_set_default_pipeline (CoglContext  *context,
                                   CoglPipeline *pipeline)
{
  context->default_pipeline = pipeline;
}

CoglPipelineLayer *
cogl_context_get_default_layer_0 (CoglContext *context)
{
  return context->default_layer_0;
}

void
cogl_context_set_default_layer_0 (CoglContext       *context,
                                  CoglPipelineLayer *layer)
{
  context->default_layer_0 = layer;
}

CoglPipelineLayer *
cogl_context_get_default_layer_n (CoglContext *context)
{
  return context->default_layer_n;
}

void
cogl_context_set_default_layer_n (CoglContext       *context,
                                  CoglPipelineLayer *layer)
{
  context->default_layer_n = layer;
}

void
cogl_context_set_dummy_layer_dependant (CoglContext       *context,
                                        CoglPipelineLayer *layer)
{
  context->dummy_layer_dependant = layer;
}

GHashTable *
cogl_context_get_attribute_name_states_hash (CoglContext *context)
{
  return context->attribute_name_states_hash;
}

GHashTable *
cogl_context_get_uniform_name_hash (CoglContext *context)
{
  return context->uniform_name_hash;
}

int
cogl_context_increment_n_attribute_names (CoglContext *context)
{
  return context->n_attribute_names++;
}

CoglSamplerCache *
cogl_context_get_sampler_cache (CoglContext *context)
{
  return context->sampler_cache;
}

GList *
cogl_context_get_framebuffers (CoglContext *context)
{
  return context->framebuffers;
}

void
cogl_context_prepend_framebuffer (CoglContext     *context,
                                  CoglFramebuffer *framebuffer)
{
  context->framebuffers = g_list_prepend (context->framebuffers, framebuffer);
}

void
cogl_context_remove_framebuffer (CoglContext     *context,
                                 CoglFramebuffer *framebuffer)
{
  context->framebuffers = g_list_remove (context->framebuffers, framebuffer);
}

GArray *
cogl_context_get_journal_flush_attributes_array (CoglContext *context)
{
  return context->journal_flush_attributes_array;
}

GArray *
cogl_context_get_journal_clip_bounds (CoglContext *context)
{
  return context->journal_clip_bounds;
}

void
cogl_context_set_journal_clip_bounds (CoglContext *context,
                                      GArray      *array)
{
  context->journal_clip_bounds = array;
}

uint8_t
cogl_context_get_journal_rectangles_color (CoglContext *context)
{
  return context->journal_rectangles_color;
}

void
cogl_context_set_journal_rectangles_color (CoglContext *context,
                                           uint8_t      color)
{
  context->journal_rectangles_color = color;
}

CoglPipeline *
cogl_context_get_opaque_color_pipeline (CoglContext *context)
{
  return context->opaque_color_pipeline;
}

CoglPipeline *
cogl_context_get_blit_texture_pipeline (CoglContext *context)
{
  return context->blit_texture_pipeline;
}

void
cogl_context_set_blit_texture_pipeline (CoglContext  *context,
                                        CoglPipeline *pipeline)
{
  context->blit_texture_pipeline = pipeline;
}

GSList *
cogl_context_get_atlases (CoglContext *context)
{
  return context->atlases;
}

void
cogl_context_prepend_atlas (CoglContext *context,
                            CoglAtlas   *atlas)
{
  context->atlases = g_slist_prepend (context->atlases, atlas);
}

void
cogl_context_remove_atlas (CoglContext *context,
                           CoglAtlas   *atlas)
{
  context->atlases = g_slist_remove (context->atlases, atlas);
}

GHookList *
cogl_context_get_atlas_reorganize_callbacks (CoglContext *context)
{
  return &context->atlas_reorganize_callbacks;
}

CoglList *
cogl_context_get_onscreen_events_queue (CoglContext *context)
{
  return &context->onscreen_events_queue;
}

CoglList *
cogl_context_get_onscreen_dirty_queue (CoglContext *context)
{
  return &context->onscreen_dirty_queue;
}

CoglClosure *
cogl_context_get_onscreen_dispatch_idle (CoglContext *context)
{
  return context->onscreen_dispatch_idle;
}

void
cogl_context_set_onscreen_dispatch_idle (CoglContext *context,
                                         CoglClosure *closure)
{
  context->onscreen_dispatch_idle = closure;
}


CoglIndices *
cogl_context_get_rectangle_byte_indices (CoglContext *context)
{
  return context->rectangle_byte_indices;
}

void
cogl_context_set_rectangle_byte_indices (CoglContext *context,
                                         CoglIndices *indices)
{
  context->rectangle_byte_indices = indices;
}

CoglIndices *
cogl_context_get_rectangle_short_indices (CoglContext *context)
{
  return context->rectangle_short_indices;
}

void
cogl_context_set_rectangle_short_indices (CoglContext *context,
                                          CoglIndices *indices)
{
  context->rectangle_short_indices = indices;
}

int
cogl_context_get_rectangle_short_indices_len (CoglContext *context)
{
  return context->rectangle_short_indices_len;
}

void
cogl_context_set_rectangle_short_indices_len (CoglContext *context,
                                              int          len)
{
  context->rectangle_short_indices_len = len;
}

GByteArray *
cogl_context_get_buffer_map_fallback_array (CoglContext *context)
{
  return context->buffer_map_fallback_array;
}

gboolean
cogl_context_get_buffer_map_fallback_in_use (CoglContext *context)
{
  return context->buffer_map_fallback_in_use;
}

void
cogl_context_set_buffer_map_fallback_in_use (CoglContext *context,
                                             gboolean     in_use)
{
  context->buffer_map_fallback_in_use = in_use;
}

size_t
cogl_context_get_buffer_map_fallback_offset (CoglContext *context)
{
  return context->buffer_map_fallback_offset;
}

void
cogl_context_set_buffer_map_fallback_offset (CoglContext *context,
                                             size_t       offset)
{
  context->buffer_map_fallback_offset = offset;
}
