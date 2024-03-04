/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <libinput.h>
#include <linux/input.h>
#include <math.h>

#include "backends/meta-cursor-tracker-private.h"
#include "backends/meta-fd-source.h"
#include "backends/native/meta-backend-native-private.h"
#include "backends/native/meta-barrier-native.h"
#include "backends/native/meta-device-pool.h"
#include "backends/native/meta-input-thread.h"
#include "backends/native/meta-virtual-input-device-native.h"
#include "clutter/clutter-mutter.h"
#include "core/bell.h"

#include "meta-private-enum-types.h"

/*
 * Clutter makes the assumption that two core devices have ID's 2 and 3 (core
 * pointer and core keyboard).
 *
 * Since the two first devices that will ever be created will be the virtual
 * pointer and virtual keyboard of the first seat, we fulfill the made
 * assumptions by having the first device having ID 2 and following 3.
 */
#define INITIAL_DEVICE_ID 2

/* Try to keep the pointer inside the stage. Hopefully no one is using
 * this backend with stages smaller than this. */
#define INITIAL_POINTER_X 16
#define INITIAL_POINTER_Y 16

#define AUTOREPEAT_VALUE 2

#define DISCRETE_SCROLL_STEP 10.0

#ifndef BTN_STYLUS3
#define BTN_STYLUS3 0x149 /* Linux 4.15 */
#endif

struct _MetaEventSource
{
  GSource source;

  MetaSeatImpl *seat_impl;
  GPollFD event_poll_fd;
};

enum
{
  PROP_0,
  PROP_SEAT,
  PROP_SEAT_ID,
  PROP_FLAGS,
  N_PROPS,
};

static GParamSpec *props[N_PROPS] = { NULL };

enum
{
  KBD_A11Y_FLAGS_CHANGED,
  KBD_A11Y_MODS_STATE_CHANGED,
  TOUCH_MODE,
  BELL,
  MODS_STATE_CHANGED,
  POINTER_POSITION_CHANGED_IN_IMPL,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

typedef struct _MetaSeatImplPrivate
{
  GHashTable *device_files;
} MetaSeatImplPrivate;

static void meta_seat_impl_initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaSeatImpl, meta_seat_impl, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                meta_seat_impl_initable_iface_init)
                         G_ADD_PRIVATE (MetaSeatImpl))

static void process_events (MetaSeatImpl *seat_impl);
void meta_seat_impl_constrain_pointer (MetaSeatImpl       *seat_impl,
                                       ClutterInputDevice *core_pointer,
                                       uint64_t            time_us,
                                       float               x,
                                       float               y,
                                       float              *new_x,
                                       float              *new_y);
void meta_seat_impl_filter_relative_motion (MetaSeatImpl       *seat_impl,
                                            ClutterInputDevice *device,
                                            float               x,
                                            float               y,
                                            float              *dx,
                                            float              *dy);
void meta_seat_impl_clear_repeat_source (MetaSeatImpl *seat_impl);

void
meta_seat_impl_run_input_task (MetaSeatImpl *seat_impl,
                               GTask        *task,
                               GSourceFunc   dispatch_func)
{
  GSource *source;

  source = g_idle_source_new ();
  g_source_set_priority (source, G_PRIORITY_HIGH);
  g_source_set_callback (source,
                         dispatch_func,
                         g_object_ref (task),
                         g_object_unref);
  g_source_attach (source, seat_impl->input_context);
  g_source_unref (source);
}

void
meta_seat_impl_sync_leds_in_impl (MetaSeatImpl *seat_impl)
{
  GSList *iter;
  MetaInputDeviceNative *device_native;
  int caps_lock, num_lock, scroll_lock;
  enum libinput_led leds = 0;

  caps_lock = xkb_state_led_index_is_active (seat_impl->xkb,
                                             seat_impl->caps_lock_led);
  num_lock = xkb_state_led_index_is_active (seat_impl->xkb,
                                            seat_impl->num_lock_led);
  scroll_lock = xkb_state_led_index_is_active (seat_impl->xkb,
                                               seat_impl->scroll_lock_led);

  if (caps_lock)
    leds |= LIBINPUT_LED_CAPS_LOCK;
  if (num_lock)
    leds |= LIBINPUT_LED_NUM_LOCK;
  if (scroll_lock)
    leds |= LIBINPUT_LED_SCROLL_LOCK;

  for (iter = seat_impl->devices; iter; iter = iter->next)
    {
      device_native = iter->data;
      meta_input_device_native_update_leds_in_impl (device_native, leds);
    }
}

MetaTouchState *
meta_seat_impl_lookup_touch_state_in_impl (MetaSeatImpl *seat_impl,
                                           int           seat_slot)
{
  if (!seat_impl->touch_states)
    return NULL;

  return g_hash_table_lookup (seat_impl->touch_states,
                              GINT_TO_POINTER (seat_slot));
}

static void
meta_touch_state_free (MetaTouchState *state)
{
  g_free (state);
}

MetaTouchState *
meta_seat_impl_acquire_touch_state_in_impl (MetaSeatImpl *seat_impl,
                                            int           seat_slot)
{
  MetaTouchState *touch_state;

  if (!seat_impl->touch_states)
    {
      seat_impl->touch_states =
        g_hash_table_new_full (NULL, NULL, NULL,
                               (GDestroyNotify) meta_touch_state_free);
    }

  g_assert (!g_hash_table_contains (seat_impl->touch_states,
                                    GINT_TO_POINTER (seat_slot)));

  touch_state = g_new0 (MetaTouchState, 1);
  *touch_state = (MetaTouchState) {
    .seat_impl = seat_impl,
    .seat_slot = seat_slot,
  };

  g_hash_table_insert (seat_impl->touch_states, GINT_TO_POINTER (seat_slot),
                       touch_state);

  return touch_state;
}

void
meta_seat_impl_release_touch_state_in_impl (MetaSeatImpl *seat_impl,
                                            int           seat_slot)
{
  if (!seat_impl->touch_states)
    return;
  g_hash_table_remove (seat_impl->touch_states, GINT_TO_POINTER (seat_slot));
}

void
meta_seat_impl_clear_repeat_source (MetaSeatImpl *seat_impl)
{
  if (seat_impl->repeat_source)
    {
      g_source_destroy (seat_impl->repeat_source);
      g_clear_pointer (&seat_impl->repeat_source, g_source_unref);
    }

  g_clear_object (&seat_impl->repeat_device);
}

static void
dispatch_libinput (MetaSeatImpl *seat_impl)
{
  COGL_TRACE_BEGIN_SCOPED (MetaSeatImplDispatchLibinput,
                           "Meta::SeatImpl::dispatch_libinput()");

  libinput_dispatch (seat_impl->libinput);

  process_events (seat_impl);
}

static gboolean
keyboard_repeat (gpointer data)
{
  MetaSeatImpl *seat_impl = data;

  /* There might be events queued in libinput that could cancel the
     repeat timer. */
  if (seat_impl->libinput)
    {
      dispatch_libinput (seat_impl);
      if (!seat_impl->repeat_source)
        return G_SOURCE_REMOVE;
    }

  g_return_val_if_fail (seat_impl->repeat_device != NULL, G_SOURCE_REMOVE);

  meta_seat_impl_notify_key_in_impl (seat_impl,
                                     seat_impl->repeat_device,
                                     g_source_get_time (seat_impl->repeat_source),
                                     seat_impl->repeat_key,
                                     AUTOREPEAT_VALUE,
                                     FALSE);

  return G_SOURCE_CONTINUE;
}

static void
queue_event (MetaSeatImpl *seat_impl,
             ClutterEvent *event)
{
#ifdef WITH_VERBOSE_MODE
  if (meta_is_topic_enabled (META_DEBUG_INPUT_EVENTS))
    {
      g_autofree char *event_description = NULL;

      event_description = clutter_event_describe (event);
      meta_topic (META_DEBUG_INPUT_EVENTS,
                  "Queuing %s",
                  event_description);
    }
#endif

  _clutter_event_push (event, FALSE);
}

static int
update_button_count (MetaSeatImpl *seat_impl,
                     uint32_t      button,
                     uint32_t      state)
{
  if (state)
    {
      return ++seat_impl->button_count[button];
    }
  else
    {
      /* Handle cases where we newer saw the initial pressed event. */
      if (seat_impl->button_count[button] == 0)
        {
          meta_topic (META_DEBUG_INPUT,
                      "Counting release of key 0x%x and count is already 0",
                      button);
          return 0;
        }

      return --seat_impl->button_count[button];
    }
}

void
meta_seat_impl_queue_main_thread_idle (MetaSeatImpl   *seat_impl,
                                       GSourceFunc     func,
                                       gpointer        user_data,
                                       GDestroyNotify  destroy_notify)
{
  GSource *source;

  source = g_idle_source_new ();
  g_source_set_priority (source, G_PRIORITY_HIGH);
  g_source_set_callback (source, func, user_data, destroy_notify);

  g_source_attach (source, seat_impl->main_context);
  g_source_unref (source);
}

typedef struct
{
  MetaSeatImpl *seat_impl;
  guint signal_id;
  GArray *args;
} MetaSeatSignalData;

static gboolean
emit_signal_in_main (MetaSeatSignalData *data)
{
  g_signal_emitv ((GValue *) data->args->data,
                  data->signal_id,
                  0, NULL);

  return G_SOURCE_REMOVE;
}

static void
signal_data_free (MetaSeatSignalData *data)
{
  g_array_unref (data->args);
  g_free (data);
}

static void
emit_signal (MetaSeatImpl *seat_impl,
             guint         signal_id,
             GValue       *args,
             int           n_args)
{
  MetaSeatSignalData *emit_signal_data;
  GArray *array;
  GValue self = G_VALUE_INIT;

  g_value_init (&self, META_TYPE_SEAT_IMPL);
  g_value_set_object (&self, seat_impl);

  array = g_array_new (FALSE, FALSE, sizeof (GValue));
  g_array_append_val (array, self);
  if (args && n_args > 0)
    g_array_append_vals (array, args, n_args);

  emit_signal_data = g_new0 (MetaSeatSignalData, 1);
  emit_signal_data->seat_impl = seat_impl;
  emit_signal_data->signal_id = signal_id;
  emit_signal_data->args = array;

  meta_seat_impl_queue_main_thread_idle (seat_impl,
                                         (GSourceFunc) emit_signal_in_main,
                                         emit_signal_data,
                                         (GDestroyNotify) signal_data_free);
}

void
meta_seat_impl_notify_key_in_impl (MetaSeatImpl       *seat_impl,
                                   ClutterInputDevice *device,
                                   uint64_t            time_us,
                                   uint32_t            key,
                                   uint32_t            state,
                                   gboolean            update_keys)
{
  ClutterEvent *event = NULL;
  ClutterEventFlags flags = CLUTTER_EVENT_NONE;
  enum xkb_state_component changed_state;
  uint32_t keycode;

  if (state != AUTOREPEAT_VALUE)
    {
      /* Drop any repeated button press (for example from virtual devices. */
      int count = update_button_count (seat_impl, key, state);
      if ((state && count > 1) ||
          (!state && count != 0))
        {
          meta_topic (META_DEBUG_INPUT,
                      "Dropping repeated %s of key 0x%x, count %d, state %d",
                      state ? "press" : "release", key, count, state);
          return;
        }
    }
  else
    {
      changed_state = 0;
      flags = CLUTTER_EVENT_FLAG_REPEATED;
    }

  event = meta_key_event_new_from_evdev (device,
                                         seat_impl->core_keyboard,
                                         flags,
                                         seat_impl->xkb,
                                         seat_impl->button_state,
                                         time_us, key, state);

  keycode = meta_xkb_evdev_to_keycode (key);

  /* We must be careful and not pass multiple releases to xkb, otherwise it gets
     confused and locks the modifiers */
  if (state != AUTOREPEAT_VALUE)
    {
      changed_state = xkb_state_update_key (seat_impl->xkb, keycode,
                                            state ? XKB_KEY_DOWN : XKB_KEY_UP);
    }

  if (!meta_input_device_native_process_kbd_a11y_event_in_impl (seat_impl->core_keyboard,
                                                                event))
    queue_event (seat_impl, event);
  else
    clutter_event_free (event);

  if (update_keys && (changed_state & XKB_STATE_LEDS))
    {
      MetaInputDeviceNative *keyboard_native;
      gboolean numlock_active;

      meta_keymap_native_update_in_impl (seat_impl->keymap,
                                         seat_impl,
                                         seat_impl->xkb);
      meta_seat_impl_sync_leds_in_impl (seat_impl);

      numlock_active =
        xkb_state_mod_name_is_active (seat_impl->xkb, XKB_MOD_NAME_NUM,
                                      XKB_STATE_MODS_LATCHED |
                                      XKB_STATE_MODS_LOCKED);
      meta_input_settings_maybe_save_numlock_state (seat_impl->input_settings,
                                                    numlock_active);

      keyboard_native = META_INPUT_DEVICE_NATIVE (seat_impl->core_keyboard);
      meta_input_device_native_a11y_maybe_notify_toggle_keys_in_impl (keyboard_native);
    }

  if (state == 0 ||             /* key release */
      !seat_impl->repeat ||
      !xkb_keymap_key_repeats (xkb_state_get_keymap (seat_impl->xkb),
                               keycode))
    {
      seat_impl->repeat_count = 0;
      meta_seat_impl_clear_repeat_source (seat_impl);
      return;
    }

  if (state == 1)               /* key press */
    seat_impl->repeat_count = 0;

  seat_impl->repeat_count += 1;
  seat_impl->repeat_key = key;

  switch (seat_impl->repeat_count)
    {
    case 1:
    case 2:
      {
        uint32_t interval;

        meta_seat_impl_clear_repeat_source (seat_impl);
        seat_impl->repeat_device = g_object_ref (device);

        if (seat_impl->repeat_count == 1)
          interval = seat_impl->repeat_delay;
        else
          interval = seat_impl->repeat_interval;

        seat_impl->repeat_source = g_timeout_source_new (interval);
        g_source_set_priority (seat_impl->repeat_source, CLUTTER_PRIORITY_EVENTS);
        g_source_set_callback (seat_impl->repeat_source,
                               keyboard_repeat, seat_impl, NULL);
        g_source_attach (seat_impl->repeat_source, seat_impl->input_context);
        return;
      }
    default:
      return;
    }
}

static void
constrain_to_barriers (MetaSeatImpl       *seat_impl,
                       ClutterInputDevice *device,
                       uint32_t            time,
                       float              *new_x,
                       float              *new_y)
{
  meta_barrier_manager_native_process_in_impl (seat_impl->barrier_manager,
                                               device,
                                               time,
                                               new_x, new_y);
}

/*
 * The pointer constrain code is mostly a rip-off of the XRandR code from Xorg.
 * (from xserver/randr/rrcrtc.c, RRConstrainCursorHarder)
 *
 * Copyright © 2006 Keith Packard
 * Copyright 2010 Red Hat, Inc
 *
 */

static void
constrain_all_screen_monitors (ClutterInputDevice *device,
                               MetaViewportInfo   *viewports,
                               float              *x,
                               float              *y)
{
  float cx, cy;
  int i, n_views;

  meta_input_device_native_get_coords_in_impl (META_INPUT_DEVICE_NATIVE (device),
                                               &cx, &cy);

  /* if we're trying to escape, clamp to the CRTC we're coming from */

  n_views = meta_viewport_info_get_num_views (viewports);

  for (i = 0; i < n_views; i++)
    {
      int left, right, top, bottom;
      MtkRectangle rect;

      meta_viewport_info_get_view_info (viewports, i, &rect, NULL);

      left = rect.x;
      right = left + rect.width;
      top = rect.y;
      bottom = top + rect.height;

      if ((cx >= left) && (cx < right) && (cy >= top) && (cy < bottom))
        {
          if (*x < left)
            *x = left;
          if (*x >= right)
            *x = right - 1;
          if (*y < top)
            *y = top;
          if (*y >= bottom)
            *y = bottom - 1;

          return;
        }
    }
}

static void
constrain_to_viewports (MetaSeatImpl       *seat_impl,
                        ClutterInputDevice *device,
                        uint64_t            time_us,
                        float              *x_inout,
                        float              *y_inout)
{
  if (seat_impl->viewports)
    {
      /* if we're moving inside a monitor, we're fine */
      if (meta_viewport_info_get_view_at (seat_impl->viewports,
                                          *x_inout, *y_inout) >= 0)
        return;

      /* if we're trying to escape, clamp to the CRTC we're coming from */
      constrain_all_screen_monitors (device, seat_impl->viewports,
                                     x_inout, y_inout);
    }
}

static void
constrain_coordinates (MetaSeatImpl       *seat_impl,
                       ClutterInputDevice *input_device,
                       uint64_t            time_us,
                       float               x,
                       float               y,
                       float              *x_out,
                       float              *y_out)
{
  MetaInputDeviceNative *device_evdev = META_INPUT_DEVICE_NATIVE (input_device);

  if (clutter_input_device_get_device_type (input_device) == CLUTTER_TABLET_DEVICE)
    {
        if (device_evdev->mapping_mode == META_INPUT_DEVICE_MAPPING_RELATIVE)
          {
            constrain_to_barriers (seat_impl, input_device,
                                   us2ms (time_us),
                                   &x, &y);
            constrain_to_viewports (seat_impl,
                                    input_device,
                                    time_us,
                                    &x, &y);
          }
        else
          {
            /* Viewport may be unset during startup */
            if (seat_impl->viewports)
              {
                meta_input_device_native_translate_coordinates_in_impl (input_device,
                                                                        seat_impl->viewports,
                                                                        &x,
                                                                        &y);
              }
          }
    }
  else
    {
      meta_seat_impl_constrain_pointer (seat_impl,
                                        seat_impl->core_pointer,
                                        time_us,
                                        seat_impl->pointer_x,
                                        seat_impl->pointer_y,
                                        &x, &y);
    }

  if (x_out)
    *x_out = x;
  if (y_out)
    *y_out = y;
}

static void
update_device_coords_in_impl (MetaSeatImpl       *seat_impl,
                              ClutterInputDevice *input_device,
                              graphene_point_t    coords)
{
  MetaInputDeviceNative *device_native;

  g_rw_lock_writer_lock (&seat_impl->state_lock);

  if (clutter_input_device_get_device_type (input_device) == CLUTTER_TABLET_DEVICE)
    {
      device_native = META_INPUT_DEVICE_NATIVE (input_device);
    }
  else
    {
      device_native = META_INPUT_DEVICE_NATIVE (seat_impl->core_pointer);
      seat_impl->pointer_x = coords.x;
      seat_impl->pointer_y = coords.y;
    }

  meta_input_device_native_set_coords_in_impl (device_native,
                                               coords.x, coords.y);

  g_rw_lock_writer_unlock (&seat_impl->state_lock);
}

void
meta_seat_impl_notify_relative_motion_in_impl (MetaSeatImpl       *seat_impl,
                                               ClutterInputDevice *input_device,
                                               uint64_t            time_us,
                                               float               dx,
                                               float               dy,
                                               float               dx_unaccel,
                                               float               dy_unaccel,
                                               double             *axes)
{
  MetaInputDeviceNative *device_native =
    META_INPUT_DEVICE_NATIVE (input_device);
  ClutterEvent *event;
  ClutterModifierType modifiers;
  float x, y, cur_x, cur_y;
  double dx_constrained, dy_constrained;

  if (clutter_input_device_get_device_type (input_device) == CLUTTER_TABLET_DEVICE)
    {
      meta_input_device_native_get_coords_in_impl (device_native,
                                                   &cur_x, &cur_y);
    }
  else
    {
      meta_input_device_native_get_coords_in_impl (META_INPUT_DEVICE_NATIVE (seat_impl->core_pointer),
                                                   &cur_x, &cur_y);
    }

  meta_seat_impl_filter_relative_motion (seat_impl,
                                         input_device,
                                         cur_x,
                                         cur_y,
                                         &dx,
                                         &dy);

  constrain_coordinates (seat_impl, input_device,
                         time_us,
                         cur_x + dx,
                         cur_y + dy,
                         &x, &y);

  modifiers =
    xkb_state_serialize_mods (seat_impl->xkb, XKB_STATE_MODS_EFFECTIVE) |
    seat_impl->button_state;

  dx_constrained = x - cur_x;
  dy_constrained = y - cur_y;

  update_device_coords_in_impl (seat_impl, input_device,
                                GRAPHENE_POINT_INIT (x, y));

  g_signal_emit (seat_impl, signals[POINTER_POSITION_CHANGED_IN_IMPL], 0,
                 &GRAPHENE_POINT_INIT (x, y));

  event =
    clutter_event_motion_new (CLUTTER_EVENT_FLAG_RELATIVE_MOTION,
                              time_us,
                              input_device,
                              device_native->last_tool,
                              modifiers,
                              GRAPHENE_POINT_INIT (x, y),
                              GRAPHENE_POINT_INIT (dx, dy),
                              GRAPHENE_POINT_INIT (dx_unaccel,
                                                   dy_unaccel),
                              GRAPHENE_POINT_INIT (dx_constrained,
                                                   dy_constrained),
                              axes);

  queue_event (seat_impl, event);
}

void
meta_seat_impl_notify_absolute_motion_in_impl (MetaSeatImpl       *seat_impl,
                                               ClutterInputDevice *input_device,
                                               uint64_t            time_us,
                                               float               x,
                                               float               y,
                                               double             *axes)
{
  MetaInputDeviceNative *device_native =
    META_INPUT_DEVICE_NATIVE (input_device);
  ClutterModifierType modifiers;
  ClutterEvent *event;

  constrain_coordinates (seat_impl, input_device, time_us, x, y, &x, &y);
  update_device_coords_in_impl (seat_impl, input_device,
                                GRAPHENE_POINT_INIT (x, y));

  modifiers =
    xkb_state_serialize_mods (seat_impl->xkb, XKB_STATE_MODS_EFFECTIVE) |
    seat_impl->button_state;

  g_signal_emit (seat_impl, signals[POINTER_POSITION_CHANGED_IN_IMPL], 0,
                 &GRAPHENE_POINT_INIT (seat_impl->pointer_x,
                                       seat_impl->pointer_y));

  event =
    clutter_event_motion_new (CLUTTER_EVENT_NONE,
                              time_us,
                              input_device,
                              device_native->last_tool,
                              modifiers,
                              GRAPHENE_POINT_INIT (x, y),
                              GRAPHENE_POINT_INIT (0, 0),
                              GRAPHENE_POINT_INIT (0, 0),
                              GRAPHENE_POINT_INIT (0, 0),
                              axes);

  queue_event (seat_impl, event);
}

void
meta_seat_impl_notify_button_in_impl (MetaSeatImpl       *seat_impl,
                                      ClutterInputDevice *input_device,
                                      uint64_t            time_us,
                                      uint32_t            button,
                                      uint32_t            state)
{
  MetaInputDeviceNative *device_native = META_INPUT_DEVICE_NATIVE (input_device);
  ClutterEvent *event = NULL;
  ClutterModifierType modifiers;
  int button_nr;
  float x, y;
  static int maskmap[8] =
    {
      CLUTTER_BUTTON1_MASK, CLUTTER_BUTTON3_MASK, CLUTTER_BUTTON2_MASK,
      CLUTTER_BUTTON4_MASK, CLUTTER_BUTTON5_MASK, 0, 0, 0
    };
  int button_count;

  /* Drop any repeated button press (for example from virtual devices. */
  button_count = update_button_count (seat_impl, button, state);
  if ((state && button_count > 1) ||
      (!state && button_count != 0))
    {
      meta_topic (META_DEBUG_INPUT,
                  "Dropping repeated %s of button 0x%x, count %d",
                  state ? "press" : "release", button, button_count);
      return;
    }

  /* The evdev button numbers don't map sequentially to clutter button
   * numbers (the right and middle mouse buttons are in the opposite
   * order) so we'll map them directly with a switch statement */
  switch (button)
    {
    case BTN_LEFT:
    case BTN_TOUCH:
      button_nr = CLUTTER_BUTTON_PRIMARY;
      break;

    case BTN_MIDDLE:
    case BTN_STYLUS:
      button_nr = CLUTTER_BUTTON_MIDDLE;
      break;

    case BTN_RIGHT:
    case BTN_STYLUS2:
      button_nr = CLUTTER_BUTTON_SECONDARY;
      break;

    case BTN_STYLUS3:
      button_nr = 8;
      break;

    default:
      /* For compatibility reasons, all additional buttons go after the old 4-7 scroll ones */
      if (clutter_input_device_get_device_type (input_device) == CLUTTER_TABLET_DEVICE)
        button_nr = button - BTN_TOOL_PEN + 4;
      else
        button_nr = meta_evdev_button_to_clutter (button);
      break;
    }

  if (button_nr < 1 || button_nr > 12)
    {
      g_warning ("Unhandled button event 0x%x", button);
      return;
    }

  if (button_nr < G_N_ELEMENTS (maskmap))
    {
      /* Update the modifiers */
      if (state)
        seat_impl->button_state |= maskmap[button_nr - 1];
      else
        seat_impl->button_state &= ~maskmap[button_nr - 1];
    }

  if (clutter_input_device_get_device_type (input_device) == CLUTTER_TABLET_DEVICE)
    {
      meta_input_device_native_get_coords_in_impl (device_native, &x, &y);
    }
  else
    {
      meta_input_device_native_get_coords_in_impl (META_INPUT_DEVICE_NATIVE (seat_impl->core_pointer),
                                                   &x, &y);
    }

  if (device_native->last_tool)
    {
      /* Apply the button event code as per the tool mapping */
      uint32_t mapped_button;

      mapped_button = meta_input_device_tool_native_get_button_code_in_impl (device_native->last_tool,
                                                                             button_nr);
      if (mapped_button != 0)
        button = mapped_button;
    }

  modifiers =
    xkb_state_serialize_mods (seat_impl->xkb, XKB_STATE_MODS_EFFECTIVE) |
    seat_impl->button_state;

  event =
    clutter_event_button_new (state ?
                              CLUTTER_BUTTON_PRESS :
                              CLUTTER_BUTTON_RELEASE,
                              CLUTTER_EVENT_NONE,
                              time_us,
                              input_device,
                              device_native->last_tool,
                              modifiers,
                              GRAPHENE_POINT_INIT (x, y),
                              button_nr,
                              button,
                              NULL);

  queue_event (seat_impl, event);
}

static MetaSeatImpl *
seat_impl_from_device (ClutterInputDevice *device)
{
  ClutterSeat *seat;

  seat = clutter_input_device_get_seat (device);

  return META_SEAT_NATIVE (seat)->impl;
}

static void
notify_scroll (ClutterInputDevice       *input_device,
               uint64_t                  time_us,
               double                    dx,
               double                    dy,
               ClutterScrollSource       scroll_source,
               ClutterScrollFinishFlags  flags,
               gboolean                  emulated)
{
  MetaSeatImpl *seat_impl;
  ClutterEvent *event = NULL;
  ClutterModifierType modifiers;
  double scroll_factor;
  float x, y;

  seat_impl = seat_impl_from_device (input_device);

  /* libinput pointer axis events are in pointer motion coordinate space.
   * To convert to Xi2 discrete step coordinate space, multiply the factor
   * 1/10. */
  scroll_factor = 1.0 / DISCRETE_SCROLL_STEP;

  x = seat_impl->pointer_x;
  y = seat_impl->pointer_y;

  modifiers =
    xkb_state_serialize_mods (seat_impl->xkb, XKB_STATE_MODS_EFFECTIVE) |
    seat_impl->button_state;

  event =
    clutter_event_scroll_smooth_new (emulated ?
                                     CLUTTER_EVENT_FLAG_POINTER_EMULATED :
                                     CLUTTER_EVENT_NONE,
                                     time_us,
                                     input_device,
                                     NULL,
                                     modifiers,
                                     GRAPHENE_POINT_INIT (x, y),
                                     GRAPHENE_POINT_INIT (scroll_factor * dx,
                                                          scroll_factor * dy),
                                     scroll_source,
                                     flags);

  queue_event (seat_impl, event);
}

static void
notify_discrete_scroll (ClutterInputDevice     *input_device,
                        uint64_t                time_us,
                        ClutterScrollDirection  direction,
                        ClutterScrollSource     scroll_source,
                        gboolean                emulated)
{
  MetaSeatImpl *seat_impl;
  ClutterEvent *event = NULL;
  ClutterModifierType modifiers;
  float x, y;

  if (direction == CLUTTER_SCROLL_SMOOTH)
    return;

  seat_impl = seat_impl_from_device (input_device);
  x = seat_impl->pointer_x;
  y = seat_impl->pointer_y;

  modifiers =
    xkb_state_serialize_mods (seat_impl->xkb, XKB_STATE_MODS_EFFECTIVE) |
    seat_impl->button_state;

  event =
    clutter_event_scroll_discrete_new (emulated ?
                                       CLUTTER_EVENT_FLAG_POINTER_EMULATED :
                                       CLUTTER_EVENT_NONE,
                                       time_us,
                                       input_device,
                                       NULL,
                                       modifiers,
                                       GRAPHENE_POINT_INIT (x, y),
                                       scroll_source,
                                       direction);

  queue_event (seat_impl, event);
}

static void
check_notify_discrete_scroll (MetaSeatImpl       *seat_impl,
                              ClutterInputDevice *device,
                              uint64_t            time_us,
                              ClutterScrollSource scroll_source)
{
  int i, n_xscrolls, n_yscrolls;

  n_xscrolls = floor ((fabs (seat_impl->accum_scroll_dx) + DBL_EPSILON) /
                      DISCRETE_SCROLL_STEP);
  n_yscrolls = floor ((fabs (seat_impl->accum_scroll_dy) + DBL_EPSILON) /
                      DISCRETE_SCROLL_STEP);

  for (i = 0; i < n_xscrolls; i++)
    {
      notify_discrete_scroll (device, time_us,
                              seat_impl->accum_scroll_dx > 0 ?
                              CLUTTER_SCROLL_RIGHT : CLUTTER_SCROLL_LEFT,
                              scroll_source, TRUE);
    }

  for (i = 0; i < n_yscrolls; i++)
    {
      notify_discrete_scroll (device, time_us,
                              seat_impl->accum_scroll_dy > 0 ?
                              CLUTTER_SCROLL_DOWN : CLUTTER_SCROLL_UP,
                              scroll_source, TRUE);
    }

  seat_impl->accum_scroll_dx =
    fmodf (seat_impl->accum_scroll_dx, DISCRETE_SCROLL_STEP);
  seat_impl->accum_scroll_dy =
    fmodf (seat_impl->accum_scroll_dy, DISCRETE_SCROLL_STEP);
}

void
meta_seat_impl_notify_scroll_continuous_in_impl (MetaSeatImpl             *seat_impl,
                                                 ClutterInputDevice       *input_device,
                                                 uint64_t                  time_us,
                                                 double                    dx,
                                                 double                    dy,
                                                 ClutterScrollSource       scroll_source,
                                                 ClutterScrollFinishFlags  finish_flags)
{
  if (finish_flags & CLUTTER_SCROLL_FINISHED_HORIZONTAL)
    seat_impl->accum_scroll_dx = 0;
  else
    seat_impl->accum_scroll_dx += dx;

  if (finish_flags & CLUTTER_SCROLL_FINISHED_VERTICAL)
    seat_impl->accum_scroll_dy = 0;
  else
    seat_impl->accum_scroll_dy += dy;

  notify_scroll (input_device, time_us, dx, dy, scroll_source,
                 finish_flags, FALSE);
  check_notify_discrete_scroll (seat_impl, input_device, time_us, scroll_source);
}

static ClutterScrollDirection
discrete_to_direction (double discrete_dx,
                       double discrete_dy)
{
  if (discrete_dx > 0)
    return CLUTTER_SCROLL_RIGHT;
  else if (discrete_dx < 0)
    return CLUTTER_SCROLL_LEFT;
  else if (discrete_dy > 0)
    return CLUTTER_SCROLL_DOWN;
  else if (discrete_dy < 0)
    return CLUTTER_SCROLL_UP;
  else
    g_assert_not_reached ();
  return 0;
}

static gboolean
should_reset_discrete_acc (double current_delta,
                           double last_delta)
{
  if (last_delta == 0)
    return TRUE;

  return (current_delta < 0 && last_delta > 0) ||
         (current_delta > 0 && last_delta < 0);
}

void
meta_seat_impl_notify_discrete_scroll_in_impl (MetaSeatImpl        *seat_impl,
                                               ClutterInputDevice  *input_device,
                                               uint64_t             time_us,
                                               double               dx_value120,
                                               double               dy_value120,
                                               ClutterScrollSource  scroll_source)
{
  MetaInputDeviceNative *evdev_device;
  double dx = 0, dy = 0;
  int low_res_value = 0;

  /* Convert into DISCRETE_SCROLL_STEP range. 120/DISCRETE_SCROLL_STEP = 12.0 */
  dx = dx_value120 / 12.0;
  dy = dy_value120 / 12.0;

  notify_scroll (input_device, time_us,
                 dx,
                 dy,
                 scroll_source, CLUTTER_SCROLL_FINISHED_NONE,
                 TRUE);

  /* Notify discrete scroll only when the accumulated value reach 120 */
  evdev_device = META_INPUT_DEVICE_NATIVE (input_device);

  if (dx_value120 != 0)
    {
      if (should_reset_discrete_acc (dx_value120, evdev_device->value120.last_dx))
        evdev_device->value120.acc_dx = 0;

      evdev_device->value120.last_dx = dx_value120;
    }

  if (dy_value120 != 0)
    {
      if (should_reset_discrete_acc (dy_value120, evdev_device->value120.last_dy))
        evdev_device->value120.acc_dy = 0;

      evdev_device->value120.last_dy = dy_value120;
    }

  evdev_device->value120.acc_dx += dx_value120;
  evdev_device->value120.acc_dy += dy_value120;

  if (abs (evdev_device->value120.acc_dx) >= 60)
    {
      low_res_value = (evdev_device->value120.acc_dx / 120);
      if (low_res_value == 0)
        low_res_value = (dx_value120 > 0) ? 1 : -1;

      notify_discrete_scroll (input_device, time_us,
                              discrete_to_direction (low_res_value, 0),
                              scroll_source, FALSE);
      evdev_device->value120.acc_dx -= (low_res_value * 120);
    }

  if (abs (evdev_device->value120.acc_dy) >= 60)
    {
      low_res_value = (evdev_device->value120.acc_dy / 120);
      if (low_res_value == 0)
        low_res_value = (dy_value120 > 0) ? 1 : -1;

      notify_discrete_scroll (input_device, time_us,
                              discrete_to_direction (0, low_res_value),
                              scroll_source, FALSE);
      evdev_device->value120.acc_dy -= (low_res_value * 120);
    }
}

void
meta_seat_impl_notify_touch_event_in_impl (MetaSeatImpl       *seat_impl,
                                           ClutterInputDevice *input_device,
                                           ClutterEventType    evtype,
                                           uint64_t            time_us,
                                           int                 slot,
                                           double              x,
                                           double              y)
{
  ClutterEvent *event = NULL;
  ClutterEventSequence *sequence;
  ClutterModifierType modifiers;

  /* "NULL" sequences are special cased in clutter */
  sequence = GINT_TO_POINTER (MAX (1, slot + 1));

  modifiers =
    xkb_state_serialize_mods (seat_impl->xkb, XKB_STATE_MODS_EFFECTIVE) |
    seat_impl->button_state;

  if (evtype == CLUTTER_TOUCH_BEGIN ||
      evtype == CLUTTER_TOUCH_UPDATE)
    modifiers |= CLUTTER_BUTTON1_MASK;

  if (evtype == CLUTTER_TOUCH_CANCEL)
    {
      event = clutter_event_touch_cancel_new (CLUTTER_EVENT_NONE,
                                              time_us,
                                              input_device,
                                              sequence);
    }
  else
    {
      event =
        clutter_event_touch_new (evtype,
                                 CLUTTER_EVENT_NONE,
                                 time_us,
                                 input_device,
                                 sequence,
                                 modifiers,
                                 GRAPHENE_POINT_INIT (x, y));
    }

  queue_event (seat_impl, event);
}

void
meta_seat_impl_constrain_pointer (MetaSeatImpl       *seat_impl,
                                  ClutterInputDevice *core_pointer,
                                  uint64_t            time_us,
                                  float               x,
                                  float               y,
                                  float              *new_x,
                                  float              *new_y)
{
  /* Constrain to barriers */
  constrain_to_barriers (seat_impl, core_pointer,
                         us2ms (time_us),
                         new_x, new_y);

  /* Bar to constraints */
  if (seat_impl->pointer_constraint)
    {
      meta_pointer_constraint_impl_constrain (seat_impl->pointer_constraint,
                                              core_pointer,
                                              us2ms (time_us),
                                              x, y,
                                              new_x, new_y);
    }

  constrain_to_viewports (seat_impl, core_pointer, time_us, new_x, new_y);
}

static void
relative_motion_across_outputs (MetaViewportInfo   *viewports,
                                int                 view,
                                float               cur_x,
                                float               cur_y,
                                float              *dx_inout,
                                float              *dy_inout)
{
  int cur_view = view;
  float x = cur_x, y = cur_y;
  float target_x = cur_x, target_y = cur_y;
  float dx = *dx_inout, dy = *dy_inout;
  MetaDisplayDirection direction_h, direction_v;

#define META_DISPLAY_NONE -1
  direction_h = dx > 0.0 ? META_DISPLAY_RIGHT :
    dx < 0.0 ? META_DISPLAY_LEFT :
    META_DISPLAY_NONE;
  direction_v = dy > 0.0 ? META_DISPLAY_DOWN :
    dy < 0.0 ? META_DISPLAY_UP :
    META_DISPLAY_NONE;
#undef META_DISPLAY_NONE

  while (cur_view >= 0)
    {
      MetaLine2 left, right, top, bottom, motion;
      MetaVector2 intersection;
      MetaDisplayDirection direction;
      MtkRectangle rect;
      float scale;

      meta_viewport_info_get_view_info (viewports, cur_view, &rect, &scale);

      target_x = x + (dx * scale);
      target_y = y + (dy * scale);

      motion = (MetaLine2) {
        .a = { x, y },
        .b = { target_x, target_y }
      };
      left = (MetaLine2) {
        { rect.x, rect.y },
        { rect.x, rect.y + rect.height }
      };
      right = (MetaLine2) {
        { rect.x + rect.width, rect.y },
        { rect.x + rect.width, rect.y + rect.height }
      };
      top = (MetaLine2) {
        { rect.x, rect.y },
        { rect.x + rect.width, rect.y }
      };
      bottom = (MetaLine2) {
        { rect.x, rect.y + rect.height },
        { rect.x + rect.width, rect.y + rect.height }
      };

      if (direction_h == META_DISPLAY_LEFT &&
          meta_line2_intersects_with (&motion, &left, &intersection))
        direction = META_DISPLAY_LEFT;
      else if (direction_h == META_DISPLAY_RIGHT &&
               meta_line2_intersects_with (&motion, &right, &intersection))
        direction = META_DISPLAY_RIGHT;
      else if (direction_v == META_DISPLAY_UP &&
               meta_line2_intersects_with (&motion, &top, &intersection))
        direction = META_DISPLAY_UP;
      else if (direction_v == META_DISPLAY_DOWN &&
               meta_line2_intersects_with (&motion, &bottom, &intersection))
        direction = META_DISPLAY_DOWN;
      else
        /* We reached the dest logical monitor */
        break;

      dx -= intersection.x - x;
      dy -= intersection.y - y;
      x = intersection.x;
      y = intersection.y;

      cur_view = meta_viewport_info_get_neighbor (viewports, cur_view,
                                                  direction);
    }

  *dx_inout = target_x - cur_x;
  *dy_inout = target_y - cur_y;
}

void
meta_seat_impl_filter_relative_motion (MetaSeatImpl       *seat_impl,
                                       ClutterInputDevice *device,
                                       float               x,
                                       float               y,
                                       float              *dx,
                                       float              *dy)
{
  int view, dest_view;
  float new_dx, new_dy, scale;

  if (!seat_impl->viewports)
    return;
  if (meta_viewport_info_is_views_scaled (seat_impl->viewports))
    return;

  view = meta_viewport_info_get_view_at (seat_impl->viewports, x, y);
  if (view < 0)
    return;

  meta_viewport_info_get_view_info (seat_impl->viewports, view, NULL, &scale);
  new_dx = (*dx) * scale;
  new_dy = (*dy) * scale;

  dest_view = meta_viewport_info_get_view_at (seat_impl->viewports,
                                              x + new_dx,
                                              y + new_dy);
  if (dest_view >= 0 && dest_view != view)
    {
      /* If we are crossing monitors, attempt to bisect the distance on each
       * axis and apply the relative scale for each of them.
       */
      new_dx = *dx;
      new_dy = *dy;
      relative_motion_across_outputs (seat_impl->viewports, view,
                                      x, y, &new_dx, &new_dy);
    }

  *dx = new_dx;
  *dy = new_dy;
}

static void
notify_absolute_motion_in_impl (ClutterInputDevice *input_device,
                                uint64_t            time_us,
                                float               x,
                                float               y,
                                double             *axes)
{
  MetaSeatImpl *seat_impl;

  seat_impl = seat_impl_from_device (input_device);
  meta_seat_impl_notify_absolute_motion_in_impl (seat_impl,
                                                 input_device,
                                                 time_us,
                                                 x, y,
                                                 axes);
}

static void
notify_relative_tool_motion_in_impl (ClutterInputDevice *input_device,
                                     uint64_t            time_us,
                                     float               dx,
                                     float               dy,
                                     double             *axes)
{
  MetaSeatImpl *seat_impl;

  seat_impl = seat_impl_from_device (input_device);
  meta_seat_impl_notify_relative_motion_in_impl (seat_impl,
                                                 input_device,
                                                 time_us,
                                                 dx, dy,
                                                 /* FIXME */
                                                 dx, dy,
                                                 axes);
}

static void
notify_pinch_gesture_event (ClutterInputDevice          *input_device,
                            ClutterTouchpadGesturePhase  phase,
                            uint64_t                     time_us,
                            double                       dx,
                            double                       dy,
                            double                       dx_unaccel,
                            double                       dy_unaccel,
                            double                       angle_delta,
                            double                       scale,
                            uint32_t                     n_fingers)
{
  MetaSeatImpl *seat_impl;
  ClutterEvent *event = NULL;
  float x, y;

  seat_impl = seat_impl_from_device (input_device);

  meta_input_device_native_get_coords_in_impl (META_INPUT_DEVICE_NATIVE (seat_impl->core_pointer),
                                               &x, &y);
  event =
    clutter_event_touchpad_pinch_new (CLUTTER_EVENT_NONE,
                                      time_us,
                                      input_device,
                                      phase,
                                      n_fingers,
                                      GRAPHENE_POINT_INIT (x, y),
                                      GRAPHENE_POINT_INIT (dx, dy),
                                      GRAPHENE_POINT_INIT (dx_unaccel,
                                                           dy_unaccel),
                                      angle_delta, scale);

  queue_event (seat_impl, event);
}

static void
notify_swipe_gesture_event (ClutterInputDevice          *input_device,
                            ClutterTouchpadGesturePhase  phase,
                            uint64_t                     time_us,
                            uint32_t                     n_fingers,
                            double                       dx,
                            double                       dy,
                            double                       dx_unaccel,
                            double                       dy_unaccel)
{
  MetaSeatImpl *seat_impl;
  ClutterEvent *event = NULL;
  float x, y;

  seat_impl = seat_impl_from_device (input_device);

  meta_input_device_native_get_coords_in_impl (META_INPUT_DEVICE_NATIVE (seat_impl->core_pointer),
                                               &x, &y);
  event =
    clutter_event_touchpad_swipe_new (CLUTTER_EVENT_NONE,
                                      time_us,
                                      input_device,
                                      phase,
                                      n_fingers,
                                      GRAPHENE_POINT_INIT (x, y),
                                      GRAPHENE_POINT_INIT (dx, dy),
                                      GRAPHENE_POINT_INIT (dx_unaccel,
                                                           dy_unaccel));

  queue_event (seat_impl, event);
}

static void
notify_hold_gesture_event (ClutterInputDevice          *input_device,
                           ClutterTouchpadGesturePhase  phase,
                           uint64_t                     time_us,
                           uint32_t                     n_fingers)
{
  MetaSeatImpl *seat_impl;
  ClutterEvent *event = NULL;
  float x, y;

  seat_impl = seat_impl_from_device (input_device);

  meta_input_device_native_get_coords_in_impl (META_INPUT_DEVICE_NATIVE (seat_impl->core_pointer),
                                               &x, &y);

  event = clutter_event_touchpad_hold_new (CLUTTER_EVENT_NONE,
                                           time_us,
                                           input_device,
                                           phase,
                                           n_fingers,
                                           GRAPHENE_POINT_INIT (x, y));

  queue_event (seat_impl, event);
}

static void
notify_proximity (ClutterInputDevice *input_device,
                  uint64_t            time_us,
                  gboolean            in)
{
  MetaInputDeviceNative *device_native;
  MetaSeatImpl *seat_impl;
  ClutterEvent *event = NULL;

  device_native = META_INPUT_DEVICE_NATIVE (input_device);
  seat_impl = seat_impl_from_device (input_device);

  event = clutter_event_proximity_new (in ?
                                       CLUTTER_PROXIMITY_IN :
                                       CLUTTER_PROXIMITY_OUT,
                                       CLUTTER_EVENT_NONE,
                                       time_us,
                                       input_device,
                                       device_native->last_tool);

  queue_event (seat_impl, event);
}

static void
notify_pad_button (ClutterInputDevice *input_device,
                   uint64_t            time_us,
                   uint32_t            button,
                   uint32_t            mode_group,
                   uint32_t            mode,
                   uint32_t            pressed)
{
  MetaSeatImpl *seat_impl;
  ClutterEvent *event;

  seat_impl = seat_impl_from_device (input_device);

  event = clutter_event_pad_button_new (pressed ?
                                        CLUTTER_PAD_BUTTON_PRESS :
                                        CLUTTER_PAD_BUTTON_RELEASE,
                                        CLUTTER_EVENT_NONE,
                                        time_us,
                                        input_device,
                                        button,
                                        mode_group,
                                        mode);

  queue_event (seat_impl, event);
}

static void
notify_pad_strip (ClutterInputDevice *input_device,
                  uint64_t            time_us,
                  uint32_t            strip_number,
                  uint32_t            strip_source,
                  uint32_t            mode_group,
                  uint32_t            mode,
                  double              value)
{
  ClutterInputDevicePadSource source;
  MetaSeatImpl *seat_impl;
  ClutterEvent *event;

  seat_impl = seat_impl_from_device (input_device);

  if (strip_source == LIBINPUT_TABLET_PAD_STRIP_SOURCE_FINGER)
    source = CLUTTER_INPUT_DEVICE_PAD_SOURCE_FINGER;
  else
    source = CLUTTER_INPUT_DEVICE_PAD_SOURCE_UNKNOWN;

  event = clutter_event_pad_strip_new (CLUTTER_EVENT_NONE,
                                       time_us,
                                       input_device,
                                       source,
                                       strip_number,
                                       mode_group,
                                       value,
                                       mode);

  queue_event (seat_impl, event);
}

static void
notify_pad_ring (ClutterInputDevice *input_device,
                 uint64_t            time_us,
                 uint32_t            ring_number,
                 uint32_t            ring_source,
                 uint32_t            mode_group,
                 uint32_t            mode,
                 double              angle)
{
  ClutterInputDevicePadSource source;
  MetaSeatImpl *seat_impl;
  ClutterEvent *event;

  seat_impl = seat_impl_from_device (input_device);

  if (ring_source == LIBINPUT_TABLET_PAD_RING_SOURCE_FINGER)
    source = CLUTTER_INPUT_DEVICE_PAD_SOURCE_FINGER;
  else
    source = CLUTTER_INPUT_DEVICE_PAD_SOURCE_UNKNOWN;

  event = clutter_event_pad_ring_new (CLUTTER_EVENT_NONE,
                                      time_us,
                                      input_device,
                                      source,
                                      ring_number,
                                      mode_group,
                                      angle,
                                      mode);

  queue_event (seat_impl, event);
}

static gboolean
has_touchscreen (MetaSeatImpl *seat_impl)
{
  GSList *l;

  for (l = seat_impl->devices; l; l = l->next)
    {
      ClutterInputDeviceType device_type;

      device_type = clutter_input_device_get_device_type (l->data);

      if (device_type == CLUTTER_TOUCHSCREEN_DEVICE)
        return TRUE;
    }

  return FALSE;
}

static inline gboolean
device_type_is_pointer (ClutterInputDeviceType device_type)
{
  return device_type == CLUTTER_POINTER_DEVICE ||
    device_type == CLUTTER_TOUCHPAD_DEVICE;
}

static gboolean
has_pointer (MetaSeatImpl *seat_impl)
{
  GSList *l;

  for (l = seat_impl->devices; l; l = l->next)
    {
      ClutterInputDeviceType device_type;

      device_type = clutter_input_device_get_device_type (l->data);
      if (device_type_is_pointer (device_type))
        return TRUE;
    }

  return FALSE;
}

static gboolean
device_is_tablet_switch (MetaInputDeviceNative *device_native)
{
  if (libinput_device_has_capability (device_native->libinput_device,
                                      LIBINPUT_DEVICE_CAP_SWITCH) &&
      libinput_device_switch_has_switch (device_native->libinput_device,
                                         LIBINPUT_SWITCH_TABLET_MODE))
    return TRUE;

  return FALSE;
}

static gboolean
has_tablet_switch (MetaSeatImpl *seat_impl)
{
  GSList *l;

  for (l = seat_impl->devices; l; l = l->next)
    {
      MetaInputDeviceNative *device_native = META_INPUT_DEVICE_NATIVE (l->data);

      if (device_is_tablet_switch (device_native))
        return TRUE;
    }

  return FALSE;
}

static void
update_touch_mode (MetaSeatImpl *seat_impl)
{
  gboolean touch_mode;

  /* No touch mode if we don't have a touchscreen, easy */
  if (!seat_impl->has_touchscreen)
    touch_mode = FALSE;
  /* If we have a tablet mode switch, honor it being unset */
  else if (seat_impl->has_tablet_switch && !seat_impl->tablet_mode_switch_state)
    touch_mode = FALSE;
  /* If tablet mode is enabled, go for it */
  else if (seat_impl->has_tablet_switch && seat_impl->tablet_mode_switch_state)
    touch_mode = TRUE;
  /* If there is no tablet mode switch (eg. kiosk machines),
   * assume touch-mode is mutually exclusive with pointers.
   */
  else
    touch_mode = !seat_impl->has_pointer;

  if (seat_impl->touch_mode != touch_mode)
    {
      GValue value = G_VALUE_INIT;

      g_value_init (&value, G_TYPE_BOOLEAN);
      g_value_set_boolean (&value, touch_mode);
      seat_impl->touch_mode = touch_mode;
      emit_signal (seat_impl, signals[TOUCH_MODE], &value, 1);
      g_value_unset (&value);
    }
}

static ClutterInputDevice *
evdev_add_device (MetaSeatImpl           *seat_impl,
                  struct libinput_device *libinput_device)
{
  ClutterInputDeviceType type;
  ClutterInputDevice *device;
  gboolean is_touchscreen, is_tablet_switch, is_pointer;

  device = meta_input_device_native_new_in_impl (seat_impl, libinput_device);

  seat_impl->devices = g_slist_prepend (seat_impl->devices, device);
  meta_seat_impl_sync_leds_in_impl (seat_impl);

  /* Clutter assumes that device types are exclusive in the
   * ClutterInputDevice API */
  type = clutter_input_device_get_device_type (device);

  is_touchscreen = type == CLUTTER_TOUCHSCREEN_DEVICE;
  is_tablet_switch =
    device_is_tablet_switch (META_INPUT_DEVICE_NATIVE (device));
  is_pointer = device_type_is_pointer (type);

  seat_impl->has_touchscreen |= is_touchscreen;
  seat_impl->has_tablet_switch |= is_tablet_switch;
  seat_impl->has_pointer |= is_pointer;

  if (is_touchscreen || is_tablet_switch || is_pointer)
    update_touch_mode (seat_impl);

  if (type == CLUTTER_KEYBOARD_DEVICE)
    {
      MetaKbdA11ySettings kbd_a11y_settings;
      MetaInputDeviceNative *keyboard_native;

      keyboard_native = META_INPUT_DEVICE_NATIVE (seat_impl->core_keyboard);
      meta_input_settings_get_kbd_a11y_settings (seat_impl->input_settings,
                                                 &kbd_a11y_settings);
      meta_input_device_native_apply_kbd_a11y_settings_in_impl (keyboard_native,
                                                                &kbd_a11y_settings);
    }

  return device;
}

static void
evdev_remove_device (MetaSeatImpl          *seat_impl,
                     MetaInputDeviceNative *device_native)
{
  ClutterInputDevice *device;
  ClutterInputDeviceType device_type;
  gboolean is_touchscreen, is_tablet_switch, is_pointer;

  device = CLUTTER_INPUT_DEVICE (device_native);
  seat_impl->devices = g_slist_remove (seat_impl->devices, device);

  device_type = clutter_input_device_get_device_type (device);

  is_touchscreen = device_type == CLUTTER_TOUCHSCREEN_DEVICE;
  is_tablet_switch = device_is_tablet_switch (device_native);
  is_pointer = device_type_is_pointer (device_type);

  if (is_touchscreen)
    seat_impl->has_touchscreen = has_touchscreen (seat_impl);
  if (is_tablet_switch)
    seat_impl->has_tablet_switch = has_tablet_switch (seat_impl);
  if (is_pointer)
    seat_impl->has_pointer = has_pointer (seat_impl);

  if (is_touchscreen || is_tablet_switch || is_pointer)
    update_touch_mode (seat_impl);

  if (seat_impl->repeat_source && seat_impl->repeat_device == device)
    meta_seat_impl_clear_repeat_source (seat_impl);

  meta_input_device_native_detach_libinput_in_impl (device_native);

  g_object_unref (device);
}

static gboolean
process_base_event (MetaSeatImpl          *seat_impl,
                    struct libinput_event *event)
{
  ClutterInputDevice *device;
  ClutterEvent *device_event = NULL;
  struct libinput_device *libinput_device;
  MetaInputSettings *input_settings;

  input_settings = seat_impl->input_settings;

  switch (libinput_event_get_type (event))
    {
    case LIBINPUT_EVENT_DEVICE_ADDED:
      libinput_device = libinput_event_get_device (event);

      device = evdev_add_device (seat_impl, libinput_device);
      device_event =
        clutter_event_device_notify_new (CLUTTER_DEVICE_ADDED,
                                         CLUTTER_EVENT_NONE,
                                         CLUTTER_CURRENT_TIME,
                                         device);

      meta_input_settings_add_device (input_settings, device);
      break;

    case LIBINPUT_EVENT_DEVICE_REMOVED:
      libinput_device = libinput_event_get_device (event);

      device = libinput_device_get_user_data (libinput_device);
      device_event =
        clutter_event_device_notify_new (CLUTTER_DEVICE_REMOVED,
                                         CLUTTER_EVENT_NONE,
                                         CLUTTER_CURRENT_TIME,
                                         device);
      meta_input_settings_remove_device (input_settings, device);
      evdev_remove_device (seat_impl,
                           META_INPUT_DEVICE_NATIVE (device));
      break;

    default:
      break;
    }

  if (device_event)
    {
      queue_event (seat_impl, device_event);
      return TRUE;
    }

  return FALSE;
}

static ClutterInputDeviceToolType
translate_tool_type (struct libinput_tablet_tool *libinput_tool)
{
  enum libinput_tablet_tool_type tool;

  tool = libinput_tablet_tool_get_type (libinput_tool);

  switch (tool)
    {
    case LIBINPUT_TABLET_TOOL_TYPE_PEN:
      return CLUTTER_INPUT_DEVICE_TOOL_PEN;
    case LIBINPUT_TABLET_TOOL_TYPE_ERASER:
      return CLUTTER_INPUT_DEVICE_TOOL_ERASER;
    case LIBINPUT_TABLET_TOOL_TYPE_BRUSH:
      return CLUTTER_INPUT_DEVICE_TOOL_BRUSH;
    case LIBINPUT_TABLET_TOOL_TYPE_PENCIL:
      return CLUTTER_INPUT_DEVICE_TOOL_PENCIL;
    case LIBINPUT_TABLET_TOOL_TYPE_AIRBRUSH:
      return CLUTTER_INPUT_DEVICE_TOOL_AIRBRUSH;
    case LIBINPUT_TABLET_TOOL_TYPE_MOUSE:
      return CLUTTER_INPUT_DEVICE_TOOL_MOUSE;
    case LIBINPUT_TABLET_TOOL_TYPE_LENS:
      return CLUTTER_INPUT_DEVICE_TOOL_LENS;
    default:
      return CLUTTER_INPUT_DEVICE_TOOL_NONE;
    }
}

static void
input_device_update_tool (MetaSeatImpl                *seat_impl,
                          ClutterInputDevice          *input_device,
                          struct libinput_tablet_tool *libinput_tool)
{
  MetaInputDeviceNative *evdev_device = META_INPUT_DEVICE_NATIVE (input_device);
  ClutterInputDeviceTool *tool = NULL;
  MetaInputSettings *input_settings;

  if (libinput_tool)
    {
      if (!seat_impl->tools)
        {
          seat_impl->tools =
            g_hash_table_new_full (NULL, NULL, NULL,
                                   (GDestroyNotify) g_object_unref);
        }

      tool = g_hash_table_lookup (seat_impl->tools, libinput_tool);

      if (!tool)
        {
          ClutterInputDeviceToolType tool_type;
          uint64_t tool_serial;

          tool_serial = libinput_tablet_tool_get_serial (libinput_tool);
          tool_type = translate_tool_type (libinput_tool);
          tool = meta_input_device_tool_native_new (libinput_tool,
                                                    tool_serial, tool_type);
          g_hash_table_insert (seat_impl->tools, libinput_tool, tool);
        }
    }

  if (evdev_device->last_tool != tool)
    {
      evdev_device->last_tool = tool;
      input_settings = seat_impl->input_settings;
      meta_input_settings_notify_tool_change (input_settings, input_device, tool);
    }
}

static double *
translate_tablet_axes (struct libinput_event_tablet_tool *tablet_event,
                       ClutterInputDeviceTool            *tool)
{
  double *axes = g_new0 (double, CLUTTER_INPUT_AXIS_LAST);
  struct libinput_tablet_tool *libinput_tool;
  double value;

  libinput_tool = libinput_event_tablet_tool_get_tool (tablet_event);

  value = libinput_event_tablet_tool_get_x (tablet_event);
  axes[CLUTTER_INPUT_AXIS_X] = value;
  value = libinput_event_tablet_tool_get_y (tablet_event);
  axes[CLUTTER_INPUT_AXIS_Y] = value;

  if (libinput_tablet_tool_has_distance (libinput_tool))
    {
      value = libinput_event_tablet_tool_get_distance (tablet_event);
      axes[CLUTTER_INPUT_AXIS_DISTANCE] = value;
    }

  if (libinput_tablet_tool_has_pressure (libinput_tool))
    {
      value = libinput_event_tablet_tool_get_pressure (tablet_event);
      value = meta_input_device_tool_native_translate_pressure_in_impl (tool, value);
      axes[CLUTTER_INPUT_AXIS_PRESSURE] = value;
    }

  if (libinput_tablet_tool_has_tilt (libinput_tool))
    {
      value = libinput_event_tablet_tool_get_tilt_x (tablet_event);
      axes[CLUTTER_INPUT_AXIS_XTILT] = value;
      value = libinput_event_tablet_tool_get_tilt_y (tablet_event);
      axes[CLUTTER_INPUT_AXIS_YTILT] = value;
    }

  if (libinput_tablet_tool_has_rotation (libinput_tool))
    {
      value = libinput_event_tablet_tool_get_rotation (tablet_event);
      axes[CLUTTER_INPUT_AXIS_ROTATION] = value;
    }

  if (libinput_tablet_tool_has_slider (libinput_tool))
    {
      value = libinput_event_tablet_tool_get_slider_position (tablet_event);
      axes[CLUTTER_INPUT_AXIS_SLIDER] = value;
    }

  if (libinput_tablet_tool_has_wheel (libinput_tool))
    {
      value = libinput_event_tablet_tool_get_wheel_delta (tablet_event);
      axes[CLUTTER_INPUT_AXIS_WHEEL] = value;
    }

  return axes;
}

static void
notify_continuous_axis (MetaSeatImpl                  *seat_impl,
                        ClutterInputDevice            *device,
                        uint64_t                       time_us,
                        ClutterScrollSource            scroll_source,
                        struct libinput_event_pointer *axis_event)
{
  double dx = 0.0, dy = 0.0;
  ClutterScrollFinishFlags finish_flags = CLUTTER_SCROLL_FINISHED_NONE;

  if (libinput_event_pointer_has_axis (axis_event,
                                       LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL))
    {
      dx = libinput_event_pointer_get_scroll_value (
          axis_event, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);

      if (fabs (dx) < DBL_EPSILON)
        finish_flags |= CLUTTER_SCROLL_FINISHED_HORIZONTAL;
    }
  if (libinput_event_pointer_has_axis (axis_event,
                                       LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL))
    {
      dy = libinput_event_pointer_get_scroll_value (
          axis_event, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);

      if (fabs (dy) < DBL_EPSILON)
        finish_flags |= CLUTTER_SCROLL_FINISHED_VERTICAL;
    }

  meta_seat_impl_notify_scroll_continuous_in_impl (seat_impl, device, time_us,
                                                   dx, dy,
                                                   scroll_source, finish_flags);
}

static void
notify_discrete_axis (MetaSeatImpl                  *seat_impl,
                      ClutterInputDevice            *device,
                      uint64_t                       time_us,
                      ClutterScrollSource            scroll_source,
                      struct libinput_event_pointer *axis_event)
{
  double dx_value120 = 0.0, dy_value120 = 0.0;

  if (libinput_event_pointer_has_axis (axis_event,
                                       LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL))
    {
      dx_value120 = libinput_event_pointer_get_scroll_value_v120 (
          axis_event, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
    }
  if (libinput_event_pointer_has_axis (axis_event,
                                       LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL))
    {
      dy_value120 = libinput_event_pointer_get_scroll_value_v120 (
          axis_event, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
    }

  meta_seat_impl_notify_discrete_scroll_in_impl (seat_impl, device,
                                                 time_us,
                                                 dx_value120, dy_value120,
                                                 scroll_source);
}

static void
handle_pointer_scroll (MetaSeatImpl          *seat_impl,
                       struct libinput_event *event,
                       ClutterScrollSource    scroll_source)
{
  struct libinput_device *libinput_device = libinput_event_get_device (event);
  ClutterInputDevice *device;
  uint64_t time_us;
  struct libinput_event_pointer *axis_event =
    libinput_event_get_pointer_event (event);

  device = libinput_device_get_user_data (libinput_device);
  time_us = libinput_event_pointer_get_time_usec (axis_event);

  /* libinput < 0.8 sent wheel click events with value 10. Since 0.8
   * the value is the angle of the click in degrees. To keep
   * backwards-compat with existing clients, we just send multiples of
   * the click count.
   */

  switch (scroll_source)
    {
    case CLUTTER_SCROLL_SOURCE_WHEEL:
      notify_discrete_axis (seat_impl, device, time_us, scroll_source,
                            axis_event);
      break;
    case CLUTTER_SCROLL_SOURCE_FINGER:
    case CLUTTER_SCROLL_SOURCE_CONTINUOUS:
    case CLUTTER_SCROLL_SOURCE_UNKNOWN:
      notify_continuous_axis (seat_impl, device, time_us, scroll_source,
                              axis_event);
      break;
    }
}

static void
process_tablet_axis (MetaSeatImpl          *seat_impl,
                     struct libinput_event *event)
{
  struct libinput_device *libinput_device = libinput_event_get_device (event);
  uint64_t time;
  double x, y, dx, dy, *axes;
  float stage_width, stage_height;
  ClutterInputDevice *device;
  struct libinput_event_tablet_tool *tablet_event =
    libinput_event_get_tablet_tool_event (event);
  MetaInputDeviceNative *evdev_device;

  device = libinput_device_get_user_data (libinput_device);
  evdev_device = META_INPUT_DEVICE_NATIVE (device);

  axes = translate_tablet_axes (tablet_event,
                                evdev_device->last_tool);

  meta_viewport_info_get_extents (seat_impl->viewports,
                                  &stage_width, &stage_height);

  time = libinput_event_tablet_tool_get_time_usec (tablet_event);

  if (meta_input_device_native_get_mapping_mode_in_impl (device) == META_INPUT_DEVICE_MAPPING_RELATIVE ||
      clutter_input_device_tool_get_tool_type (evdev_device->last_tool) == CLUTTER_INPUT_DEVICE_TOOL_MOUSE ||
      clutter_input_device_tool_get_tool_type (evdev_device->last_tool) == CLUTTER_INPUT_DEVICE_TOOL_LENS)
    {
      dx = libinput_event_tablet_tool_get_dx (tablet_event);
      dy = libinput_event_tablet_tool_get_dy (tablet_event);
      notify_relative_tool_motion_in_impl (device, time, dx, dy, axes);
    }
  else
    {
      x = libinput_event_tablet_tool_get_x_transformed (tablet_event, stage_width);
      y = libinput_event_tablet_tool_get_y_transformed (tablet_event, stage_height);
      notify_absolute_motion_in_impl (device, time, x, y, axes);
    }
}

static gboolean
process_device_event (MetaSeatImpl          *seat_impl,
                      struct libinput_event *event)
{
  gboolean handled = TRUE;
  struct libinput_device *libinput_device = libinput_event_get_device(event);
  ClutterInputDevice *device;
  MetaInputDeviceNative *device_native;

  switch (libinput_event_get_type (event))
    {
    case LIBINPUT_EVENT_KEYBOARD_KEY:
      {
        uint32_t key, key_state, seat_key_count;
        uint64_t time_us;
        struct libinput_event_keyboard *key_event =
          libinput_event_get_keyboard_event (event);

        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_keyboard_get_time_usec (key_event);
        key = libinput_event_keyboard_get_key (key_event);
        key_state = libinput_event_keyboard_get_key_state (key_event) ==
                    LIBINPUT_KEY_STATE_PRESSED;
        seat_key_count =
          libinput_event_keyboard_get_seat_key_count (key_event);

        /* Ignore key events that are not seat wide state changes. */
        if ((key_state == LIBINPUT_KEY_STATE_PRESSED &&
             seat_key_count != 1) ||
            (key_state == LIBINPUT_KEY_STATE_RELEASED &&
             seat_key_count != 0))
          {
            meta_topic (META_DEBUG_INPUT,
                        "Dropping key-%s of key 0x%x because seat-wide "
                        "key count is %d",
                        key_state == LIBINPUT_KEY_STATE_PRESSED ? "press" : "release",
                        key, seat_key_count);
            break;
          }

        meta_seat_impl_notify_key_in_impl (seat_impl,
                                           device,
                                           time_us, key, key_state, TRUE);

        break;
      }

    case LIBINPUT_EVENT_POINTER_MOTION:
      {
        struct libinput_event_pointer *pointer_event =
          libinput_event_get_pointer_event (event);
        uint64_t time_us;
        double dx;
        double dy;
        double dx_unaccel;
        double dy_unaccel;

        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_pointer_get_time_usec (pointer_event);
        dx = libinput_event_pointer_get_dx (pointer_event);
        dy = libinput_event_pointer_get_dy (pointer_event);
        dx_unaccel = libinput_event_pointer_get_dx_unaccelerated (pointer_event);
        dy_unaccel = libinput_event_pointer_get_dy_unaccelerated (pointer_event);

        meta_seat_impl_notify_relative_motion_in_impl (seat_impl,
                                                       device,
                                                       time_us,
                                                       dx, dy,
                                                       dx_unaccel, dy_unaccel,
                                                       NULL);

        break;
      }

    case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
      {
        uint64_t time_us;
        double x, y;
        float stage_width, stage_height;
        struct libinput_event_pointer *motion_event =
          libinput_event_get_pointer_event (event);
        device = libinput_device_get_user_data (libinput_device);

        meta_viewport_info_get_extents (seat_impl->viewports,
                                        &stage_width, &stage_height);

        time_us = libinput_event_pointer_get_time_usec (motion_event);
        x = libinput_event_pointer_get_absolute_x_transformed (motion_event,
                                                               stage_width);
        y = libinput_event_pointer_get_absolute_y_transformed (motion_event,
                                                               stage_height);

        meta_seat_impl_notify_absolute_motion_in_impl (seat_impl,
                                                       device,
                                                       time_us,
                                                       x, y,
                                                       NULL);

        break;
      }

    case LIBINPUT_EVENT_POINTER_BUTTON:
      {
        uint32_t button, button_state, seat_button_count;
        uint64_t time_us;
        struct libinput_event_pointer *button_event =
          libinput_event_get_pointer_event (event);
        device = libinput_device_get_user_data (libinput_device);

        time_us = libinput_event_pointer_get_time_usec (button_event);
        button = libinput_event_pointer_get_button (button_event);
        button_state = libinput_event_pointer_get_button_state (button_event) ==
                       LIBINPUT_BUTTON_STATE_PRESSED;
        seat_button_count =
          libinput_event_pointer_get_seat_button_count (button_event);

        /* Ignore button events that are not seat wide state changes. */
        if ((button_state == LIBINPUT_BUTTON_STATE_PRESSED &&
             seat_button_count != 1) ||
            (button_state == LIBINPUT_BUTTON_STATE_RELEASED &&
             seat_button_count != 0))
          {
            meta_topic (META_DEBUG_INPUT,
                        "Dropping button-%s of button 0x%x because seat-wide "
                        "button count is %d",
                        button_state == LIBINPUT_BUTTON_STATE_PRESSED ? "press" : "release",
                        button, seat_button_count);
            break;
          }

        meta_seat_impl_notify_button_in_impl (seat_impl, device,
                                              time_us, button, button_state);
        break;
      }

    case LIBINPUT_EVENT_POINTER_AXIS:
      /* This event must be ignored in favor of the SCROLL_* events */
      handled = FALSE;
      break;

    case LIBINPUT_EVENT_POINTER_SCROLL_WHEEL:
      handle_pointer_scroll (seat_impl, event, CLUTTER_SCROLL_SOURCE_WHEEL);
      break;

    case LIBINPUT_EVENT_POINTER_SCROLL_FINGER:
      handle_pointer_scroll (seat_impl, event, CLUTTER_SCROLL_SOURCE_FINGER);
      break;

    case LIBINPUT_EVENT_POINTER_SCROLL_CONTINUOUS:
      handle_pointer_scroll (seat_impl, event,
                             CLUTTER_SCROLL_SOURCE_CONTINUOUS);
      break;

    case LIBINPUT_EVENT_TOUCH_DOWN:
      {
        int seat_slot;
        uint64_t time_us;
        double x, y;
        float stage_width, stage_height;
        MetaTouchState *touch_state;
        struct libinput_event_touch *touch_event =
          libinput_event_get_touch_event (event);

        device = libinput_device_get_user_data (libinput_device);
        device_native = META_INPUT_DEVICE_NATIVE (device);

        meta_viewport_info_get_extents (seat_impl->viewports,
                                        &stage_width, &stage_height);

        seat_slot = libinput_event_touch_get_seat_slot (touch_event);
        time_us = libinput_event_touch_get_time_usec (touch_event);
        x = libinput_event_touch_get_x_transformed (touch_event,
                                                    stage_width);
        y = libinput_event_touch_get_y_transformed (touch_event,
                                                    stage_height);

        g_rw_lock_writer_lock (&seat_impl->state_lock);

        touch_state = meta_seat_impl_acquire_touch_state_in_impl (seat_impl, seat_slot);
        touch_state->coords.x = x;
        touch_state->coords.y = y;
        meta_input_device_native_translate_coordinates_in_impl (device,
                                                                seat_impl->viewports,
                                                                &touch_state->coords.x,
                                                                &touch_state->coords.y);

        g_rw_lock_writer_unlock (&seat_impl->state_lock);

        meta_seat_impl_notify_touch_event_in_impl (seat_impl, device,
                                                   CLUTTER_TOUCH_BEGIN,
                                                   time_us,
                                                   touch_state->seat_slot,
                                                   touch_state->coords.x,
                                                   touch_state->coords.y);
        break;
      }

    case LIBINPUT_EVENT_TOUCH_UP:
      {
        int seat_slot;
        uint64_t time_us;
        MetaTouchState *touch_state;
        struct libinput_event_touch *touch_event =
          libinput_event_get_touch_event (event);

        device = libinput_device_get_user_data (libinput_device);
        device_native = META_INPUT_DEVICE_NATIVE (device);

        seat_slot = libinput_event_touch_get_seat_slot (touch_event);
        time_us = libinput_event_touch_get_time_usec (touch_event);
        touch_state = meta_seat_impl_lookup_touch_state_in_impl (seat_impl, seat_slot);
        if (!touch_state)
          break;

        meta_seat_impl_notify_touch_event_in_impl (seat_impl, device,
                                                   CLUTTER_TOUCH_END, time_us,
                                                   touch_state->seat_slot,
                                                   touch_state->coords.x,
                                                   touch_state->coords.y);

        g_rw_lock_writer_lock (&seat_impl->state_lock);
        meta_seat_impl_release_touch_state_in_impl (seat_impl, seat_slot);
        g_rw_lock_writer_unlock (&seat_impl->state_lock);
        break;
      }

    case LIBINPUT_EVENT_TOUCH_MOTION:
      {
        int seat_slot;
        uint64_t time_us;
        double x, y;
        float stage_width, stage_height;
        MetaTouchState *touch_state;
        struct libinput_event_touch *touch_event =
          libinput_event_get_touch_event (event);

        device = libinput_device_get_user_data (libinput_device);
        device_native = META_INPUT_DEVICE_NATIVE (device);

        meta_viewport_info_get_extents (seat_impl->viewports,
                                        &stage_width, &stage_height);

        seat_slot = libinput_event_touch_get_seat_slot (touch_event);
        time_us = libinput_event_touch_get_time_usec (touch_event);
        x = libinput_event_touch_get_x_transformed (touch_event,
                                                    stage_width);
        y = libinput_event_touch_get_y_transformed (touch_event,
                                                    stage_height);

        g_rw_lock_writer_lock (&seat_impl->state_lock);
        touch_state = meta_seat_impl_lookup_touch_state_in_impl (seat_impl, seat_slot);
        if (touch_state)
          {
            touch_state->coords.x = x;
            touch_state->coords.y = y;
            meta_input_device_native_translate_coordinates_in_impl (device,
                                                                    seat_impl->viewports,
                                                                    &touch_state->coords.x,
                                                                    &touch_state->coords.y);
          }
        g_rw_lock_writer_unlock (&seat_impl->state_lock);

        if (!touch_state)
          break;

        meta_seat_impl_notify_touch_event_in_impl (seat_impl, device,
                                                   CLUTTER_TOUCH_UPDATE,
                                                   time_us,
                                                   touch_state->seat_slot,
                                                   touch_state->coords.x,
                                                   touch_state->coords.y);
        break;
      }
    case LIBINPUT_EVENT_TOUCH_CANCEL:
      {
        int seat_slot;
        MetaTouchState *touch_state;
        uint64_t time_us;
        struct libinput_event_touch *touch_event =
          libinput_event_get_touch_event (event);

        device = libinput_device_get_user_data (libinput_device);
        device_native = META_INPUT_DEVICE_NATIVE (device);
        time_us = libinput_event_touch_get_time_usec (touch_event);

        seat_slot = libinput_event_touch_get_seat_slot (touch_event);
        touch_state = meta_seat_impl_lookup_touch_state_in_impl (seat_impl, seat_slot);
        if (!touch_state)
          break;

        meta_seat_impl_notify_touch_event_in_impl (touch_state->seat_impl,
                                                   CLUTTER_INPUT_DEVICE (device_native),
                                                   CLUTTER_TOUCH_CANCEL,
                                                   time_us,
                                                   touch_state->seat_slot,
                                                   touch_state->coords.x,
                                                   touch_state->coords.y);

        meta_seat_impl_release_touch_state_in_impl (seat_impl, seat_slot);
        break;
      }
    case LIBINPUT_EVENT_GESTURE_PINCH_BEGIN:
    case LIBINPUT_EVENT_GESTURE_PINCH_END:
      {
        struct libinput_event_gesture *gesture_event =
          libinput_event_get_gesture_event (event);
        ClutterTouchpadGesturePhase phase;
        uint32_t n_fingers;
        uint64_t time_us;

        if (libinput_event_get_type (event) == LIBINPUT_EVENT_GESTURE_PINCH_BEGIN)
          phase = CLUTTER_TOUCHPAD_GESTURE_PHASE_BEGIN;
        else
          phase = libinput_event_gesture_get_cancelled (gesture_event) ?
            CLUTTER_TOUCHPAD_GESTURE_PHASE_CANCEL : CLUTTER_TOUCHPAD_GESTURE_PHASE_END;

        n_fingers = libinput_event_gesture_get_finger_count (gesture_event);
        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_gesture_get_time_usec (gesture_event);
        notify_pinch_gesture_event (device, phase, time_us, 0, 0, 0, 0, 0, 0, n_fingers);
        break;
      }
    case LIBINPUT_EVENT_GESTURE_PINCH_UPDATE:
      {
        struct libinput_event_gesture *gesture_event =
          libinput_event_get_gesture_event (event);
        double angle_delta, scale, dx, dy, dx_unaccel, dy_unaccel;
        uint32_t n_fingers;
        uint64_t time_us;

        n_fingers = libinput_event_gesture_get_finger_count (gesture_event);
        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_gesture_get_time_usec (gesture_event);
        angle_delta = libinput_event_gesture_get_angle_delta (gesture_event);
        scale = libinput_event_gesture_get_scale (gesture_event);
        dx = libinput_event_gesture_get_dx (gesture_event);
        dy = libinput_event_gesture_get_dy (gesture_event);
        dx_unaccel = libinput_event_gesture_get_dx_unaccelerated (gesture_event);
        dy_unaccel = libinput_event_gesture_get_dy_unaccelerated (gesture_event);

        notify_pinch_gesture_event (device,
                                    CLUTTER_TOUCHPAD_GESTURE_PHASE_UPDATE,
                                    time_us, dx, dy, dx_unaccel, dy_unaccel,
                                    angle_delta, scale, n_fingers);
        break;
      }
    case LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN:
    case LIBINPUT_EVENT_GESTURE_SWIPE_END:
      {
        struct libinput_event_gesture *gesture_event =
          libinput_event_get_gesture_event (event);
        ClutterTouchpadGesturePhase phase;
        uint32_t n_fingers;
        uint64_t time_us;

        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_gesture_get_time_usec (gesture_event);
        n_fingers = libinput_event_gesture_get_finger_count (gesture_event);

        if (libinput_event_get_type (event) == LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN)
          phase = CLUTTER_TOUCHPAD_GESTURE_PHASE_BEGIN;
        else
          phase = libinput_event_gesture_get_cancelled (gesture_event) ?
            CLUTTER_TOUCHPAD_GESTURE_PHASE_CANCEL : CLUTTER_TOUCHPAD_GESTURE_PHASE_END;

        notify_swipe_gesture_event (device, phase, time_us, n_fingers, 0, 0, 0, 0);
        break;
      }
    case LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE:
      {
        struct libinput_event_gesture *gesture_event =
          libinput_event_get_gesture_event (event);
        uint32_t n_fingers;
        uint64_t time_us;
        double dx, dy, dx_unaccel, dy_unaccel;

        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_gesture_get_time_usec (gesture_event);
        n_fingers = libinput_event_gesture_get_finger_count (gesture_event);
        dx = libinput_event_gesture_get_dx (gesture_event);
        dy = libinput_event_gesture_get_dy (gesture_event);
        dx_unaccel = libinput_event_gesture_get_dx_unaccelerated (gesture_event);
        dy_unaccel = libinput_event_gesture_get_dy_unaccelerated (gesture_event);

        notify_swipe_gesture_event (device,
                                    CLUTTER_TOUCHPAD_GESTURE_PHASE_UPDATE,
                                    time_us, n_fingers, dx, dy, dx_unaccel, dy_unaccel);
        break;
      }
    case LIBINPUT_EVENT_GESTURE_HOLD_BEGIN:
    case LIBINPUT_EVENT_GESTURE_HOLD_END:
      {
        struct libinput_event_gesture *gesture_event =
          libinput_event_get_gesture_event (event);
        ClutterTouchpadGesturePhase phase;
        uint32_t n_fingers;
        uint64_t time_us;

        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_gesture_get_time_usec (gesture_event);
        n_fingers = libinput_event_gesture_get_finger_count (gesture_event);

        if (libinput_event_get_type (event) == LIBINPUT_EVENT_GESTURE_HOLD_BEGIN)
          phase = CLUTTER_TOUCHPAD_GESTURE_PHASE_BEGIN;
        else
          phase = libinput_event_gesture_get_cancelled (gesture_event) ?
            CLUTTER_TOUCHPAD_GESTURE_PHASE_CANCEL : CLUTTER_TOUCHPAD_GESTURE_PHASE_END;

        notify_hold_gesture_event (device, phase, time_us, n_fingers);
        break;
      }
    case LIBINPUT_EVENT_TABLET_TOOL_AXIS:
      {
        process_tablet_axis (seat_impl, event);
        break;
      }
    case LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY:
      {
        uint64_t time;
        struct libinput_event_tablet_tool *tablet_event =
          libinput_event_get_tablet_tool_event (event);
        struct libinput_tablet_tool *libinput_tool = NULL;
        enum libinput_tablet_tool_proximity_state state;
        gboolean in;

        state = libinput_event_tablet_tool_get_proximity_state (tablet_event);
        time = libinput_event_tablet_tool_get_time_usec (tablet_event);
        device = libinput_device_get_user_data (libinput_device);
        in = state == LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_IN;

        libinput_tool = libinput_event_tablet_tool_get_tool (tablet_event);

        if (in)
          input_device_update_tool (seat_impl, device, libinput_tool);
        notify_proximity (device, time, in);
        if (!in)
          input_device_update_tool (seat_impl, device, NULL);

        break;
      }
    case LIBINPUT_EVENT_TABLET_TOOL_BUTTON:
      {
        uint64_t time_us;
        uint32_t button_state;
        struct libinput_event_tablet_tool *tablet_event =
          libinput_event_get_tablet_tool_event (event);
        uint32_t tablet_button;

        process_tablet_axis (seat_impl, event);

        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_tablet_tool_get_time_usec (tablet_event);
        tablet_button = libinput_event_tablet_tool_get_button (tablet_event);

        button_state = libinput_event_tablet_tool_get_button_state (tablet_event) ==
                       LIBINPUT_BUTTON_STATE_PRESSED;

        meta_seat_impl_notify_button_in_impl (seat_impl, device,
                                              time_us, tablet_button, button_state);
        break;
      }
    case LIBINPUT_EVENT_TABLET_TOOL_TIP:
      {
        uint64_t time_us;
        uint32_t button_state;
        struct libinput_event_tablet_tool *tablet_event =
          libinput_event_get_tablet_tool_event (event);

        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_tablet_tool_get_time_usec (tablet_event);

        button_state = libinput_event_tablet_tool_get_tip_state (tablet_event) ==
                       LIBINPUT_TABLET_TOOL_TIP_DOWN;

        /* To avoid jumps on tip, notify axes before the tip down event
           but after the tip up event */
        if (button_state)
          process_tablet_axis (seat_impl, event);

        meta_seat_impl_notify_button_in_impl (seat_impl, device,
                                              time_us, BTN_TOUCH, button_state);
        if (!button_state)
          process_tablet_axis (seat_impl, event);
        break;
      }
    case LIBINPUT_EVENT_TABLET_PAD_BUTTON:
      {
        uint64_t time;
        uint32_t button_state, button, group, mode;
        struct libinput_tablet_pad_mode_group *mode_group;
        struct libinput_event_tablet_pad *pad_event =
          libinput_event_get_tablet_pad_event (event);

        device = libinput_device_get_user_data (libinput_device);
        time = libinput_event_tablet_pad_get_time_usec (pad_event);

        mode_group = libinput_event_tablet_pad_get_mode_group (pad_event);
        group = libinput_tablet_pad_mode_group_get_index (mode_group);
        mode = libinput_event_tablet_pad_get_mode (pad_event);

        button = libinput_event_tablet_pad_get_button_number (pad_event);
        button_state = libinput_event_tablet_pad_get_button_state (pad_event) ==
                       LIBINPUT_BUTTON_STATE_PRESSED;
        notify_pad_button (device, time, button, group, mode, button_state);
        break;
      }
    case LIBINPUT_EVENT_TABLET_PAD_STRIP:
      {
        uint64_t time;
        uint32_t number, source, group, mode;
        struct libinput_tablet_pad_mode_group *mode_group;
        struct libinput_event_tablet_pad *pad_event =
          libinput_event_get_tablet_pad_event (event);
        double value;

        device = libinput_device_get_user_data (libinput_device);
        time = libinput_event_tablet_pad_get_time_usec (pad_event);
        number = libinput_event_tablet_pad_get_strip_number (pad_event);
        value = libinput_event_tablet_pad_get_strip_position (pad_event);
        source = libinput_event_tablet_pad_get_strip_source (pad_event);

        mode_group = libinput_event_tablet_pad_get_mode_group (pad_event);
        group = libinput_tablet_pad_mode_group_get_index (mode_group);
        mode = libinput_event_tablet_pad_get_mode (pad_event);

        notify_pad_strip (device, time, number, source, group, mode, value);
        break;
      }
    case LIBINPUT_EVENT_TABLET_PAD_RING:
      {
        uint64_t time;
        uint32_t number, source, group, mode;
        struct libinput_tablet_pad_mode_group *mode_group;
        struct libinput_event_tablet_pad *pad_event =
          libinput_event_get_tablet_pad_event (event);
        double angle;

        device = libinput_device_get_user_data (libinput_device);
        time = libinput_event_tablet_pad_get_time_usec (pad_event);
        number = libinput_event_tablet_pad_get_ring_number (pad_event);
        angle = libinput_event_tablet_pad_get_ring_position (pad_event);
        source = libinput_event_tablet_pad_get_ring_source (pad_event);

        mode_group = libinput_event_tablet_pad_get_mode_group (pad_event);
        group = libinput_tablet_pad_mode_group_get_index (mode_group);
        mode = libinput_event_tablet_pad_get_mode (pad_event);

        notify_pad_ring (device, time, number, source, group, mode, angle);
        break;
      }
    case LIBINPUT_EVENT_SWITCH_TOGGLE:
      {
        struct libinput_event_switch *switch_event =
          libinput_event_get_switch_event (event);
        enum libinput_switch sw =
          libinput_event_switch_get_switch (switch_event);
        enum libinput_switch_state state =
          libinput_event_switch_get_switch_state (switch_event);

        if (sw == LIBINPUT_SWITCH_TABLET_MODE)
          {
            seat_impl->tablet_mode_switch_state = (state == LIBINPUT_SWITCH_STATE_ON);
            update_touch_mode (seat_impl);
          }
        break;
      }
    default:
      handled = FALSE;
    }

  return handled;
}

static void
process_event (MetaSeatImpl          *seat_impl,
               struct libinput_event *event)
{
  if (process_base_event (seat_impl, event))
    return;
  if (process_device_event (seat_impl, event))
    return;
}

static void
process_events (MetaSeatImpl *seat_impl)
{
  struct libinput_event *event;

  COGL_TRACE_BEGIN_SCOPED (MetaSeatImplProcessEvents,
                           "Meta::SeatImpl::process_events()");

  while ((event = libinput_get_event (seat_impl->libinput)))
    {
      process_event (seat_impl, event);
      libinput_event_destroy (event);
    }
}

static int
open_restricted (const char *path,
                 int         open_flags,
                 void       *user_data)
{
  MetaSeatImpl *seat_impl = user_data;
  MetaSeatImplPrivate *priv = meta_seat_impl_get_instance_private (seat_impl);
  MetaBackend *backend = meta_seat_native_get_backend (seat_impl->seat_native);
  MetaDevicePool *device_pool =
    meta_backend_native_get_device_pool (META_BACKEND_NATIVE (backend));
  MetaDeviceFileFlags flags;
  g_autoptr (GError) error = NULL;
  MetaDeviceFile *device_file;
  int fd;

  flags = META_DEVICE_FILE_FLAG_NONE;
  if (!(open_flags & (O_RDWR | O_WRONLY)))
    flags |= META_DEVICE_FILE_FLAG_READ_ONLY;

  if (!g_str_has_prefix (path, "/sys/"))
    flags |= META_DEVICE_FILE_FLAG_TAKE_CONTROL;

  device_file = meta_device_pool_open (device_pool, path, flags, &error);
  if (!device_file)
    {
      g_warning ("Could not open device %s: %s", path, error->message);
      return -1;
    }

  fd = meta_device_file_get_fd (device_file);
  g_hash_table_insert (priv->device_files, GINT_TO_POINTER (fd), device_file);

  return fd;
}

static void
close_restricted (int   fd,
                  void *user_data)
{
  MetaSeatImpl *seat_impl = user_data;
  MetaSeatImplPrivate *priv = meta_seat_impl_get_instance_private (seat_impl);

  g_hash_table_remove (priv->device_files, GINT_TO_POINTER (fd));
}

static const struct libinput_interface libinput_interface = {
  open_restricted,
  close_restricted
};

static void
kbd_a11y_changed_cb (MetaInputSettings   *input_settings,
                     MetaKbdA11ySettings *a11y_settings,
                     MetaSeatImpl        *seat_impl)
{
  MetaInputDeviceNative *keyboard;

  keyboard = META_INPUT_DEVICE_NATIVE (seat_impl->core_keyboard);
  meta_input_device_native_apply_kbd_a11y_settings_in_impl (keyboard, a11y_settings);
}

static void
meta_seat_impl_set_keyboard_numlock_in_impl (MetaSeatImpl *seat_impl,
                                             gboolean      numlock_state)
{
  xkb_mod_mask_t depressed_mods;
  xkb_mod_mask_t latched_mods;
  xkb_mod_mask_t locked_mods;
  xkb_mod_mask_t group_mods;
  xkb_mod_mask_t numlock;
  struct xkb_keymap *xkb_keymap;
  MetaKeymapNative *keymap;

  keymap = seat_impl->keymap;
  xkb_keymap = meta_keymap_native_get_keyboard_map_in_impl (keymap);

  numlock = (1 << xkb_keymap_mod_get_index (xkb_keymap, "Mod2"));

  depressed_mods =
    xkb_state_serialize_mods (seat_impl->xkb, XKB_STATE_MODS_DEPRESSED);
  latched_mods =
    xkb_state_serialize_mods (seat_impl->xkb, XKB_STATE_MODS_LATCHED);
  locked_mods =
    xkb_state_serialize_mods (seat_impl->xkb, XKB_STATE_MODS_LOCKED);
  group_mods =
    xkb_state_serialize_layout (seat_impl->xkb, XKB_STATE_LAYOUT_EFFECTIVE);

  if (numlock_state)
    locked_mods |= numlock;
  else
    locked_mods &= ~numlock;

  xkb_state_update_mask (seat_impl->xkb,
                         depressed_mods,
                         latched_mods,
                         locked_mods,
                         0, 0,
                         group_mods);

  meta_seat_impl_sync_leds_in_impl (seat_impl);
  meta_keymap_native_update_in_impl (seat_impl->keymap,
                                     seat_impl,
                                     seat_impl->xkb);
}

static gboolean
meta_libinput_source_prepare (gpointer user_data)
{
  MetaSeatImpl *seat_impl = META_SEAT_IMPL (user_data);

  switch (libinput_next_event_type (seat_impl->libinput))
    {
    case LIBINPUT_EVENT_NONE:
      return FALSE;
    default:
      return TRUE;
    }
}

static gboolean
meta_libinput_source_dispatch (gpointer user_data)
{
  MetaSeatImpl *seat_impl = META_SEAT_IMPL (user_data);

  dispatch_libinput (seat_impl);

  return G_SOURCE_CONTINUE;
}

static gboolean
init_libinput (MetaSeatImpl  *seat_impl,
               GError       **error)
{
  struct udev *udev;
  struct libinput *libinput;

  udev = udev_new ();
  if (G_UNLIKELY (udev == NULL))
    {
      g_warning ("Failed to create udev object");
      seat_impl->input_thread_initialized = TRUE;
      return FALSE;
    }

  libinput = libinput_udev_create_context (&libinput_interface,
                                           seat_impl, udev);
  udev_unref (udev);

  if (libinput == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to create the libinput object.");
      return FALSE;
    }

  if (libinput_udev_assign_seat (libinput, seat_impl->seat_id) == -1)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to assign a seat to the libinput object.");
      libinput_unref (seat_impl->libinput);
      return FALSE;
    }

  seat_impl->libinput = libinput;

  process_events (seat_impl);

  return TRUE;
}

static void
init_libinput_source (MetaSeatImpl *seat_impl)
{
  int fd;
  GSource *source;

  fd = libinput_get_fd (seat_impl->libinput);
  source = meta_create_fd_source (fd,
                                  "[mutter] libinput",
                                  meta_libinput_source_prepare,
                                  meta_libinput_source_dispatch,
                                  seat_impl,
                                  NULL);
  seat_impl->libinput_source = source;
  g_source_attach (source, seat_impl->input_context);
  g_source_unref (source);
}

static gpointer
input_thread (MetaSeatImpl *seat_impl)
{
  MetaSeatImplPrivate *priv = meta_seat_impl_get_instance_private (seat_impl);
#ifdef HAVE_PROFILER
  MetaBackend *backend = meta_seat_native_get_backend (seat_impl->seat_native);
  MetaContext *context = meta_backend_get_context (backend);
  MetaProfiler *profiler = meta_context_get_profiler (context);
#endif
  struct xkb_keymap *xkb_keymap;

  g_main_context_push_thread_default (seat_impl->input_context);

#ifdef HAVE_PROFILER
  meta_profiler_register_thread (profiler,
                                 seat_impl->input_context,
                                 "Mutter Input Thread");
#endif

  priv->device_files =
    g_hash_table_new_full (NULL, NULL,
                           NULL,
                           (GDestroyNotify) meta_device_file_release);

  seat_impl->input_settings = meta_input_settings_native_new_in_impl (seat_impl);
  g_signal_connect_object (seat_impl->input_settings, "kbd-a11y-changed",
                           G_CALLBACK (kbd_a11y_changed_cb), seat_impl, 0);

  seat_impl->keymap = g_object_new (META_TYPE_KEYMAP_NATIVE, NULL);

  xkb_keymap = meta_keymap_native_get_keyboard_map_in_impl (seat_impl->keymap);

  if (xkb_keymap)
    {
      seat_impl->xkb = xkb_state_new (xkb_keymap);

      seat_impl->caps_lock_led =
        xkb_keymap_led_get_index (xkb_keymap, XKB_LED_NAME_CAPS);
      seat_impl->num_lock_led =
        xkb_keymap_led_get_index (xkb_keymap, XKB_LED_NAME_NUM);
      seat_impl->scroll_lock_led =
        xkb_keymap_led_get_index (xkb_keymap, XKB_LED_NAME_SCROLL);
    }

  if (meta_input_settings_maybe_restore_numlock_state (seat_impl->input_settings))
    meta_seat_impl_set_keyboard_numlock_in_impl (seat_impl, TRUE);

  if (!(seat_impl->flags & META_SEAT_NATIVE_FLAG_NO_LIBINPUT))
    {
      g_autoptr (GError) error = NULL;

      if (!init_libinput (seat_impl, &error))
        {
          g_critical ("Failed to initialize seat: %s", error->message);
          seat_impl->input_thread_initialized = TRUE;
          return NULL;
        }
    }

  seat_impl->has_touchscreen = has_touchscreen (seat_impl);
  seat_impl->has_tablet_switch = has_tablet_switch (seat_impl);
  update_touch_mode (seat_impl);

  g_mutex_lock (&seat_impl->init_mutex);
  seat_impl->input_thread_initialized = TRUE;
  g_cond_signal (&seat_impl->init_cond);
  g_mutex_unlock (&seat_impl->init_mutex);

  seat_impl->input_loop = g_main_loop_new (seat_impl->input_context, FALSE);
  g_main_loop_run (seat_impl->input_loop);
  g_main_loop_unref (seat_impl->input_loop);

#ifdef HAVE_PROFILER
  meta_profiler_unregister_thread (profiler, seat_impl->input_context);
#endif

  g_main_context_pop_thread_default (seat_impl->input_context);

  return NULL;
}

static gboolean
meta_seat_impl_initable_init (GInitable     *initable,
                              GCancellable  *cancellable,
                              GError       **error)
{
  MetaSeatImpl *seat_impl = META_SEAT_IMPL (initable);

  seat_impl->input_context = g_main_context_new ();
  seat_impl->main_context = g_main_context_ref_thread_default ();
  g_assert (seat_impl->main_context == g_main_context_default ());

  seat_impl->input_thread =
    g_thread_try_new ("Mutter Input Thread",
                      (GThreadFunc) input_thread,
                      initable,
                      error);
  if (!seat_impl->input_thread)
    return FALSE;

  /* Initialize thread synchronously */
  g_mutex_lock (&seat_impl->init_mutex);
  while (!seat_impl->input_thread_initialized)
    g_cond_wait (&seat_impl->init_cond, &seat_impl->init_mutex);
  g_mutex_unlock (&seat_impl->init_mutex);

  return TRUE;
}

static void
meta_seat_impl_constructed (GObject *object)
{
  MetaSeatImpl *seat_impl = META_SEAT_IMPL (object);
  ClutterInputDevice *device;

  device = meta_input_device_native_new_virtual (
    CLUTTER_SEAT (seat_impl->seat_native), CLUTTER_POINTER_DEVICE,
    CLUTTER_INPUT_MODE_LOGICAL);
  seat_impl->pointer_x = INITIAL_POINTER_X;
  seat_impl->pointer_y = INITIAL_POINTER_Y;
  meta_input_device_native_set_coords_in_impl (META_INPUT_DEVICE_NATIVE (device),
                                               seat_impl->pointer_x,
                                               seat_impl->pointer_y);
  seat_impl->core_pointer = device;

  device = meta_input_device_native_new_virtual (
    CLUTTER_SEAT (seat_impl->seat_native), CLUTTER_KEYBOARD_DEVICE,
    CLUTTER_INPUT_MODE_LOGICAL);
  seat_impl->core_keyboard = device;

  if (G_OBJECT_CLASS (meta_seat_impl_parent_class)->constructed)
    G_OBJECT_CLASS (meta_seat_impl_parent_class)->constructed (object);
}

static void
meta_seat_impl_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  MetaSeatImpl *seat_impl = META_SEAT_IMPL (object);

  switch (prop_id)
    {
    case PROP_SEAT:
      seat_impl->seat_native = g_value_get_object (value);
      break;
    case PROP_SEAT_ID:
      seat_impl->seat_id = g_value_dup_string (value);
      break;
    case PROP_FLAGS:
      seat_impl->flags = g_value_get_flags (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_seat_impl_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  MetaSeatImpl *seat_impl = META_SEAT_IMPL (object);

  switch (prop_id)
    {
    case PROP_SEAT:
      g_value_set_object (value, seat_impl->seat_native);
      break;
    case PROP_SEAT_ID:
      g_value_set_string (value, seat_impl->seat_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static gboolean
destroy_in_impl (GTask *task)
{
  MetaSeatImpl *seat_impl = g_task_get_source_object (task);
  MetaSeatImplPrivate *priv = meta_seat_impl_get_instance_private (seat_impl);
  gboolean numlock_active;

  g_slist_foreach (seat_impl->devices,
                   (GFunc) meta_input_device_native_detach_libinput_in_impl,
                   NULL);
  g_slist_free_full (seat_impl->devices, g_object_unref);
  seat_impl->devices = NULL;

  g_clear_pointer (&seat_impl->libinput, libinput_unref);
  g_clear_pointer (&seat_impl->tools, g_hash_table_unref);
  g_clear_pointer (&seat_impl->touch_states, g_hash_table_destroy);
  g_clear_pointer (&seat_impl->libinput_source, g_source_destroy);

  numlock_active =
    xkb_state_mod_name_is_active (seat_impl->xkb, XKB_MOD_NAME_NUM,
                                  XKB_STATE_MODS_LATCHED |
                                  XKB_STATE_MODS_LOCKED);
  meta_input_settings_maybe_save_numlock_state (seat_impl->input_settings,
                                                numlock_active);

  g_clear_pointer (&seat_impl->xkb, xkb_state_unref);

  meta_seat_impl_clear_repeat_source (seat_impl);

  g_clear_pointer (&priv->device_files, g_hash_table_destroy);

  g_main_loop_quit (seat_impl->input_loop);
  g_task_return_boolean (task, TRUE);

  return G_SOURCE_REMOVE;
}

void
meta_seat_impl_destroy (MetaSeatImpl *seat_impl)
{
  if (seat_impl->input_thread)
    {
      GTask *task;

      task = g_task_new (seat_impl, NULL, NULL, NULL);
      meta_seat_impl_run_input_task (seat_impl, task,
                                     (GSourceFunc) destroy_in_impl);
      g_object_unref (task);

      g_thread_join (seat_impl->input_thread);
      seat_impl->input_thread = NULL;
      g_assert (!seat_impl->libinput);
    }

  g_object_unref (seat_impl);
}

static void
meta_seat_impl_finalize (GObject *object)
{
  MetaSeatImpl *seat_impl = META_SEAT_IMPL (object);

  g_assert (!seat_impl->libinput);
  g_assert (!seat_impl->tools);
  g_assert (!seat_impl->libinput_source);

  g_free (seat_impl->seat_id);

  g_rw_lock_clear (&seat_impl->state_lock);

  G_OBJECT_CLASS (meta_seat_impl_parent_class)->finalize (object);
}

ClutterInputDevice *
meta_seat_impl_get_pointer (MetaSeatImpl *seat_impl)
{
  return seat_impl->core_pointer;
}

ClutterInputDevice *
meta_seat_impl_get_keyboard (MetaSeatImpl *seat_impl)
{
  return seat_impl->core_keyboard;
}

MetaKeymapNative *
meta_seat_impl_get_keymap (MetaSeatImpl *seat_impl)
{
  return g_object_ref (seat_impl->keymap);
}

static gboolean
warp_pointer_in_impl (GTask *task)
{
  MetaSeatImpl *seat_impl = g_task_get_source_object (task);
  graphene_point_t *point;

  point = g_task_get_task_data (task);
  notify_absolute_motion_in_impl (seat_impl->core_pointer, 0,
                                  point->x, point->y, NULL);
  g_task_return_boolean (task, TRUE);

  return G_SOURCE_REMOVE;
}

void
meta_seat_impl_warp_pointer (MetaSeatImpl *seat_impl,
                             int           x,
                             int           y)
{
  graphene_point_t *point;
  GTask *task;

  point = graphene_point_alloc ();
  point->x = x;
  point->y = y;

  task = g_task_new (seat_impl, NULL, NULL, NULL);
  g_task_set_task_data (task, point, (GDestroyNotify) graphene_point_free);
  meta_seat_impl_run_input_task (seat_impl, task,
                                 (GSourceFunc) warp_pointer_in_impl);
  g_object_unref (task);
}

typedef struct
{
  graphene_point_t position;
  gboolean done;
  GMutex mutex;
  GCond cond;
} InitPointerPositionData;

static gboolean
init_pointer_position_in_impl (GTask *task)
{
  MetaSeatImpl *seat_impl = g_task_get_source_object (task);
  InitPointerPositionData *data = g_task_get_task_data (task);
  MetaInputDeviceNative *core_pointer =
    META_INPUT_DEVICE_NATIVE (seat_impl->core_pointer);

  seat_impl->pointer_x = data->position.x;
  seat_impl->pointer_y = data->position.y;
  core_pointer->pointer_x = data->position.x;
  core_pointer->pointer_y = data->position.y;
  g_task_return_boolean (task, TRUE);

  g_mutex_lock (&data->mutex);
  data->done = TRUE;
  g_cond_signal (&data->cond);
  g_mutex_unlock (&data->mutex);

  return G_SOURCE_REMOVE;
}

void
meta_seat_impl_init_pointer_position (MetaSeatImpl *seat_impl,
                                      float         x,
                                      float         y)
{
  InitPointerPositionData data = {};
  g_autoptr (GTask) task = NULL;

  data.position.x = x;
  data.position.y = y;
  g_mutex_init (&data.mutex);
  g_cond_init (&data.cond);

  task = g_task_new (seat_impl, NULL, NULL, NULL);
  g_task_set_task_data (task, &data, NULL);
  meta_seat_impl_run_input_task (seat_impl, task,
                                 (GSourceFunc) init_pointer_position_in_impl);

  g_mutex_lock (&data.mutex);
  while (!data.done)
    g_cond_wait (&data.cond, &data.mutex);
  g_mutex_unlock (&data.mutex);

  g_mutex_clear (&data.mutex);
  g_cond_clear (&data.cond);
}

gboolean
meta_seat_impl_query_state (MetaSeatImpl         *seat_impl,
                            ClutterInputDevice   *device,
                            ClutterEventSequence *sequence,
                            graphene_point_t     *coords,
                            ClutterModifierType  *modifiers)
{
  MetaInputDeviceNative *device_native = META_INPUT_DEVICE_NATIVE (device);
  gboolean retval = FALSE;
  ClutterModifierType mods = 0;

  g_rw_lock_reader_lock (&seat_impl->state_lock);

  if (sequence)
    {
      MetaTouchState *touch_state;
      int slot;

      slot = clutter_event_sequence_get_slot (sequence);
      touch_state = meta_seat_impl_lookup_touch_state_in_impl (seat_impl, slot);
      if (!touch_state)
        goto out;

      if (coords)
        {
          coords->x = touch_state->coords.x;
          coords->y = touch_state->coords.y;
        }

      if (seat_impl->xkb)
        mods = meta_xkb_translate_modifiers (seat_impl->xkb, 0);

      retval = TRUE;
    }
  else
    {
      if (coords)
        {
          coords->x = device_native->pointer_x;
          coords->y = device_native->pointer_y;
        }

      if (seat_impl->xkb)
        {
          mods = meta_xkb_translate_modifiers (seat_impl->xkb,
                                               seat_impl->button_state);
        }

      retval = TRUE;
    }

  if (modifiers)
    *modifiers = mods;

 out:
  g_rw_lock_reader_unlock (&seat_impl->state_lock);
  return retval;
}

static void
meta_seat_impl_initable_iface_init (GInitableIface *iface)
{
  iface->init = meta_seat_impl_initable_init;
}

static void
meta_seat_impl_class_init (MetaSeatImplClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_seat_impl_constructed;
  object_class->set_property = meta_seat_impl_set_property;
  object_class->get_property = meta_seat_impl_get_property;
  object_class->finalize = meta_seat_impl_finalize;

  props[PROP_SEAT] =
    g_param_spec_object ("seat", NULL, NULL,
                         META_TYPE_SEAT_NATIVE,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_SEAT_ID] =
    g_param_spec_string ("seat-id", NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY);

  props[PROP_FLAGS] =
    g_param_spec_flags ("flags", NULL, NULL,
                        META_TYPE_SEAT_NATIVE_FLAG,
                        META_SEAT_NATIVE_FLAG_NONE,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT_ONLY);

  signals[KBD_A11Y_FLAGS_CHANGED] =
    g_signal_new ("kbd-a11y-flags-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 2,
                  G_TYPE_UINT, G_TYPE_UINT);
  signals[KBD_A11Y_MODS_STATE_CHANGED] =
    g_signal_new ("kbd-a11y-mods-state-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 2,
                  G_TYPE_UINT, G_TYPE_UINT);
  signals[TOUCH_MODE] =
    g_signal_new ("touch-mode",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__BOOLEAN,
                  G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
  signals[BELL] =
    g_signal_new ("bell",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
  signals[MODS_STATE_CHANGED] =
    g_signal_new ("mods-state-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
  signals[POINTER_POSITION_CHANGED_IN_IMPL] =
    g_signal_new ("pointer-position-changed-in-impl",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  GRAPHENE_TYPE_POINT);

  g_object_class_install_properties (object_class, N_PROPS, props);
}

static void
meta_seat_impl_init (MetaSeatImpl *seat_impl)
{
  g_rw_lock_init (&seat_impl->state_lock);

  seat_impl->repeat = TRUE;
  seat_impl->repeat_delay = 250;     /* ms */
  seat_impl->repeat_interval = 33;   /* ms */

  g_mutex_init (&seat_impl->init_mutex);
  g_cond_init (&seat_impl->init_cond);

  seat_impl->barrier_manager = meta_barrier_manager_native_new ();
}

void
meta_seat_impl_update_xkb_state_in_impl (MetaSeatImpl *seat_impl)
{
  xkb_mod_mask_t latched_mods = 0;
  xkb_mod_mask_t locked_mods = 0;
  struct xkb_keymap *xkb_keymap;

  g_rw_lock_writer_lock (&seat_impl->state_lock);

  xkb_keymap = meta_keymap_native_get_keyboard_map_in_impl (seat_impl->keymap);

  if (seat_impl->xkb)
    {
      latched_mods = xkb_state_serialize_mods (seat_impl->xkb,
                                               XKB_STATE_MODS_LATCHED);
      locked_mods = xkb_state_serialize_mods (seat_impl->xkb,
                                              XKB_STATE_MODS_LOCKED);
      xkb_state_unref (seat_impl->xkb);
    }

  seat_impl->xkb = xkb_state_new (xkb_keymap);

  xkb_state_update_mask (seat_impl->xkb,
                         0, /* depressed */
                         latched_mods,
                         locked_mods,
                         0, 0, seat_impl->layout_idx);

  seat_impl->caps_lock_led =
    xkb_keymap_led_get_index (xkb_keymap, XKB_LED_NAME_CAPS);
  seat_impl->num_lock_led =
    xkb_keymap_led_get_index (xkb_keymap, XKB_LED_NAME_NUM);
  seat_impl->scroll_lock_led =
    xkb_keymap_led_get_index (xkb_keymap, XKB_LED_NAME_SCROLL);

  meta_seat_impl_sync_leds_in_impl (seat_impl);
  meta_keymap_native_update_in_impl (seat_impl->keymap,
                                     seat_impl,
                                     seat_impl->xkb);

  g_rw_lock_writer_unlock (&seat_impl->state_lock);
}

static gboolean
release_devices (GTask *task)
{
  MetaSeatImpl *seat_impl = g_task_get_source_object (task);

  if (seat_impl->released)
    {
      g_warning ("meta_seat_impl_release_devices() shouldn't be called "
                 "multiple times without a corresponding call to "
                 "meta_seat_impl_reclaim_devices() first");
    }
  else
    {
      libinput_suspend (seat_impl->libinput);
      process_events (seat_impl);

      seat_impl->released = TRUE;
    }

  g_task_return_boolean (task, TRUE);

  return G_SOURCE_REMOVE;
}

/**
 * meta_seat_impl_release_devices:
 *
 * Releases all the evdev devices that Clutter is currently managing. This api
 * is typically used when switching away from the Clutter application when
 * switching tty. The devices can be reclaimed later with a call to
 * meta_seat_impl_reclaim_devices().
 *
 * This function should only be called after clutter has been initialized.
 */
void
meta_seat_impl_release_devices (MetaSeatImpl *seat_impl)
{
  GTask *task;

  g_return_if_fail (META_IS_SEAT_IMPL (seat_impl));

  task = g_task_new (seat_impl, NULL, NULL, NULL);
  meta_seat_impl_run_input_task (seat_impl, task,
                                 (GSourceFunc) release_devices);
  g_object_unref (task);
}

static gboolean
reclaim_devices (GTask *task)
{
  MetaSeatImpl *seat_impl = g_task_get_source_object (task);

  if (seat_impl->released)
    {
      libinput_resume (seat_impl->libinput);
      meta_seat_impl_update_xkb_state_in_impl (seat_impl);
      process_events (seat_impl);

      seat_impl->released = FALSE;
    }
  else
    {
      g_warning ("Spurious call to meta_seat_impl_reclaim_devices() without "
                 "previous call to meta_seat_impl_release_devices");
    }

  g_task_return_boolean (task, TRUE);

  return G_SOURCE_REMOVE;
}

/**
 * meta_seat_impl_reclaim_devices:
 *
 * This causes Clutter to re-probe for evdev devices. This is must only be
 * called after a corresponding call to meta_seat_impl_release_devices()
 * was previously used to release all evdev devices. This API is typically
 * used when a clutter application using evdev has regained focus due to
 * switching ttys.
 *
 * This function should only be called after clutter has been initialized.
 */
void
meta_seat_impl_reclaim_devices (MetaSeatImpl *seat_impl)
{
  GTask *task;

  g_return_if_fail (META_IS_SEAT_IMPL (seat_impl));

  task = g_task_new (seat_impl, NULL, NULL, NULL);
  meta_seat_impl_run_input_task (seat_impl, task, (GSourceFunc) reclaim_devices);
  g_object_unref (task);
}

static gboolean
set_keyboard_map (GTask *task)
{
  MetaSeatImpl *seat_impl = g_task_get_source_object (task);
  struct xkb_keymap *xkb_keymap = g_task_get_task_data (task);
  MetaKeymapNative *keymap;

  keymap = seat_impl->keymap;
  meta_keymap_native_set_keyboard_map_in_impl (keymap, xkb_keymap);

  meta_seat_impl_update_xkb_state_in_impl (seat_impl);
  g_task_return_boolean (task, TRUE);

  return G_SOURCE_REMOVE;
}

/**
 * meta_seat_impl_set_keyboard_map: (skip)
 * @seat_impl: the #ClutterSeat created by the evdev backend
 * @keymap: the new keymap
 *
 * Instructs @evdev to use the specified keyboard map. This will cause
 * the backend to drop the state and create a new one with the new
 * map. To avoid state being lost, callers should ensure that no key
 * is pressed when calling this function.
 */
void
meta_seat_impl_set_keyboard_map (MetaSeatImpl      *seat_impl,
                                 struct xkb_keymap *xkb_keymap)
{
  GTask *task;

  g_return_if_fail (META_IS_SEAT_IMPL (seat_impl));
  g_return_if_fail (xkb_keymap != NULL);

  task = g_task_new (seat_impl, NULL, NULL, NULL);
  g_task_set_task_data (task,
                        xkb_keymap_ref (xkb_keymap),
                        (GDestroyNotify) xkb_keymap_unref);
  meta_seat_impl_run_input_task (seat_impl, task, (GSourceFunc) set_keyboard_map);
  g_object_unref (task);
}

static gboolean
set_keyboard_layout_index (GTask *task)
{
  MetaSeatImpl *seat_impl = g_task_get_source_object (task);
  xkb_layout_index_t idx = GPOINTER_TO_UINT (g_task_get_task_data (task));
  xkb_mod_mask_t depressed_mods;
  xkb_mod_mask_t latched_mods;
  xkb_mod_mask_t locked_mods;
  struct xkb_state *state;

  g_rw_lock_writer_lock (&seat_impl->state_lock);

  state = seat_impl->xkb;

  depressed_mods = xkb_state_serialize_mods (state, XKB_STATE_MODS_DEPRESSED);
  latched_mods = xkb_state_serialize_mods (state, XKB_STATE_MODS_LATCHED);
  locked_mods = xkb_state_serialize_mods (state, XKB_STATE_MODS_LOCKED);

  xkb_state_update_mask (state, depressed_mods, latched_mods, locked_mods, 0, 0, idx);
  meta_keymap_native_update_in_impl (seat_impl->keymap,
                                     seat_impl,
                                     seat_impl->xkb);

  seat_impl->layout_idx = idx;

  meta_seat_impl_sync_leds_in_impl (seat_impl);

  g_rw_lock_writer_unlock (&seat_impl->state_lock);

  g_task_return_boolean (task, TRUE);

  return G_SOURCE_REMOVE;
}

/**
 * meta_seat_impl_set_keyboard_layout_index: (skip)
 * @seat_impl: the #ClutterSeat created by the evdev backend
 * @idx: the xkb layout index to set
 *
 * Sets the xkb layout index on the backend's #xkb_state .
 */
void
meta_seat_impl_set_keyboard_layout_index (MetaSeatImpl       *seat_impl,
                                          xkb_layout_index_t  idx)
{
  GTask *task;

  g_return_if_fail (META_IS_SEAT_IMPL (seat_impl));

  task = g_task_new (seat_impl, NULL, NULL, NULL);
  g_task_set_task_data (task, GUINT_TO_POINTER (idx), NULL);
  meta_seat_impl_run_input_task (seat_impl, task,
                                 (GSourceFunc) set_keyboard_layout_index);
  g_object_unref (task);
}

/**
 * meta_seat_impl_set_keyboard_repeat_in_impl:
 * @seat_impl: the #ClutterSeat created by the evdev backend
 * @repeat: whether to enable or disable keyboard repeat events
 * @delay: the delay in ms between the hardware key press event and
 * the first synthetic event
 * @interval: the period in ms between consecutive synthetic key
 * press events
 *
 * Enables or disables sythetic key press events, allowing for initial
 * delay and interval period to be specified.
 */
void
meta_seat_impl_set_keyboard_repeat_in_impl (MetaSeatImpl *seat_impl,
                                            gboolean      repeat,
                                            uint32_t      delay,
                                            uint32_t      interval)
{
  g_return_if_fail (META_IS_SEAT_IMPL (seat_impl));

  seat_impl->repeat = repeat;
  seat_impl->repeat_delay = delay;
  seat_impl->repeat_interval = interval;
}

struct xkb_state *
meta_seat_impl_get_xkb_state_in_impl (MetaSeatImpl *seat_impl)
{
  return seat_impl->xkb;
}

MetaBarrierManagerNative *
meta_seat_impl_get_barrier_manager (MetaSeatImpl *seat_impl)
{
  return seat_impl->barrier_manager;
}

static gboolean
set_pointer_constraint (GTask *task)
{
  MetaSeatImpl *seat_impl = g_task_get_source_object (task);
  MetaPointerConstraintImpl *constraint_impl = g_task_get_task_data (task);

  if (!g_set_object (&seat_impl->pointer_constraint, constraint_impl))
    return G_SOURCE_REMOVE;

  if (constraint_impl)
    {
      meta_pointer_constraint_impl_ensure_constrained (constraint_impl,
                                                       seat_impl->core_pointer);
    }

  g_task_return_boolean (task, TRUE);

  return G_SOURCE_REMOVE;
}

void
meta_seat_impl_set_pointer_constraint (MetaSeatImpl              *seat_impl,
                                       MetaPointerConstraintImpl *constraint_impl)
{
  GTask *task;

  g_return_if_fail (META_IS_SEAT_IMPL (seat_impl));

  task = g_task_new (seat_impl, NULL, NULL, NULL);
  if (constraint_impl)
    g_task_set_task_data (task, g_object_ref (constraint_impl), g_object_unref);
  meta_seat_impl_run_input_task (seat_impl, task,
                                 (GSourceFunc) set_pointer_constraint);
  g_object_unref (task);
}

static void
ensure_pointer_onscreen (MetaSeatImpl *seat_impl)
{
  int i, candidate = -1;
  int nearest_monitor_x, nearest_monitor_y, min_distance = G_MAXINT;
  MtkRectangle monitor_rect;
  graphene_point_t coords;

  if (!meta_seat_impl_query_state (seat_impl,
                                   seat_impl->core_pointer, NULL,
                                   &coords, NULL))
    return;

  /* Pointer is in a view */
  if (meta_viewport_info_get_view_at (seat_impl->viewports,
                                      coords.x, coords.y) >= 0)
    return;

  /* Find nearest view */
  for (i = 0; i < meta_viewport_info_get_num_views (seat_impl->viewports); i++)
    {
      meta_viewport_info_get_view_info (seat_impl->viewports, i,
                                        &monitor_rect, NULL);
      nearest_monitor_x = MIN (ABS (coords.x - monitor_rect.x),
                               ABS (coords.x -
                                    monitor_rect.x + monitor_rect.width));
      nearest_monitor_y = MIN (ABS (coords.y - monitor_rect.y),
                               ABS (coords.y -
                                    monitor_rect.y + monitor_rect.height));
      if (nearest_monitor_x < min_distance ||
          nearest_monitor_y < min_distance)
        {
          min_distance = MIN (nearest_monitor_x, nearest_monitor_y);
          candidate = i;
        }
    }

  if (candidate < 0)
    return;

  /* Calculate new coordinates on nearest view */
  meta_viewport_info_get_view_info (seat_impl->viewports,
                                    candidate,
                                    &monitor_rect, NULL);
  coords.x = CLAMP (coords.x, monitor_rect.x,
                    monitor_rect.x + monitor_rect.width - 1);
  coords.y = CLAMP (coords.y, monitor_rect.y,
                    monitor_rect.y + monitor_rect.height - 1);

  notify_absolute_motion_in_impl (seat_impl->core_pointer, 0,
                                  coords.x, coords.y, NULL);
}

typedef struct
{
  MetaViewportInfo *viewports;
  GMutex mutex;
  GCond cond;
  gboolean constrained;
} SetViewportsData;

static gboolean
set_viewports (GTask *task)
{
  MetaSeatImpl *seat_impl = g_task_get_source_object (task);
  SetViewportsData *data = g_task_get_task_data (task);
  MetaViewportInfo *viewports = data->viewports;

  g_set_object (&seat_impl->viewports, viewports);
  g_task_return_boolean (task, TRUE);

  ensure_pointer_onscreen (seat_impl);

  g_mutex_lock (&data->mutex);
  data->constrained = TRUE;
  g_cond_signal (&data->cond);
  g_mutex_unlock (&data->mutex);

  return G_SOURCE_REMOVE;
}

void
meta_seat_impl_set_viewports (MetaSeatImpl     *seat_impl,
                              MetaViewportInfo *viewports)
{
  SetViewportsData data = {};
  GTask *task;

  g_return_if_fail (META_IS_SEAT_IMPL (seat_impl));

  data.viewports = viewports;
  g_mutex_init (&data.mutex);
  g_cond_init (&data.cond);

  task = g_task_new (seat_impl, NULL, NULL, NULL);
  g_task_set_task_data (task, &data, NULL);
  meta_seat_impl_run_input_task (seat_impl, task,
                                 (GSourceFunc) set_viewports);
  g_object_unref (task);

  g_mutex_lock (&data.mutex);
  while (!data.constrained)
    g_cond_wait (&data.cond, &data.mutex);
  g_mutex_unlock (&data.mutex);

  g_mutex_clear (&data.mutex);
  g_cond_clear (&data.cond);
}

MetaSeatImpl *
meta_seat_impl_new (MetaSeatNative     *seat_native,
                    const char         *seat_id,
                    MetaSeatNativeFlag  flags)
{
  return g_initable_new (META_TYPE_SEAT_IMPL,
                         NULL, NULL,
                         "seat", seat_native,
                         "seat-id", seat_id,
                         "flags", flags,
                         NULL);
}

static gboolean
start_in_impl (GTask *task)
{
  MetaSeatImpl *seat_impl = g_task_get_source_object (task);

  if (seat_impl->libinput)
    init_libinput_source (seat_impl);

  g_task_return_boolean (task, TRUE);

  return G_SOURCE_REMOVE;
}

void
meta_seat_impl_start (MetaSeatImpl *seat_impl)
{
  GTask *task;

  g_return_if_fail (META_IS_SEAT_IMPL (seat_impl));

  task = g_task_new (seat_impl, NULL, NULL, NULL);
  meta_seat_impl_run_input_task (seat_impl, task,
                                 (GSourceFunc) start_in_impl);
  g_object_unref (task);
}

void
meta_seat_impl_notify_kbd_a11y_flags_changed_in_impl (MetaSeatImpl          *seat_impl,
                                                      MetaKeyboardA11yFlags  new_flags,
                                                      MetaKeyboardA11yFlags  what_changed)
{
  MetaInputSettings *input_settings;
  GValue values[] = { G_VALUE_INIT, G_VALUE_INIT };

  input_settings = seat_impl->input_settings;
  meta_input_settings_notify_kbd_a11y_change (input_settings,
                                              new_flags, what_changed);
  g_value_init (&values[0], G_TYPE_UINT);
  g_value_set_uint (&values[0], new_flags);
  g_value_init (&values[1], G_TYPE_UINT);
  g_value_set_uint (&values[1], what_changed);

  emit_signal (seat_impl, signals[KBD_A11Y_FLAGS_CHANGED],
               values, G_N_ELEMENTS (values));
}

void
meta_seat_impl_notify_kbd_a11y_mods_state_changed_in_impl (MetaSeatImpl   *seat_impl,
                                                           xkb_mod_mask_t  new_latched_mods,
                                                           xkb_mod_mask_t  new_locked_mods)
{
  GValue values[] = { G_VALUE_INIT, G_VALUE_INIT };

  g_value_init (&values[0], G_TYPE_UINT);
  g_value_set_uint (&values[0], new_latched_mods);
  g_value_init (&values[1], G_TYPE_UINT);
  g_value_set_uint (&values[1], new_locked_mods);

  emit_signal (seat_impl, signals[KBD_A11Y_MODS_STATE_CHANGED],
               values, G_N_ELEMENTS (values));
}

void
meta_seat_impl_notify_bell_in_impl (MetaSeatImpl *seat_impl)
{
  emit_signal (seat_impl, signals[BELL], NULL, 0);
}

MetaInputSettings *
meta_seat_impl_get_input_settings (MetaSeatImpl *seat_impl)
{
  return seat_impl->input_settings;
}

MetaBackend *
meta_seat_impl_get_backend (MetaSeatImpl *seat_impl)
{
  return meta_seat_native_get_backend (seat_impl->seat_native);
}
