/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2012 Intel Corporation.
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

#if !defined(HAVE_GL) && !defined(HAVE_GLES2)
#error "config.h must be included before this header"
#endif

#include "cogl/cogl-context.h"
#include "cogl/cogl-pipeline-layer-state.h"

/* XXX: keep the values in sync with the CoglPipelineWrapMode enum
 * so no conversion is actually needed.
 */
typedef enum _CoglSamplerCacheWrapMode
{
  COGL_SAMPLER_CACHE_WRAP_MODE_REPEAT,
  COGL_SAMPLER_CACHE_WRAP_MODE_MIRRORED_REPEAT,
  COGL_SAMPLER_CACHE_WRAP_MODE_CLAMP_TO_EDGE,
  COGL_SAMPLER_CACHE_WRAP_MODE_AUTOMATIC,
  COGL_SAMPLER_CACHE_WRAP_MODE_CLAMP_TO_BORDER
} CoglSamplerCacheWrapMode;

typedef struct _CoglSamplerCache CoglSamplerCache;

typedef struct _CoglSamplerCacheEntry
{
  unsigned int sampler_object;

  CoglPipelineFilter min_filter;
  CoglPipelineFilter mag_filter;

  CoglSamplerCacheWrapMode wrap_mode_s;
  CoglSamplerCacheWrapMode wrap_mode_t;
} CoglSamplerCacheEntry;

CoglSamplerCache *
_cogl_sampler_cache_new (CoglContext *context);

const CoglSamplerCacheEntry *
_cogl_sampler_cache_get_default_entry (CoglSamplerCache *cache);

const CoglSamplerCacheEntry *
_cogl_sampler_cache_update_wrap_modes (CoglSamplerCache *cache,
                                       const CoglSamplerCacheEntry *old_entry,
                                       CoglSamplerCacheWrapMode wrap_mode_s,
                                       CoglSamplerCacheWrapMode wrap_mode_t);

const CoglSamplerCacheEntry *
_cogl_sampler_cache_update_filters (CoglSamplerCache            *cache,
                                    const CoglSamplerCacheEntry *old_entry,
                                    CoglPipelineFilter           min_filter,
                                    CoglPipelineFilter           mag_filter);

void
_cogl_sampler_cache_free (CoglSamplerCache *cache);
