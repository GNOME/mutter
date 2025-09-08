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
                                             CoglContext    *ctx,
                                             CoglPixelFormat format);
