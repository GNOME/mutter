/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2008 OpenedHand
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
 *
 *
 * Authors:
 *   Neil Roberts <neil@linux.intel.com>
 *   Robert Bragg <robert@linux.intel.com>
 *   Matthew Allum  <mallum@openedhand.com>
 */

#pragma once

#include <pango/pango.h>

#include "clutter/clutter-color-state.h"
#include "cogl/cogl.h"

G_BEGIN_DECLS

PangoRenderer * clutter_pango_renderer_new (CoglContext *context);

/**
 * clutter_ensure_glyph_cache_for_layout:
 * @layout: A #PangoLayout
 *
 * This updates any internal glyph cache textures as necessary to be
 * able to render the given @layout.
 *
 * This api should be used to avoid mid-scene modifications of
 * glyph-cache textures which can lead to undefined rendering results.
 */
void clutter_ensure_glyph_cache_for_layout (PangoLayout *layout);

/**
 * clutter_show_layout: (skip)
 * @framebuffer: A #CoglFramebuffer to draw too.
 * @layout: a #PangoLayout
 * @x: X coordinate to render the layout at
 * @y: Y coordinate to render the layout at
 * @color: color to use when rendering the layout
 *
 * Draws a solidly coloured @layout on the given @framebuffer at (@x,
 * @y) within the `framebuffer`'s current model-view coordinate space.
 */
void clutter_show_layout (CoglFramebuffer   *framebuffer,
                          PangoLayout       *layout,
                          float              x,
                          float              y,
                          const CoglColor   *color,
                          ClutterColorState *color_state,
                          ClutterColorState *target_color_state);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PangoRenderer, g_object_unref)

G_END_DECLS
