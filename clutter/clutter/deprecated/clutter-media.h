/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By: Matthew Allum  <mallum@openedhand.com>
 *              Emmanuele Bassi <ebassi@linux.intel.com>
 *
 * Copyright (C) 2006 OpenedHand
 * Copyright (C) 2009 Intel Corp.
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
 */

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#ifndef __CLUTTER_MEDIA_H__
#define __CLUTTER_MEDIA_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define CLUTTER_TYPE_MEDIA                      (clutter_media_get_type ())
#define CLUTTER_MEDIA(obj)                      (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_MEDIA, ClutterMedia))
#define CLUTTER_IS_MEDIA(obj)                   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_MEDIA))
#define CLUTTER_MEDIA_GET_INTERFACE(obj)        (G_TYPE_INSTANCE_GET_INTERFACE ((obj), CLUTTER_TYPE_MEDIA, ClutterMediaIface))

typedef struct _ClutterMedia            ClutterMedia; /* dummy typedef */
typedef struct _ClutterMediaIface       ClutterMediaIface;

/**
 * ClutterMedia:
 *
 * #ClutterMedia is an opaque structure whose members cannot be directly
 * accessed
 *
 * Since: 0.2
 */

/**
 * ClutterMediaIface:
 * @eos: handler for the #ClutterMedia::eos signal
 * @error: handler for the #ClutterMedia::error signal
 *
 * Interface vtable for #ClutterMedia implementations
 *
 * Since: 0.2
 */
struct _ClutterMediaIface
{
  /*< private >*/
  GTypeInterface base_iface;

  /*< public >*/
  /* signals */
  void (* eos)   (ClutterMedia *media);
  void (* error) (ClutterMedia *media,
		  const GError *error);
};

CLUTTER_DEPRECATED
GType    clutter_media_get_type               (void) G_GNUC_CONST;

CLUTTER_DEPRECATED
void     clutter_media_set_uri                (ClutterMedia *media,
                                               const gchar  *uri);
CLUTTER_DEPRECATED
gchar *  clutter_media_get_uri                (ClutterMedia *media);
CLUTTER_DEPRECATED
void     clutter_media_set_filename           (ClutterMedia *media,
                                               const gchar  *filename);

CLUTTER_DEPRECATED
void     clutter_media_set_playing            (ClutterMedia *media,
                                               gboolean      playing);
CLUTTER_DEPRECATED
gboolean clutter_media_get_playing            (ClutterMedia *media);
CLUTTER_DEPRECATED
void     clutter_media_set_progress           (ClutterMedia *media,
                                               gdouble       progress);
CLUTTER_DEPRECATED
gdouble  clutter_media_get_progress           (ClutterMedia *media);
CLUTTER_DEPRECATED
void     clutter_media_set_subtitle_uri       (ClutterMedia *media,
                                               const gchar  *uri);
CLUTTER_DEPRECATED
gchar *  clutter_media_get_subtitle_uri       (ClutterMedia *media);
CLUTTER_DEPRECATED
void     clutter_media_set_subtitle_font_name (ClutterMedia *media,
                                               const char   *font_name);
CLUTTER_DEPRECATED
gchar *  clutter_media_get_subtitle_font_name (ClutterMedia *media);
CLUTTER_DEPRECATED
void     clutter_media_set_audio_volume       (ClutterMedia *media,
                                               gdouble       volume);
CLUTTER_DEPRECATED
gdouble  clutter_media_get_audio_volume       (ClutterMedia *media);
CLUTTER_DEPRECATED
gboolean clutter_media_get_can_seek           (ClutterMedia *media);
CLUTTER_DEPRECATED
gdouble  clutter_media_get_buffer_fill        (ClutterMedia *media);
CLUTTER_DEPRECATED
gdouble  clutter_media_get_duration           (ClutterMedia *media);

G_END_DECLS

#endif /* __CLUTTER_MEDIA_H__ */
