/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2009  Intel Corporation.
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

#include <glib.h>

#include "clutter/pango/clutter-pango-pipeline-cache.h"
#include "clutter/clutter-color-state.h"


G_BEGIN_DECLS

typedef struct _ClutterPangoDisplayList ClutterPangoDisplayList;

ClutterPangoDisplayList * clutter_pango_display_list_new (ClutterPangoPipelineCache *pipeline_cache);

void clutter_pango_display_list_set_color_override (ClutterPangoDisplayList  *dl,
                                                    const CoglColor          *color);

void clutter_pango_display_list_remove_color_override (ClutterPangoDisplayList *dl);

void clutter_pango_display_list_add_texture (ClutterPangoDisplayList *dl,
                                             CoglTexture             *texture,
                                             float                    x_1,
                                             float                    y_1,
                                             float                    x_2,
                                             float                    y_2,
                                             float                    tx_1,
                                             float                    ty_1,
                                             float                    tx_2,
                                             float                    ty_2);

void clutter_pango_display_list_add_rectangle (ClutterPangoDisplayList *dl,
                                               float                    x_1,
                                               float                    y_1,
                                               float                    x_2,
                                               float                    y_2);

void clutter_pango_display_list_add_trapezoid (ClutterPangoDisplayList *dl,
                                               float                    y_1,
                                               float                    x_11,
                                               float                    x_21,
                                               float                    y_2,
                                               float                    x_12,
                                               float                    x_22);

void clutter_pango_display_list_render (CoglFramebuffer         *framebuffer,
                                        ClutterPangoDisplayList *dl,
                                        ClutterColorState       *color_state,
                                        ClutterColorState       *target_color_state,
                                        const CoglColor         *color);

void clutter_pango_display_list_free (ClutterPangoDisplayList *dl);

G_END_DECLS
