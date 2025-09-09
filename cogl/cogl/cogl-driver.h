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
 */

#pragma once

#include "cogl/cogl-macros.h"

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

/**
 * CoglGraphicsResetStatus:
 * @COGL_GRAPHICS_RESET_STATUS_NO_ERROR:
 * @COGL_GRAPHICS_RESET_STATUS_GUILTY_CONTEXT_RESET:
 * @COGL_GRAPHICS_RESET_STATUS_INNOCENT_CONTEXT_RESET:
 * @COGL_GRAPHICS_RESET_STATUS_UNKNOWN_CONTEXT_RESET:
 * @COGL_GRAPHICS_RESET_STATUS_PURGED_CONTEXT_RESET:
 *
 * All the error values that might be returned by
 * cogl_driver_get_graphics_reset_status(). Each value's meaning corresponds
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

COGL_EXPORT
G_DECLARE_DERIVABLE_TYPE (CoglDriver,
                          cogl_driver,
                          COGL,
                          DRIVER,
                          GObject)

#define COGL_TYPE_DRIVER (cogl_driver_get_type ())

typedef struct _CoglDriverClass CoglDriverClass;

/**
 * cogl_driver_is_hardware_accelerated:
 * @driver: a #CoglDriver
 *
 * Returns: %TRUE if the @driver is hardware accelerated, or %FALSE if
 * not.
 */
COGL_EXPORT
gboolean cogl_driver_is_hardware_accelerated (CoglDriver *driver);

COGL_EXPORT_TEST
const char * cogl_driver_get_vendor (CoglDriver *driver);

/**
 * cogl_driver_get_graphics_reset_status:
 * @driver: a #CoglDriver
 *
 * Returns the graphics reset status as reported by
 * GetGraphicsResetStatusARB defined in the ARB_robustness extension.
 *
 * Note that Cogl doesn't normally enable the ARB_robustness
 * extension in which case this will only ever return
 * #COGL_GRAPHICS_RESET_STATUS_NO_ERROR.
 *
 * Return value: a #CoglGraphicsResetStatus
 */
COGL_EXPORT
CoglGraphicsResetStatus cogl_driver_get_graphics_reset_status (CoglDriver *driver);

COGL_EXPORT
gboolean cogl_driver_format_supports_upload (CoglDriver     *driver,
                                             CoglPixelFormat format);


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
 * @COGL_FEATURE_ID_BLIT_FRAMEBUFFER: Whether blitting using
 *    [method@Cogl.Framebuffer.blit] is supported.
 *
 * All the capabilities that can vary between different GPUs supported
 * by Cogl. Applications that depend on any of these features should explicitly
 * check for them using [method@Cogl.Driver.has_feature].
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
  COGL_FEATURE_ID_TEXTURE_EGL_IMAGE_EXTERNAL,
  COGL_FEATURE_ID_BLIT_FRAMEBUFFER,
  COGL_FEATURE_ID_TIMESTAMP_QUERY,
  COGL_FEATURE_ID_TEXTURE_2D_FROM_EGL_IMAGE,
  COGL_FEATURE_ID_MESA_PACK_INVERT,
  COGL_FEATURE_ID_PBOS,
  COGL_FEATURE_ID_EXT_PACKED_DEPTH_STENCIL,
  COGL_FEATURE_ID_OES_PACKED_DEPTH_STENCIL,
  COGL_FEATURE_ID_TEXTURE_FORMAT_BGRA8888,
  COGL_FEATURE_ID_TEXTURE_FORMAT_SIZED_RGBA,
  COGL_FEATURE_ID_UNPACK_SUBIMAGE,
  COGL_FEATURE_ID_SAMPLER_OBJECTS,
  COGL_FEATURE_ID_READ_PIXELS_ANY_STRIDE,
  COGL_FEATURE_ID_FORMAT_CONVERSION,
  COGL_FEATURE_ID_QUERY_FRAMEBUFFER_BITS,
  COGL_FEATURE_ID_ALPHA_TEXTURES,
  COGL_FEATURE_ID_TEXTURE_SWIZZLE,
  COGL_FEATURE_ID_TEXTURE_MAX_LEVEL,
  COGL_FEATURE_ID_TEXTURE_LOD_BIAS,
  COGL_FEATURE_ID_OES_EGL_SYNC,

  /* This is a Mali bug/quirk: */
  COGL_FEATURE_ID_QUIRK_GENERATE_MIPMAP_NEEDS_FLUSH,
  /*< private >*/
  _COGL_N_FEATURE_IDS   /*< skip >*/
} CoglFeatureID;


/**
 * cogl_driver_has_feature:
 * @driver: A #CoglDriver
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
COGL_EXPORT
gboolean cogl_driver_has_feature (CoglDriver    *driver,
                                  CoglFeatureID  feature);

COGL_EXPORT
void cogl_driver_set_feature (CoglDriver    *driver,
                              CoglFeatureID  feature,
                              gboolean       value);
