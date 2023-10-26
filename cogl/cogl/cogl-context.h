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

/* We forward declare the CoglContext type here to avoid some circular
 * dependency issues with the following headers.
 */
typedef struct _CoglContext CoglContext;
typedef struct _CoglTimestampQuery CoglTimestampQuery;

#include "cogl/cogl-display.h"
#include "cogl/cogl-pipeline.h"
#include "cogl/cogl-primitive.h"

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


/* XXX: not guarded by the EXPERIMENTAL_API defines to avoid
 * upsetting glib-mkenums, but this can still be considered implicitly
 * experimental since it's only useable with experimental API... */
/**
 * CoglFeatureID:
 * @COGL_FEATURE_ID_TEXTURE_RG: Support for
 *    %COGL_TEXTURE_COMPONENTS_RG as the internal components of a
 *    texture.
 * @COGL_FEATURE_ID_TEXTURE_RGBA1010102: Support for 10bpc RGBA formats
 * @COGL_FEATURE_ID_TEXTURE_HALF_FLOAT: Support for half float formats
 * @COGL_FEATURE_ID_TEXTURE_NORM16: Support for 16bpc formats
 * @COGL_FEATURE_ID_UNSIGNED_INT_INDICES: Set if
 *     %COGL_INDICES_TYPE_UNSIGNED_INT is supported in
 *     cogl_indices_new().
 * @COGL_FEATURE_ID_MAP_BUFFER_FOR_READ: Whether cogl_buffer_map() is
 *     supported with CoglBufferAccess including read support.
 * @COGL_FEATURE_ID_MAP_BUFFER_FOR_WRITE: Whether cogl_buffer_map() is
 *     supported with CoglBufferAccess including write support.
 * @COGL_FEATURE_ID_BUFFER_AGE: Available if the age of #CoglOnscreen back
 *    buffers are tracked and so cogl_onscreen_get_buffer_age() can be
 *    expected to return age values other than 0.
 * @COGL_FEATURE_ID_BLIT_FRAMEBUFFER: Whether blitting using
 *    cogl_blit_framebuffer() is supported.
 *
 * All the capabilities that can vary between different GPUs supported
 * by Cogl. Applications that depend on any of these features should explicitly
 * check for them using cogl_has_feature() or cogl_has_features().
 */
typedef enum _CoglFeatureID
{
  COGL_FEATURE_ID_UNSIGNED_INT_INDICES,
  COGL_FEATURE_ID_MAP_BUFFER_FOR_READ,
  COGL_FEATURE_ID_MAP_BUFFER_FOR_WRITE,
  COGL_FEATURE_ID_FENCE,
  COGL_FEATURE_ID_TEXTURE_RG,
  COGL_FEATURE_ID_TEXTURE_RGBA1010102,
  COGL_FEATURE_ID_TEXTURE_HALF_FLOAT,
  COGL_FEATURE_ID_TEXTURE_NORM16,
  COGL_FEATURE_ID_BUFFER_AGE,
  COGL_FEATURE_ID_TEXTURE_EGL_IMAGE_EXTERNAL,
  COGL_FEATURE_ID_BLIT_FRAMEBUFFER,
  COGL_FEATURE_ID_TIMESTAMP_QUERY,

  /*< private >*/
  _COGL_N_FEATURE_IDS   /*< skip >*/
} CoglFeatureID;


/**
 * cogl_has_feature:
 * @context: A #CoglContext pointer
 * @feature: A #CoglFeatureID
 *
 * Checks if a given @feature is currently available
 *
 * Cogl does not aim to be a lowest common denominator API, it aims to
 * expose all the interesting features of GPUs to application which
 * means applications have some responsibility to explicitly check
 * that certain features are available before depending on them.
 *
 * Returns: %TRUE if the @feature is currently supported or %FALSE if
 * not.
 */
COGL_EXPORT gboolean
cogl_has_feature (CoglContext *context, CoglFeatureID feature);

/**
 * cogl_has_features:
 * @context: A #CoglContext pointer
 * @...: A 0 terminated list of `CoglFeatureID`s
 *
 * Checks if a list of features are all currently available.
 *
 * This checks all of the listed features using cogl_has_feature() and
 * returns %TRUE if all the features are available or %FALSE
 * otherwise.
 *
 * Return value: %TRUE if all the features are available, %FALSE
 * otherwise.
 */
COGL_EXPORT gboolean
cogl_has_features (CoglContext *context, ...);

/**
 * CoglFeatureCallback:
 * @feature: A single feature currently supported by Cogl
 * @user_data: A private pointer passed to cogl_foreach_feature().
 *
 * A callback used with cogl_foreach_feature() for enumerating all
 * context level features supported by Cogl.
 */
typedef void (*CoglFeatureCallback) (CoglFeatureID feature, void *user_data);

/**
 * cogl_foreach_feature:
 * @context: A #CoglContext pointer
 * @callback: (scope call): A #CoglFeatureCallback called for each
 *            supported feature
 * @user_data: (closure): Private data to pass to the callback
 *
 * Iterates through all the context level features currently supported
 * for a given @context and for each feature @callback is called.
 */
COGL_EXPORT void
cogl_foreach_feature (CoglContext *context,
                      CoglFeatureCallback callback,
                      void *user_data);

/**
 * CoglGraphicsResetStatus:
 * @COGL_GRAPHICS_RESET_STATUS_NO_ERROR:
 * @COGL_GRAPHICS_RESET_STATUS_GUILTY_CONTEXT_RESET:
 * @COGL_GRAPHICS_RESET_STATUS_INNOCENT_CONTEXT_RESET:
 * @COGL_GRAPHICS_RESET_STATUS_UNKNOWN_CONTEXT_RESET:
 * @COGL_GRAPHICS_RESET_STATUS_PURGED_CONTEXT_RESET:
 *
 * All the error values that might be returned by
 * cogl_get_graphics_reset_status(). Each value's meaning corresponds
 * to the similarly named value defined in the ARB_robustness and
 * NV_robustness_video_memory_purge extensions.
 */
typedef enum _CoglGraphicsResetStatus
{
  COGL_GRAPHICS_RESET_STATUS_NO_ERROR,
  COGL_GRAPHICS_RESET_STATUS_GUILTY_CONTEXT_RESET,
  COGL_GRAPHICS_RESET_STATUS_INNOCENT_CONTEXT_RESET,
  COGL_GRAPHICS_RESET_STATUS_UNKNOWN_CONTEXT_RESET,
  COGL_GRAPHICS_RESET_STATUS_PURGED_CONTEXT_RESET,
} CoglGraphicsResetStatus;

/**
 * cogl_get_graphics_reset_status:
 * @context: a #CoglContext pointer
 *
 * Returns the graphics reset status as reported by
 * GetGraphicsResetStatusARB defined in the ARB_robustness extension.
 *
 * Note that Cogl doesn't normally enable the ARB_robustness
 * extension in which case this will only ever return
 * #COGL_GRAPHICS_RESET_STATUS_NO_ERROR.
 *
 * Applications must explicitly use a backend specific method to
 * request that errors get reported such as X11's
 * cogl_xlib_renderer_request_reset_on_video_memory_purge().
 *
 * Return value: a #CoglGraphicsResetStatus
 */
COGL_EXPORT CoglGraphicsResetStatus
cogl_get_graphics_reset_status (CoglContext *context);

/**
 * cogl_context_is_hardware_accelerated:
 * @context: a #CoglContext pointer
 *
 * Returns: %TRUE if the @context is hardware accelerated, or %FALSE if
 * not.
 */
COGL_EXPORT gboolean
cogl_context_is_hardware_accelerated (CoglContext *context);

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
 * cogl_context_free_timestamp_query:
 * @context: a #CoglContext pointer
 * @query: (transfer full): a #CoglTimestampQuery
 */
COGL_EXPORT void
cogl_context_free_timestamp_query (CoglContext        *context,
                                   CoglTimestampQuery *query);

COGL_EXPORT int64_t
cogl_context_timestamp_query_get_time_ns (CoglContext        *context,
                                          CoglTimestampQuery *query);

/**
 * cogl_context_get_gpu_time_ns:
 * @context: a #CoglContext pointer
 *
 * This function should only be called if the COGL_FEATURE_ID_TIMESTAMP_QUERY
 * feature is advertised.
 *
 * Return value: Current GPU time in nanoseconds
 */
COGL_EXPORT int64_t
cogl_context_get_gpu_time_ns (CoglContext *context);

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

G_END_DECLS
