/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2010 Intel Corporation.
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
 * Authors:
 *  Robert Bragg <robert@linux.intel.com>
 *
 */

#pragma once

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#include "cogl/cogl-display.h"
#include "cogl/cogl-driver.h"
#include "cogl/cogl-pipeline.h"
#include "cogl/cogl-primitive.h"

#ifdef HAVE_EGL
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglmesaext.h>
#endif

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * CoglContext:
 *
 * The top level application context.
 *
 * A #CoglContext is the top most sandbox of Cogl state for an
 * application or toolkit. Its main purpose is to act as a sandbox
 * for the memory management of state objects. Normally an application
 * will only create a single context since there is no way to share
 * resources between contexts.
 *
 * For those familiar with OpenGL or perhaps Cairo it should be
 * understood that unlike these APIs a Cogl context isn't a rendering
 * context as such. In other words Cogl doesn't aim to provide a state
 * machine style model for configuring rendering parameters. Most
 * rendering state in Cogl is directly associated with user managed
 * objects called pipelines and geometry is drawn with a specific
 * pipeline object to a framebuffer object and those 3 things fully
 * define the state for drawing. This is an important part of Cogl's
 * design since it helps you write orthogonal rendering components
 * that can all access the same GPU without having to worry about
 * what state other components have left you with.
 *
 * Cogl does not maintain internal references to the context for
 * resources that depend on the context so applications. This is to
 * help applications control the lifetime a context without us needing to
 * introduce special api to handle the breakup of internal circular
 * references due to internal resources and caches associated with the
 * context.
 *
 * One a context has been destroyed then all directly or indirectly
 * dependent resources will be in an inconsistent state and should not
 * be manipulated or queried in any way.
 *
 * For applications that rely on the operating system to clean up
 * resources this policy shouldn't affect them, but for applications
 * that need to carefully destroy and re-create Cogl contexts multiple
 * times throughout their lifetime (such as Android applications) they
 * should be careful to destroy all context dependent resources, such as
 * framebuffers or textures etc before unrefing and destroying the
 * context.
 */

#define COGL_TYPE_CONTEXT (cogl_context_get_type ())

COGL_EXPORT
G_DECLARE_FINAL_TYPE (CoglContext,
                      cogl_context,
                      COGL,
                      CONTEXT,
                      GObject)

/**
 * cogl_context_new: (constructor)
 * @display: (allow-none): A #CoglDisplay pointer
 * @error: A GError return location.
 *
 * Creates a new #CoglContext which acts as an application sandbox
 * for any state objects that are allocated.
 *
 * Return value: (transfer full): A newly allocated #CoglContext
 */
COGL_EXPORT CoglContext *
cogl_context_new (CoglDisplay *display,
                  GError **error);

/**
 * cogl_context_get_display:
 * @context: A #CoglContext pointer
 *
 * Retrieves the #CoglDisplay that is internally associated with the
 * given @context. This will return the same #CoglDisplay that was
 * passed to cogl_context_new() or if %NULL was passed to
 * cogl_context_new() then this function returns a pointer to the
 * display that was automatically setup internally.
 *
 * Return value: (transfer none): The #CoglDisplay associated with the
 *               given @context.
 */
COGL_EXPORT CoglDisplay *
cogl_context_get_display (CoglContext *context);

/**
 * cogl_context_get_renderer:
 * @context: A #CoglContext pointer
 *
 * Retrieves the #CoglRenderer that is internally associated with the
 * given @context. This will return the same #CoglRenderer that was
 * passed to cogl_display_new() or if %NULL was passed to
 * cogl_display_new() or cogl_context_new() then this function returns
 * a pointer to the renderer that was automatically connected
 * internally.
 *
 * Return value: (transfer none): The #CoglRenderer associated with the
 *               given @context.
 */
COGL_EXPORT CoglRenderer *
cogl_context_get_renderer (CoglContext *context);

typedef const char * const CoglPipelineKey;

/**
 * cogl_context_set_named_pipeline:
 * @context: a #CoglContext pointer
 * @key: a #CoglPipelineKey pointer
 * @pipeline: (nullable): a #CoglPipeline to associate with the @context and
 *            @key
 *
 * Associate a #CoglPipeline with a @context and @key. This will not take a new
 * reference to the @pipeline, but will unref all associated pipelines when
 * the @context gets destroyed. Similarly, if a pipeline gets overwritten,
 * it will get unreffed as well.
 */
COGL_EXPORT void
cogl_context_set_named_pipeline (CoglContext     *context,
                                 CoglPipelineKey *key,
                                 CoglPipeline    *pipeline);

/**
 * cogl_context_get_named_pipeline:
 * @context: a #CoglContext pointer
 * @key: a #CoglPipelineKey pointer
 *
 * Return value: (transfer none): The #CoglPipeline associated with the
 *               given @context and @key, or %NULL if no such #CoglPipeline
 *               was found.
 */
COGL_EXPORT CoglPipeline *
cogl_context_get_named_pipeline (CoglContext     *context,
                                 CoglPipelineKey *key);

/**
 * cogl_context_get_latest_sync_fd
 * @context: a #CoglContext pointer
 *
 * This function is used to get support for waiting on previous
 * GPU work through sync fds. It will return a sync fd which will
 * signal when the previous work has completed.
 *
 * Return value: sync fd for latest GPU submission if available,
 * returns -1 if not.
 */
COGL_EXPORT int
cogl_context_get_latest_sync_fd (CoglContext *context);

COGL_EXPORT gboolean
cogl_context_has_winsys_feature (CoglContext       *context,
                                 CoglWinsysFeature  feature);
/**
 * cogl_context_flush:
 * @context: A #CoglContext
 *
 * This function should only need to be called in exceptional circumstances.
 *
 * As an optimization Cogl drawing functions may batch up primitives
 * internally, so if you are trying to use raw GL outside of Cogl you stand a
 * better chance of being successful if you ask Cogl to flush any batched
 * geometry before making your state changes.
 *
 * It only ensure that the underlying driver is issued all the commands
 * necessary to draw the batched primitives. It provides no guarantees about
 * when the driver will complete the rendering.
 *
 * This provides no guarantees about the GL state upon returning and to avoid
 * confusing Cogl you should aim to restore any changes you make before
 * resuming use of Cogl.
 *
 * If you are making state changes with the intention of affecting Cogl drawing
 * primitives you are 100% on your own since you stand a good chance of
 * conflicting with Cogl internals. For example clutter-gst which currently
 * uses direct GL calls to bind ARBfp programs will very likely break when Cogl
 * starts to use ARBfb programs itself for the pipeline API.
 */
COGL_EXPORT void
cogl_context_flush (CoglContext *context);

/**
 * cogl_context_get_rectangle_indices:
 *
 * Returns: (transfer none): a #CoglIndices
 */
COGL_EXPORT CoglIndices *
cogl_context_get_rectangle_indices (CoglContext *context,
                                    int          n_rectangles);

#ifdef HAVE_EGL
/**
 * cogl_context_get_egl_display:
 * @context: A #CoglContext pointer
 *
 * If you have done a runtime check to determine that Cogl is using
 * EGL internally then this API can be used to retrieve the EGLDisplay
 * handle that was setup internally. The result is undefined if Cogl
 * is not using EGL.
 *
 *
 * Return value: The internally setup EGLDisplay handle.
 */
COGL_EXPORT EGLDisplay
cogl_context_get_egl_display (CoglContext *context);
#endif /* HAVE_EGL */

/**
 * cogl_context_get_driver:
 * @context: A #CoglContext
 *
 * Returns: (transfer none): the associated #CoglDriver
 */
COGL_EXPORT
CoglDriver * cogl_context_get_driver (CoglContext *context);

COGL_EXPORT_TEST
gboolean cogl_context_get_gl_blend_enable_cache (CoglContext *context);


COGL_EXPORT
void cogl_context_set_winsys_feature (CoglContext      *context,
                                      CoglWinsysFeature feature,
                                      gboolean          value);


G_END_DECLS
