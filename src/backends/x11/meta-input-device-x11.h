/*
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

#pragma once

#include <X11/extensions/XInput2.h>

#ifdef HAVE_LIBWACOM
#include <libwacom/libwacom.h>
#endif

#include "backends/meta-input-device-private.h"
#include "clutter/clutter.h"

G_BEGIN_DECLS

#define META_TYPE_INPUT_DEVICE_X11 (meta_input_device_x11_get_type ())

G_DECLARE_FINAL_TYPE (MetaInputDeviceX11,
                      meta_input_device_x11,
                      META, INPUT_DEVICE_X11,
                      MetaInputDevice)

typedef struct _MetaInputDeviceX11 MetaInputDeviceX11;

void  meta_input_device_x11_update_tool     (ClutterInputDevice     *device,
                                             ClutterInputDeviceTool *tool);
ClutterInputDeviceTool * meta_input_device_x11_get_current_tool (ClutterInputDevice *device);

#ifdef HAVE_LIBWACOM
void meta_input_device_x11_ensure_wacom_info (ClutterInputDevice  *device,
                                              WacomDeviceDatabase *wacom_db);

uint32_t meta_input_device_x11_get_pad_group_mode (ClutterInputDevice *device,
                                                   uint32_t            group);

void meta_input_device_x11_update_pad_state (ClutterInputDevice *device,
                                             uint32_t            button,
                                             uint32_t            state,
                                             uint32_t           *group,
                                             uint32_t           *mode);

#endif

gboolean meta_input_device_x11_get_pointer_location (ClutterInputDevice *device,
                                                     float              *x,
                                                     float              *y);
int meta_input_device_x11_get_device_id (ClutterInputDevice *device);

int meta_input_device_x11_get_n_axes (ClutterInputDevice *device);
void meta_input_device_x11_reset_axes (ClutterInputDevice *device);
int meta_input_device_x11_add_axis (ClutterInputDevice *device,
                                    ClutterInputAxis    axis,
                                    double              minimum,
                                    double              maximum,
                                    double              resolution);
gboolean meta_input_device_x11_get_axis (ClutterInputDevice *device,
                                         int                 idx,
                                         ClutterInputAxis   *use);
gboolean meta_input_device_x11_translate_axis (ClutterInputDevice *device,
                                               int                 idx,
                                               double              value,
                                               double             *axis_value);

void meta_input_device_x11_add_scroll_info (ClutterInputDevice     *device,
                                            int                     idx,
                                            ClutterScrollDirection  direction,
                                            double                  increment);
gboolean meta_input_device_x11_get_scroll_delta (ClutterInputDevice     *device,
                                                 int                     idx,
                                                 gdouble                 value,
                                                 ClutterScrollDirection *direction_p,
                                                 double                 *delta_p);
void meta_input_device_x11_reset_scroll_info (ClutterInputDevice *device);

G_END_DECLS
