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

static void
meta_device_manager_native_copy_event_data (ClutterDeviceManager *device_manager,
                                            const ClutterEvent   *src,
                                            ClutterEvent         *dest)
{
  MetaEventNative *event_evdev;

  event_evdev = _clutter_event_get_platform_data (src);
  if (event_evdev != NULL)
    _clutter_event_set_platform_data (dest, meta_event_native_copy (event_evdev));
}

static void
meta_device_manager_native_free_event_data (ClutterDeviceManager *device_manager,
                                            ClutterEvent         *event)
{
  MetaEventNative *event_evdev;

  event_evdev = _clutter_event_get_platform_data (event);
  if (event_evdev != NULL)
    meta_event_native_free (event_evdev);
}

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
  manager_class->create_virtual_device = meta_device_manager_native_create_virtual_device;
  manager_class->get_supported_virtual_device_types = meta_device_manager_native_get_supported_virtual_device_types;
  manager_class->compress_motion = meta_device_manager_native_compress_motion;
  manager_class->apply_kbd_a11y_settings = meta_device_manager_native_apply_kbd_a11y_settings;
  manager_class->copy_event_data = meta_device_manager_native_copy_event_data;
  manager_class->free_event_data = meta_device_manager_native_free_event_data;
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

/**
 * meta_device_manager_native_set_keyboard_map: (skip)
 * @evdev: the #ClutterDeviceManager created by the evdev backend
 * @keymap: the new keymap
 *
 * Instructs @evdev to use the speficied keyboard map. This will cause
 * the backend to drop the state and create a new one with the new
 * map. To avoid state being lost, callers should ensure that no key
 * is pressed when calling this function.
 *
 * Since: 1.16
 * Stability: unstable
 */
void
meta_device_manager_native_set_keyboard_map (ClutterDeviceManager *evdev,
                                             struct xkb_keymap    *xkb_keymap)
{
  MetaDeviceManagerNative *manager_evdev;
  ClutterKeymap *keymap;

  g_return_if_fail (META_IS_DEVICE_MANAGER_NATIVE (evdev));

  manager_evdev = META_DEVICE_MANAGER_NATIVE (evdev);
  keymap = clutter_seat_get_keymap (CLUTTER_SEAT (manager_evdev->priv->main_seat));
  meta_keymap_native_set_keyboard_map (META_KEYMAP_NATIVE (keymap),
                                       xkb_keymap);

  meta_seat_native_update_xkb_state (manager_evdev->priv->main_seat);
}

/**
 * meta_device_manager_native_get_keyboard_map: (skip)
 * @evdev: the #ClutterDeviceManager created by the evdev backend
 *
 * Retrieves the #xkb_keymap in use by the evdev backend.
 *
 * Return value: the #xkb_keymap.
 *
 * Since: 1.18
 * Stability: unstable
 */
struct xkb_keymap *
meta_device_manager_native_get_keyboard_map (ClutterDeviceManager *evdev)
{
  MetaDeviceManagerNative *manager_evdev;

  g_return_val_if_fail (META_IS_DEVICE_MANAGER_NATIVE (evdev), NULL);

  manager_evdev = META_DEVICE_MANAGER_NATIVE (evdev);

  return xkb_state_get_keymap (manager_evdev->priv->main_seat->xkb);
}

/**
 * meta_device_manager_set_keyboard_layout_index: (skip)
 * @evdev: the #ClutterDeviceManager created by the evdev backend
 * @idx: the xkb layout index to set
 *
 * Sets the xkb layout index on the backend's #xkb_state .
 *
 * Since: 1.20
 * Stability: unstable
 */
void
meta_device_manager_native_set_keyboard_layout_index (ClutterDeviceManager *evdev,
                                                      xkb_layout_index_t    idx)
{
  MetaDeviceManagerNative *manager_evdev;
  MetaSeatNative *seat;
  xkb_mod_mask_t depressed_mods;
  xkb_mod_mask_t latched_mods;
  xkb_mod_mask_t locked_mods;
  struct xkb_state *state;

  g_return_if_fail (META_IS_DEVICE_MANAGER_NATIVE (evdev));

  manager_evdev = META_DEVICE_MANAGER_NATIVE (evdev);
  state = manager_evdev->priv->main_seat->xkb;

  depressed_mods = xkb_state_serialize_mods (state, XKB_STATE_MODS_DEPRESSED);
  latched_mods = xkb_state_serialize_mods (state, XKB_STATE_MODS_LATCHED);
  locked_mods = xkb_state_serialize_mods (state, XKB_STATE_MODS_LOCKED);

  xkb_state_update_mask (state, depressed_mods, latched_mods, locked_mods, 0, 0, idx);

  seat = manager_evdev->priv->main_seat;
  seat->layout_idx = idx;
}

/**
 * clutter_evdev_get_keyboard_layout_index: (skip)
 */
xkb_layout_index_t
meta_device_manager_native_get_keyboard_layout_index (ClutterDeviceManager *evdev)
{
  MetaDeviceManagerNative *manager_evdev;

  manager_evdev = META_DEVICE_MANAGER_NATIVE (evdev);
  return manager_evdev->priv->main_seat->layout_idx;
}

/**
 * meta_device_manager_native_set_keyboard_numlock: (skip)
 * @evdev: the #ClutterDeviceManager created by the evdev backend
 * @numlock_set: TRUE to set NumLock ON, FALSE otherwise.
 *
 * Sets the NumLock state on the backend's #xkb_state .
 *
 * Stability: unstable
 */
void
meta_device_manager_native_set_keyboard_numlock (ClutterDeviceManager *evdev,
                                                 gboolean              numlock_state)
{
  MetaDeviceManagerNative *manager_evdev;
  MetaDeviceManagerNativePrivate *priv;
  MetaSeatNative *seat;
  xkb_mod_mask_t depressed_mods;
  xkb_mod_mask_t latched_mods;
  xkb_mod_mask_t locked_mods;
  xkb_mod_mask_t group_mods;
  xkb_mod_mask_t numlock;
  struct xkb_keymap *xkb_keymap;
  ClutterKeymap *keymap;

  g_return_if_fail (META_IS_DEVICE_MANAGER_NATIVE (evdev));

  manager_evdev = META_DEVICE_MANAGER_NATIVE (evdev);
  priv = manager_evdev->priv;

  keymap = clutter_seat_get_keymap (CLUTTER_SEAT (priv->main_seat));
  xkb_keymap = meta_keymap_native_get_keyboard_map (META_KEYMAP_NATIVE (keymap));

  numlock = (1 << xkb_keymap_mod_get_index (xkb_keymap, "Mod2"));

  seat = priv->main_seat;

  depressed_mods = xkb_state_serialize_mods (seat->xkb, XKB_STATE_MODS_DEPRESSED);
  latched_mods = xkb_state_serialize_mods (seat->xkb, XKB_STATE_MODS_LATCHED);
  locked_mods = xkb_state_serialize_mods (seat->xkb, XKB_STATE_MODS_LOCKED);
  group_mods = xkb_state_serialize_layout (seat->xkb, XKB_STATE_LAYOUT_EFFECTIVE);

  if (numlock_state)
    locked_mods |= numlock;
  else
    locked_mods &= ~numlock;

  xkb_state_update_mask (seat->xkb,
                         depressed_mods,
                         latched_mods,
                         locked_mods,
                         0, 0,
                         group_mods);

  meta_seat_native_sync_leds (seat);
}

/**
 * meta_device_manager_native_set_keyboard_repeat:
 * @evdev: the #ClutterDeviceManager created by the evdev backend
 * @repeat: whether to enable or disable keyboard repeat events
 * @delay: the delay in ms between the hardware key press event and
 * the first synthetic event
 * @interval: the period in ms between consecutive synthetic key
 * press events
 *
 * Enables or disables sythetic key press events, allowing for initial
 * delay and interval period to be specified.
 *
 * Since: 1.18
 * Stability: unstable
 */
void
meta_device_manager_native_set_keyboard_repeat (ClutterDeviceManager *evdev,
                                                gboolean              repeat,
                                                uint32_t              delay,
                                                uint32_t              interval)
{
  MetaDeviceManagerNative *manager_evdev;
  MetaSeatNative *seat;

  g_return_if_fail (META_IS_DEVICE_MANAGER_NATIVE (evdev));

  manager_evdev = META_DEVICE_MANAGER_NATIVE (evdev);
  seat = manager_evdev->priv->main_seat;

  seat->repeat = repeat;
  seat->repeat_delay = delay;
  seat->repeat_interval = interval;
}

struct xkb_state *
meta_device_manager_native_get_xkb_state (MetaDeviceManagerNative *manager_evdev)
{
  return manager_evdev->priv->main_seat->xkb;
}
