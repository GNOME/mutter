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
 */

#pragma once

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#include "cogl/cogl-types.h"
#include "cogl/cogl-pixel-format.h"

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum _CoglDrmModifierFilter
{
  COGL_DRM_MODIFIER_FILTER_NONE = 0,
  COGL_DRM_MODIFIER_FILTER_SINGLE_PLANE = 1 << 0,
  COGL_DRM_MODIFIER_FILTER_NOT_EXTERNAL_ONLY = 1 << 1,
} CoglDrmModifierFilter;

/**
 * CoglRenderer:
 *
 * Choosing a means to render
 *
 * A #CoglRenderer represents a means to render. It encapsulates the
 * selection of an underlying driver, such as OpenGL or OpenGL-ES and
 * a selection of a window system binding API such as GLX or EGL.
 *
 * A #CoglRenderer has two states, "unconnected" and "connected". When
 * a renderer is first instantiated using cogl_renderer_new() it is
 * unconnected so that it can be configured and constraints can be
 * specified for how the backend driver and window system should be
 * chosen.
 *
 * After configuration a #CoglRenderer can (optionally) be explicitly
 * connected using cogl_renderer_connect() which allows for the
 * handling of connection errors so that fallback configurations can
 * be tried if necessary. Applications that don't support any
 * fallbacks though can skip using cogl_renderer_connect() and leave
 * Cogl to automatically connect the renderer.
 *
 * Once you have a configured #CoglRenderer it can be used to create a
 * #CoglDisplay object using cogl_display_new().
 */


/**
 * COGL_RENDERER_ERROR:
 *
 * An error domain for exceptions reported by Cogl
 */
#define COGL_RENDERER_ERROR cogl_renderer_error_quark ()

COGL_EXPORT uint32_t
cogl_renderer_error_quark (void);

#define COGL_TYPE_RENDERER (cogl_renderer_get_type ())

COGL_EXPORT
G_DECLARE_FINAL_TYPE (CoglRenderer,
                      cogl_renderer,
                      COGL,
                      RENDERER,
                      GObject)


/**
 * cogl_renderer_new:
 *
 * Instantiates a new (unconnected) #CoglRenderer object. A
 * #CoglRenderer represents a means to render. It encapsulates the
 * selection of an underlying driver, such as OpenGL or OpenGL-ES and
 * a selection of a window system binding API such as GLX or EGL.
 *
 * There are also some platform specific configuration apis such
 * as cogl_xlib_renderer_set_foreign_display() that may also be
 * used while the renderer is unconnected.
 *
 * Once the renderer has been configured, then it may (optionally) be
 * explicitly connected using cogl_renderer_connect() which allows
 * errors to be handled gracefully and potentially fallback
 * configurations can be tried out if there are initial failures.
 *
 * If a renderer is not explicitly connected then cogl_display_new()
 * will automatically connect the renderer for you. If you don't
 * have any code to deal with error/fallback situations then its fine
 * to just let Cogl do the connection for you.
 *
 * Once you have setup your renderer then the next step is to create a
 * #CoglDisplay using cogl_display_new().
 *
 * Return value: (transfer full): A newly created #CoglRenderer.
 */
COGL_EXPORT CoglRenderer *
cogl_renderer_new (void);

/* optional configuration APIs */

/**
 * CoglWinsysID:
 * @COGL_WINSYS_ID_ANY: Implies no preference for which backend is used
 * @COGL_WINSYS_ID_STUB: Use the no-op stub backend
 * @COGL_WINSYS_ID_GLX: Use the GLX window system binding API
 * @COGL_WINSYS_ID_EGL_XLIB: Use EGL with the X window system via XLib
 *
 * Identifies specific window system backends that Cogl supports.
 *
 * These can be used to query what backend Cogl is using or to try and
 * explicitly select a backend to use.
 */
typedef enum
{
  COGL_WINSYS_ID_ANY,
  COGL_WINSYS_ID_STUB,
  COGL_WINSYS_ID_GLX,
  COGL_WINSYS_ID_EGL_XLIB,
  COGL_WINSYS_ID_CUSTOM,
} CoglWinsysID;

/**
 * cogl_renderer_get_winsys_id:
 * @renderer: A #CoglRenderer
 *
 * Queries which window system backend Cogl has chosen to use.
 *
 * This may only be called on a connected #CoglRenderer.
 *
 * Returns: The #CoglWinsysID corresponding to the chosen window
 *          system backend.
 */
COGL_EXPORT CoglWinsysID
cogl_renderer_get_winsys_id (CoglRenderer *renderer);

/* Final connection API */

/**
 * cogl_renderer_connect:
 * @renderer: An unconnected #CoglRenderer
 * @error: a pointer to a #GError for reporting exceptions
 *
 * Connects the configured @renderer. Renderer connection isn't a
 * very active process, it basically just means validating that
 * any given constraint criteria can be satisfied and that a
 * usable driver and window system backend can be found.
 *
 * Return value: %TRUE if there was no error while connecting the
 *               given @renderer. %FALSE if there was an error.
 */
COGL_EXPORT gboolean
cogl_renderer_connect (CoglRenderer *renderer, GError **error);

/**
 * CoglDriverId:
 * @COGL_DRIVER_ID_ANY: Implies no preference for which driver is used
 * @COGL_DRIVER_ID_NOP: A No-Op driver.
 * @COGL_DRIVER_ID_GL3: An OpenGL driver using the core GL 3.1 profile
 * @COGL_DRIVER_ID_GLES2: An OpenGL ES 2.0 driver.
 *
 * Identifiers for underlying hardware drivers that may be used by
 * Cogl for rendering.
 */
typedef enum
{
  COGL_DRIVER_ID_ANY,
  COGL_DRIVER_ID_NOP,
  COGL_DRIVER_ID_GL3,
  COGL_DRIVER_ID_GLES2,
} CoglDriverId;

/**
 * cogl_renderer_set_driver:
 * @renderer: An unconnected #CoglRenderer
 *
 * Requests that Cogl should try to use a specific underlying driver
 * for rendering.
 *
 * If you select an unsupported driver then cogl_renderer_connect()
 * will fail and report an error. Most applications should not
 * explicitly select a driver and should rely on Cogl automatically
 * choosing the driver.
 *
 * This may only be called on an un-connected #CoglRenderer.
 */
COGL_EXPORT void
cogl_renderer_set_driver (CoglRenderer *renderer,
                          CoglDriverId  driver);

/**
 * cogl_renderer_get_driver_id:
 * @renderer: A connected #CoglRenderer
 *
 * Queries what underlying driver is being used by Cogl.
 *
 * This may only be called on a connected #CoglRenderer.
 */
COGL_EXPORT CoglDriverId
cogl_renderer_get_driver_id (CoglRenderer *renderer);


/**
 * cogl_renderer_query_drm_modifiers: (skip)
 * @renderer: A #CoglRenderer
 * @format: The #CoglPixelFormat
 * @error: (nullable): return location for a #GError
 */
COGL_EXPORT GArray *
cogl_renderer_query_drm_modifiers (CoglRenderer           *renderer,
                                   CoglPixelFormat         format,
                                   CoglDrmModifierFilter   filter,
                                   GError                **error);

COGL_EXPORT uint64_t
cogl_renderer_get_implicit_drm_modifier (CoglRenderer *renderer);

COGL_EXPORT gboolean
cogl_renderer_is_implicit_drm_modifier (CoglRenderer *renderer,
                                        uint64_t      modifier);

/**
 * cogl_renderer_create_dma_buf: (skip)
 * @renderer: A #CoglRenderer
 * @format: A #CoglPixelFormat
 * @modifiers: array of DRM format modifiers
 * @n_modifiers: length of modifiers array
 * @width: width of the new
 * @height: height of the new
 * @error: (nullable): return location for a #GError
 *
 * Creates a new #CoglFramebuffer with @width x @height, with pixel
 * format @format, and exports the new framebuffer's DMA buffer
 * handle.
 *
 * Passing an empty modifier array (passing a 0 n_modifiers) means implicit
 * modifiers will be used.
 *
 * Returns: (nullable)(transfer full): a #CoglDmaBufHandle. The
 * return result must be released with cogl_dma_buf_handle_free()
 * after use.
 */
COGL_EXPORT CoglDmaBufHandle *
cogl_renderer_create_dma_buf (CoglRenderer     *renderer,
                              CoglPixelFormat   format,
                              uint64_t         *modifiers,
                              int               n_modifiers,
                              int               width,
                              int               height,
                              GError          **error);

/**
 * cogl_renderer_is_dma_buf_supported:
 * @renderer: A #CoglRenderer
 *
 * Returns: %TRUE if DMA buffers can be allocated
 */
COGL_EXPORT gboolean
cogl_renderer_is_dma_buf_supported (CoglRenderer *renderer);

/**
 * cogl_renderer_bind_api:
 */
COGL_EXPORT void
cogl_renderer_bind_api (CoglRenderer *renderer);

/**
 * cogl_renderer_get_proc_address:
 * @renderer: A #CoglRenderer.
 * @name: the name of the function.
 *
 * Gets a pointer to a given GL or GL ES extension function. This acts
 * as a wrapper around glXGetProcAddress() or whatever is the
 * appropriate function for the current backend.
 *
 * This function should not be used to query core opengl API
 * symbols since eglGetProcAddress for example doesn't allow this and
 * and may return a junk pointer if you do.
 *
 * Return value: a pointer to the requested function or %NULL if the
 *   function is not available.
 */
COGL_EXPORT void *
cogl_renderer_get_proc_address (CoglRenderer *renderer,
                                const char   *name);

/**
 * cogl_renderer_handle_event: (skip)
 * @renderer: a #CoglRenderer
 * @event: pointer to an event structure
 *
 * Processes a single event.
 *
 * Return value: #CoglFilterReturn. %COGL_FILTER_REMOVE indicates that
 * Cogl has internally handled the event and the caller should do no
 * further processing. %COGL_FILTER_CONTINUE indicates that Cogl is
 * either not interested in the event, or has used the event to update
 * internal state without taking any exclusive action.
 */
COGL_EXPORT CoglFilterReturn
cogl_renderer_handle_event (CoglRenderer *renderer,
                            void         *event);

COGL_EXPORT
void cogl_renderer_set_winsys (CoglRenderer *renderer,
                               void         *winsys);

COGL_EXPORT
void * cogl_renderer_get_winsys (CoglRenderer *renderer);

COGL_EXPORT
void * cogl_renderer_get_custom_winsys_data (CoglRenderer *renderer);

G_END_DECLS
