/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
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
 */

#pragma once

#include <EGL/eglext.h>

#include "cogl/cogl-context.h"
#include "cogl/cogl-framebuffer-private.h"
#include "cogl/winsys/cogl-winsys.h"
#include "cogl/winsys/cogl-winsys-egl.h"

/* XXX: depending on what version of Mesa you have then
 * eglQueryWaylandBuffer may take a wl_buffer or wl_resource argument
 * and the EGL header will only forward declare the corresponding
 * type.
 *
 * The use of wl_buffer has been deprecated and so internally we
 * assume that eglQueryWaylandBuffer takes a wl_resource but for
 * compatibility we forward declare wl_resource in case we are
 * building with EGL headers that still use wl_buffer.
 *
 * Placing the forward declaration here means it comes before we
 * #include cogl-winsys-egl-feature-functions.h bellow which
 * declares lots of function pointers for accessing EGL extensions
 * and cogl-winsys-egl.c will include this header before it also
 * includes cogl-winsys-egl-feature-functions.h that may depend
 * on this type.
 */
#ifdef EGL_WL_bind_wayland_display
struct wl_resource;
#endif

typedef struct _CoglContextEGL
{
  EGLSurface saved_draw_surface;
  EGLSurface saved_read_surface;
} CoglContextEGL;


#ifdef EGL_KHR_image_base
EGLImageKHR
_cogl_egl_create_image (CoglContext *ctx,
                        EGLenum target,
                        EGLClientBuffer buffer,
                        const EGLint *attribs);

void
_cogl_egl_destroy_image (CoglContext *ctx,
                         EGLImageKHR image);
#endif
