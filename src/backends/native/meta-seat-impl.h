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

#ifndef META_SEAT_IMPL_H
#define META_SEAT_IMPL_H

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
  MetaSeatImpl *seat;

  int device_slot;
  int seat_slot;
  graphene_point_t coords;
};

struct _MetaSeatImpl
{
  GObject parent_instance;

  MetaSeatNative *seat;
  char *seat_id;
  MetaEventSource *event_source;
  struct libinput *libinput;
  struct libinput_seat *libinput_seat;
  GRWLock state_lock;

  GSList *devices;

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

  int device_id_next;
  GList *free_device_ids;

  MetaBarrierManagerNative *barrier_manager;
  MetaPointerConstraintImpl *pointer_constraint;

  MetaKeymapNative *keymap;
  MetaInputSettings *input_settings;

  MetaViewportInfo *viewports;

  GUdevClient *udev_client;
  guint tablet_mode_switch_state : 1;
  guint has_touchscreen          : 1;
  guint has_tablet_switch        : 1;
  guint touch_mode               : 1;

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

  gboolean released;
};

#define META_TYPE_SEAT_IMPL meta_seat_impl_get_type ()
G_DECLARE_FINAL_TYPE (MetaSeatImpl, meta_seat_impl,
                      META, SEAT_IMPL, GObject)

MetaSeatImpl * meta_seat_impl_new (MetaSeatNative *seat,
                                   const char     *seat_id);

void meta_seat_impl_notify_key (MetaSeatImpl       *seat,
                                ClutterInputDevice *device,
                                uint64_t            time_us,
                                uint32_t            key,
                                uint32_t            state,
                                gboolean            update_keys);

void meta_seat_impl_notify_relative_motion (MetaSeatImpl       *seat_evdev,
                                            ClutterInputDevice *input_device,
                                            uint64_t            time_us,
                                            float               dx,
                                            float               dy,
                                            float               dx_unaccel,
                                            float               dy_unaccel);

void meta_seat_impl_notify_absolute_motion (MetaSeatImpl       *seat_evdev,
                                            ClutterInputDevice *input_device,
                                            uint64_t            time_us,
                                            float               x,
                                            float               y,
                                            double             *axes);

void meta_seat_impl_notify_button (MetaSeatImpl       *seat,
                                   ClutterInputDevice *input_device,
                                   uint64_t            time_us,
                                   uint32_t            button,
                                   uint32_t            state);

void meta_seat_impl_notify_scroll_continuous (MetaSeatImpl             *seat,
                                              ClutterInputDevice       *input_device,
                                              uint64_t                  time_us,
                                              double                    dx,
                                              double                    dy,
                                              ClutterScrollSource       source,
                                              ClutterScrollFinishFlags  flags);

void meta_seat_impl_notify_discrete_scroll (MetaSeatImpl        *seat,
                                            ClutterInputDevice  *input_device,
                                            uint64_t             time_us,
                                            double               discrete_dx,
                                            double               discrete_dy,
                                            ClutterScrollSource  source);

void meta_seat_impl_notify_touch_event (MetaSeatImpl       *seat,
                                        ClutterInputDevice *input_device,
                                        ClutterEventType    evtype,
                                        uint64_t            time_us,
                                        int                 slot,
                                        double              x,
                                        double              y);

void meta_seat_impl_set_libinput_seat (MetaSeatImpl         *seat,
                                       struct libinput_seat *libinput_seat);

void meta_seat_impl_sync_leds (MetaSeatImpl *seat);

MetaTouchState * meta_seat_impl_acquire_touch_state (MetaSeatImpl   *seat,
                                                     int             seat_slot);
MetaTouchState * meta_seat_impl_lookup_touch_state  (MetaSeatImpl   *seat,
                                                     int             seat_slot);

void meta_seat_impl_release_touch_state (MetaSeatImpl   *seat,
                                         int             seat_slot);

int  meta_seat_impl_acquire_device_id (MetaSeatImpl       *seat);
void meta_seat_impl_release_device_id (MetaSeatImpl       *seat,
                                       ClutterInputDevice *device);

void meta_seat_impl_update_xkb_state (MetaSeatImpl *seat);

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

void  meta_seat_impl_set_device_callbacks (MetaOpenDeviceCallback  open_callback,
                                           MetaCloseDeviceCallback close_callback,
                                           gpointer                user_data);

void  meta_seat_impl_release_devices (MetaSeatImpl *seat);
void  meta_seat_impl_reclaim_devices (MetaSeatImpl *seat);

struct xkb_state * meta_seat_impl_get_xkb_state (MetaSeatImpl *seat);

void               meta_seat_impl_set_keyboard_map   (MetaSeatImpl      *seat,
                                                      struct xkb_keymap *keymap);

struct xkb_keymap * meta_seat_impl_get_keyboard_map (MetaSeatImpl *seat);

void meta_seat_impl_set_keyboard_layout_index (MetaSeatImpl       *seat,
                                               xkb_layout_index_t  idx);

xkb_layout_index_t meta_seat_impl_get_keyboard_layout_index (MetaSeatImpl *seat);

void meta_seat_impl_set_keyboard_numlock (MetaSeatImpl *seat,
                                          gboolean      numlock_state);

void meta_seat_impl_set_keyboard_repeat (MetaSeatImpl *seat,
                                         gboolean      repeat,
                                         uint32_t      delay,
                                         uint32_t      interval);

MetaBarrierManagerNative * meta_seat_impl_get_barrier_manager (MetaSeatImpl *seat);

void meta_seat_impl_set_pointer_constraint (MetaSeatImpl              *seat,
                                            MetaPointerConstraintImpl *impl);
void meta_seat_impl_set_viewports (MetaSeatImpl     *seat,
                                   MetaViewportInfo *viewports);

void meta_seat_impl_warp_pointer (MetaSeatImpl *seat,
                                  int           x,
                                  int           y);
ClutterVirtualInputDevice *
meta_seat_impl_create_virtual_device (MetaSeatImpl           *seat,
                                      ClutterInputDeviceType  device_type);
gboolean meta_seat_impl_query_state (MetaSeatImpl         *seat,
                                     ClutterInputDevice   *device,
                                     ClutterEventSequence *sequence,
                                     graphene_point_t     *coords,
                                     ClutterModifierType  *modifiers);
ClutterInputDevice * meta_seat_impl_get_pointer (MetaSeatImpl *seat);
ClutterInputDevice * meta_seat_impl_get_keyboard (MetaSeatImpl *seat);
GSList * meta_seat_impl_get_devices (MetaSeatImpl *seat);

MetaKeymapNative * meta_seat_impl_get_keymap (MetaSeatImpl *seat);

void meta_seat_impl_notify_kbd_a11y_flags_changed (MetaSeatImpl          *impl,
                                                   MetaKeyboardA11yFlags  new_flags,
                                                   MetaKeyboardA11yFlags  what_changed);
void meta_seat_impl_notify_kbd_a11y_mods_state_changed (MetaSeatImpl   *impl,
                                                        xkb_mod_mask_t  new_latched_mods,
                                                        xkb_mod_mask_t  new_locked_mods);
void meta_seat_impl_notify_bell (MetaSeatImpl *impl);

MetaInputSettings * meta_seat_impl_get_input_settings (MetaSeatImpl *impl);

#endif /* META_SEAT_IMPL_H */
