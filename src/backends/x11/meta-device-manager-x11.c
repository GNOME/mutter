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

#include "config.h"

#include <stdint.h>
#include <X11/extensions/XInput2.h>

#include "backends/x11/meta-backend-x11.h"
#include "backends/x11/meta-device-manager-x11.h"
#include "backends/x11/meta-event-x11.h"
#include "backends/x11/meta-input-device-x11.h"
#include "backends/x11/meta-input-device-tool-x11.h"
#include "backends/x11/meta-keymap-x11.h"
#include "backends/x11/meta-seat-x11.h"
#include "backends/x11/meta-stage-x11.h"
#include "backends/x11/meta-virtual-input-device-x11.h"
#include "backends/x11/meta-xkb-a11y-x11.h"
#include "clutter/clutter-mutter.h"
#include "clutter/x11/clutter-x11.h"
#include "core/display-private.h"
#include "meta/meta-x11-errors.h"

enum
{
  PROP_0,

  PROP_SEAT,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST] = { NULL, };

G_DEFINE_TYPE (MetaDeviceManagerX11,
               meta_device_manager_x11,
               CLUTTER_TYPE_DEVICE_MANAGER)

static void
meta_device_manager_x11_select_stage_events (ClutterDeviceManager *manager,
                                             ClutterStage         *stage)
{
  MetaStageX11 *stage_x11;
  XIEventMask xi_event_mask;
  unsigned char *mask;
  int len;

  stage_x11 = META_STAGE_X11 (_clutter_stage_get_window (stage));

  len = XIMaskLen (XI_LASTEVENT);
  mask = g_new0 (unsigned char, len);

  XISetMask (mask, XI_Motion);
  XISetMask (mask, XI_ButtonPress);
  XISetMask (mask, XI_ButtonRelease);
  XISetMask (mask, XI_KeyPress);
  XISetMask (mask, XI_KeyRelease);
  XISetMask (mask, XI_Enter);
  XISetMask (mask, XI_Leave);

  XISetMask (mask, XI_TouchBegin);
  XISetMask (mask, XI_TouchUpdate);
  XISetMask (mask, XI_TouchEnd);

  xi_event_mask.deviceid = XIAllMasterDevices;
  xi_event_mask.mask = mask;
  xi_event_mask.mask_len = len;

  XISelectEvents (clutter_x11_get_default_display (),
                  stage_x11->xwin, &xi_event_mask, 1);

  g_free (mask);
}

static void
meta_device_manager_x11_add_device (ClutterDeviceManager *manager,
                                    ClutterInputDevice   *device)
{
  /* XXX implement */
}

static void
meta_device_manager_x11_remove_device (ClutterDeviceManager *manager,
                                       ClutterInputDevice   *device)
{
  /* XXX implement */
}

static const GSList *
meta_device_manager_x11_get_devices (ClutterDeviceManager *manager)
{
  MetaDeviceManagerX11 *manager_xi2 = META_DEVICE_MANAGER_X11 (manager);
  ClutterBackend *backend = clutter_get_default_backend ();
  ClutterSeat *seat = clutter_backend_get_default_seat (backend);
  GSList *all_devices = NULL;
  GList *l, *devices;

  if (manager_xi2->all_devices != NULL)
    return manager_xi2->all_devices;

  all_devices = g_slist_prepend (all_devices,
                                 clutter_seat_get_pointer (seat));
  all_devices = g_slist_prepend (all_devices,
                                 clutter_seat_get_keyboard (seat));

  devices = clutter_seat_list_devices (seat, clutter_seat_get_pointer (seat));
  for (l = devices; l; l = l->next)
    all_devices = g_slist_prepend (all_devices, l->data);
  g_list_free (devices);

  devices = clutter_seat_list_devices (seat, clutter_seat_get_keyboard (seat));
  for (l = devices; l; l = l->next)
    all_devices = g_slist_prepend (all_devices, l->data);
  g_list_free (devices);

  manager_xi2->all_devices = g_slist_reverse (all_devices);

  return manager_xi2->all_devices;
}

static ClutterInputDevice *
meta_device_manager_x11_get_device (ClutterDeviceManager *manager,
                                    gint                  id)
{
  ClutterBackend *backend = clutter_get_default_backend ();
  ClutterSeat *seat = clutter_backend_get_default_seat (backend);

  return meta_seat_x11_lookup_device_id (META_SEAT_X11 (seat), id);
}

static ClutterInputDevice *
meta_device_manager_x11_get_core_device (ClutterDeviceManager   *manager,
                                         ClutterInputDeviceType  device_type)
{
  ClutterBackend *backend = clutter_get_default_backend ();
  ClutterSeat *seat = clutter_backend_get_default_seat (backend);

  switch (device_type)
    {
    case CLUTTER_POINTER_DEVICE:
      return clutter_seat_get_pointer (seat);;

    case CLUTTER_KEYBOARD_DEVICE:
      return clutter_seat_get_keyboard (seat);;

    default:
      break;
    }

  return NULL;
}

static void
on_device_added (ClutterSeat          *seat,
                 ClutterInputDevice   *parent,
                 ClutterInputDevice   *device,
                 ClutterDeviceManager *manager)
{
  g_signal_emit_by_name (manager, "device-added", device);
}

static void
on_device_removed (ClutterSeat          *seat,
                   ClutterInputDevice   *parent,
                   ClutterInputDevice   *device,
                   ClutterDeviceManager *manager)
{
  g_signal_emit_by_name (manager, "device-removed", device);
}

static void
on_tool_changed (ClutterSeat            *seat,
                 ClutterInputDevice     *device,
                 ClutterInputDeviceTool *tool,
                 ClutterDeviceManager   *manager)
{
  g_signal_emit_by_name (manager, "tool-changed", device, tool);
}

static void
meta_device_manager_x11_constructed (GObject *object)
{
  ClutterDeviceManager *manager = CLUTTER_DEVICE_MANAGER (object);
  MetaDeviceManagerX11 *manager_xi2 = META_DEVICE_MANAGER_X11 (object);

  g_signal_connect (manager_xi2->seat, "device-added",
                    G_CALLBACK (on_device_added), manager_xi2);
  g_signal_connect (manager_xi2->seat, "device-added",
                    G_CALLBACK (on_device_removed), manager_xi2);
  g_signal_connect (manager_xi2->seat, "tool-changed",
                    G_CALLBACK (on_tool_changed), manager_xi2);

  meta_device_manager_x11_a11y_init (manager);

  if (G_OBJECT_CLASS (meta_device_manager_x11_parent_class)->constructed)
    G_OBJECT_CLASS (meta_device_manager_x11_parent_class)->constructed (object);
}

static void
meta_device_manager_x11_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  MetaDeviceManagerX11 *manager_xi2 = META_DEVICE_MANAGER_X11 (object);

  switch (prop_id)
    {
    case PROP_SEAT:
      manager_xi2->seat = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static ClutterVirtualInputDevice *
meta_device_manager_x11_create_virtual_device (ClutterDeviceManager   *manager,
                                               ClutterInputDeviceType  device_type)
{
  return g_object_new (META_TYPE_VIRTUAL_INPUT_DEVICE_X11,
                       "device-manager", manager,
                       "device-type", device_type,
                       NULL);
}

static ClutterVirtualDeviceType
meta_device_manager_x11_get_supported_virtual_device_types (ClutterDeviceManager *device_manager)
{
  return (CLUTTER_VIRTUAL_DEVICE_TYPE_KEYBOARD |
          CLUTTER_VIRTUAL_DEVICE_TYPE_POINTER);
}

static void
meta_device_manager_x11_class_init (MetaDeviceManagerX11Class *klass)
{
  ClutterDeviceManagerClass *manager_class;
  GObjectClass *gobject_class;

  obj_props[PROP_SEAT] =
    g_param_spec_object ("seat",
                         "Seat",
                         "Seat",
                         CLUTTER_TYPE_SEAT,
                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->constructed = meta_device_manager_x11_constructed;
  gobject_class->set_property = meta_device_manager_x11_set_property;

  g_object_class_install_properties (gobject_class, PROP_LAST, obj_props);
  
  manager_class = CLUTTER_DEVICE_MANAGER_CLASS (klass);
  manager_class->add_device = meta_device_manager_x11_add_device;
  manager_class->remove_device = meta_device_manager_x11_remove_device;
  manager_class->get_devices = meta_device_manager_x11_get_devices;
  manager_class->get_core_device = meta_device_manager_x11_get_core_device;
  manager_class->get_device = meta_device_manager_x11_get_device;
  manager_class->select_stage_events = meta_device_manager_x11_select_stage_events;
  manager_class->create_virtual_device = meta_device_manager_x11_create_virtual_device;
  manager_class->get_supported_virtual_device_types = meta_device_manager_x11_get_supported_virtual_device_types;
  manager_class->apply_kbd_a11y_settings = meta_device_manager_x11_apply_kbd_a11y_settings;
}

static void
meta_device_manager_x11_init (MetaDeviceManagerX11 *self)
{
}
