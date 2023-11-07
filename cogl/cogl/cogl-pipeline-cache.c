/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2011, 2013 Intel Corporation.
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
 * Authors:
 *   Neil Roberts <neil@linux.intel.com>
 *   Robert Bragg <robert@linux.intel.com>
 */

#include "config.h"

#include "cogl/cogl-context-private.h"
#include "cogl/cogl-pipeline-private.h"
#include "cogl/cogl-pipeline-cache.h"
#include "cogl/cogl-pipeline-cache-private.h"
#include "cogl/cogl-pipeline-hash-table.h"

struct _CoglPipelineCache
{
  CoglPipelineHashTable fragment_hash;
  CoglPipelineHashTable vertex_hash;
  CoglPipelineHashTable combined_hash;
};

CoglPipelineCache *
_cogl_pipeline_cache_new (CoglContext *ctx)
{
  g_autofree CoglPipelineCache *cache = g_new (CoglPipelineCache, 1);
  unsigned long vertex_state;
  unsigned long layer_vertex_state;
  unsigned int fragment_state;
  unsigned int layer_fragment_state;

  vertex_state =
    _cogl_pipeline_get_state_for_vertex_codegen (ctx);
  layer_vertex_state =
    COGL_PIPELINE_LAYER_STATE_AFFECTS_VERTEX_CODEGEN;
  fragment_state =
    _cogl_pipeline_get_state_for_fragment_codegen (ctx);
  layer_fragment_state =
    _cogl_pipeline_get_layer_state_for_fragment_codegen (ctx);

  _cogl_pipeline_hash_table_init (&cache->vertex_hash,
                                  vertex_state,
                                  layer_vertex_state,
                                  "vertex shaders");
  _cogl_pipeline_hash_table_init (&cache->fragment_hash,
                                  fragment_state,
                                  layer_fragment_state,
                                  "fragment shaders");
  _cogl_pipeline_hash_table_init (&cache->combined_hash,
                                  vertex_state | fragment_state,
                                  layer_vertex_state | layer_fragment_state,
                                  "programs");

  return g_steal_pointer (&cache);
}

void
_cogl_pipeline_cache_free (CoglPipelineCache *cache)
{
  _cogl_pipeline_hash_table_destroy (&cache->fragment_hash);
  _cogl_pipeline_hash_table_destroy (&cache->vertex_hash);
  _cogl_pipeline_hash_table_destroy (&cache->combined_hash);
  g_free (cache);
}

CoglPipelineCacheEntry *
_cogl_pipeline_cache_get_fragment_template (CoglPipelineCache *cache,
                                            CoglPipeline *key_pipeline)
{
  return _cogl_pipeline_hash_table_get (&cache->fragment_hash,
                                        key_pipeline);
}

CoglPipelineCacheEntry *
_cogl_pipeline_cache_get_vertex_template (CoglPipelineCache *cache,
                                          CoglPipeline *key_pipeline)
{
  return _cogl_pipeline_hash_table_get (&cache->vertex_hash,
                                        key_pipeline);
}

CoglPipelineCacheEntry *
_cogl_pipeline_cache_get_combined_template (CoglPipelineCache *cache,
                                            CoglPipeline *key_pipeline)
{
  return _cogl_pipeline_hash_table_get (&cache->combined_hash,
                                        key_pipeline);
}

CoglPipelineHashTable *
cogl_pipeline_cache_get_fragment_hash (CoglPipelineCache *cache)
{
  return &cache->fragment_hash;
}

CoglPipelineHashTable *
cogl_pipeline_cache_get_combined_hash (CoglPipelineCache *cache)
{
  return &cache->combined_hash;
}
