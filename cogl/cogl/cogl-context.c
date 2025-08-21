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
#include "cogl/cogl-context-test-utils.h"
#include "cogl/cogl-display-private.h"
#include "cogl/cogl-renderer-private.h"
#include "cogl/cogl-journal-private.h"
#include "cogl/cogl-texture-private.h"
#include "cogl/cogl-texture-2d-private.h"
#include "cogl/cogl-pipeline-private.h"
#include "cogl/cogl-framebuffer-private.h"
#include "cogl/cogl-onscreen-private.h"
#include "cogl/cogl-attribute-private.h"
#include "cogl/winsys/cogl-winsys-private.h"

#include <gio/gio.h>
#include <string.h>
#include <stdlib.h>

G_DEFINE_FINAL_TYPE (CoglContext, cogl_context, G_TYPE_OBJECT);


const CoglWinsysVtable *
_cogl_context_get_winsys (CoglContext *context)
{
  return cogl_renderer_get_winsys_vtable (context->display->renderer);
}

static void
cogl_context_dispose (GObject *object)
{
  CoglContext *context = COGL_CONTEXT (object);
  const CoglWinsysVtable *winsys = _cogl_context_get_winsys (context);

  winsys->context_deinit (context);

  if (context->default_gl_texture_2d_tex)
    g_object_unref (context->default_gl_texture_2d_tex);

  if (context->opaque_color_pipeline)
    g_object_unref (context->opaque_color_pipeline);

  if (context->blit_texture_pipeline)
    g_object_unref (context->blit_texture_pipeline);

  if (context->journal_flush_attributes_array)
    g_array_free (context->journal_flush_attributes_array, TRUE);
  if (context->journal_clip_bounds)
    g_array_free (context->journal_clip_bounds, TRUE);

  if (context->rectangle_byte_indices)
    g_object_unref (context->rectangle_byte_indices);
  if (context->rectangle_short_indices)
    g_object_unref (context->rectangle_short_indices);

  if (context->default_pipeline)
    g_object_unref (context->default_pipeline);

  if (context->dummy_layer_dependant)
    g_object_unref (context->dummy_layer_dependant);
  if (context->default_layer_n)
    g_object_unref (context->default_layer_n);
  if (context->default_layer_0)
    g_object_unref (context->default_layer_0);

  if (context->current_clip_stack_valid)
    _cogl_clip_stack_unref (context->current_clip_stack);

  g_slist_free (context->atlases);
  g_hook_list_clear (&context->atlas_reorganize_callbacks);

  _cogl_bitmask_destroy (&context->enabled_custom_attributes);
  _cogl_bitmask_destroy (&context->enable_custom_attributes_tmp);
  _cogl_bitmask_destroy (&context->changed_bits_tmp);

  if (context->current_modelview_entry)
    cogl_matrix_entry_unref (context->current_modelview_entry);
  if (context->current_projection_entry)
    cogl_matrix_entry_unref (context->current_projection_entry);

  _cogl_pipeline_cache_free (context->pipeline_cache);

  _cogl_sampler_cache_free (context->sampler_cache);

  g_ptr_array_free (context->uniform_names, TRUE);
  g_hash_table_destroy (context->uniform_name_hash);

  g_hash_table_destroy (context->attribute_name_states_hash);
  g_array_free (context->attribute_name_index_map, TRUE);

  g_byte_array_free (context->buffer_map_fallback_array, TRUE);

  g_object_unref (context->display);

  g_hash_table_remove_all (context->named_pipelines);
  g_hash_table_destroy (context->named_pipelines);

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

extern void
_cogl_create_context_driver (CoglContext *context);

static void
_cogl_init_feature_overrides (CoglContext *ctx)
{
  if (G_UNLIKELY (COGL_DEBUG_ENABLED (COGL_DEBUG_DISABLE_PBOS)))
    COGL_FLAGS_SET (ctx->private_features, COGL_PRIVATE_FEATURE_PBOS, FALSE);
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

  g_return_val_if_fail (display != NULL, NULL);

  CoglContext *context;
  uint8_t white_pixel[] = { 0xff, 0xff, 0xff, 0xff };
  const CoglWinsysVtable *winsys;
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
  memset (context->features, 0, sizeof (context->features));
  memset (context->private_features, 0, sizeof (context->private_features));
  memset (context->winsys_features, 0, sizeof (context->winsys_features));

  context->display = g_object_ref (display);
  /* Keep a backpointer to the context */
  display->context = context;

  winsys = _cogl_context_get_winsys (context);
  if (!winsys->context_init (context, error))
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
  context->attribute_name_index_map = NULL;
  context->n_attribute_names = 0;

  /* The "cogl_color_in" attribute needs a deterministic name_index
   * so we make sure it's the first attribute name we register */
  _cogl_attribute_register_attribute_name (context, "cogl_color_in");


  context->uniform_names =
    g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);
  context->uniform_name_hash = g_hash_table_new (g_str_hash, g_str_equal);
  context->n_uniform_names = 0;

  /* Initialise the driver specific state */
  _cogl_init_feature_overrides (context);

  context->sampler_cache = _cogl_sampler_cache_new (context);

  _cogl_pipeline_init_default_pipeline (context);
  _cogl_pipeline_init_default_layers (context);
  _cogl_pipeline_init_state_hash_functions ();
  _cogl_pipeline_init_layer_state_hash_functions ();

  context->current_clip_stack_valid = FALSE;
  context->current_clip_stack = NULL;

  graphene_matrix_init_identity (&context->identity_matrix);
  graphene_matrix_init_identity (&context->y_flip_matrix);
  graphene_matrix_scale (&context->y_flip_matrix, 1, -1, 1);

  context->opaque_color_pipeline = cogl_pipeline_new (context);
  cogl_pipeline_set_static_name (context->opaque_color_pipeline,
                                 "CoglContext (opaque color)");

  context->codegen_header_buffer = g_string_new ("");
  context->codegen_source_buffer = g_string_new ("");

  context->default_gl_texture_2d_tex = NULL;

  context->framebuffers = NULL;
  context->current_draw_buffer = NULL;
  context->current_read_buffer = NULL;
  context->current_draw_buffer_state_flushed = 0;
  context->current_draw_buffer_changes = COGL_FRAMEBUFFER_STATE_ALL;

  _cogl_list_init (&context->onscreen_events_queue);
  _cogl_list_init (&context->onscreen_dirty_queue);

  context->journal_flush_attributes_array =
    g_array_new (TRUE, FALSE, sizeof (CoglAttribute *));
  context->journal_clip_bounds = NULL;

  context->current_pipeline = NULL;
  context->current_pipeline_changes_since_flush = 0;
  context->current_pipeline_with_color_attrib = FALSE;

  _cogl_bitmask_init (&context->enabled_custom_attributes);
  _cogl_bitmask_init (&context->enable_custom_attributes_tmp);
  _cogl_bitmask_init (&context->changed_bits_tmp);

  context->max_activateable_texture_units = -1;

  context->current_gl_program = 0;

  context->current_gl_dither_enabled = TRUE;

  context->gl_blend_enable_cache = FALSE;

  context->depth_test_enabled_cache = FALSE;
  context->depth_test_function_cache = COGL_DEPTH_TEST_FUNCTION_LESS;
  context->depth_writing_enabled_cache = TRUE;
  context->depth_range_near_cache = 0;
  context->depth_range_far_cache = 1;

  context->pipeline_cache = _cogl_pipeline_cache_new (context);

  for (i = 0; i < COGL_BUFFER_BIND_TARGET_COUNT; i++)
    context->current_buffer[i] = NULL;

  context->stencil_pipeline = cogl_pipeline_new (context);
  cogl_pipeline_set_static_name (context->stencil_pipeline,
                                 "Cogl (stencil)");

  context->rectangle_byte_indices = NULL;
  context->rectangle_short_indices = NULL;
  context->rectangle_short_indices_len = 0;

  context->blit_texture_pipeline = NULL;

  context->current_modelview_entry = NULL;
  context->current_projection_entry = NULL;
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

  context->atlases = NULL;
  g_hook_list_init (&context->atlas_reorganize_callbacks, sizeof (GHook));

  context->buffer_map_fallback_array = g_byte_array_new ();
  context->buffer_map_fallback_in_use = FALSE;

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

const char *
_cogl_context_get_driver_vendor (CoglContext *context)
{
  CoglDriver *driver = cogl_context_get_driver (context);
  CoglDriverClass *driver_klass = COGL_DRIVER_GET_CLASS (driver);

  return driver_klass->get_vendor (driver, context);
}

gboolean
_cogl_context_update_features (CoglContext *context,
                               GError **error)
{
  CoglDriver *driver = cogl_context_get_driver (context);
  CoglDriverClass *driver_klass = COGL_DRIVER_GET_CLASS (driver);

  return driver_klass->update_features (driver, context, error);
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
  const CoglWinsysVtable *winsys = _cogl_context_get_winsys (context);

  if (!winsys->update_sync)
    return;

  winsys->update_sync (context);
}

int
cogl_context_get_latest_sync_fd (CoglContext *context)
{
  const CoglWinsysVtable *winsys = _cogl_context_get_winsys (context);

  if (!winsys->get_sync_fd)
    return -1;

  return winsys->get_sync_fd (context);
}

CoglGraphicsResetStatus
cogl_context_get_graphics_reset_status (CoglContext *context)
{
  CoglDriver *driver = cogl_context_get_driver (context);
  CoglDriverClass *driver_klass = COGL_DRIVER_GET_CLASS (driver);

  return driver_klass->get_graphics_reset_status (driver, context);
}

gboolean
cogl_context_is_hardware_accelerated (CoglContext *context)
{
  CoglDriver *driver = cogl_context_get_driver (context);
  CoglDriverClass *driver_klass = COGL_DRIVER_GET_CLASS (driver);

  if (driver_klass->is_hardware_accelerated)
    return driver_klass->is_hardware_accelerated (driver, context);
  else
    return FALSE;
}

gboolean
cogl_context_format_supports_upload (CoglContext *ctx,
                                     CoglPixelFormat format)
{
  CoglDriver *driver = cogl_context_get_driver (ctx);
  CoglDriverClass *driver_klass = COGL_DRIVER_GET_CLASS (driver);

  return driver_klass->format_supports_upload (driver, ctx, format);
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

/**
 * cogl_context_free_timestamp_query:
 * @context: a #CoglContext object
 * @query: (transfer full): the #CoglTimestampQuery to free
 *
 * Free the #CoglTimestampQuery
 */
void
cogl_context_free_timestamp_query (CoglContext        *context,
                                   CoglTimestampQuery *query)
{
  CoglDriver *driver = cogl_context_get_driver (context);
  CoglDriverClass *driver_klass = COGL_DRIVER_GET_CLASS (driver);

  driver_klass->free_timestamp_query (driver, context, query);
}

int64_t
cogl_context_timestamp_query_get_time_ns (CoglContext        *context,
                                          CoglTimestampQuery *query)
{
  CoglDriver *driver = cogl_context_get_driver (context);
  CoglDriverClass *driver_klass = COGL_DRIVER_GET_CLASS (driver);

  return driver_klass->timestamp_query_get_time_ns (driver, context, query);
}

int64_t
cogl_context_get_gpu_time_ns (CoglContext *context)
{
  CoglDriver *driver = cogl_context_get_driver (context);
  CoglDriverClass *driver_klass;

  g_return_val_if_fail (cogl_context_has_feature (context,
                                                  COGL_FEATURE_ID_TIMESTAMP_QUERY),
                        0);

  driver_klass = COGL_DRIVER_GET_CLASS (driver);

  return driver_klass->get_gpu_time_ns (driver, context);
}

/* FIXME: we should distinguish renderer and context features */
gboolean
cogl_context_has_winsys_feature (CoglContext       *context,
                                 CoglWinsysFeature  feature)
{
  return COGL_FLAGS_GET (context->winsys_features, feature);
}

gboolean
cogl_context_has_feature (CoglContext   *context,
                          CoglFeatureID  feature)
{
  return COGL_FLAGS_GET (context->features, feature);
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
