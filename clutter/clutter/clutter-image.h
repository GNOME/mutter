/*
 * Clutter.
 *
 * An OpenGL based 'interactive image' library.
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

#ifndef __CLUTTER_IMAGE_H__
#define __CLUTTER_IMAGE_H__

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <cogl/cogl.h>
#include <clutter/clutter-types.h>

G_BEGIN_DECLS

#define CLUTTER_TYPE_IMAGE (clutter_image_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterImage, clutter_image, CLUTTER, IMAGE, GObject)

/**
 * ClutterImageClass:
 *
 * The #ClutterImageClass structure contains
 * private data.
 */
struct _ClutterImageClass
{
  /*< private >*/
  GObjectClass parent_class;

  gpointer _padding[16];
};

CLUTTER_EXPORT
ClutterContent *        clutter_image_new               (void);
CLUTTER_EXPORT
gboolean                clutter_image_set_data          (ClutterImage                 *image,
                                                         const guint8                 *data,
                                                         CoglPixelFormat               pixel_format,
                                                         guint                         width,
                                                         guint                         height,
                                                         guint                         row_stride,
                                                         GError                      **error);
CLUTTER_EXPORT
gboolean                clutter_image_set_area          (ClutterImage                 *image,
                                                         const guint8                 *data,
                                                         CoglPixelFormat               pixel_format,
                                                         const cairo_rectangle_int_t  *rect,
                                                         guint                         row_stride,
                                                         GError                      **error);
CLUTTER_EXPORT
gboolean                clutter_image_set_bytes         (ClutterImage                 *image,
                                                         GBytes                       *data,
                                                         CoglPixelFormat               pixel_format,
                                                         guint                         width,
                                                         guint                         height,
                                                         guint                         row_stride,
                                                         GError                      **error);

CLUTTER_EXPORT
CoglTexture *           clutter_image_get_texture       (ClutterImage                 *image);

G_END_DECLS

#endif /* __CLUTTER_IMAGE_H__ */
