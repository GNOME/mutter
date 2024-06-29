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

#include "cogl/cogl-types.h"
#include "cogl/cogl-texture.h"
#include "cogl/cogl-framebuffer.h"
#include "cogl/cogl-macros.h"

G_BEGIN_DECLS

/* Misc */
/**
 * cogl_get_proc_address: (skip)
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
COGL_EXPORT GCallback
cogl_get_proc_address (const char *name);


G_END_DECLS
