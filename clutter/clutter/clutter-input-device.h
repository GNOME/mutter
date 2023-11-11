/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright © 2009, 2010, 2011  Intel Corp.
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
 * Author: Emmanuele Bassi <ebassi@linux.intel.com>
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-backend.h"
#include "clutter/clutter-types.h"
#include "clutter/clutter-seat.h"

G_BEGIN_DECLS

struct _ClutterInputDeviceClass
{
  GObjectClass parent_class;

  gboolean (* is_mode_switch_button) (ClutterInputDevice *device,
                                      guint               group,
                                      guint               button);
  gint (* get_group_n_modes) (ClutterInputDevice *device,
                              gint                group);

  gboolean (* is_grouped) (ClutterInputDevice *device,
                           ClutterInputDevice *other_device);

  int (* get_pad_feature_group) (ClutterInputDevice           *device,
                                 ClutterInputDevicePadFeature  feature,
                                 int                           n_feature);
  gboolean (* get_dimensions) (ClutterInputDevice *device,
                               unsigned int       *width,
                               unsigned int       *height);
};

#define CLUTTER_TYPE_INPUT_DEVICE               (clutter_input_device_get_type ())
#define CLUTTER_INPUT_DEVICE(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_INPUT_DEVICE, ClutterInputDevice))
#define CLUTTER_IS_INPUT_DEVICE(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_INPUT_DEVICE))
#define CLUTTER_INPUT_DEVICE_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST ((klass), CLUTTER_TYPE_INPUT_DEVICE, ClutterInputDeviceClass))
#define CLUTTER_IS_INPUT_DEVICE_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE ((klass), CLUTTER_TYPE_INPUT_DEVICE))
#define CLUTTER_INPUT_DEVICE_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS ((obj), CLUTTER_TYPE_INPUT_DEVICE, ClutterInputDeviceClass))

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterInputDevice, g_object_unref)

typedef struct _ClutterInputDeviceClass ClutterInputDeviceClass;

CLUTTER_EXPORT
GType clutter_input_device_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterInputDeviceType  clutter_input_device_get_device_type    (ClutterInputDevice  *device);

CLUTTER_EXPORT
const gchar *           clutter_input_device_get_device_name    (ClutterInputDevice  *device);
CLUTTER_EXPORT
ClutterInputMode        clutter_input_device_get_device_mode    (ClutterInputDevice  *device);
CLUTTER_EXPORT
gboolean                clutter_input_device_get_has_cursor     (ClutterInputDevice  *device);

CLUTTER_EXPORT
const gchar *           clutter_input_device_get_vendor_id      (ClutterInputDevice *device);
CLUTTER_EXPORT
const gchar *           clutter_input_device_get_product_id     (ClutterInputDevice *device);

CLUTTER_EXPORT
gint                    clutter_input_device_get_n_rings        (ClutterInputDevice *device);
CLUTTER_EXPORT
gint                    clutter_input_device_get_n_strips       (ClutterInputDevice *device);
CLUTTER_EXPORT
gint                    clutter_input_device_get_n_mode_groups  (ClutterInputDevice *device);
CLUTTER_EXPORT
int                     clutter_input_device_get_n_buttons (ClutterInputDevice *device);


CLUTTER_EXPORT
gint                    clutter_input_device_get_group_n_modes  (ClutterInputDevice *device,
                                                                 gint                group);

CLUTTER_EXPORT
gboolean                clutter_input_device_is_mode_switch_button (ClutterInputDevice *device,
                                                                    guint               group,
								    guint               button);
CLUTTER_EXPORT
gint                    clutter_input_device_get_mode_switch_button_group (ClutterInputDevice *device,
                                                                           guint               button);

CLUTTER_EXPORT
const gchar *           clutter_input_device_get_device_node    (ClutterInputDevice *device);

CLUTTER_EXPORT
gboolean                  clutter_input_device_is_grouped       (ClutterInputDevice *device,
                                                                 ClutterInputDevice *other_device);
CLUTTER_EXPORT
ClutterSeat *             clutter_input_device_get_seat         (ClutterInputDevice *device);

CLUTTER_EXPORT
int clutter_input_device_get_pad_feature_group (ClutterInputDevice           *device,
                                                ClutterInputDevicePadFeature  feature,
                                                int                           n_feature);

CLUTTER_EXPORT
ClutterInputCapabilities clutter_input_device_get_capabilities (ClutterInputDevice *device);

CLUTTER_EXPORT
gboolean clutter_input_device_get_dimensions (ClutterInputDevice *device,
                                              unsigned int       *width,
                                              unsigned int       *height);

G_END_DECLS
