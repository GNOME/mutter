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

#include "config.h"

#include "cogl/cogl-context-egl-private.h"
#include "cogl/cogl-context-private.h"
#include "cogl/cogl-pixel-format.h"
#include "cogl/cogl-texture-2d-private.h"
#include "cogl/cogl-pipeline-private.h"

#include <gio/gio.h>

static gboolean
cogl_context_egl_initable_init (GInitable     *initable,
                                GCancellable  *cancellable,
                                GError       **error);

static void
cogl_context_egl_initable_iface_init (GInitableIface *iface)
{
  iface->init = cogl_context_egl_initable_init;
}

struct _CoglContextEGL
{
  CoglContext parent_instance;

  CoglBitmask enabled_custom_attributes;
  /* These are temporary bitmasks that are used when disabling
   * builtin and custom attribute arrays. They are here just
   * to avoid allocating new ones each time */
  CoglBitmask enable_custom_attributes_tmp;
  CoglBitmask changed_bits_tmp;

  gboolean gl_blend_enable_cache;

  gboolean depth_test_enabled_cache;
  CoglDepthTestFunction depth_test_function_cache;
  gboolean depth_writing_enabled_cache;
  float depth_range_near_cache;
  float depth_range_far_cache;

  gboolean have_last_offscreen_allocate_flags;
  CoglOffscreenAllocateFlags last_offscreen_allocate_flags;

  /* This becomes TRUE the first time the context is bound to an
   * onscreen buffer. This is used by cogl-framebuffer-gl to determine
   * when to initialise the glDrawBuffer state */
  gboolean was_bound_to_onscreen;

  /* Fragment processing programs */
  GLuint current_gl_program;

  gboolean current_gl_dither_enabled;

  GString *codegen_header_buffer;
  GString *codegen_source_buffer;
};

G_DEFINE_FINAL_TYPE_WITH_CODE (CoglContextEGL, cogl_context_egl, COGL_TYPE_CONTEXT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                       cogl_context_egl_initable_iface_init))

static void
cogl_context_egl_dispose (GObject *object)
{
  CoglContextEGL *context_egl = COGL_CONTEXT_EGL (object);

  _cogl_bitmask_destroy (&context_egl->enabled_custom_attributes);
  _cogl_bitmask_destroy (&context_egl->enable_custom_attributes_tmp);
  _cogl_bitmask_destroy (&context_egl->changed_bits_tmp);

  G_OBJECT_CLASS (cogl_context_egl_parent_class)->dispose (object);
}

static void
cogl_context_egl_init (CoglContextEGL *context_egl)
{
  _cogl_bitmask_init (&context_egl->enabled_custom_attributes);
  _cogl_bitmask_init (&context_egl->enable_custom_attributes_tmp);
  _cogl_bitmask_init (&context_egl->changed_bits_tmp);

  context_egl->current_gl_dither_enabled = TRUE;

  context_egl->depth_test_function_cache = COGL_DEPTH_TEST_FUNCTION_LESS;
  context_egl->depth_writing_enabled_cache = TRUE;
  context_egl->depth_range_far_cache = 1;

  context_egl->codegen_header_buffer = g_string_new ("");
  context_egl->codegen_source_buffer = g_string_new ("");
}

static void
cogl_context_egl_finalize (GObject *object)
{
  CoglContextEGL *context_egl = COGL_CONTEXT_EGL (object);

  g_string_free (context_egl->codegen_header_buffer, TRUE);
  g_string_free (context_egl->codegen_source_buffer, TRUE);

  G_OBJECT_CLASS (cogl_context_egl_parent_class)->finalize (object);
}

static void
cogl_context_egl_class_init (CoglContextEGLClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->dispose = cogl_context_egl_dispose;
  object_class->finalize = cogl_context_egl_finalize;
}

static gboolean
cogl_context_egl_initable_init (GInitable     *initable,
                                GCancellable  *cancellable,
                                GError       **error)
{
  CoglContext *context = COGL_CONTEXT (initable);
  uint8_t white_pixel[] = { 0xff, 0xff, 0xff, 0xff };

  GInitableIface *parent_iface =
    g_type_interface_peek (cogl_context_egl_parent_class, G_TYPE_INITABLE);

  if (!parent_iface->init (initable, cancellable, error))
    return FALSE;

  {
    g_autoptr (CoglTexture) default_texture =
      cogl_texture_2d_new_from_data (context,
                                     1, 1,
                                     COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                                     0,
                                     white_pixel,
                                     error);
    if (!default_texture)
      return FALSE;

    cogl_context_set_default_2d_texture (context, g_steal_pointer (&default_texture));
  }

  return TRUE;
}

CoglContext *
cogl_context_egl_new (CoglDisplay  *display,
                      GError      **error)
{
  g_return_val_if_fail (display != NULL, NULL);

  return g_initable_new (COGL_TYPE_CONTEXT_EGL, NULL, error,
                         "display", display,
                         NULL);
}

/* Accessors */

GLuint
cogl_context_egl_get_current_gl_program (CoglContextEGL *context_egl)
{
  return context_egl->current_gl_program;
}

void
cogl_context_egl_set_current_gl_program (CoglContextEGL *context_egl,
                                         GLuint          program)
{
  context_egl->current_gl_program = program;
}

gboolean
cogl_context_egl_get_gl_blend_enable_cache (CoglContextEGL *context_egl)
{
  return context_egl->gl_blend_enable_cache;
}

void
cogl_context_egl_set_gl_blend_enable_cache (CoglContextEGL *context_egl,
                                            gboolean        enabled)
{
  context_egl->gl_blend_enable_cache = enabled;
}

gboolean
cogl_context_egl_get_current_gl_dither_enabled (CoglContextEGL *context_egl)
{
  return context_egl->current_gl_dither_enabled;
}

void
cogl_context_egl_set_current_gl_dither_enabled (CoglContextEGL *context_egl,
                                                gboolean        enabled)
{
  context_egl->current_gl_dither_enabled = enabled;
}

gboolean
cogl_context_egl_get_depth_test_enabled_cache (CoglContextEGL *context_egl)
{
  return context_egl->depth_test_enabled_cache;
}

void
cogl_context_egl_set_depth_test_enabled_cache (CoglContextEGL *context_egl,
                                               gboolean        enabled)
{
  context_egl->depth_test_enabled_cache = enabled;
}

CoglDepthTestFunction
cogl_context_egl_get_depth_test_function_cache (CoglContextEGL *context_egl)
{
  return context_egl->depth_test_function_cache;
}

void
cogl_context_egl_set_depth_test_function_cache (CoglContextEGL         *context_egl,
                                                CoglDepthTestFunction   function)
{
  context_egl->depth_test_function_cache = function;
}

gboolean
cogl_context_egl_get_depth_writing_enabled_cache (CoglContextEGL *context_egl)
{
  return context_egl->depth_writing_enabled_cache;
}

void
cogl_context_egl_set_depth_writing_enabled_cache (CoglContextEGL *context_egl,
                                                  gboolean        enabled)
{
  context_egl->depth_writing_enabled_cache = enabled;
}

float
cogl_context_egl_get_depth_range_near_cache (CoglContextEGL *context_egl)
{
  return context_egl->depth_range_near_cache;
}

void
cogl_context_egl_set_depth_range_near_cache (CoglContextEGL *context_egl,
                                             float           near_val)
{
  context_egl->depth_range_near_cache = near_val;
}

float
cogl_context_egl_get_depth_range_far_cache (CoglContextEGL *context_egl)
{
  return context_egl->depth_range_far_cache;
}

void
cogl_context_egl_set_depth_range_far_cache (CoglContextEGL *context_egl,
                                            float           far_val)
{
  context_egl->depth_range_far_cache = far_val;
}

gboolean
cogl_context_egl_get_was_bound_to_onscreen (CoglContextEGL *context_egl)
{
  return context_egl->was_bound_to_onscreen;
}

void
cogl_context_egl_set_was_bound_to_onscreen (CoglContextEGL *context_egl,
                                            gboolean        bound)
{
  context_egl->was_bound_to_onscreen = bound;
}

gboolean
cogl_context_egl_get_have_last_offscreen_allocate_flags (CoglContextEGL *context_egl)
{
  return context_egl->have_last_offscreen_allocate_flags;
}

void
cogl_context_egl_set_have_last_offscreen_allocate_flags (CoglContextEGL *context_egl,
                                                         gboolean        have_flags)
{
  context_egl->have_last_offscreen_allocate_flags = have_flags;
}

CoglOffscreenAllocateFlags
cogl_context_egl_get_last_offscreen_allocate_flags (CoglContextEGL *context_egl)
{
  return context_egl->last_offscreen_allocate_flags;
}

void
cogl_context_egl_set_last_offscreen_allocate_flags (CoglContextEGL             *context_egl,
                                                    CoglOffscreenAllocateFlags  flags)
{
  context_egl->last_offscreen_allocate_flags = flags;
}

CoglBitmask *
cogl_context_egl_get_enabled_custom_attributes (CoglContextEGL *context_egl)
{
  return &context_egl->enabled_custom_attributes;
}

CoglBitmask *
cogl_context_egl_get_enable_custom_attributes_tmp (CoglContextEGL *context_egl)
{
  return &context_egl->enable_custom_attributes_tmp;
}

CoglBitmask *
cogl_context_egl_get_changed_bits_tmp (CoglContextEGL *context_egl)
{
  return &context_egl->changed_bits_tmp;
}

GString *
cogl_context_egl_get_codegen_header_buffer (CoglContextEGL *context_egl)
{
  return context_egl->codegen_header_buffer;
}

GString *
cogl_context_egl_get_codegen_source_buffer (CoglContextEGL *context_egl)
{
  return context_egl->codegen_source_buffer;
}
