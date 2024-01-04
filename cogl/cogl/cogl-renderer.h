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
#include "cogl/cogl-onscreen-template.h"
#include "cogl/cogl-output.h"
#include "cogl/cogl-pixel-format.h"

#include <glib-object.h>

G_BEGIN_DECLS

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

typedef struct _CoglRenderer CoglRenderer;

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
 * While the renderer is unconnected it can be configured so that
 * applications may specify backend constraints, such as "must use
 * x11" for example via cogl_renderer_add_constraint().
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
 * cogl_renderer_set_winsys_id:
 * @renderer: A #CoglRenderer
 * @winsys_id: An ID of the winsys you explicitly want to use.
 *
 * This allows you to explicitly select a winsys backend to use instead
 * of letting Cogl automatically select a backend.
 *
 * if you select an unsupported backend then cogl_renderer_connect()
 * will fail and report an error.
 *
 * This may only be called on an un-connected #CoglRenderer.
 */
COGL_EXPORT void
cogl_renderer_set_winsys_id (CoglRenderer *renderer,
                             CoglWinsysID winsys_id);

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

/**
 * cogl_renderer_check_onscreen_template: (skip)
 * @renderer: A #CoglRenderer
 * @onscreen_template: A #CoglOnscreenTemplate
 * @error: A pointer to a #GError for reporting exceptions
 *
 * Tests if a given @onscreen_template can be supported with the given
 * @renderer.
 *
 * Return value: %TRUE if the @onscreen_template can be supported,
 *               else %FALSE.
 */
COGL_EXPORT gboolean
cogl_renderer_check_onscreen_template (CoglRenderer *renderer,
                                       CoglOnscreenTemplate *onscreen_template,
                                       GError **error);

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
 * CoglRendererConstraint:
 * @COGL_RENDERER_CONSTRAINT_USES_X11: Require the renderer to be X11 based
 * @COGL_RENDERER_CONSTRAINT_USES_XLIB: Require the renderer to be X11
 *                                      based and use Xlib
 * @COGL_RENDERER_CONSTRAINT_USES_EGL: Require the renderer to be EGL based
 *
 * These constraint flags are hard-coded features of the different renderer
 * backends. Sometimes a platform may support multiple rendering options which
 * Cogl will usually choose from automatically. Some of these features are
 * important to higher level applications and frameworks though, such as
 * whether a renderer is X11 based because an application might only support
 * X11 based input handling. An application might also need to ensure EGL is
 * used internally too if they depend on access to an EGLDisplay for some
 * purpose.
 *
 * Applications should ideally minimize how many of these constraints
 * they depend on to ensure maximum portability.
 */
typedef enum
{
  COGL_RENDERER_CONSTRAINT_USES_X11 = (1 << 0),
  COGL_RENDERER_CONSTRAINT_USES_XLIB = (1 << 1),
  COGL_RENDERER_CONSTRAINT_USES_EGL = (1 << 2),
} CoglRendererConstraint;


/**
 * cogl_renderer_add_constraint:
 * @renderer: An unconnected #CoglRenderer
 * @constraint: A #CoglRendererConstraint to add
 *
 * This adds a renderer selection @constraint.
 *
 * Applications should ideally minimize how many of these constraints they
 * depend on to ensure maximum portability.
 */
COGL_EXPORT void
cogl_renderer_add_constraint (CoglRenderer *renderer,
                              CoglRendererConstraint constraint);

/**
 * cogl_renderer_remove_constraint:
 * @renderer: An unconnected #CoglRenderer
 * @constraint: A #CoglRendererConstraint to remove
 *
 * This removes a renderer selection @constraint.
 *
 * Applications should ideally minimize how many of these constraints they
 * depend on to ensure maximum portability.
 */
COGL_EXPORT void
cogl_renderer_remove_constraint (CoglRenderer *renderer,
                                 CoglRendererConstraint constraint);

/**
 * CoglDriver:
 * @COGL_DRIVER_ANY: Implies no preference for which driver is used
 * @COGL_DRIVER_NOP: A No-Op driver.
 * @COGL_DRIVER_GL3: An OpenGL driver using the core GL 3.1 profile
 * @COGL_DRIVER_GLES2: An OpenGL ES 2.0 driver.
 *
 * Identifiers for underlying hardware drivers that may be used by
 * Cogl for rendering.
 */
typedef enum
{
  COGL_DRIVER_ANY,
  COGL_DRIVER_NOP,
  COGL_DRIVER_GL3,
  COGL_DRIVER_GLES2,
} CoglDriver;

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
                          CoglDriver driver);

/**
 * cogl_renderer_get_driver:
 * @renderer: A connected #CoglRenderer
 *
 * Queries what underlying driver is being used by Cogl.
 *
 * This may only be called on a connected #CoglRenderer.
 */
COGL_EXPORT CoglDriver
cogl_renderer_get_driver (CoglRenderer *renderer);

/**
 * CoglOutputCallback:
 * @output: The current display output being iterated
 * @user_data: The user pointer passed to
 *             cogl_renderer_foreach_output()
 *
 * A callback type that can be passed to
 * cogl_renderer_foreach_output() for iterating display outputs for a
 * given renderer.
 */
typedef void (*CoglOutputCallback) (CoglOutput *output, void *user_data);

/**
 * cogl_renderer_foreach_output:
 * @renderer: A connected #CoglRenderer
 * @callback: (scope call): A #CoglOutputCallback to be called for
 *            each display output
 * @user_data: A user pointer to be passed to @callback
 *
 * Iterates all known display outputs for the given @renderer and
 * passes a corresponding #CoglOutput pointer to the given @callback
 * for each one, along with the given @user_data.
 */
COGL_EXPORT void
cogl_renderer_foreach_output (CoglRenderer *renderer,
                              CoglOutputCallback callback,
                              void *user_data);

/**
 * cogl_renderer_create_dma_buf: (skip)
 * @renderer: A #CoglRenderer
 * @format: A #CoglPixelFormat
 * @width: width of the new
 * @height: height of the new
 * @error: (nullable): return location for a #GError
 *
 * Creates a new #CoglFramebuffer with @width x @height, with pixel
 * format @format, and exports the new framebuffer's DMA buffer
 * handle.
 *
 * Returns: (nullable)(transfer full): a #CoglDmaBufHandle. The
 * return result must be released with cogl_dma_buf_handle_free()
 * after use.
 */
COGL_EXPORT CoglDmaBufHandle *
cogl_renderer_create_dma_buf (CoglRenderer     *renderer,
                              CoglPixelFormat   format,
                              int               width,
                              int               height,
                              GError          **error);


/**
 * cogl_renderer_is_dma_buf_supported: (skip)
 * @renderer: A #CoglRenderer
 *
 * Returns: %TRUE if DMA buffers can be allocated
 */
COGL_EXPORT gboolean
cogl_renderer_is_dma_buf_supported (CoglRenderer *renderer);

/**
 * cogl_renderer_bind_api: (skip)
 */
COGL_EXPORT void
cogl_renderer_bind_api (CoglRenderer *renderer);

G_END_DECLS
