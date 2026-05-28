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
#include "cogl/cogl-renderer-private.h"
#include "cogl/cogl-journal-private.h"
#include "cogl/cogl-texture-private.h"
#include "cogl/cogl-pipeline-private.h"
#include "cogl/cogl-framebuffer-private.h"
#include "cogl/cogl-onscreen-private.h"
#include "cogl/cogl-attribute-private.h"

#include <gio/gio.h>
#include <string.h>
#include <stdlib.h>

typedef struct _CoglContextPrivate
{
  CoglDisplay *display;

  CoglPipeline *default_pipeline;
  CoglPipelineLayer *default_layer_0;
  CoglPipelineLayer *default_layer_n;
  CoglPipelineLayer *dummy_layer_dependant;

  GHashTable *attribute_name_states_hash;
  GArray *attribute_name_index_map;
  int n_attribute_names;

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

  CoglPipelineCache *pipeline_cache;

  /* Textures */
  CoglTexture *default_2d_texture;

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

  CoglBuffer *current_buffer[COGL_BUFFER_BIND_TARGET_COUNT];

  /* Framebuffers */
  unsigned long current_draw_buffer_state_flushed;
  unsigned long current_draw_buffer_changes;
  CoglFramebuffer *current_draw_buffer;
  CoglFramebuffer *current_read_buffer;

  CoglList onscreen_dirty_queue;
  CoglClosure *onscreen_dispatch_idle;

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
} CoglContextPrivate;


enum
{
  PROP_0,
  PROP_DISPLAY,
  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

static void cogl_context_initable_iface_init (GInitableIface *iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (CoglContext, cogl_context, G_TYPE_OBJECT,
                                  G_ADD_PRIVATE (CoglContext)
                                  G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                         cogl_context_initable_iface_init))

static void
cogl_context_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  CoglContext *context = COGL_CONTEXT (object);
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  switch (prop_id)
    {
    case PROP_DISPLAY:
      g_value_set_object (value, priv->display);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cogl_context_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  CoglContext *context = COGL_CONTEXT (object);
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  switch (prop_id)
    {
    case PROP_DISPLAY:
      priv->display = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

void
cogl_context_clear_onscreen_dirty_queue (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  while (!_cogl_list_empty (&priv->onscreen_dirty_queue))
    {
      CoglOnscreenQueuedDirty *qe =
        _cogl_container_of (priv->onscreen_dirty_queue.next,
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
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  cogl_context_clear_onscreen_dirty_queue (context);

  g_clear_object (&priv->default_2d_texture);
  g_clear_object (&priv->opaque_color_pipeline);
  g_clear_object (&priv->blit_texture_pipeline);

  g_clear_pointer (&priv->journal_flush_attributes_array, g_array_unref);
  g_clear_pointer (&priv->journal_clip_bounds, g_array_unref);

  g_clear_object (&priv->rectangle_byte_indices);
  g_clear_object (&priv->rectangle_short_indices);

  g_clear_object (&priv->default_pipeline);

  g_clear_object (&priv->dummy_layer_dependant);
  g_clear_object (&priv->default_layer_n);
  g_clear_object (&priv->default_layer_0);

  if (priv->current_clip_stack_valid)
    g_clear_pointer (&priv->current_clip_stack, _cogl_clip_stack_unref);

  g_clear_slist (&priv->atlases, NULL);
  g_hook_list_clear (&priv->atlas_reorganize_callbacks);

  g_clear_pointer (&priv->current_modelview_entry, cogl_matrix_entry_unref);
  g_clear_pointer (&priv->current_projection_entry, cogl_matrix_entry_unref);

  g_clear_pointer (&priv->uniform_names, g_ptr_array_unref);
  g_clear_pointer (&priv->uniform_name_hash, g_hash_table_destroy);

  g_clear_pointer (&priv->attribute_name_states_hash,
                   g_hash_table_destroy);
  g_clear_pointer (&priv->attribute_name_index_map, g_array_unref);

  g_clear_pointer (&priv->buffer_map_fallback_array, g_byte_array_unref);

  g_clear_pointer (&priv->named_pipelines, g_hash_table_destroy);

  g_clear_pointer (&priv->pipeline_cache, _cogl_pipeline_cache_free);
  g_clear_pointer (&priv->sampler_cache, _cogl_sampler_cache_free);

  g_clear_object (&priv->display);

  G_OBJECT_CLASS (cogl_context_parent_class)->dispose (object);
}

static void
cogl_context_init (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->attribute_name_states_hash =
    g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

  _cogl_attribute_register_attribute_name (context, "cogl_color_in");

  priv->uniform_names =
    g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);
  priv->uniform_name_hash = g_hash_table_new (g_str_hash, g_str_equal);

  graphene_matrix_init_identity (&priv->identity_matrix);
  graphene_matrix_init_identity (&priv->y_flip_matrix);
  graphene_matrix_scale (&priv->y_flip_matrix, 1, -1, 1);

  priv->current_draw_buffer_changes = COGL_FRAMEBUFFER_STATE_ALL;

  _cogl_list_init (&priv->onscreen_dirty_queue);

  priv->journal_flush_attributes_array =
    g_array_new (TRUE, FALSE, sizeof (CoglAttribute *));

  _cogl_matrix_entry_identity_init (&priv->identity_entry);

  g_hook_list_init (&priv->atlas_reorganize_callbacks, sizeof (GHook));

  priv->buffer_map_fallback_array = g_byte_array_new ();

  priv->sampler_cache = _cogl_sampler_cache_new (context);

  _cogl_pipeline_init_default_pipeline (context);
  _cogl_pipeline_init_state_hash_functions ();
  _cogl_pipeline_init_layer_state_hash_functions ();

  priv->opaque_color_pipeline = cogl_pipeline_new (context);
  cogl_pipeline_set_static_name (priv->opaque_color_pipeline,
                                 "CoglContext (opaque color)");

  priv->pipeline_cache = _cogl_pipeline_cache_new (context);

  priv->stencil_pipeline = cogl_pipeline_new (context);
  cogl_pipeline_set_static_name (priv->stencil_pipeline,
                                 "Cogl (stencil)");

  priv->named_pipelines =
    g_hash_table_new_full (NULL, NULL, NULL, g_object_unref);
}

static void
cogl_context_class_init (CoglContextClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->get_property = cogl_context_get_property;
  object_class->set_property = cogl_context_set_property;
  object_class->dispose = cogl_context_dispose;

  obj_props[PROP_DISPLAY] =
    g_param_spec_object ("display", NULL, NULL,
                         COGL_TYPE_DISPLAY,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_NAME);

  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static gboolean
cogl_context_initable_init (GInitable     *initable,
                            GCancellable  *cancellable,
                            GError       **error)
{
  return TRUE;
}

static void
cogl_context_initable_iface_init (GInitableIface *iface)
{
  iface->init = cogl_context_initable_init;
}


CoglDisplay *
cogl_context_get_display (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->display;
}

CoglRenderer *
cogl_context_get_renderer (CoglContext *context)
{
  CoglDisplay *display = cogl_context_get_display (context);

  return cogl_display_get_renderer (display);
}

void
cogl_context_set_winsys_feature (CoglContext      *context,
                                 CoglWinsysFeature feature,
                                 gboolean          value)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  COGL_FLAGS_SET (priv->winsys_features, feature, value);
}

void
_cogl_context_set_current_projection_entry (CoglContext *context,
                                            CoglMatrixEntry *entry)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  cogl_matrix_entry_ref (entry);
  if (priv->current_projection_entry)
    cogl_matrix_entry_unref (priv->current_projection_entry);
  priv->current_projection_entry = entry;
}

void
_cogl_context_set_current_modelview_entry (CoglContext *context,
                                           CoglMatrixEntry *entry)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  cogl_matrix_entry_ref (entry);
  if (priv->current_modelview_entry)
    cogl_matrix_entry_unref (priv->current_modelview_entry);
  priv->current_modelview_entry = entry;
}

void
cogl_context_set_named_pipeline (CoglContext     *context,
                                 CoglPipelineKey *key,
                                 CoglPipeline    *pipeline)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  if (pipeline)
    {
      g_debug ("Adding named pipeline %s", *key);
      g_hash_table_insert (priv->named_pipelines, (gpointer) key, pipeline);
    }
  else
    {
      g_debug ("Removing named pipeline %s", *key);
      g_hash_table_remove (priv->named_pipelines, (gpointer) key);
    }
}

CoglPipeline *
cogl_context_get_named_pipeline (CoglContext     *context,
                                 CoglPipelineKey *key)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return g_hash_table_lookup (priv->named_pipelines, key);
}

/* FIXME: we should distinguish renderer and context features */
gboolean
cogl_context_has_winsys_feature (CoglContext       *context,
                                 CoglWinsysFeature  feature)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return COGL_FLAGS_GET (priv->winsys_features, feature);
}

void
cogl_context_flush (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);
  GList *l;

  for (l = priv->framebuffers; l; l = l->next)
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
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->current_pipeline;
}

void
cogl_context_set_current_pipeline (CoglContext  *context,
                                   CoglPipeline *pipeline)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  if (priv->current_pipeline != NULL)
    g_object_unref (priv->current_pipeline);
  priv->current_pipeline = pipeline;
}

unsigned long
cogl_context_get_current_pipeline_age (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->current_pipeline_age;
}

void
cogl_context_set_current_pipeline_age (CoglContext   *context,
                                       unsigned long  age)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->current_pipeline_age = age;
}

void
cogl_context_decrement_current_pipeline_age (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->current_pipeline_age--;
}

unsigned long
cogl_context_get_current_pipeline_changes_since_flush (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->current_pipeline_changes_since_flush;
}

void
cogl_context_set_current_pipeline_changes_since_flush (CoglContext   *context,
                                                       unsigned long  changes)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->current_pipeline_changes_since_flush = changes;
}

void
cogl_context_add_current_pipeline_changes_since_flush (CoglContext   *context,
                                                       unsigned long  changes)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->current_pipeline_changes_since_flush |= changes;
}

gboolean
cogl_context_get_current_pipeline_with_color_attrib (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->current_pipeline_with_color_attrib;
}

void
cogl_context_set_current_pipeline_with_color_attrib (CoglContext *context,
                                                     gboolean     with_color_attrib)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->current_pipeline_with_color_attrib = with_color_attrib;
}

gboolean
cogl_context_get_current_pipeline_unknown_color_alpha (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->current_pipeline_unknown_color_alpha;
}

void
cogl_context_set_current_pipeline_unknown_color_alpha (CoglContext *context,
                                                       gboolean     unknown_color_alpha)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->current_pipeline_unknown_color_alpha = unknown_color_alpha;
}

CoglPipelineCache *
cogl_context_get_pipeline_cache (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->pipeline_cache;
}

CoglFramebuffer *
cogl_context_get_current_draw_buffer (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->current_draw_buffer;
}

void
cogl_context_set_current_draw_buffer (CoglContext     *context,
                                      CoglFramebuffer *framebuffer)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->current_draw_buffer = framebuffer;
}

CoglFramebuffer *
cogl_context_get_current_read_buffer (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->current_read_buffer;
}

void
cogl_context_set_current_read_buffer (CoglContext     *context,
                                      CoglFramebuffer *framebuffer)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->current_read_buffer = framebuffer;
}

unsigned long
cogl_context_get_current_draw_buffer_state_flushed (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->current_draw_buffer_state_flushed;
}

void
cogl_context_set_current_draw_buffer_state_flushed (CoglContext   *context,
                                                    unsigned long  state)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->current_draw_buffer_state_flushed = state;
}

void
cogl_context_add_current_draw_buffer_state_flushed (CoglContext   *context,
                                                    unsigned long  state)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->current_draw_buffer_state_flushed |= state;
}

unsigned long
cogl_context_get_current_draw_buffer_changes (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->current_draw_buffer_changes;
}

void
cogl_context_add_current_draw_buffer_changes (CoglContext   *context,
                                              unsigned long  changes)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->current_draw_buffer_changes |= changes;
}

void
cogl_context_clear_current_draw_buffer_changes (CoglContext   *context,
                                                unsigned long  changes)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->current_draw_buffer_changes &= ~changes;
}

CoglClipStack *
cogl_context_get_current_clip_stack (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->current_clip_stack;
}

void
cogl_context_set_current_clip_stack (CoglContext   *context,
                                     CoglClipStack *stack)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->current_clip_stack = stack;
}

gboolean
cogl_context_get_current_clip_stack_valid (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->current_clip_stack_valid;
}

void
cogl_context_set_current_clip_stack_valid (CoglContext *context,
                                           gboolean     valid)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->current_clip_stack_valid = valid;
}

CoglMatrixEntry *
cogl_context_get_current_projection_entry (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->current_projection_entry;
}

void
cogl_context_set_current_projection_entry (CoglContext     *context,
                                           CoglMatrixEntry *entry)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->current_projection_entry = entry;
}

CoglMatrixEntry *
cogl_context_get_current_modelview_entry (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->current_modelview_entry;
}

void
cogl_context_set_current_modelview_entry (CoglContext     *context,
                                          CoglMatrixEntry *entry)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->current_modelview_entry = entry;
}

CoglMatrixEntry *
cogl_context_get_identity_entry (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return &priv->identity_entry;
}

CoglPipeline *
cogl_context_get_stencil_pipeline (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->stencil_pipeline;
}

graphene_matrix_t *
cogl_context_get_y_flip_matrix (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return &priv->y_flip_matrix;
}

CoglTexture *
cogl_context_get_default_2d_texture (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->default_2d_texture;
}

void
cogl_context_set_default_2d_texture (CoglContext *context,
                                     CoglTexture *texture)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->default_2d_texture = texture;
}

CoglBuffer *
cogl_context_get_current_buffer (CoglContext         *context,
                                 CoglBufferBindTarget target)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->current_buffer[target];
}

void
cogl_context_set_current_buffer (CoglContext         *context,
                                 CoglBufferBindTarget target,
                                 CoglBuffer          *buffer)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->current_buffer[target] = buffer;
}

GArray *
cogl_context_get_attribute_name_index_map (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->attribute_name_index_map;
}

void
cogl_context_set_attribute_name_index_map (CoglContext *context,
                                           GArray      *array)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->attribute_name_index_map = array;
}

GPtrArray *
cogl_context_get_uniform_names (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->uniform_names;
}

int
cogl_context_get_n_uniform_names (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->n_uniform_names;
}

int
cogl_context_increment_n_uniform_names (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->n_uniform_names++;
}

CoglPipeline *
cogl_context_get_default_pipeline (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->default_pipeline;
}

void
cogl_context_set_default_pipeline (CoglContext  *context,
                                   CoglPipeline *pipeline)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->default_pipeline = pipeline;
}

CoglPipelineLayer *
cogl_context_get_default_layer_0 (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->default_layer_0;
}

void
cogl_context_set_default_layer_0 (CoglContext       *context,
                                  CoglPipelineLayer *layer)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->default_layer_0 = layer;
}

CoglPipelineLayer *
cogl_context_get_default_layer_n (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->default_layer_n;
}

void
cogl_context_set_default_layer_n (CoglContext       *context,
                                  CoglPipelineLayer *layer)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->default_layer_n = layer;
}

void
cogl_context_set_dummy_layer_dependant (CoglContext       *context,
                                        CoglPipelineLayer *layer)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->dummy_layer_dependant = layer;
}

GHashTable *
cogl_context_get_attribute_name_states_hash (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->attribute_name_states_hash;
}

GHashTable *
cogl_context_get_uniform_name_hash (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->uniform_name_hash;
}

int
cogl_context_get_n_attribute_names (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->n_attribute_names;
}

int
cogl_context_increment_n_attribute_names (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->n_attribute_names++;
}

CoglSamplerCache *
cogl_context_get_sampler_cache (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->sampler_cache;
}

GList *
cogl_context_get_framebuffers (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->framebuffers;
}

void
cogl_context_prepend_framebuffer (CoglContext     *context,
                                  CoglFramebuffer *framebuffer)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->framebuffers = g_list_prepend (priv->framebuffers, framebuffer);
}

void
cogl_context_remove_framebuffer (CoglContext     *context,
                                 CoglFramebuffer *framebuffer)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->framebuffers = g_list_remove (priv->framebuffers, framebuffer);
}

GArray *
cogl_context_get_journal_flush_attributes_array (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->journal_flush_attributes_array;
}

GArray *
cogl_context_get_journal_clip_bounds (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->journal_clip_bounds;
}

void
cogl_context_set_journal_clip_bounds (CoglContext *context,
                                      GArray      *array)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->journal_clip_bounds = array;
}

uint8_t
cogl_context_get_journal_rectangles_color (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->journal_rectangles_color;
}

void
cogl_context_set_journal_rectangles_color (CoglContext *context,
                                           uint8_t      color)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->journal_rectangles_color = color;
}

CoglPipeline *
cogl_context_get_opaque_color_pipeline (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->opaque_color_pipeline;
}

CoglPipeline *
cogl_context_get_blit_texture_pipeline (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->blit_texture_pipeline;
}

void
cogl_context_set_blit_texture_pipeline (CoglContext  *context,
                                        CoglPipeline *pipeline)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->blit_texture_pipeline = pipeline;
}

GSList *
cogl_context_get_atlases (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->atlases;
}

void
cogl_context_prepend_atlas (CoglContext *context,
                            CoglAtlas   *atlas)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->atlases = g_slist_prepend (priv->atlases, atlas);
}

void
cogl_context_remove_atlas (CoglContext *context,
                           CoglAtlas   *atlas)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->atlases = g_slist_remove (priv->atlases, atlas);
}

GHookList *
cogl_context_get_atlas_reorganize_callbacks (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return &priv->atlas_reorganize_callbacks;
}

CoglList *
cogl_context_get_onscreen_dirty_queue (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return &priv->onscreen_dirty_queue;
}

CoglClosure *
cogl_context_get_onscreen_dispatch_idle (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->onscreen_dispatch_idle;
}

void
cogl_context_set_onscreen_dispatch_idle (CoglContext *context,
                                         CoglClosure *closure)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->onscreen_dispatch_idle = closure;
}


CoglIndices *
cogl_context_get_rectangle_byte_indices (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->rectangle_byte_indices;
}

void
cogl_context_set_rectangle_byte_indices (CoglContext *context,
                                         CoglIndices *indices)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->rectangle_byte_indices = indices;
}

CoglIndices *
cogl_context_get_rectangle_short_indices (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->rectangle_short_indices;
}

void
cogl_context_set_rectangle_short_indices (CoglContext *context,
                                          CoglIndices *indices)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->rectangle_short_indices = indices;
}

int
cogl_context_get_rectangle_short_indices_len (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->rectangle_short_indices_len;
}

void
cogl_context_set_rectangle_short_indices_len (CoglContext *context,
                                              int          len)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->rectangle_short_indices_len = len;
}

GByteArray *
cogl_context_get_buffer_map_fallback_array (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->buffer_map_fallback_array;
}

gboolean
cogl_context_get_buffer_map_fallback_in_use (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->buffer_map_fallback_in_use;
}

void
cogl_context_set_buffer_map_fallback_in_use (CoglContext *context,
                                             gboolean     in_use)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->buffer_map_fallback_in_use = in_use;
}

size_t
cogl_context_get_buffer_map_fallback_offset (CoglContext *context)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  return priv->buffer_map_fallback_offset;
}

void
cogl_context_set_buffer_map_fallback_offset (CoglContext *context,
                                             size_t       offset)
{
  CoglContextPrivate *priv =
    cogl_context_get_instance_private (context);

  priv->buffer_map_fallback_offset = offset;
}
