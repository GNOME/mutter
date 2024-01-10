/*
 * Copyright © 2016 Red Hat
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

#include "clutter/clutter.h"

#define META_TYPE_INPUT_DEVICE_TOOL_X11 (meta_input_device_tool_x11_get_type ())

G_DECLARE_FINAL_TYPE (MetaInputDeviceToolX11,
                      meta_input_device_tool_x11,
                      META, INPUT_DEVICE_TOOL_X11,
                      ClutterInputDeviceTool)

typedef struct _MetaInputDeviceToolX11 MetaInputDeviceToolX11;

ClutterInputDeviceTool * meta_input_device_tool_x11_new (guint                        serial,
                                                         ClutterInputDeviceToolType   type);
