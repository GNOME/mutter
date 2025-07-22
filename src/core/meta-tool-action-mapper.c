/*
 * Copyright (C) 2014-2020 Red Hat
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <glib/gi18n-lib.h>
#include <gdesktop-enums.h>

#ifdef HAVE_LIBWACOM
#include <libwacom/libwacom.h>
#endif

#include "core/meta-tool-action-mapper.h"
#include "backends/meta-input-device-private.h"
#include "core/display-private.h"

struct _MetaToolActionMapper
{
  MetaTabletActionMapper parent_instance;
  MetaInputSettings *input_settings;
};

G_DEFINE_TYPE (MetaToolActionMapper, meta_tool_action_mapper, META_TYPE_TABLET_ACTION_MAPPER);

static gboolean
meta_tool_action_mapper_handle_event (MetaTabletActionMapper *mapper,
                                      const ClutterEvent     *event);

static void
meta_tool_action_mapper_finalize (GObject *object)
{
  MetaToolActionMapper *mapper = META_TOOL_ACTION_MAPPER (object);

  g_clear_object (&mapper->input_settings);

  G_OBJECT_CLASS (meta_tool_action_mapper_parent_class)->finalize (object);
}

static void
meta_tool_action_mapper_class_init (MetaToolActionMapperClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = meta_tool_action_mapper_finalize;
}

static void
meta_tool_action_mapper_init (MetaToolActionMapper *mapper)
{
  g_signal_connect (mapper, "input-event", G_CALLBACK (meta_tool_action_mapper_handle_event), NULL);
}

MetaToolActionMapper *
meta_tool_action_mapper_new (MetaBackend *backend)
{
  MetaToolActionMapper *action_mapper;
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  action_mapper = g_object_new (META_TYPE_TOOL_ACTION_MAPPER,
                                "monitor-manager", monitor_manager,
                                NULL);
  action_mapper->input_settings = g_object_ref (meta_backend_get_input_settings (backend));

  return action_mapper;
}

static gboolean
meta_tool_action_mapper_handle_button (MetaToolActionMapper *mapper,
                                       ClutterInputDevice   *device,
                                       const ClutterEvent   *event)
{
  MetaTabletActionMapper *tablet_mapper = META_TABLET_ACTION_MAPPER (mapper);
  MetaTabletActionMapperClass *tablet_klass = META_TABLET_ACTION_MAPPER_GET_CLASS (mapper);
  GDesktopStylusButtonAction action;
  gboolean is_press;
  g_autofree char *accel = NULL;
  uint32_t button, evcode;
  ClutterInputDeviceTool *tool;

  g_return_val_if_fail (clutter_event_type (event) == CLUTTER_BUTTON_PRESS ||
                        clutter_event_type (event) == CLUTTER_BUTTON_RELEASE, FALSE);

  if ((clutter_input_device_get_capabilities (device) & CLUTTER_INPUT_CAPABILITY_TABLET_TOOL) == 0)
    return FALSE;

  tool = clutter_event_get_device_tool (event);
  evcode = clutter_event_get_event_code (event);
  button = meta_evdev_tool_button_to_clutter (evcode);
  is_press = clutter_event_type (event) == CLUTTER_BUTTON_PRESS;

  action = meta_input_settings_get_tool_button_action (mapper->input_settings,
                                                       device, tool, button, &accel);
  switch (action)
    {
    case G_DESKTOP_STYLUS_BUTTON_ACTION_DEFAULT:
    case G_DESKTOP_STYLUS_BUTTON_ACTION_MIDDLE:
    case G_DESKTOP_STYLUS_BUTTON_ACTION_RIGHT:
    case G_DESKTOP_STYLUS_BUTTON_ACTION_BACK:
    case G_DESKTOP_STYLUS_BUTTON_ACTION_FORWARD:
      /* These are handled as normal button events, nothing to do here */
      return FALSE;
    case G_DESKTOP_STYLUS_BUTTON_ACTION_SWITCH_MONITOR:
      if (is_press)
        tablet_klass->cycle_tablet_output (tablet_mapper, device);
      return TRUE;
    case G_DESKTOP_STYLUS_BUTTON_ACTION_KEYBINDING:
      if (accel)
        tablet_klass->emulate_keybinding (tablet_mapper, accel, is_press);
      return TRUE;
    default:
      g_warn_if_reached ();
      return FALSE;
    }
}

gboolean
meta_tool_action_mapper_handle_event (MetaTabletActionMapper *tablet_mapper,
                                      const ClutterEvent     *event)
{
  MetaToolActionMapper *mapper = META_TOOL_ACTION_MAPPER (tablet_mapper);
  ClutterInputDevice *tool;

  tool = clutter_event_get_source_device ((ClutterEvent *) event);

  switch (clutter_event_type (event))
    {
    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_BUTTON_RELEASE:
      return meta_tool_action_mapper_handle_button (mapper, tool, event);
    default:
      break;
    }

  return CLUTTER_EVENT_PROPAGATE;
}
