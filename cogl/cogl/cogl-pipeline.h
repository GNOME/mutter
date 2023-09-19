/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009 Intel Corporation.
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

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

/* We forward declare the CoglPipeline type here to avoid some circular
 * dependency issues with the following headers.
 */
typedef struct _CoglPipeline CoglPipeline;

#include "cogl/cogl-types.h"
#include "cogl/cogl-context.h"
#include "cogl/cogl-snippet.h"

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * CoglPipeline:
 *
 * Functions for creating and manipulating the GPU pipeline
 *
 * Cogl allows creating and manipulating objects representing the full
 * configuration of the GPU pipeline. In simplified terms the GPU
 * pipeline takes primitive geometry as the input, it first performs
 * vertex processing, allowing you to deform your geometry, then
 * rasterizes that (turning it from pure geometry into fragments) then
 * performs fragment processing including depth testing and texture
 * mapping. Finally it blends the result with the framebuffer.
 */
#define COGL_TYPE_PIPELINE            (cogl_pipeline_get_type ())
#define COGL_PIPELINE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COGL_TYPE_PIPELINE, CoglPipeline))
#define COGL_PIPELINE_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), COGL_TYPE_PIPELINE, CoglPipeline const))
#define COGL_PIPELINE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COGL_TYPE_PIPELINE, CoglPipelineClass))
#define COGL_IS_PIPELINE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COGL_TYPE_PIPELINE))
#define COGL_IS_PIPELINE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COGL_TYPE_PIPELINE))
#define COGL_PIPELINE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COGL_TYPE_PIPELINE, CoglPipelineClass))

typedef struct _CoglPipelineClass CoglPipelineClass;
typedef struct _CoglPipeline CoglPipeline;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglPipeline, g_object_unref)

COGL_EXPORT
GType               cogl_pipeline_get_type       (void) G_GNUC_CONST;

/**
 * cogl_pipeline_new: (constructor)
 * @context: a #CoglContext
 *
 * Allocates and initializes a default simple pipeline that will color
 * a primitive white.
 *
 * Return value: (transfer full): a pointer to a new #CoglPipeline
 */
COGL_EXPORT CoglPipeline *
cogl_pipeline_new (CoglContext *context);

/**
 * cogl_pipeline_copy:
 * @source: a #CoglPipeline object to copy
 *
 * Creates a new pipeline with the configuration copied from the
 * source pipeline.
 *
 * We would strongly advise developers to always aim to use
 * cogl_pipeline_copy() instead of cogl_pipeline_new() whenever there will
 * be any similarity between two pipelines. Copying a pipeline helps Cogl
 * keep track of a pipelines ancestry which we may use to help minimize GPU
 * state changes.
 *
 * Return value: (transfer full): a pointer to the newly allocated #CoglPipeline
 */
COGL_EXPORT CoglPipeline *
cogl_pipeline_copy (CoglPipeline *source);

/**
 * CoglPipelineLayerCallback:
 * @pipeline: The #CoglPipeline whose layers are being iterated
 * @layer_index: The current layer index
 * @user_data: The private data passed to cogl_pipeline_foreach_layer()
 *
 * The callback prototype used with cogl_pipeline_foreach_layer() for
 * iterating all the layers of a @pipeline.
 */
typedef gboolean (*CoglPipelineLayerCallback) (CoglPipeline *pipeline,
                                               int layer_index,
                                               void *user_data);

/**
 * cogl_pipeline_foreach_layer:
 * @pipeline: A #CoglPipeline object
 * @callback: (scope call): A #CoglPipelineLayerCallback to be
 *            called for each layer index
 * @user_data: (closure): Private data that will be passed to the
 *             callback
 *
 * Iterates all the layer indices of the given @pipeline.
 */
COGL_EXPORT void
cogl_pipeline_foreach_layer (CoglPipeline *pipeline,
                             CoglPipelineLayerCallback callback,
                             void *user_data);

/**
 * cogl_pipeline_get_uniform_location:
 * @pipeline: A #CoglPipeline object
 * @uniform_name: The name of a uniform
 *
 * This is used to get an integer representing the uniform with the
 * name @uniform_name. The integer can be passed to functions such as
 * cogl_pipeline_set_uniform_1f() to set the value of a uniform.
 *
 * This function will always return a valid integer. Ie, unlike
 * OpenGL, it does not return -1 if the uniform is not available in
 * this pipeline so it can not be used to test whether uniforms are
 * present. It is not necessary to set the program on the pipeline
 * before calling this function.
 *
 * Return value: A integer representing the location of the given uniform.
 */
COGL_EXPORT int
cogl_pipeline_get_uniform_location (CoglPipeline *pipeline,
                                    const char *uniform_name);

G_END_DECLS
