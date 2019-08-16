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

gint  meta_device_manager_native_acquire_device_id (MetaDeviceManagerNative *manager_evdev);

void  meta_device_manager_native_release_device_id (MetaDeviceManagerNative *manager_evdev,
                                                    ClutterInputDevice      *device);

ClutterStage * meta_device_manager_native_get_stage (MetaDeviceManagerNative *manager_evdev);

void meta_device_manager_native_constrain_pointer (MetaDeviceManagerNative *manager_evdev,
                                                   ClutterInputDevice      *core_pointer,
                                                   uint64_t                 time_us,
                                                   float                    x,
                                                   float                    y,
                                                   float                   *new_x,
                                                   float                   *new_y);

void meta_device_manager_native_filter_relative_motion (MetaDeviceManagerNative *manager_evdev,
                                                        ClutterInputDevice      *device,
                                                        float                    x,
                                                        float                    y,
                                                        float                   *dx,
                                                        float                   *dy);

void meta_device_manager_native_dispatch (MetaDeviceManagerNative *manager_evdev);

struct xkb_state * meta_device_manager_native_get_xkb_state (MetaDeviceManagerNative *manager_evdev);

static inline uint64_t
us (uint64_t us)
{
  return us;
}

static inline uint64_t
ms2us (uint64_t ms)
{
  return us (ms * 1000);
}

static inline uint32_t
us2ms (uint64_t us)
{
  return (uint32_t) (us / 1000);
}

/**
 * MetaOpenDeviceCallback:
 * @path: the device path
 * @flags: flags to be passed to open
 *
 * This callback will be called when Clutter needs to access an input
 * device. It should return an open file descriptor for the file at @path,
 * or -1 if opening failed.
 */
typedef int (*MetaOpenDeviceCallback) (const char  *path,
                                       int          flags,
                                       gpointer     user_data,
                                       GError     **error);
typedef void (*MetaCloseDeviceCallback) (int          fd,
                                         gpointer     user_data);

void  meta_device_manager_native_set_device_callbacks (MetaOpenDeviceCallback  open_callback,
                                                       MetaCloseDeviceCallback close_callback,
                                                       gpointer                user_data);

void  meta_device_manager_native_set_seat_id (const gchar *seat_id);

void  meta_device_manager_native_release_devices (void);
void  meta_device_manager_native_reclaim_devices (void);

/**
 * MetaPointerConstrainCallback:
 * @device: the core pointer device
 * @time: the event time in milliseconds
 * @x: (inout): the new X coordinate
 * @y: (inout): the new Y coordinate
 * @user_data: user data passed to this function
 *
 * This callback will be called for all pointer motion events, and should
 * update (@x, @y) to constrain the pointer position appropriately.
 * The subsequent motion event will use the updated values as the new coordinates.
 * Note that the coordinates are not clamped to the stage size, and the callback
 * must make sure that this happens before it returns.
 * Also note that the event will be emitted even if the pointer is constrained
 * to be in the same position.
 */
typedef void (* MetaPointerConstrainCallback) (ClutterInputDevice *device,
                                               uint32_t            time,
                                               float               prev_x,
                                               float               prev_y,
                                               float              *x,
                                               float              *y,
                                               gpointer            user_data);

void  meta_device_manager_native_set_pointer_constrain_callback (ClutterDeviceManager         *evdev,
                                                                 MetaPointerConstrainCallback  callback,
                                                                 gpointer                      user_data,
                                                                 GDestroyNotify                user_data_notify);

typedef void (* MetaRelativeMotionFilter) (ClutterInputDevice *device,
                                           float               x,
                                           float               y,
                                           float              *dx,
                                           float              *dy,
                                           gpointer            user_data);

void meta_device_manager_native_set_relative_motion_filter (ClutterDeviceManager     *evdev,
                                                            MetaRelativeMotionFilter  filter,
                                                            gpointer                  user_data);

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

typedef gboolean (* MetaEvdevFilterFunc) (struct libinput_event *event,
                                          gpointer               data);

void meta_device_manager_native_add_filter    (MetaEvdevFilterFunc    func,
                                               gpointer               data,
                                               GDestroyNotify         destroy_notify);
void meta_device_manager_native_remove_filter (MetaEvdevFilterFunc    func,
                                               gpointer               data);

void meta_device_manager_native_warp_pointer (ClutterInputDevice   *pointer_device,
                                              uint32_t              time_,
                                              int                   x,
                                              int                   y);

#endif /* META_DEVICE_MANAGER_NATIVE_H */
