/*
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#pragma once

#ifndef META_INPUT_THREAD_H_INSIDE
#error "This header cannot be included directly. Use "backends/native/meta-input-thread.h""
#endif /* META_INPUT_THREAD_H_INSIDE */

#include <graphene.h>
#include <libinput.h>

#include "backends/native/meta-bezier.h"
#include "clutter/clutter.h"

G_BEGIN_DECLS

#define META_TYPE_INPUT_DEVICE_TOOL_NATIVE (meta_input_device_tool_native_get_type ())

G_DECLARE_FINAL_TYPE (MetaInputDeviceToolNative,
                      meta_input_device_tool_native,
                      META,
                      INPUT_DEVICE_TOOL_NATIVE,
                      ClutterInputDeviceTool)

typedef struct _MetaInputDeviceToolNative MetaInputDeviceToolNative;

#define N_PRESSURECURVE_POINTS 256

ClutterInputDeviceTool * meta_input_device_tool_native_new      (struct libinput_tablet_tool *tool,
                                                                 uint64_t                     serial,
                                                                 ClutterInputDeviceToolType   type);

gdouble                  meta_input_device_tool_native_translate_pressure_in_impl (ClutterInputDeviceTool *tool,
                                                                                   double                  pressure);
uint32_t                 meta_input_device_tool_native_get_button_code_in_impl (ClutterInputDeviceTool *tool,
                                                                                uint32_t                button);

void                     meta_input_device_tool_native_set_pressure_curve_in_impl (ClutterInputDeviceTool *tool,
                                                                                   double                  curve[4]);
void                     meta_input_device_tool_native_set_button_code_in_impl (ClutterInputDeviceTool *tool,
                                                                                uint32_t                button,
                                                                                uint32_t                evcode);

G_END_DECLS
