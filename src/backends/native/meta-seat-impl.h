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

#ifndef META_INPUT_THREAD_H_INSIDE
#error "This header cannot be included directly. Use "backends/native/meta-input-thread.h""
#endif /* META_INPUT_THREAD_H_INSIDE */

#include <gudev/gudev.h>
#include <libinput.h>
#include <linux/input-event-codes.h>

#include "backends/meta-input-settings-private.h"
#include "backends/meta-viewport-info.h"
#include "backends/native/meta-backend-native-types.h"
#include "backends/native/meta-barrier-native.h"
#include "backends/native/meta-cursor-renderer-native.h"
#include "backends/native/meta-keymap-native.h"
#include "backends/native/meta-pointer-constraint-native.h"
#include "backends/native/meta-xkb-utils.h"
#include "clutter/clutter.h"

typedef struct _MetaTouchState MetaTouchState;
typedef struct _MetaSeatImpl MetaSeatImpl;
typedef struct _MetaEventSource  MetaEventSource;

struct _MetaTouchState
{
  MetaSeatImpl *seat_impl;

  int device_slot;
  int seat_slot;
  graphene_point_t coords;
};

struct _MetaSeatImpl
{
  GObject parent_instance;

  GMainContext *main_context;
  GMainContext *input_context;
  GMainLoop *input_loop;
  GThread *input_thread;
  GMutex init_mutex;
  GCond init_cond;

  MetaSeatNative *seat_native;
  char *seat_id;
  MetaSeatNativeFlag flags;
  GSource *libinput_source;
  struct libinput *libinput;
  GRWLock state_lock;

  GSList *devices;
  GHashTable *tools;

  ClutterInputDevice *core_pointer;
  ClutterInputDevice *core_keyboard;

  GHashTable *touch_states;
  GHashTable *cursor_renderers;

  struct xkb_state *xkb;
  xkb_led_index_t caps_lock_led;
  xkb_led_index_t num_lock_led;
  xkb_led_index_t scroll_lock_led;
  xkb_layout_index_t layout_idx;
  uint32_t button_state;
  int button_count[KEY_CNT];

  MetaBarrierManagerNative *barrier_manager;
  MetaPointerConstraintImpl *pointer_constraint;

  MetaKeymapNative *keymap;
  MetaInputSettings *input_settings;

  MetaViewportInfo *viewports;

  gboolean tablet_mode_switch_state;
  gboolean has_touchscreen;
  gboolean has_tablet_switch;
  gboolean has_pointer;
  gboolean touch_mode;
  gboolean input_thread_initialized;

  /* keyboard repeat */
  gboolean repeat;
  uint32_t repeat_delay;
  uint32_t repeat_interval;
  uint32_t repeat_key;
  uint32_t repeat_count;
  ClutterInputDevice *repeat_device;
  GSource *repeat_source;

  float pointer_x;
  float pointer_y;

  /* Emulation of discrete scroll events out of smooth ones */
  float accum_scroll_dx;
  float accum_scroll_dy;

  gboolean released;
};

#define META_TYPE_SEAT_IMPL meta_seat_impl_get_type ()
G_DECLARE_FINAL_TYPE (MetaSeatImpl, meta_seat_impl,
                      META, SEAT_IMPL, GObject)

MetaSeatImpl * meta_seat_impl_new (MetaSeatNative     *seat_native,
                                   const char         *seat_id,
                                   MetaSeatNativeFlag  flags);

void meta_seat_impl_start (MetaSeatImpl *seat_impl);

void meta_seat_impl_destroy (MetaSeatImpl *seat_impl);

META_EXPORT_TEST
void meta_seat_impl_run_input_task (MetaSeatImpl *seat_impl,
                                    GTask        *task,
                                    GSourceFunc   dispatch_func);

void meta_seat_impl_notify_key_in_impl (MetaSeatImpl       *seat_impl,
                                        ClutterInputDevice *device,
                                        uint64_t            time_us,
                                        uint32_t            key,
                                        uint32_t            state,
                                        gboolean            update_keys);

void meta_seat_impl_notify_relative_motion_in_impl (MetaSeatImpl       *seat_impl,
                                                    ClutterInputDevice *input_device,
                                                    uint64_t            time_us,
                                                    float               dx,
                                                    float               dy,
                                                    float               dx_unaccel,
                                                    float               dy_unaccel,
                                                    double             *axes);

void meta_seat_impl_notify_absolute_motion_in_impl (MetaSeatImpl       *seat_impl,
                                                    ClutterInputDevice *input_device,
                                                    uint64_t            time_us,
                                                    float               x,
                                                    float               y,
                                                    double             *axes);

void meta_seat_impl_notify_button_in_impl (MetaSeatImpl       *seat_impl,
                                           ClutterInputDevice *input_device,
                                           uint64_t            time_us,
                                           uint32_t            button,
                                           uint32_t            state);

void meta_seat_impl_notify_scroll_continuous_in_impl (MetaSeatImpl             *seat_impl,
                                                      ClutterInputDevice       *input_device,
                                                      uint64_t                  time_us,
                                                      double                    dx,
                                                      double                    dy,
                                                      ClutterScrollSource       source,
                                                      ClutterScrollFinishFlags  flags);

void meta_seat_impl_notify_discrete_scroll_in_impl (MetaSeatImpl        *seat_impl,
                                                    ClutterInputDevice  *input_device,
                                                    uint64_t             time_us,
                                                    double               dx_value120,
                                                    double               dy_value120,
                                                    ClutterScrollSource  source);

void meta_seat_impl_notify_touch_event_in_impl (MetaSeatImpl       *seat_impl,
                                                ClutterInputDevice *input_device,
                                                ClutterEventType    evtype,
                                                uint64_t            time_us,
                                                int                 slot,
                                                double              x,
                                                double              y);

void meta_seat_impl_sync_leds_in_impl (MetaSeatImpl *seat_impl);

MetaTouchState * meta_seat_impl_acquire_touch_state_in_impl (MetaSeatImpl *seat_impl,
                                                             int           seat_slot);
MetaTouchState * meta_seat_impl_lookup_touch_state_in_impl (MetaSeatImpl *seat_impl,
                                                            int           seat_slot);
void meta_seat_impl_release_touch_state_in_impl (MetaSeatImpl   *seat_impl,
                                                 int             seat_slot);

void meta_seat_impl_update_xkb_state_in_impl (MetaSeatImpl *seat_impl);

void  meta_seat_impl_release_devices (MetaSeatImpl *seat_impl);
void  meta_seat_impl_reclaim_devices (MetaSeatImpl *seat_impl);

struct xkb_state * meta_seat_impl_get_xkb_state_in_impl (MetaSeatImpl *seat_impl);

void meta_seat_impl_set_keyboard_map (MetaSeatImpl      *seat_impl,
                                      struct xkb_keymap *keymap);

void meta_seat_impl_set_keyboard_layout_index (MetaSeatImpl       *seat_impl,
                                               xkb_layout_index_t  idx);

void meta_seat_impl_set_keyboard_repeat_in_impl (MetaSeatImpl *seat_impl,
                                                 gboolean      repeat,
                                                 uint32_t      delay,
                                                 uint32_t      interval);

MetaBarrierManagerNative * meta_seat_impl_get_barrier_manager (MetaSeatImpl *seat_impl);

void meta_seat_impl_set_pointer_constraint (MetaSeatImpl              *seat_impl,
                                            MetaPointerConstraintImpl *constraint_impl);
void meta_seat_impl_set_viewports (MetaSeatImpl     *seat_impl,
                                   MetaViewportInfo *viewports);

void meta_seat_impl_warp_pointer (MetaSeatImpl *seat_impl,
                                  int           x,
                                  int           y);
void meta_seat_impl_init_pointer_position (MetaSeatImpl *seat_impl,
                                           float         x,
                                           float         y);
gboolean meta_seat_impl_query_state (MetaSeatImpl         *seat_impl,
                                     ClutterInputDevice   *device,
                                     ClutterEventSequence *sequence,
                                     graphene_point_t     *coords,
                                     ClutterModifierType  *modifiers);
ClutterInputDevice * meta_seat_impl_get_pointer (MetaSeatImpl *seat_impl);
ClutterInputDevice * meta_seat_impl_get_keyboard (MetaSeatImpl *seat_impl);

MetaKeymapNative * meta_seat_impl_get_keymap (MetaSeatImpl *seat_impl);

void meta_seat_impl_notify_kbd_a11y_flags_changed_in_impl (MetaSeatImpl          *seat_impl,
                                                           MetaKeyboardA11yFlags  new_flags,
                                                           MetaKeyboardA11yFlags  what_changed);
void meta_seat_impl_notify_kbd_a11y_mods_state_changed_in_impl (MetaSeatImpl   *seat_impl,
                                                                xkb_mod_mask_t  new_latched_mods,
                                                                xkb_mod_mask_t  new_locked_mods);
void meta_seat_impl_notify_bell_in_impl (MetaSeatImpl *seat_impl);

MetaInputSettings * meta_seat_impl_get_input_settings (MetaSeatImpl *seat_impl);

void meta_seat_impl_queue_main_thread_idle (MetaSeatImpl   *seat_impl,
                                            GSourceFunc     func,
                                            gpointer        user_data,
                                            GDestroyNotify  destroy_notify);

MetaBackend * meta_seat_impl_get_backend (MetaSeatImpl *seat_impl);
