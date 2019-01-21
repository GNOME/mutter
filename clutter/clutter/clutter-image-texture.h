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

#ifndef __CLUTTER_IMAGE_TEXTURE_H__
#define __CLUTTER_IMAGE_TEXTURE_H__

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <cogl/cogl.h>
#include <clutter/clutter-types.h>

G_BEGIN_DECLS

#define CLUTTER_TYPE_IMAGE_TEXTURE              (clutter_image_texture_get_type ())
#define CLUTTER_IMAGE_TEXTURE(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_IMAGE_TEXTURE, ClutterImageTexture))
#define CLUTTER_IS_IMAGE_TEXTURE(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_IMAGE_TEXTURE))
#define CLUTTER_IMAGE_TEXTURE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), CLUTTER_TYPE_IMAGE_TEXTURE, ClutterImageTextureClass))
#define CLUTTER_IS_IMAGE_TEXTURE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), CLUTTER_TYPE_IMAGE_TEXTURE))
#define CLUTTER_IMAGE_TEXTURE_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), CLUTTER_TYPE_IMAGE_TEXTURE, ClutterImageTextureClass))


#define CLUTTER_IMAGE_TEXTURE_ERROR             (clutter_image_error_quark ())

typedef struct _ClutterImageTexture           ClutterImageTexture;
typedef struct _ClutterImageTexturePrivate    ClutterImageTexturePrivate;
typedef struct _ClutterImageTextureClass      ClutterImageTextureClass;

typedef enum {
  CLUTTER_IMAGE_TEXTURE_ERROR_INVALID_DATA
} ClutterImageTextureError;

struct _ClutterImageTexture
{
  /*< private >*/
  GObject parent_instance;

  ClutterImageTexturePrivate *priv;
};

struct _ClutterImageTextureClass
{
  /*< private >*/
  GObjectClass parent_class;

  gpointer _padding[16];
};

CLUTTER_EXPORT
GQuark clutter_image_texture_error_quark (void);
CLUTTER_EXPORT
GType clutter_image_texture_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterContent * clutter_image_texture_new_from_texture (CoglTexture *texture);

CLUTTER_EXPORT
CoglTexture * clutter_image_texture_get_texture (ClutterImageTexture *image);

G_END_DECLS

#endif /* __CLUTTER_IMAGE_TEXTURE_H__ */
