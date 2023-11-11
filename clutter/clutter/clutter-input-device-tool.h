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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-types.h"
#include "clutter/clutter-enum-types.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_INPUT_DEVICE_TOOL            (clutter_input_device_tool_get_type ())

struct _ClutterInputDeviceToolClass
{
  GObjectClass parent_class;
};

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterInputDeviceTool,
                          clutter_input_device_tool,
                          CLUTTER, INPUT_DEVICE_TOOL,
                          GObject)

CLUTTER_EXPORT
guint64                    clutter_input_device_tool_get_serial    (ClutterInputDeviceTool *tool);

CLUTTER_EXPORT
ClutterInputDeviceToolType clutter_input_device_tool_get_tool_type (ClutterInputDeviceTool *tool);

CLUTTER_EXPORT
guint64                    clutter_input_device_tool_get_id        (ClutterInputDeviceTool *tool);

CLUTTER_EXPORT
ClutterInputAxisFlags      clutter_input_device_tool_get_axes      (ClutterInputDeviceTool *tool);

G_END_DECLS
