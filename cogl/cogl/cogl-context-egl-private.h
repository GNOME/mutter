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

#include "config.h"
#include "cogl/cogl-driver-private.h"

#include "cogl/cogl-context-egl.h"
#include "cogl/cogl-macros.h"
#include "cogl/cogl-bitmask.h"
#include "cogl/cogl-clip-stack.h"
#include "cogl/cogl-offscreen-private.h"
#include "cogl/cogl-types.h"

#if defined(HAVE_GL)
#include <GL/gl.h>
#elif defined(HAVE_GLES2)
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#endif

GLuint
cogl_context_egl_get_current_gl_program (CoglContextEGL *context_egl);

void
cogl_context_egl_set_current_gl_program (CoglContextEGL *context_egl,
                                         GLuint          program);

COGL_EXPORT_TEST
gboolean
cogl_context_egl_get_gl_blend_enable_cache (CoglContextEGL *context_egl);

void
cogl_context_egl_set_gl_blend_enable_cache (CoglContextEGL *context_egl,
                                            gboolean        enabled);

gboolean
cogl_context_egl_get_current_gl_dither_enabled (CoglContextEGL *context_egl);

void
cogl_context_egl_set_current_gl_dither_enabled (CoglContextEGL *context_egl,
                                                gboolean        enabled);

gboolean
cogl_context_egl_get_depth_test_enabled_cache (CoglContextEGL *context_egl);

void
cogl_context_egl_set_depth_test_enabled_cache (CoglContextEGL *context_egl,
                                               gboolean        enabled);

CoglDepthTestFunction
cogl_context_egl_get_depth_test_function_cache (CoglContextEGL *context_egl);

void
cogl_context_egl_set_depth_test_function_cache (CoglContextEGL        *context_egl,
                                                CoglDepthTestFunction  function);

gboolean
cogl_context_egl_get_depth_writing_enabled_cache (CoglContextEGL *context_egl);

void
cogl_context_egl_set_depth_writing_enabled_cache (CoglContextEGL *context_egl,
                                                  gboolean        enabled);

float
cogl_context_egl_get_depth_range_near_cache (CoglContextEGL *context_egl);

void
cogl_context_egl_set_depth_range_near_cache (CoglContextEGL *context_egl,
                                             float           near_val);

float
cogl_context_egl_get_depth_range_far_cache (CoglContextEGL *context_egl);

void
cogl_context_egl_set_depth_range_far_cache (CoglContextEGL *context_egl,
                                            float           far_val);

gboolean
cogl_context_egl_get_was_bound_to_onscreen (CoglContextEGL *context_egl);

void
cogl_context_egl_set_was_bound_to_onscreen (CoglContextEGL *context_egl,
                                            gboolean        bound);

gboolean
cogl_context_egl_get_have_last_offscreen_allocate_flags (CoglContextEGL *context_egl);

void
cogl_context_egl_set_have_last_offscreen_allocate_flags (CoglContextEGL *context_egl,
                                                         gboolean        have_flags);

CoglOffscreenAllocateFlags
cogl_context_egl_get_last_offscreen_allocate_flags (CoglContextEGL *context_egl);

void
cogl_context_egl_set_last_offscreen_allocate_flags (CoglContextEGL              *context_egl,
                                                    CoglOffscreenAllocateFlags   flags);

CoglBitmask *
cogl_context_egl_get_enabled_custom_attributes (CoglContextEGL *context_egl);

CoglBitmask *
cogl_context_egl_get_enable_custom_attributes_tmp (CoglContextEGL *context_egl);

CoglBitmask *
cogl_context_egl_get_changed_bits_tmp (CoglContextEGL *context_egl);

GString *
cogl_context_egl_get_codegen_header_buffer (CoglContextEGL *context_egl);

GString *
cogl_context_egl_get_codegen_source_buffer (CoglContextEGL *context_egl);
