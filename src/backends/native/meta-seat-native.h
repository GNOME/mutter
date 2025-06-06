/*
 * Copyright (C) 2010  Intel Corp.
 * Copyright (C) 2014  Jonas Ådahl
 * Copyright (C) 2016  Red Hat Inc.
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

#pragma once

#include <libinput.h>
#include <linux/input-event-codes.h>

#include "backends/meta-input-settings-private.h"
#include "backends/meta-viewport-info.h"
#include "backends/native/meta-backend-native-types.h"
#include "backends/native/meta-barrier-native.h"
#include "backends/native/meta-cursor-renderer-native.h"
#include "backends/native/meta-pointer-constraint-native.h"
#include "backends/native/meta-xkb-utils.h"
#include "clutter/clutter.h"
#include "core/util-private.h"

typedef struct _MetaSeatNative MetaSeatNative;

struct _MetaSeatNative
{
  ClutterSeat parent_instance;

  MetaBackend *backend;

  MetaSeatImpl *impl;
  char *seat_id;
  MetaSeatNativeFlag flags;

  GList *devices;
  struct xkb_keymap *xkb_keymap;
  xkb_layout_index_t xkb_layout_index;

  ClutterInputDevice *core_pointer;
  ClutterInputDevice *core_keyboard;

  guint virtual_touch_slot_base;
  GHashTable *reserved_virtual_slots;

  MetaKeymapNative *keymap;
  MetaCursorRenderer *cursor_renderer;
  GHashTable *tablet_cursors;

  gboolean released;
  gboolean touch_mode;
};

#define META_TYPE_SEAT_NATIVE meta_seat_native_get_type ()
META_EXPORT_TEST
G_DECLARE_FINAL_TYPE (MetaSeatNative, meta_seat_native,
                      META, SEAT_NATIVE, ClutterSeat)

void meta_seat_native_start (MetaSeatNative *seat_native);

void meta_seat_native_set_libinput_seat (MetaSeatNative       *seat,
                                         struct libinput_seat *libinput_seat);

void meta_seat_native_sync_leds (MetaSeatNative *seat);

/**
 * MetaOpenDeviceCallback:
 * @path: the device path
 * @flags: flags to be passed to open
 *
 * This callback will be called when Clutter needs to access an input
 * device. It should return an open file descriptor for the file at @path,
 * or -1 if opening failed.
 */
typedef int (* MetaOpenDeviceCallback) (const char  *path,
                                        int          flags,
                                        gpointer     user_data,
                                        GError     **error);
typedef void (* MetaCloseDeviceCallback) (int          fd,
                                          gpointer     user_data);

void  meta_seat_native_set_device_callbacks (MetaOpenDeviceCallback  open_callback,
                                             MetaCloseDeviceCallback close_callback,
                                             gpointer                user_data);

void  meta_seat_native_release_devices (MetaSeatNative *seat);
void  meta_seat_native_reclaim_devices (MetaSeatNative *seat);

void meta_seat_native_set_keyboard_map_async (MetaSeatNative      *seat,
                                              const char          *layouts,
                                              const char          *variants,
                                              const char          *options,
                                              const char          *model,
                                              GCancellable        *cancellable,
                                              GAsyncReadyCallback  callback,
                                              gpointer             user_data);

gboolean meta_seat_native_set_keyboard_map_finish (MetaSeatNative  *seat_native,
                                                   GAsyncResult    *result,
                                                   GError         **error);

META_EXPORT_TEST
struct xkb_keymap * meta_seat_native_get_keyboard_map (MetaSeatNative *seat);

gboolean meta_seat_native_set_keyboard_layout_index_finish (MetaSeatNative  *seat_native,
                                                            GAsyncResult    *result,
                                                            GError         **error);

void meta_seat_native_set_keyboard_layout_index_async (MetaSeatNative      *seat,
                                                       xkb_layout_index_t   idx,
                                                       GCancellable        *cancellable,
                                                       GAsyncReadyCallback  callback,
                                                       gpointer             user_data);

xkb_layout_index_t meta_seat_native_get_keyboard_layout_index (MetaSeatNative *seat);

void meta_seat_native_set_keyboard_repeat (MetaSeatNative *seat,
                                           gboolean        repeat,
                                           uint32_t        delay,
                                           uint32_t        interval);

void meta_seat_native_release_touch_slots (MetaSeatNative *seat,
                                           guint           base_slot);

MetaBarrierManagerNative * meta_seat_native_get_barrier_manager (MetaSeatNative *seat);

MetaBackend * meta_seat_native_get_backend (MetaSeatNative *seat);

void meta_seat_native_set_pointer_constraint (MetaSeatNative            *seat,
                                              MetaPointerConstraintImpl *constraint_impl);
MetaCursorRenderer * meta_seat_native_maybe_ensure_cursor_renderer (MetaSeatNative     *seat,
                                                                    ClutterInputDevice *device);

void meta_seat_native_set_viewports (MetaSeatNative   *seat,
                                     MetaViewportInfo *viewports);

void meta_seat_native_run_impl_task (MetaSeatNative *seat,
                                     GSourceFunc     dispatch_func,
                                     gpointer        user_data,
                                     GDestroyNotify  destroy_notify);

void meta_seat_native_set_a11y_modifiers (MetaSeatNative *seat,
                                          const uint32_t *modifiers,
                                          int             n_modifiers);
