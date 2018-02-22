/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright © 2011  Intel Corp.
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

#ifndef __CLUTTER_INPUT_DEVICE_XI2_H__
#define __CLUTTER_INPUT_DEVICE_XI2_H__

#include <clutter/clutter-input-device.h>
#include <X11/extensions/XInput2.h>

#ifdef HAVE_LIBWACOM
#include <libwacom/libwacom.h>
#endif

G_BEGIN_DECLS

#define CLUTTER_TYPE_INPUT_DEVICE_XI2           (_clutter_input_device_xi2_get_type ())
#define CLUTTER_INPUT_DEVICE_XI2(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_INPUT_DEVICE_XI2, ClutterInputDeviceXI2))
#define CLUTTER_IS_INPUT_DEVICE_XI2(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_INPUT_DEVICE_XI2))

typedef struct _ClutterInputDeviceXI2           ClutterInputDeviceXI2;

GType _clutter_input_device_xi2_get_type (void) G_GNUC_CONST;

void  _clutter_input_device_xi2_translate_state (ClutterEvent    *event,
						 XIModifierState *modifiers_state,
						 XIButtonState   *buttons_state,
						 XIGroupState    *group_state);
void  clutter_input_device_xi2_update_tool      (ClutterInputDevice     *device,
                                                 ClutterInputDeviceTool *tool);
ClutterInputDeviceTool * clutter_input_device_xi2_get_current_tool (ClutterInputDevice *device);

#ifdef HAVE_LIBWACOM
void clutter_input_device_xi2_ensure_wacom_info (ClutterInputDevice  *device,
                                                 WacomDeviceDatabase *wacom_db);

guint clutter_input_device_xi2_get_pad_group_mode (ClutterInputDevice *device,
                                                   guint               group);

void clutter_input_device_xi2_update_pad_state (ClutterInputDevice *device,
                                                guint               button,
                                                guint               state,
                                                guint              *group,
                                                guint              *mode);

#endif

G_END_DECLS

#endif /* __CLUTTER_INPUT_DEVICE_XI2_H__ */
