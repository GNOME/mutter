/*
 * Copyright (C) 2010  Intel Corp.
 * Copyright (C) 2014  Jonas Ådahl
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
 * Author: Damien Lespiau <damien.lespiau@intel.com>
 * Author: Jonas Ådahl <jadahl@gmail.com>
 */

#include "config.h"

#include <math.h>
#include <float.h>
#include <linux/input.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>

#include "backends/native/meta-device-manager-native.h"
#include "backends/native/meta-event-native.h"
#include "backends/native/meta-input-device-native.h"
#include "backends/native/meta-input-device-tool-native.h"
#include "backends/native/meta-keymap-native.h"
#include "backends/native/meta-seat-native.h"
#include "backends/native/meta-virtual-input-device-native.h"
#include "backends/native/meta-xkb-utils.h"
#include "clutter/clutter-mutter.h"

struct _MetaDeviceManagerNativePrivate
{
  MetaSeatNative *main_seat;
};

G_DEFINE_TYPE_WITH_CODE (MetaDeviceManagerNative,
                         meta_device_manager_native,
                         CLUTTER_TYPE_DEVICE_MANAGER,
                         G_ADD_PRIVATE (MetaDeviceManagerNative))

/*
 * ClutterDeviceManager implementation
 */

static void
meta_device_manager_native_add_device (ClutterDeviceManager *manager,
                                       ClutterInputDevice   *device)
{
}

static void
meta_device_manager_native_remove_device (ClutterDeviceManager *manager,
                                          ClutterInputDevice   *device)
{
}

static const GSList *
meta_device_manager_native_get_devices (ClutterDeviceManager *manager)
{
  ClutterBackend *clutter_backend = clutter_get_default_backend ();
  ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);
  GSList *retval = NULL;

  retval = g_slist_copy (META_SEAT_NATIVE (seat)->devices);
  retval = g_slist_prepend (retval, clutter_seat_get_pointer (seat));
  retval = g_slist_prepend (retval, clutter_seat_get_keyboard (seat));

  return retval;
}

static ClutterInputDevice *
meta_device_manager_native_get_core_device (ClutterDeviceManager   *manager,
                                            ClutterInputDeviceType  type)
{
  MetaDeviceManagerNative *manager_evdev;
  MetaDeviceManagerNativePrivate *priv;

  manager_evdev = META_DEVICE_MANAGER_NATIVE (manager);
  priv = manager_evdev->priv;

  switch (type)
    {
    case CLUTTER_POINTER_DEVICE:
      return priv->main_seat->core_pointer;

    case CLUTTER_KEYBOARD_DEVICE:
      return priv->main_seat->core_keyboard;

    case CLUTTER_EXTENSION_DEVICE:
    default:
      return NULL;
    }

  return NULL;
}

static ClutterInputDevice *
meta_device_manager_native_get_device (ClutterDeviceManager *manager,
                                       gint                  id)
{
  MetaDeviceManagerNative *manager_evdev;
  MetaSeatNative *seat;
  ClutterInputDevice *device;

  manager_evdev = META_DEVICE_MANAGER_NATIVE (manager);
  seat = manager_evdev->priv->main_seat;
  device = meta_seat_native_get_device (seat, id);

  if (device)
    return device;

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
meta_device_manager_native_constructed (GObject *object)
{
  MetaDeviceManagerNative *manager_native = META_DEVICE_MANAGER_NATIVE (object);

  g_signal_connect (manager_native->priv->main_seat, "device-added",
                    G_CALLBACK (on_device_added), manager_native);
  g_signal_connect (manager_native->priv->main_seat, "device-added",
                    G_CALLBACK (on_device_removed), manager_native);
  g_signal_connect (manager_native->priv->main_seat, "tool-changed",
                    G_CALLBACK (on_tool_changed), manager_native);

  if (G_OBJECT_CLASS (meta_device_manager_native_parent_class)->constructed)
    G_OBJECT_CLASS (meta_device_manager_native_parent_class)->constructed (object);
}

/*
 * GObject implementation
 */
static void
meta_device_manager_native_class_init (MetaDeviceManagerNativeClass *klass)
{
  ClutterDeviceManagerClass *manager_class;
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (klass);
  object_class->constructed = meta_device_manager_native_constructed;

  manager_class = CLUTTER_DEVICE_MANAGER_CLASS (klass);
  manager_class->add_device = meta_device_manager_native_add_device;
  manager_class->remove_device = meta_device_manager_native_remove_device;
  manager_class->get_devices = meta_device_manager_native_get_devices;
  manager_class->get_core_device = meta_device_manager_native_get_core_device;
  manager_class->get_device = meta_device_manager_native_get_device;
}

static void
meta_device_manager_native_init (MetaDeviceManagerNative *self)
{
}

MetaDeviceManagerNative *
meta_device_manager_native_new (ClutterBackend *backend,
                                MetaSeatNative *seat)
{
  MetaDeviceManagerNative *manager_native;

  manager_native = g_object_new (META_TYPE_DEVICE_MANAGER_NATIVE,
                                 "backend", backend,
                                 NULL);
  manager_native->priv->main_seat = seat;

  return manager_native;
}
