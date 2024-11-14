/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2024 Red Hat.
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

#include "cogl/driver/gl/cogl-driver-gl-private.h"
#include "cogl/driver/gl/cogl-pipeline-opengl-private.h"
#include "cogl/driver/gl/cogl-buffer-gl-private.h"
#include "cogl/driver/gl/cogl-clip-stack-gl-private.h"
#include "cogl/driver/gl/cogl-attribute-gl-private.h"
#include "cogl/driver/gl/cogl-texture-2d-gl-private.h"
#include "cogl/driver/gl/cogl-util-gl-private.h"

G_DEFINE_TYPE (CoglDriverGL, cogl_driver_gl, COGL_TYPE_DRIVER);

static void
cogl_driver_gl_class_init (CoglDriverGLClass *klass)
{
  CoglDriverClass *driver_klass = COGL_DRIVER_CLASS (klass);

  driver_klass->context_init = _cogl_driver_gl_context_init;
  driver_klass->context_deinit = _cogl_driver_gl_context_deinit;
  driver_klass->get_vendor = _cogl_context_get_gl_vendor;
  driver_klass->is_hardware_accelerated = _cogl_driver_gl_is_hardware_accelerated;
  driver_klass->get_graphics_reset_status = _cogl_gl_get_graphics_reset_status;
  driver_klass->create_framebuffer_driver = _cogl_driver_gl_create_framebuffer_driver;
  driver_klass->flush_framebuffer_state = _cogl_driver_gl_flush_framebuffer_state;
  driver_klass->flush_attributes_state = _cogl_gl_flush_attributes_state;
  driver_klass->clip_stack_flush = _cogl_clip_stack_gl_flush;
  driver_klass->buffer_create = _cogl_buffer_gl_create;
  driver_klass->buffer_destroy = _cogl_buffer_gl_destroy;
  driver_klass->buffer_map_range = _cogl_buffer_gl_map_range;
  driver_klass->buffer_unmap = _cogl_buffer_gl_unmap;
  driver_klass->buffer_set_data = _cogl_buffer_gl_set_data;
  driver_klass->sampler_init = _cogl_sampler_gl_init;
  driver_klass->sampler_free = _cogl_sampler_gl_free;
  driver_klass->set_uniform = _cogl_gl_set_uniform; /* XXX name is weird... */
  driver_klass->create_timestamp_query = cogl_gl_create_timestamp_query;
  driver_klass->free_timestamp_query = cogl_gl_free_timestamp_query;
  driver_klass->timestamp_query_get_time_ns = cogl_gl_timestamp_query_get_time_ns;
  driver_klass->get_gpu_time_ns = cogl_gl_get_gpu_time_ns;
}

static void
cogl_driver_gl_init (CoglDriverGL *driver)
{
}
