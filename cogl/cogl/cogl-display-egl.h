/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2025 Red Hat.
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

#include "cogl/cogl-display.h"

#include <glib-object.h>
#include <EGL/egl.h>

G_BEGIN_DECLS

#define COGL_TYPE_DISPLAY_EGL (cogl_display_egl_get_type ())

COGL_EXPORT
G_DECLARE_DERIVABLE_TYPE (CoglDisplayEGL,
                          cogl_display_egl,
                          COGL,
                          DISPLAY_EGL,
                          CoglDisplay)

struct _CoglDisplayEGLClass
{
  CoglDisplayClass parent_class;

  int (* add_config_attributes) (CoglDisplayEGL *display,
                                 EGLint         *attributes);
};

COGL_EXPORT
CoglDisplayEGL * cogl_display_egl_new (CoglRenderer *renderer);

/**
 * cogl_display_egl_get_dummy_surface: (skip)
 */
COGL_EXPORT
EGLSurface cogl_display_egl_get_dummy_surface (CoglDisplayEGL *display_egl);

/**
 * cogl_display_egl_set_dummy_surface: (skip)
 */
COGL_EXPORT
void cogl_display_egl_set_dummy_surface (CoglDisplayEGL *display_egl,
                                         EGLSurface      dummy_surface);

/**
 * cogl_display_egl_get_egl_context: (skip)
 */
COGL_EXPORT
EGLContext cogl_display_egl_get_egl_context (CoglDisplayEGL *display_egl);

/**
 * cogl_display_egl_get_egl_config: (skip)
 */
COGL_EXPORT
EGLConfig cogl_display_egl_get_egl_config (CoglDisplayEGL *display_egl);

/**
 * cogl_display_egl_determine_attributes: (skip)
 */
COGL_EXPORT
void cogl_display_egl_determine_attributes (CoglDisplayEGL *display,
                                            EGLint         *attributes);

/**
 * cogl_display_egl_make_current: (skip)
 */
COGL_EXPORT
EGLBoolean cogl_display_egl_make_current (CoglDisplayEGL *display_egl,
                                          EGLSurface      draw,
                                          EGLSurface      read,
                                          EGLContext      context);

/**
 * cogl_display_egl_ensure_current: (skip)
 */
COGL_EXPORT
EGLBoolean cogl_display_egl_ensure_current (CoglDisplayEGL *display_egl);

G_END_DECLS
