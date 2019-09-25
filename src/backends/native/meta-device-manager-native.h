/*
 * Copyright (C) 2010  Intel Corp.
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
 */

#ifndef META_DEVICE_MANAGER_NATIVE_H
#define META_DEVICE_MANAGER_NATIVE_H

#include <libinput.h>
#include <xkbcommon/xkbcommon.h>

#include "backends/native/meta-seat-native.h"
#include "clutter/clutter-mutter.h"

#define META_TYPE_DEVICE_MANAGER_NATIVE            (meta_device_manager_native_get_type ())
#define META_DEVICE_MANAGER_NATIVE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_DEVICE_MANAGER_NATIVE, MetaDeviceManagerNative))
#define META_IS_DEVICE_MANAGER_NATIVE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_DEVICE_MANAGER_NATIVE))
#define META_DEVICE_MANAGER_NATIVE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), META_TYPE_DEVICE_MANAGER_NATIVE, MetaDeviceManagerNativeClass))
#define META_IS_DEVICE_MANAGER_NATIVE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), META_TYPE_DEVICE_MANAGER_NATIVE))
#define META_DEVICE_MANAGER_NATIVE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), META_TYPE_DEVICE_MANAGER_NATIVE, MetaDeviceManagerNativeClass))

typedef struct _MetaDeviceManagerNative         MetaDeviceManagerNative;
typedef struct _MetaDeviceManagerNativeClass    MetaDeviceManagerNativeClass;
typedef struct _MetaDeviceManagerNativePrivate  MetaDeviceManagerNativePrivate;

struct _MetaDeviceManagerNative
{
  ClutterDeviceManager parent_instance;

  MetaDeviceManagerNativePrivate *priv;
};

struct _MetaDeviceManagerNativeClass
{
  ClutterDeviceManagerClass parent_class;
};

GType meta_device_manager_native_get_type (void) G_GNUC_CONST;

MetaDeviceManagerNative * meta_device_manager_native_new (ClutterBackend *backend,
                                                          MetaSeatNative *seat);

struct xkb_state * meta_device_manager_native_get_xkb_state (MetaDeviceManagerNative *manager_evdev);

void               meta_device_manager_native_set_keyboard_map   (ClutterDeviceManager *evdev,
                                                                  struct xkb_keymap    *keymap);

struct xkb_keymap * meta_device_manager_native_get_keyboard_map (ClutterDeviceManager *evdev);

void meta_device_manager_native_set_keyboard_layout_index (ClutterDeviceManager *evdev,
                                                           xkb_layout_index_t    idx);

xkb_layout_index_t meta_device_manager_native_get_keyboard_layout_index (ClutterDeviceManager *evdev);

void meta_device_manager_native_set_keyboard_numlock (ClutterDeviceManager *evdev,
                                                      gboolean              numlock_state);

void meta_device_manager_native_set_keyboard_repeat (ClutterDeviceManager *evdev,
                                                     gboolean              repeat,
                                                     uint32_t              delay,
                                                     uint32_t              interval);

#endif /* META_DEVICE_MANAGER_NATIVE_H */
