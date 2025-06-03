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

#include <gmodule.h>

#include "cogl/cogl-driver-private.h"
#include "cogl/cogl-texture-driver.h"
#include "cogl/cogl-context.h"
#include "cogl/cogl-closure-list-private.h"
#include "cogl/winsys/cogl-winsys-private.h"

typedef const CoglWinsysVtable *(*CoglCustomWinsysVtableGetter) (CoglRenderer *renderer);

typedef CoglFilterReturn (* CoglNativeFilterFunc) (void *native_event,
                                                   void *data);

void
_cogl_renderer_add_native_filter (CoglRenderer *renderer,
                                  CoglNativeFilterFunc func,
                                  void *data);

void
_cogl_renderer_remove_native_filter (CoglRenderer *renderer,
                                     CoglNativeFilterFunc func,
                                     void *data);

CoglDriver * cogl_renderer_get_driver (CoglRenderer *renderer);

const CoglWinsysVtable * cogl_renderer_get_winsys_vtable (CoglRenderer *renderer);

void cogl_renderer_set_custom_winsys_data (CoglRenderer *renderer,
                                           void         *winsys_data);

CoglClosure * cogl_renderer_add_idle_closure (CoglRenderer  *renderer,
                                              void (*closure)(void *),
                                              gpointer       data);

CoglList * cogl_renderer_get_idle_closures (CoglRenderer *renderer);

GModule * cogl_renderer_get_gl_module (CoglRenderer *renderer);

void cogl_renderer_set_display (CoglRenderer *renderer,
                                CoglDisplay   *display);

CoglDisplay * cogl_renderer_get_display (CoglRenderer *renderer);
