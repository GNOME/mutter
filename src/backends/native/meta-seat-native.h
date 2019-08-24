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

#ifndef META_SEAT_NATIVE_H
#define META_SEAT_NATIVE_H

#include <libinput.h>
#include <linux/input-event-codes.h>

#include "backends/native/meta-device-manager-native.h"
#include "backends/native/meta-xkb-utils.h"
#include "clutter/clutter.h"

typedef struct _MetaTouchState MetaTouchState;
typedef struct _MetaSeatNative MetaSeatNative;

struct _MetaTouchState
{
  MetaSeatNative *seat;

  int device_slot;
  int seat_slot;
  ClutterPoint coords;
};

struct _MetaSeatNative
{
  struct libinput_seat *libinput_seat;
  MetaDeviceManagerNative *manager_evdev;

  GSList *devices;

  ClutterInputDevice *core_pointer;
  ClutterInputDevice *core_keyboard;

  MetaTouchState **touch_states;
  int n_alloc_touch_states;

  struct xkb_state *xkb;
  xkb_led_index_t caps_lock_led;
  xkb_led_index_t num_lock_led;
  xkb_led_index_t scroll_lock_led;
  xkb_layout_index_t layout_idx;
  uint32_t button_state;
  int button_count[KEY_CNT];

  /* keyboard repeat */
  gboolean repeat;
  uint32_t repeat_delay;
  uint32_t repeat_interval;
  uint32_t repeat_key;
  uint32_t repeat_count;
  uint32_t repeat_timer;
  ClutterInputDevice *repeat_device;

  float pointer_x;
  float pointer_y;

  /* Emulation of discrete scroll events out of smooth ones */
  float accum_scroll_dx;
  float accum_scroll_dy;
};

void meta_seat_native_notify_key (MetaSeatNative     *seat,
                                  ClutterInputDevice *device,
                                  uint64_t            time_us,
                                  uint32_t            key,
                                  uint32_t            state,
                                  gboolean            update_keys);

void meta_seat_native_notify_relative_motion (MetaSeatNative     *seat_evdev,
                                              ClutterInputDevice *input_device,
                                              uint64_t            time_us,
                                              float               dx,
                                              float               dy,
                                              float               dx_unaccel,
                                              float               dy_unaccel);

void meta_seat_native_notify_absolute_motion (MetaSeatNative     *seat_evdev,
                                              ClutterInputDevice *input_device,
                                              uint64_t            time_us,
                                              float               x,
                                              float               y,
                                              double             *axes);

void meta_seat_native_notify_button (MetaSeatNative     *seat,
                                     ClutterInputDevice *input_device,
                                     uint64_t            time_us,
                                     uint32_t            button,
                                     uint32_t            state);

void meta_seat_native_notify_scroll_continuous (MetaSeatNative           *seat,
                                                ClutterInputDevice       *input_device,
                                                uint64_t                  time_us,
                                                double                    dx,
                                                double                    dy,
                                                ClutterScrollSource       source,
                                                ClutterScrollFinishFlags  flags);

void meta_seat_native_notify_discrete_scroll (MetaSeatNative      *seat,
                                              ClutterInputDevice  *input_device,
                                              uint64_t             time_us,
                                              double               discrete_dx,
                                              double               discrete_dy,
                                              ClutterScrollSource  source);

void meta_seat_native_notify_touch_event (MetaSeatNative     *seat,
                                          ClutterInputDevice *input_device,
                                          ClutterEventType    evtype,
                                          uint64_t            time_us,
                                          int                 slot,
                                          double              x,
                                          double              y);

void meta_seat_native_set_libinput_seat (MetaSeatNative       *seat,
                                         struct libinput_seat *libinput_seat);

void meta_seat_native_sync_leds (MetaSeatNative *seat);

ClutterInputDevice * meta_seat_native_get_device (MetaSeatNative *seat,
                                                  int             id);

MetaTouchState * meta_seat_native_acquire_touch_state (MetaSeatNative *seat,
                                                       int             device_slot);

void meta_seat_native_release_touch_state (MetaSeatNative *seat,
                                           MetaTouchState *touch_state);

MetaTouchState * meta_seat_native_get_touch (MetaSeatNative *seat,
                                             uint32_t        id);

void meta_seat_native_set_stage (MetaSeatNative *seat,
                                 ClutterStage   *stage);

void meta_seat_native_clear_repeat_timer (MetaSeatNative *seat);

MetaSeatNative * meta_seat_native_new (MetaDeviceManagerNative *manager_evdev);

void meta_seat_native_free (MetaSeatNative *seat);

#endif /* META_SEAT_NATIVE_H */
