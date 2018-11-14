/*
 * Authored By Niels De Graef <niels.degraef@barco.com>
 *
 * Copyright (C) 2018 Barco NV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef __COGL_COLORSPACE_CONVERSION_H__
#define __COGL_COLORSPACE_CONVERSION_H__

#include "cogl/cogl-types.h"
#include "cogl/cogl-pipeline.h"

G_BEGIN_DECLS

/**
 * SECTION:cogl-color-space-conversion
 * @title: CoglColorspaceConversion
 * @short_description: A collection of snippets to handle colorspace conversion
 *
 * In some use cases, one might generate non-RGBA textures (e.g. YUV), which is
 * problematic if you then have to composite them in to an RGBA framebuffer. In
 * comes #CoglColorspaceConversion, which you can attach to a #CoglPipeline to
 * do this all for you. Internally, it consists of nothing more than a
 * collection of #CoglSnippets which do the right thing for you.
 */

typedef struct _CoglColorspaceConversion CoglColorspaceConversion;
#define COGL_COLORSPACE_CONVERSION(ptr) ((CoglColorspaceConversion *) ptr)


/**
 * cogl_multiplane_texture_get_gtype:
 *
 * Returns: a #GType that can be used with the GLib type system.
 */
GType cogl_colorspace_conversion_get_gtype (void);

/*
 * cogl_is_colorspace_conversion:
 * @object: A #CoglObject pointer
 *
 * Gets whether the given @object references an existing
 * CoglColorspaceConversion.
 *
 * Return value: %TRUE if the @object references a #CoglColorspaceConversion,
 *   %FALSE otherwise
 */
gboolean
cogl_is_colorspace_conversion (void *object);

/**
 * cogl_colorspace_conversion_new:
 * @format: The input format
 *
 * Creates a #CoglColorspaceConversion to convert the given @formatro RGBA. If
 * no such conversion is needed, it will return %NULL.
 *
 * Returns: (transfer full) (nullable): A new #CoglColorspaceConversion, or
 * %NULL if none is needed.
 */
CoglColorspaceConversion * cogl_colorspace_conversion_new  (CoglPixelFormat format);

/**
 * cogl_colorspace_conversion_attach_to_pipeline:
 * @self: The #CoglColorspaceConversion you want to add
 * @pipeline: The #CoglPipeline which needs the color conversion
 * @layer: The layer you want to perform the color space conversion at
 *
 * Adds color conversion to the given @pipeline at the given @layer.
 */
void cogl_colorspace_conversion_attach_to_pipeline (CoglColorspaceConversion *self,
                                                    CoglPipeline *pipeline,
                                                    gint layer);

G_END_DECLS

#endif
