/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2012  Intel Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Author:
 *   Emmanuele Bassi <ebassi@linux.intel.com>
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-types.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_CANVAS             (clutter_canvas_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterCanvas,
                          clutter_canvas,
                          CLUTTER,
                          CANVAS,
                          GObject)

/**
 * ClutterCanvasClass:
 * @draw: class handler for the #ClutterCanvas::draw signal
 *
 * The #ClutterCanvasClass structure contains
 * private data.
 */
struct _ClutterCanvasClass
{
  /*< private >*/
  GObjectClass parent_class;

  /*< public >*/
  gboolean (* draw) (ClutterCanvas *canvas,
                     cairo_t       *cr,
                     int            width,
                     int            height);
};

CLUTTER_EXPORT
ClutterContent *        clutter_canvas_new                      (void);
CLUTTER_EXPORT
gboolean                clutter_canvas_set_size                 (ClutterCanvas *canvas,
                                                                 int            width,
                                                                 int            height);

CLUTTER_EXPORT
void                    clutter_canvas_set_scale_factor         (ClutterCanvas *canvas,
                                                                 float          scale);
CLUTTER_EXPORT
float                   clutter_canvas_get_scale_factor         (ClutterCanvas *canvas);

G_END_DECLS
