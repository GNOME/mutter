/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2008,2009 Intel Corporation.
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


#include <stdint.h>
#include <stddef.h>

#include "cogl/cogl-macros.h"
#include <graphene.h>

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

/* Some structures are meant to be opaque but they have public
   definitions because we want the size to be public so they can be
   allocated on the stack. This macro is used to ensure that users
   don't accidentally access private members */
#ifdef COGL_COMPILATION
#define COGL_PRIVATE(x) x
#else
#define COGL_PRIVATE(x) private_member_ ## x
#endif

typedef struct _CoglColor CoglColor;
typedef struct _CoglFramebuffer CoglFramebuffer;

/**
 * CoglDmaBufHandle: (free-func cogl_dma_buf_handle_free)
 *
 * An opaque type that tracks the lifetime of a DMA buffer fd. Release
 * with cogl_dma_buf_handle_free().
 */
typedef struct _CoglDmaBufHandle CoglDmaBufHandle;

#define COGL_SYSTEM_ERROR (_cogl_system_error_quark ())

/**
 * CoglSystemError:
 * @COGL_SYSTEM_ERROR_UNSUPPORTED: You tried to use a feature or
 *    configuration not currently available.
 * @COGL_SYSTEM_ERROR_NO_MEMORY: You tried to allocate a resource
 *    such as a texture and there wasn't enough memory.
 *
 * Error enumeration for Cogl
 *
 * The @COGL_SYSTEM_ERROR_UNSUPPORTED error can be thrown for a
 * variety of reasons. For example:
 *
 * - You've tried to use a feature that is not advertised by
 *   [method@Cogl.Context.has_feature].
 * - The GPU can not handle the configuration you have requested.
 *   An example might be if you try to use too many texture
 *   layers in a single #CoglPipeline
 * - The driver does not support some configuration.
 *
 * Currently this is only used by Cogl API marked as experimental so
 * this enum should also be considered experimental.
 */
typedef enum /*< prefix=COGL_ERROR >*/
{
  COGL_SYSTEM_ERROR_UNSUPPORTED,
  COGL_SYSTEM_ERROR_NO_MEMORY
} CoglSystemError;

COGL_EXPORT uint32_t
_cogl_system_error_quark (void);

/**
 * CoglAttributeType:
 * @COGL_ATTRIBUTE_TYPE_BYTE: Data is the same size of a byte
 * @COGL_ATTRIBUTE_TYPE_UNSIGNED_BYTE: Data is the same size of an
 *   unsigned byte
 * @COGL_ATTRIBUTE_TYPE_SHORT: Data is the same size of a short integer
 * @COGL_ATTRIBUTE_TYPE_UNSIGNED_SHORT: Data is the same size of
 *   an unsigned short integer
 * @COGL_ATTRIBUTE_TYPE_FLOAT: Data is the same size of a float
 *
 * Data types for the components of a vertex attribute.
 */
typedef enum
{
  COGL_ATTRIBUTE_TYPE_BYTE           = 0x1400,
  COGL_ATTRIBUTE_TYPE_UNSIGNED_BYTE  = 0x1401,
  COGL_ATTRIBUTE_TYPE_SHORT          = 0x1402,
  COGL_ATTRIBUTE_TYPE_UNSIGNED_SHORT = 0x1403,
  COGL_ATTRIBUTE_TYPE_FLOAT          = 0x1406
} CoglAttributeType;

/**
 * CoglIndicesType:
 * @COGL_INDICES_TYPE_UNSIGNED_BYTE: Your indices are unsigned bytes
 * @COGL_INDICES_TYPE_UNSIGNED_SHORT: Your indices are unsigned shorts
 * @COGL_INDICES_TYPE_UNSIGNED_INT: Your indices are unsigned ints
 *
 * You should aim to use the smallest data type that gives you enough
 * range, since it reduces the size of your index array and can help
 * reduce the demand on memory bandwidth.
 *
 * Note that %COGL_INDICES_TYPE_UNSIGNED_INT is only supported if the
 * %COGL_FEATURE_ID_UNSIGNED_INT_INDICES feature is available. This
 * should always be available on OpenGL but on OpenGL ES it will only
 * be available if the GL_OES_element_index_uint extension is
 * advertized.
 */
typedef enum
{
  COGL_INDICES_TYPE_UNSIGNED_BYTE,
  COGL_INDICES_TYPE_UNSIGNED_SHORT,
  COGL_INDICES_TYPE_UNSIGNED_INT
} CoglIndicesType;

/**
 * CoglVerticesMode:
 * @COGL_VERTICES_MODE_POINTS: FIXME, equivalent to `GL_POINTS`
 * @COGL_VERTICES_MODE_LINES: FIXME, equivalent to `GL_LINES`
 * @COGL_VERTICES_MODE_LINE_LOOP: FIXME, equivalent to `GL_LINE_LOOP`
 * @COGL_VERTICES_MODE_LINE_STRIP: FIXME, equivalent to `GL_LINE_STRIP`
 * @COGL_VERTICES_MODE_TRIANGLES: FIXME, equivalent to `GL_TRIANGLES`
 * @COGL_VERTICES_MODE_TRIANGLE_STRIP: FIXME, equivalent to `GL_TRIANGLE_STRIP`
 * @COGL_VERTICES_MODE_TRIANGLE_FAN: FIXME, equivalent to `GL_TRIANGLE_FAN`
 *
 * Different ways of interpreting vertices when drawing.
 */
typedef enum
{
  COGL_VERTICES_MODE_POINTS = 0x0000,
  COGL_VERTICES_MODE_LINES = 0x0001,
  COGL_VERTICES_MODE_LINE_LOOP = 0x0002,
  COGL_VERTICES_MODE_LINE_STRIP = 0x0003,
  COGL_VERTICES_MODE_TRIANGLES = 0x0004,
  COGL_VERTICES_MODE_TRIANGLE_STRIP = 0x0005,
  COGL_VERTICES_MODE_TRIANGLE_FAN = 0x0006
} CoglVerticesMode;

/* NB: The above definitions are taken from gl.h equivalents */


/* XXX: should this be CoglPipelineDepthTestFunction?
 * It makes it very verbose but would be consistent with
 * CoglPipelineWrapMode */

/**
 * CoglDepthTestFunction:
 * @COGL_DEPTH_TEST_FUNCTION_NEVER: Never passes.
 * @COGL_DEPTH_TEST_FUNCTION_LESS: Passes if the fragment's depth
 * value is less than the value currently in the depth buffer.
 * @COGL_DEPTH_TEST_FUNCTION_EQUAL: Passes if the fragment's depth
 * value is equal to the value currently in the depth buffer.
 * @COGL_DEPTH_TEST_FUNCTION_LEQUAL: Passes if the fragment's depth
 * value is less or equal to the value currently in the depth buffer.
 * @COGL_DEPTH_TEST_FUNCTION_GREATER: Passes if the fragment's depth
 * value is greater than the value currently in the depth buffer.
 * @COGL_DEPTH_TEST_FUNCTION_NOTEQUAL: Passes if the fragment's depth
 * value is not equal to the value currently in the depth buffer.
 * @COGL_DEPTH_TEST_FUNCTION_GEQUAL: Passes if the fragment's depth
 * value greater than or equal to the value currently in the depth buffer.
 * @COGL_DEPTH_TEST_FUNCTION_ALWAYS: Always passes.
 *
 * When using depth testing one of these functions is used to compare
 * the depth of an incoming fragment against the depth value currently
 * stored in the depth buffer. The function is changed using
 * cogl_depth_state_set_test_function().
 *
 * The test is only done when depth testing is explicitly enabled. (See
 * cogl_depth_state_set_test_enabled())
 */
typedef enum
{
  COGL_DEPTH_TEST_FUNCTION_NEVER    = 0x0200,
  COGL_DEPTH_TEST_FUNCTION_LESS     = 0x0201,
  COGL_DEPTH_TEST_FUNCTION_EQUAL    = 0x0202,
  COGL_DEPTH_TEST_FUNCTION_LEQUAL   = 0x0203,
  COGL_DEPTH_TEST_FUNCTION_GREATER  = 0x0204,
  COGL_DEPTH_TEST_FUNCTION_NOTEQUAL = 0x0205,
  COGL_DEPTH_TEST_FUNCTION_GEQUAL   = 0x0206,
  COGL_DEPTH_TEST_FUNCTION_ALWAYS   = 0x0207
} CoglDepthTestFunction;
/* NB: The above definitions are taken from gl.h equivalents */

typedef enum /*< prefix=COGL_RENDERER_ERROR >*/
{
  COGL_RENDERER_ERROR_XLIB_DISPLAY_OPEN,
  COGL_RENDERER_ERROR_BAD_CONSTRAINT
} CoglRendererError;

/**
 * CoglFilterReturn:
 * @COGL_FILTER_CONTINUE: The event was not handled, continues the
 *                        processing
 * @COGL_FILTER_REMOVE: Remove the event, stops the processing
 *
 * Return values for the #CoglXlibFilterFunc and #CoglWin32FilterFunc functions.
 */
typedef enum _CoglFilterReturn { /*< prefix=COGL_FILTER >*/
  COGL_FILTER_CONTINUE,
  COGL_FILTER_REMOVE
} CoglFilterReturn;

typedef enum _CoglWinsysFeature
{
  /* Available if its possible to query a counter that
   * increments at each vblank. */
  COGL_WINSYS_FEATURE_VBLANK_COUNTER,

  /* Available if its possible to wait until the next vertical
   * blank period */
  COGL_WINSYS_FEATURE_VBLANK_WAIT,

  /* Available if the window system supports mapping native
   * pixmaps to textures. */
  COGL_WINSYS_FEATURE_TEXTURE_FROM_PIXMAP,

  /* Available if the window system supports reporting an event
   * for swap buffer completions. */
  COGL_WINSYS_FEATURE_SWAP_BUFFERS_EVENT,

  /* Available if it's possible to swap a list of sub rectangles
   * from the back buffer to the front buffer */
  COGL_WINSYS_FEATURE_SWAP_REGION,

  /* Available if swap_region requests can be automatically throttled
   * to the vblank frequency. */
  COGL_WINSYS_FEATURE_SWAP_REGION_THROTTLE,

  /* Available if the swap region implementation won't tear and thus
   * only needs to be throttled to the framerate */
  COGL_WINSYS_FEATURE_SWAP_REGION_SYNCHRONIZED,

  /* Available if the age of the back buffer can be queried */
  COGL_WINSYS_FEATURE_BUFFER_AGE,

  /* Available if the winsys directly handles _SYNC and _COMPLETE events */
  COGL_WINSYS_FEATURE_SYNC_AND_COMPLETE_EVENT,

  COGL_WINSYS_FEATURE_N_FEATURES
} CoglWinsysFeature;

/**
 * CoglWinding:
 * @COGL_WINDING_CLOCKWISE: Vertices are in a clockwise order
 * @COGL_WINDING_COUNTER_CLOCKWISE: Vertices are in a counter-clockwise order
 *
 * Enum used to represent the two directions of rotation. This can be
 * used to set the front face for culling by calling
 * cogl_pipeline_set_front_face_winding().
 */
typedef enum
{
  COGL_WINDING_CLOCKWISE,
  COGL_WINDING_COUNTER_CLOCKWISE
} CoglWinding;

/**
 * CoglBufferBit:
 * @COGL_BUFFER_BIT_COLOR: Selects the primary color buffer
 * @COGL_BUFFER_BIT_DEPTH: Selects the depth buffer
 * @COGL_BUFFER_BIT_STENCIL: Selects the stencil buffer
 *
 * Types of auxiliary buffers
 */
typedef enum
{
  COGL_BUFFER_BIT_COLOR   = 1L<<0,
  COGL_BUFFER_BIT_DEPTH   = 1L<<1,
  COGL_BUFFER_BIT_STENCIL = 1L<<2
} CoglBufferBit;

/**
 * CoglReadPixelsFlags:
 * @COGL_READ_PIXELS_COLOR_BUFFER: Read from the color buffer
 *
 * Flags for cogl_framebuffer_read_pixels_into_bitmap()
 */
typedef enum /*< prefix=COGL_READ_PIXELS >*/
{
  COGL_READ_PIXELS_COLOR_BUFFER = 1L << 0
} CoglReadPixelsFlags;

/**
 * CoglShaderType:
 * @COGL_SHADER_TYPE_VERTEX: A program for processing vertices
 * @COGL_SHADER_TYPE_FRAGMENT: A program for processing fragments
 *
 * Types of shaders
 */
typedef enum
{
  COGL_SHADER_TYPE_VERTEX,
  COGL_SHADER_TYPE_FRAGMENT
} CoglShaderType;

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

typedef struct _CoglAttribute CoglAttribute;
typedef struct _CoglAttributeBuffer CoglAttributeBuffer;
typedef struct _CoglAtlas CoglAtlas;
typedef struct _CoglAtlasTexture CoglAtlasTexture;
typedef struct _CoglBitmap CoglBitmap;
typedef struct _CoglBuffer CoglBuffer;
typedef struct _CoglContext CoglContext;
typedef struct _CoglDisplay CoglDisplay;
typedef struct _CoglFrameInfo CoglFrameInfo;
typedef struct _CoglIndices CoglIndices;
typedef struct _CoglParamSpecColor CoglParamSpecColor;
typedef struct _CoglPipeline CoglPipeline;
typedef struct _CoglPixelBuffer CoglPixelBuffer;
typedef struct _CoglPrimitive CoglPrimitive;
typedef struct _CoglRenderer CoglRenderer;
typedef struct _CoglScanout CoglScanout;
typedef struct _CoglScanoutBuffer CoglScanoutBuffer;
typedef struct _CoglSnippet CoglSnippet;
typedef struct _CoglSubTexture CoglSubTexture;
typedef struct _CoglTexture CoglTexture;
typedef struct _CoglTexture2D CoglTexture2D;
typedef struct _CoglTexture2DSliced CoglTexture2DSliced;
typedef struct _CoglTimestampQuery CoglTimestampQuery;

#define COGL_SCANOUT_ERROR (cogl_scanout_error_quark ())

/**
 * CoglScanoutError:
 * @COGL_SCANOUT_ERROR_INHIBITED: Scanout inhibited
 */
typedef enum _CoglScanoutError
{
  COGL_SCANOUT_ERROR_INHIBITED,
} CoglScanoutError;

COGL_EXPORT GQuark
cogl_scanout_error_quark (void);

G_END_DECLS
