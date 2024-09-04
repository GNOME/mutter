/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2011 Intel Corporation.
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
 *   Neil Roberts <neil@linux.intel.com>
 */

#include "config.h"

#include <glib.h>

#include "clutter/pango/clutter-pango-pipeline-cache.h"

static GQuark pipeline_destroy_notify_key = 0;

typedef struct _PangoPipelineCacheEntry
{
  /* This will take a reference or it can be NULL to represent the
     pipeline used to render colors */
  CoglTexture *texture;

  /* This will only take a weak reference */
  CoglPipeline *pipeline;
} PangoPipelineCacheEntry;

static void
clutter_pango_pipeline_cache_key_destroy (void *data)
{
  if (data)
    g_object_unref (data);
}

static void
clutter_pango_pipeline_cache_value_destroy (void *data)
{
  PangoPipelineCacheEntry *cache_entry = data;

  g_clear_object (&cache_entry->texture);

  /* We don't need to unref the pipeline because it only takes a weak
     reference */

  g_free (cache_entry);
}

ClutterPangoPipelineCache *
clutter_pango_pipeline_cache_new (CoglContext *ctx)
{
  ClutterPangoPipelineCache *cache = g_new (ClutterPangoPipelineCache, 1);

  cache->ctx = g_object_ref (ctx);

  /* The key is the pipeline pointer. A reference is taken when the
     pipeline is used as a key so we should unref it again in the
     destroy function */
  cache->hash_table =
    g_hash_table_new_full (g_direct_hash,
                           g_direct_equal,
                           clutter_pango_pipeline_cache_key_destroy,
                           clutter_pango_pipeline_cache_value_destroy);

  cache->base_texture_rgba_pipeline = NULL;
  cache->base_texture_alpha_pipeline = NULL;

  return cache;
}

static CoglPipeline *
get_base_texture_rgba_pipeline (ClutterPangoPipelineCache *cache)
{
  if (cache->base_texture_rgba_pipeline == NULL)
    {
      CoglPipeline *pipeline;

      pipeline = cache->base_texture_rgba_pipeline =
        cogl_pipeline_new (cache->ctx);
      cogl_pipeline_set_static_name (pipeline, "CoglPango (texture rgba)");

      cogl_pipeline_set_layer_wrap_mode (pipeline, 0,
                                         COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);
    }

  return cache->base_texture_rgba_pipeline;
}

static CoglPipeline *
get_base_texture_alpha_pipeline (ClutterPangoPipelineCache *cache)
{
  if (cache->base_texture_alpha_pipeline == NULL)
    {
      CoglPipeline *pipeline;

      pipeline = cogl_pipeline_copy (get_base_texture_rgba_pipeline (cache));
      cogl_pipeline_set_static_name (pipeline, "CoglPango (texture alpha)");
      cache->base_texture_alpha_pipeline = pipeline;

      /* The default combine mode of pipelines is to modulate (A x B)
       * the texture RGBA channels with the RGBA channels of the
       * previous layer (which in our case is just the font color)
       *
       * Since the RGB for an alpha texture is defined as 0, this gives us:
       *
       *  result.rgb = color.rgb * 0
       *  result.a = color.a * texture.a
       *
       * What we want is premultiplied rgba values:
       *
       *  result.rgba = color.rgb * texture.a
       *  result.a = color.a * texture.a
       */
      cogl_pipeline_set_layer_combine (pipeline, 0, /* layer */
                                       "RGBA = MODULATE (PREVIOUS, TEXTURE[A])",
                                       NULL);
    }

  return cache->base_texture_alpha_pipeline;
}

typedef struct
{
  ClutterPangoPipelineCache *cache;
  CoglTexture *texture;
} PipelineDestroyNotifyData;

static void
pipeline_destroy_notify_cb (void *user_data)
{
  PipelineDestroyNotifyData *data = user_data;

  g_hash_table_remove (data->cache->hash_table, data->texture);
  g_free (data);
}

CoglPipeline *
clutter_pango_pipeline_cache_get (ClutterPangoPipelineCache *cache,
                                  CoglTexture               *texture)
{
  PangoPipelineCacheEntry *entry;
  PipelineDestroyNotifyData *destroy_data;
  pipeline_destroy_notify_key = g_quark_from_static_string ("-clutter-pango-pipeline-cache-key");

  /* Look for an existing entry */
  entry = g_hash_table_lookup (cache->hash_table, texture);

  if (entry)
    return g_object_ref (entry->pipeline);

  /* No existing pipeline was found so let's create another */
  entry = g_new0 (PangoPipelineCacheEntry, 1);

  if (texture)
    {
      CoglPipeline *base;

      entry->texture = g_object_ref (texture);

      if (cogl_texture_get_format (entry->texture) == COGL_PIXEL_FORMAT_A_8)
        base = get_base_texture_alpha_pipeline (cache);
      else
        base = get_base_texture_rgba_pipeline (cache);

      entry->pipeline = cogl_pipeline_copy (base);

      cogl_pipeline_set_layer_texture (entry->pipeline, 0 /* layer */, texture);
    }
  else
    {
      entry->texture = NULL;
      entry->pipeline = cogl_pipeline_new (cache->ctx);
      cogl_pipeline_set_static_name (entry->pipeline, "CoglPango (list entry)");
    }

  /* Add a weak reference to the pipeline so we can remove it from the
     hash table when it is destroyed */
  destroy_data = g_new0 (PipelineDestroyNotifyData, 1);
  destroy_data->cache = cache;
  destroy_data->texture = texture;
  g_object_set_qdata_full (G_OBJECT (entry->pipeline),
                           pipeline_destroy_notify_key,
                           destroy_data,
                           pipeline_destroy_notify_cb);

  g_hash_table_insert (cache->hash_table,
                       texture ? g_object_ref (texture) : NULL,
                       entry);

  /* This doesn't take a reference on the pipeline so that it will use
     the newly created reference */
  return entry->pipeline;
}

void
clutter_pango_pipeline_cache_free (ClutterPangoPipelineCache *cache)
{
  g_clear_object (&cache->base_texture_rgba_pipeline);
  g_clear_object (&cache->base_texture_alpha_pipeline);

  g_clear_pointer (&cache->hash_table, g_hash_table_destroy);

  g_clear_object (&cache->ctx);

  g_free (cache);
}
