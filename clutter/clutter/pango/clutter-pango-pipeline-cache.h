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

#pragma once

#include <glib.h>

#include "cogl/cogl.h"

G_BEGIN_DECLS

typedef struct
{
  CoglContext *ctx;

  GHashTable *hash_table;

  CoglPipeline *base_texture_alpha_pipeline;
  CoglPipeline *base_texture_rgba_pipeline;
} ClutterPangoPipelineCache;


ClutterPangoPipelineCache * clutter_pango_pipeline_cache_new (CoglContext *ctx);

/* Returns a pipeline that can be used to render glyphs in the given
   texture. The pipeline has a new reference so it is up to the caller
   to unref it */
CoglPipeline * clutter_pango_pipeline_cache_get (ClutterPangoPipelineCache *cache,
                                                 CoglTexture               *texture);

void clutter_pango_pipeline_cache_free (ClutterPangoPipelineCache *cache);

G_END_DECLS
