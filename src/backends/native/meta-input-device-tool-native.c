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

#include "config.h"

#include "backends/native/meta-input-thread.h"

struct _MetaInputDeviceToolNative
{
  ClutterInputDeviceTool parent_instance;
  struct libinput_tablet_tool *tool;
  GHashTable *button_map;
  graphene_point_t pressure_curve[2];
  MetaBezier *bezier;
};

G_DEFINE_FINAL_TYPE (MetaInputDeviceToolNative, meta_input_device_tool_native,
                     CLUTTER_TYPE_INPUT_DEVICE_TOOL)

static void
meta_input_device_tool_native_finalize (GObject *object)
{
  MetaInputDeviceToolNative *tool = META_INPUT_DEVICE_TOOL_NATIVE (object);

  g_hash_table_unref (tool->button_map);
  libinput_tablet_tool_unref (tool->tool);
  meta_bezier_free (tool->bezier);

  G_OBJECT_CLASS (meta_input_device_tool_native_parent_class)->finalize (object);
}

static void
meta_input_device_tool_native_class_init (MetaInputDeviceToolNativeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_input_device_tool_native_finalize;
}

static void
meta_input_device_tool_native_init (MetaInputDeviceToolNative *tool)
{
  tool->button_map = g_hash_table_new (NULL, NULL);
}

static void
init_pressurecurve (MetaInputDeviceToolNative *tool)
{
  MetaBezier *bezier = meta_bezier_new (N_PRESSURECURVE_POINTS);

  g_clear_pointer (&tool->bezier, meta_bezier_free);
  meta_bezier_init (bezier,
                    tool->pressure_curve[0].x,
                    tool->pressure_curve[0].y,
                    tool->pressure_curve[1].x,
                    tool->pressure_curve[1].y);
  tool->bezier = bezier;
}

static ClutterInputAxisFlags
translate_axes (struct libinput_tablet_tool *tool)
{
  ClutterInputAxisFlags axes = 0;

  if (libinput_tablet_tool_has_pressure (tool))
    axes |= CLUTTER_INPUT_AXIS_FLAG_PRESSURE;
  if (libinput_tablet_tool_has_distance (tool))
    axes |= CLUTTER_INPUT_AXIS_FLAG_DISTANCE;
  if (libinput_tablet_tool_has_rotation (tool))
    axes |= CLUTTER_INPUT_AXIS_FLAG_ROTATION;
  if (libinput_tablet_tool_has_slider (tool))
    axes |= CLUTTER_INPUT_AXIS_FLAG_SLIDER;
  if (libinput_tablet_tool_has_wheel (tool))
    axes |= CLUTTER_INPUT_AXIS_FLAG_WHEEL;
  if (libinput_tablet_tool_has_tilt (tool))
    axes |= CLUTTER_INPUT_AXIS_FLAG_XTILT | CLUTTER_INPUT_AXIS_FLAG_YTILT;

  return axes;
}

ClutterInputDeviceTool *
meta_input_device_tool_native_new (struct libinput_tablet_tool *tool,
                                   uint64_t                     serial,
                                   ClutterInputDeviceToolType   type)
{
  MetaInputDeviceToolNative *evdev_tool;

  evdev_tool = g_object_new (META_TYPE_INPUT_DEVICE_TOOL_NATIVE,
                             "type", type,
                             "serial", serial,
                             "id", libinput_tablet_tool_get_tool_id (tool),
                             "axes", translate_axes (tool),
                             NULL);

  evdev_tool->tool = libinput_tablet_tool_ref (tool);

  init_pressurecurve (evdev_tool);

  return CLUTTER_INPUT_DEVICE_TOOL (evdev_tool);
}

void
meta_input_device_tool_native_set_pressure_curve_in_impl (ClutterInputDeviceTool *tool,
                                                          double                  curve[4],
                                                          double                  range[2])
{
  MetaInputDeviceToolNative *evdev_tool;
  graphene_point_t p1, p2;

  g_return_if_fail (META_IS_INPUT_DEVICE_TOOL_NATIVE (tool));
  g_return_if_fail (curve[0] >= 0 && curve[0] <= 1 &&
                    curve[1] >= 0 && curve[1] <= 1 &&
                    curve[2] >= 0 && curve[2] <= 1 &&
                    curve[3] >= 0 && curve[3] <= 1);

  p1.x = (float) curve[0];
  p1.y = (float) curve[1];
  p2.x = (float) curve[2];
  p2.y = (float) curve[3];
  evdev_tool = META_INPUT_DEVICE_TOOL_NATIVE (tool);

  if (!graphene_point_equal (&p1, &evdev_tool->pressure_curve[0]) ||
      !graphene_point_equal (&p2, &evdev_tool->pressure_curve[1]))
    {
      evdev_tool->pressure_curve[0] = p1;
      evdev_tool->pressure_curve[1] = p2;
      init_pressurecurve (evdev_tool);
    }

  libinput_tablet_tool_config_pressure_range_set (evdev_tool->tool, range[0], range[1]);
}

void
meta_input_device_tool_native_set_button_code_in_impl (ClutterInputDeviceTool     *tool,
                                                       uint32_t                    button,
                                                       GDesktopStylusButtonAction  action)
{
  MetaInputDeviceToolNative *evdev_tool;

  g_return_if_fail (META_IS_INPUT_DEVICE_TOOL_NATIVE (tool));

  evdev_tool = META_INPUT_DEVICE_TOOL_NATIVE (tool);

  if (action == G_DESKTOP_STYLUS_BUTTON_ACTION_DEFAULT)
    {
      g_hash_table_remove (evdev_tool->button_map, GUINT_TO_POINTER (button));
    }
  else
    {
      g_hash_table_insert (evdev_tool->button_map, GUINT_TO_POINTER (button),
                           GUINT_TO_POINTER (action));
    }
}

double
meta_input_device_tool_native_translate_pressure_in_impl (ClutterInputDeviceTool *tool,
                                                          double                  pressure)
{
  MetaInputDeviceToolNative *evdev_tool;
  double factor;

  g_return_val_if_fail (META_IS_INPUT_DEVICE_TOOL_NATIVE (tool), pressure);

  evdev_tool = META_INPUT_DEVICE_TOOL_NATIVE (tool);

  pressure = CLAMP (pressure, 0.0, 1.0);
  factor = meta_bezier_lookup (evdev_tool->bezier, pressure);

  return pressure * factor;
}

GDesktopStylusButtonAction
meta_input_device_tool_native_get_button_code_in_impl (ClutterInputDeviceTool *tool,
                                                       uint32_t                button)
{
  MetaInputDeviceToolNative *evdev_tool;

  g_return_val_if_fail (META_IS_INPUT_DEVICE_TOOL_NATIVE (tool), 0);

  evdev_tool = META_INPUT_DEVICE_TOOL_NATIVE (tool);

  return GPOINTER_TO_UINT (g_hash_table_lookup (evdev_tool->button_map,
                                                GUINT_TO_POINTER (button)));
}
