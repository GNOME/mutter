/*
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
 * Author: Jonas Ådahl <jadahl@gmail.com>
 */

#include "config.h"

#include <glib-object.h>
#include <linux/input.h>

#include "backends/meta-backend-private.h"
#include "backends/native/meta-input-thread.h"
#include "backends/native/meta-seat-native.h"
#include "backends/native/meta-virtual-input-device-native.h"
#include "clutter/clutter-mutter.h"
#include "meta/util.h"

enum
{
  PROP_0,

  PROP_SLOT_BASE,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

typedef struct _ImplState ImplState;

struct _ImplState
{
  MetaSeatImpl *seat_impl;
  ClutterInputDevice *device;
  int button_count[KEY_CNT];
};

struct _MetaVirtualInputDeviceNative
{
  ClutterVirtualInputDevice parent;

  guint slot_base;
  ImplState *impl_state;
};

typedef struct
{
  uint64_t time_us;
  double x;
  double y;
} MetaVirtualEventMotion;

typedef struct
{
  uint64_t time_us;
  uint32_t button;
  ClutterButtonState button_state;
} MetaVirtualEventButton;

typedef struct
{
  uint64_t time_us;
  double dx;
  double dy;
  ClutterScrollDirection direction;
  ClutterScrollSource scroll_source;
  ClutterScrollFinishFlags finish_flags;
} MetaVirtualEventScroll;

typedef struct
{
  uint64_t time_us;
  uint32_t key;
  ClutterKeyState key_state;
} MetaVirtualEventKey;

typedef struct
{
  uint64_t time_us;
  int device_slot;
  double x;
  double y;
} MetaVirtualEventTouch;

G_DEFINE_TYPE (MetaVirtualInputDeviceNative,
               meta_virtual_input_device_native,
               CLUTTER_TYPE_VIRTUAL_INPUT_DEVICE)

typedef enum _EvdevButtonType
{
  EVDEV_BUTTON_TYPE_NONE,
  EVDEV_BUTTON_TYPE_KEY,
  EVDEV_BUTTON_TYPE_BUTTON,
} EvdevButtonType;

static int
update_button_count_in_impl (MetaVirtualInputDeviceNative *virtual_native,
                             uint32_t                      button,
                             uint32_t                      state)
{
  if (state)
    return ++virtual_native->impl_state->button_count[button];
  else
    return --virtual_native->impl_state->button_count[button];
}

static EvdevButtonType
get_button_type (uint16_t code)
{
  switch (code)
    {
    case BTN_TOOL_PEN:
    case BTN_TOOL_RUBBER:
    case BTN_TOOL_BRUSH:
    case BTN_TOOL_PENCIL:
    case BTN_TOOL_AIRBRUSH:
    case BTN_TOOL_MOUSE:
    case BTN_TOOL_LENS:
    case BTN_TOOL_QUINTTAP:
    case BTN_TOOL_DOUBLETAP:
    case BTN_TOOL_TRIPLETAP:
    case BTN_TOOL_QUADTAP:
    case BTN_TOOL_FINGER:
    case BTN_TOUCH:
      return EVDEV_BUTTON_TYPE_NONE;
    }

  if (code >= KEY_ESC && code <= KEY_MICMUTE)
    return EVDEV_BUTTON_TYPE_KEY;
  if (code >= BTN_MISC && code <= BTN_GEAR_UP)
    return EVDEV_BUTTON_TYPE_BUTTON;
  if (code >= KEY_OK && code <= KEY_LIGHTS_TOGGLE)
    return EVDEV_BUTTON_TYPE_KEY;
  if (code >= BTN_DPAD_UP && code <= BTN_DPAD_RIGHT)
    return EVDEV_BUTTON_TYPE_BUTTON;
  if (code >= KEY_ALS_TOGGLE && code <= KEY_KBDINPUTASSIST_CANCEL)
    return EVDEV_BUTTON_TYPE_KEY;
  if (code >= BTN_TRIGGER_HAPPY && code <= BTN_TRIGGER_HAPPY40)
    return EVDEV_BUTTON_TYPE_BUTTON;
  return EVDEV_BUTTON_TYPE_NONE;
}

static gboolean
release_device_in_impl (GTask *task)
{
  ImplState *impl_state = g_task_get_task_data (task);
  MetaSeatImpl *seat_impl = impl_state->seat_impl;
  int code;
  uint64_t time_us;

  time_us = g_get_monotonic_time ();

  meta_topic (META_DEBUG_INPUT,
              "Releasing pressed buttons while destroying virtual input device "
              "(device %p)", impl_state->device);

  for (code = 0; code < G_N_ELEMENTS (impl_state->button_count); code++)
    {
      if (impl_state->button_count[code] == 0)
        continue;

      switch (get_button_type (code))
        {
        case EVDEV_BUTTON_TYPE_KEY:
          meta_seat_impl_notify_key_in_impl (seat_impl,
                                             impl_state->device,
                                             time_us,
                                             code,
                                             CLUTTER_KEY_STATE_RELEASED,
                                             TRUE);
          break;
        case EVDEV_BUTTON_TYPE_BUTTON:
          meta_seat_impl_notify_button_in_impl (seat_impl,
                                                impl_state->device,
                                                time_us,
                                                code,
                                                CLUTTER_BUTTON_STATE_RELEASED);
          break;
        case EVDEV_BUTTON_TYPE_NONE:
          g_assert_not_reached ();
        }
    }

  meta_seat_impl_remove_virtual_input_device (seat_impl, impl_state->device);

  g_clear_object (&impl_state->device);
  g_task_return_boolean (task, TRUE);
  return G_SOURCE_REMOVE;
}

static MetaSeatNative*
meta_virtual_input_device_native_get_seat_native (MetaVirtualInputDeviceNative *virtual_native)
{
  ClutterSeat *seat =
    clutter_virtual_input_device_get_seat (CLUTTER_VIRTUAL_INPUT_DEVICE (virtual_native));
  return META_SEAT_NATIVE (seat);
}

static gboolean
notify_relative_motion_in_impl (GTask *task)
{
  MetaVirtualInputDeviceNative *virtual_native =
    g_task_get_source_object (task);
  MetaSeatNative *seat_native =
    meta_virtual_input_device_native_get_seat_native (virtual_native);
  MetaSeatImpl *seat = seat_native->impl;
  MetaVirtualEventMotion *event = g_task_get_task_data (task);

  if (event->time_us == CLUTTER_CURRENT_TIME)
    event->time_us = g_get_monotonic_time ();

  meta_seat_impl_notify_relative_motion_in_impl (seat,
                                                 virtual_native->impl_state->device,
                                                 event->time_us,
                                                 (float) event->x,
                                                 (float) event->y,
                                                 (float) event->x,
                                                 (float) event->y,
                                                 NULL);
  g_task_return_boolean (task, TRUE);
  return G_SOURCE_REMOVE;
}

static void
meta_virtual_input_device_native_notify_relative_motion (ClutterVirtualInputDevice *virtual_device,
                                                         uint64_t                   time_us,
                                                         double                     dx,
                                                         double                     dy)
{
  MetaVirtualEventMotion *event;
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  MetaSeatNative *seat_native =
    meta_virtual_input_device_native_get_seat_native (virtual_native);
  GTask *task;

  g_return_if_fail (virtual_native->impl_state != NULL);

  event = g_new0 (MetaVirtualEventMotion, 1);
  event->time_us = time_us;
  event->x = dx;
  event->y = dy;

  task = g_task_new (virtual_device, NULL, NULL, NULL);
  g_task_set_task_data (task, event, g_free);
  meta_seat_impl_run_input_task (seat_native->impl, task,
                                 (GSourceFunc) notify_relative_motion_in_impl);
  g_object_unref (task);
}

static gboolean
notify_absolute_motion_in_impl (GTask *task)
{
  MetaVirtualInputDeviceNative *virtual_native =
    g_task_get_source_object (task);
  MetaSeatNative *seat_native =
    meta_virtual_input_device_native_get_seat_native (virtual_native);
  MetaSeatImpl *seat = seat_native->impl;
  MetaVirtualEventMotion *event = g_task_get_task_data (task);

  if (event->time_us == CLUTTER_CURRENT_TIME)
    event->time_us = g_get_monotonic_time ();

  meta_seat_impl_notify_absolute_motion_in_impl (seat,
                                                 virtual_native->impl_state->device,
                                                 event->time_us,
                                                 (float) event->x,
                                                 (float) event->y,
                                                 NULL);
  g_task_return_boolean (task, TRUE);
  return G_SOURCE_REMOVE;
}

static void
meta_virtual_input_device_native_notify_absolute_motion (ClutterVirtualInputDevice *virtual_device,
                                                         uint64_t                   time_us,
                                                         double                     x,
                                                         double                     y)
{
  MetaVirtualEventMotion *event;
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  MetaSeatNative *seat_native =
    meta_virtual_input_device_native_get_seat_native (virtual_native);
  GTask *task;

  g_return_if_fail (virtual_native->impl_state != NULL);

  event = g_new0 (MetaVirtualEventMotion, 1);
  event->time_us = time_us;
  event->x = x;
  event->y = y;

  task = g_task_new (virtual_device, NULL, NULL, NULL);
  g_task_set_task_data (task, event, g_free);
  meta_seat_impl_run_input_task (seat_native->impl, task,
                                 (GSourceFunc) notify_absolute_motion_in_impl);
  g_object_unref (task);
}

static gboolean
notify_button_in_impl (GTask *task)
{
  MetaVirtualInputDeviceNative *virtual_native =
    g_task_get_source_object (task);
  MetaSeatNative *seat_native =
    meta_virtual_input_device_native_get_seat_native (virtual_native);
  MetaSeatImpl *seat = seat_native->impl;
  MetaVirtualEventButton *event = g_task_get_task_data (task);
  int button_count;
  int evdev_button;

  if (event->time_us == CLUTTER_CURRENT_TIME)
    event->time_us = g_get_monotonic_time ();

  evdev_button = meta_clutter_button_to_evdev (event->button);

  if (get_button_type (evdev_button) != EVDEV_BUTTON_TYPE_BUTTON)
    {
      g_warning ("Unknown/invalid virtual device button 0x%x pressed",
                 evdev_button);
      goto out;
    }

  button_count = update_button_count_in_impl (virtual_native, evdev_button,
                                              event->button_state);
  if (button_count < 0 || button_count > 1)
    {
      g_warning ("Received multiple virtual 0x%x button %s (ignoring)", evdev_button,
                 event->button_state == CLUTTER_BUTTON_STATE_PRESSED ?
                 "presses" : "releases");
      update_button_count_in_impl (virtual_native, evdev_button, 1 - event->button_state);
      goto out;
    }

  meta_topic (META_DEBUG_INPUT,
              "Emitting virtual button-%s of button 0x%x (device %p)",
              event->button_state == CLUTTER_BUTTON_STATE_PRESSED ?
              "press" : "release",
              evdev_button, virtual_native);

  meta_seat_impl_notify_button_in_impl (seat,
					virtual_native->impl_state->device,
					event->time_us,
					evdev_button,
					event->button_state);
 out:
  g_task_return_boolean (task, TRUE);
  return G_SOURCE_REMOVE;
}

static void
meta_virtual_input_device_native_notify_button (ClutterVirtualInputDevice *virtual_device,
                                                uint64_t                   time_us,
                                                uint32_t                   button,
                                                ClutterButtonState         button_state)
{
  MetaVirtualEventButton *event;
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  MetaSeatNative *seat_native =
    meta_virtual_input_device_native_get_seat_native (virtual_native);
  GTask *task;

  g_return_if_fail (virtual_native->impl_state != NULL);

  event = g_new0 (MetaVirtualEventButton, 1);
  event->time_us = time_us;
  event->button = button;
  event->button_state = button_state;

  task = g_task_new (virtual_device, NULL, NULL, NULL);
  g_task_set_task_data (task, event, g_free);
  meta_seat_impl_run_input_task (seat_native->impl, task,
                                 (GSourceFunc) notify_button_in_impl);
  g_object_unref (task);
}

static gboolean
notify_key_in_impl (GTask *task)
{
  MetaVirtualInputDeviceNative *virtual_native =
    g_task_get_source_object (task);
  MetaSeatNative *seat_native =
    meta_virtual_input_device_native_get_seat_native (virtual_native);
  MetaSeatImpl *seat = seat_native->impl;
  MetaVirtualEventKey *event = g_task_get_task_data (task);
  int key_count;

  if (event->time_us == CLUTTER_CURRENT_TIME)
    event->time_us = g_get_monotonic_time ();

  if (get_button_type (event->key) != EVDEV_BUTTON_TYPE_KEY)
    {
      g_warning ("Unknown/invalid virtual device key 0x%x pressed", event->key);
      goto out;
    }

  key_count = update_button_count_in_impl (virtual_native, event->key, event->key_state);
  if (key_count < 0 || key_count > 1)
    {
      g_warning ("Received multiple virtual 0x%x key %s (ignoring)", event->key,
                 event->key_state == CLUTTER_KEY_STATE_PRESSED ?
                 "presses" : "releases");
      update_button_count_in_impl (virtual_native, event->key, 1 - event->key_state);
      goto out;
    }

  meta_topic (META_DEBUG_INPUT,
              "Emitting virtual key-%s of key 0x%x (device %p)",
              event->key_state == CLUTTER_KEY_STATE_PRESSED ? "press" : "release",
              event->key, virtual_native);

  meta_seat_impl_notify_key_in_impl (seat,
				     virtual_native->impl_state->device,
				     event->time_us,
				     event->key,
				     event->key_state,
				     TRUE);

 out:
  g_task_return_boolean (task, TRUE);
  return G_SOURCE_REMOVE;
}

static void
meta_virtual_input_device_native_notify_key (ClutterVirtualInputDevice *virtual_device,
                                             uint64_t                   time_us,
                                             uint32_t                   key,
                                             ClutterKeyState            key_state)
{
  MetaVirtualEventKey *event;
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  MetaSeatNative *seat_native =
    meta_virtual_input_device_native_get_seat_native (virtual_native);
  GTask *task;

  g_return_if_fail (virtual_native->impl_state != NULL);

  event = g_new0 (MetaVirtualEventKey, 1);
  event->time_us = time_us;
  event->key = key;
  event->key_state = key_state;

  task = g_task_new (virtual_device, NULL, NULL, NULL);
  g_task_set_task_data (task, event, g_free);
  meta_seat_impl_run_input_task (seat_native->impl, task,
                                 (GSourceFunc) notify_key_in_impl);
  g_object_unref (task);
}

static gboolean
pick_keycode_for_keyval_in_current_group_in_impl (ClutterVirtualInputDevice *virtual_device,
						  guint                      keyval,
						  guint                     *keycode_out,
						  guint                     *level_out)
{
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  MetaSeatNative *seat_native =
    meta_virtual_input_device_native_get_seat_native (virtual_native);
  ClutterSeat *seat;
  ClutterKeymap *keymap;
  struct xkb_keymap *xkb_keymap;
  struct xkb_state  *state;
  guint keycode, layout;
  xkb_keycode_t min_keycode, max_keycode;

  seat = clutter_virtual_input_device_get_seat (virtual_device);
  keymap = clutter_seat_get_keymap (seat);
  xkb_keymap = meta_keymap_native_get_keyboard_map_in_impl (META_KEYMAP_NATIVE (keymap));
  state = meta_seat_impl_get_xkb_state_in_impl (seat_native->impl);

  layout = xkb_state_serialize_layout (state, XKB_STATE_LAYOUT_EFFECTIVE);
  min_keycode = xkb_keymap_min_keycode (xkb_keymap);
  max_keycode = xkb_keymap_max_keycode (xkb_keymap);
  for (keycode = min_keycode; keycode < max_keycode; keycode++)
    {
      gint num_levels, level;
      num_levels = xkb_keymap_num_levels_for_key (xkb_keymap, keycode, layout);
      for (level = 0; level < num_levels; level++)
        {
          const xkb_keysym_t *syms;
          gint num_syms, sym;
          num_syms = xkb_keymap_key_get_syms_by_level (xkb_keymap, keycode, layout, level, &syms);
          for (sym = 0; sym < num_syms; sym++)
            {
              if (syms[sym] == keyval)
                {
                  *keycode_out = keycode;
                  if (level_out)
                    *level_out = level;
                  return TRUE;
                }
            }
        }
    }

  return FALSE;
}

static void
apply_level_modifiers_in_impl (ClutterVirtualInputDevice *virtual_device,
                               uint64_t                   time_us,
                               uint32_t                   level,
                               uint32_t                   key_state)
{
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  MetaSeatNative *seat_native =
    meta_virtual_input_device_native_get_seat_native (virtual_native);
  guint keysym, keycode, evcode;

  if (level == 0)
    return;

  if (level == 1)
    {
      keysym = XKB_KEY_Shift_L;
    }
  else if (level == 2)
    {
      keysym = XKB_KEY_ISO_Level3_Shift;
    }
  else
    {
      g_warning ("Unhandled level: %d", level);
      return;
    }

  if (!pick_keycode_for_keyval_in_current_group_in_impl (virtual_device, keysym,
							 &keycode, NULL))
    return;

  evcode = meta_xkb_keycode_to_evdev (keycode);

  meta_topic (META_DEBUG_INPUT,
              "Emitting virtual key-%s of modifier key 0x%x (device %p)",
              key_state == CLUTTER_KEY_STATE_PRESSED ? "press" : "release",
              evcode, virtual_device);

  meta_seat_impl_notify_key_in_impl (seat_native->impl,
				     virtual_native->impl_state->device,
				     time_us,
				     evcode,
				     key_state,
				     TRUE);
}

static gboolean
notify_keyval_in_impl (GTask *task)
{
  MetaVirtualInputDeviceNative *virtual_native =
    g_task_get_source_object (task);
  ClutterVirtualInputDevice *virtual_device =
    CLUTTER_VIRTUAL_INPUT_DEVICE (virtual_native);
  MetaSeatNative *seat_native =
    meta_virtual_input_device_native_get_seat_native (virtual_native);
  MetaSeatImpl *seat = seat_native->impl;
  MetaVirtualEventKey *event = g_task_get_task_data (task);
  int key_count;
  guint keycode = 0, level = 0, evcode = 0;

  if (event->time_us == CLUTTER_CURRENT_TIME)
    event->time_us = g_get_monotonic_time ();

  if (!pick_keycode_for_keyval_in_current_group_in_impl (virtual_device,
							 event->key,
							 &keycode, &level))
    {
      g_warning ("No keycode found for keyval %x in current group", event->key);
      goto out;
    }

  evcode = meta_xkb_keycode_to_evdev (keycode);

  if (get_button_type (evcode) != EVDEV_BUTTON_TYPE_KEY)
    {
      g_warning ("Unknown/invalid virtual device key 0x%x pressed", evcode);
      goto out;
    }

  key_count = update_button_count_in_impl (virtual_native, evcode, event->key_state);
  if (key_count < 0 || key_count > 1)
    {
      g_warning ("Received multiple virtual 0x%x key %s (ignoring)", evcode,
                 event->key_state == CLUTTER_KEY_STATE_PRESSED ?
                 "presses" : "releases");
      update_button_count_in_impl (virtual_native, evcode, 1 - event->key_state);
      goto out;
    }

  meta_topic (META_DEBUG_INPUT,
              "Emitting virtual key-%s of key 0x%x with modifier level %d, "
              "press count %d (device %p)",
              event->key_state == CLUTTER_KEY_STATE_PRESSED ? "press" : "release",
              evcode, level, key_count, virtual_native);

  if (event->key_state)
    {
      apply_level_modifiers_in_impl (virtual_device, event->time_us,
                                     level, event->key_state);
    }

  meta_seat_impl_notify_key_in_impl (seat,
                                     virtual_native->impl_state->device,
                                     event->time_us,
                                     evcode,
                                     event->key_state,
                                     TRUE);

  if (!event->key_state)
    {
      apply_level_modifiers_in_impl (virtual_device, event->time_us,
                                     level, event->key_state);
    }

 out:
  g_task_return_boolean (task, TRUE);
  return G_SOURCE_REMOVE;
}

static void
meta_virtual_input_device_native_notify_keyval (ClutterVirtualInputDevice *virtual_device,
                                                uint64_t                   time_us,
                                                uint32_t                   keyval,
                                                ClutterKeyState            key_state)
{
  MetaVirtualEventKey *event;
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  MetaSeatNative *seat_native =
    meta_virtual_input_device_native_get_seat_native (virtual_native);
  GTask *task;

  g_return_if_fail (virtual_native->impl_state != NULL);

  event = g_new0 (MetaVirtualEventKey, 1);
  event->time_us = time_us;
  event->key = keyval;
  event->key_state = key_state;

  task = g_task_new (virtual_device, NULL, NULL, NULL);
  g_task_set_task_data (task, event, g_free);
  meta_seat_impl_run_input_task (seat_native->impl, task,
                                 (GSourceFunc) notify_keyval_in_impl);
  g_object_unref (task);
}

static void
direction_to_discrete (ClutterScrollDirection direction,
                       double                *discrete_dx,
                       double                *discrete_dy)
{
  switch (direction)
    {
    case CLUTTER_SCROLL_UP:
      *discrete_dx = 0.0;
      *discrete_dy = -1.0;
      break;
    case CLUTTER_SCROLL_DOWN:
      *discrete_dx = 0.0;
      *discrete_dy = 1.0;
      break;
    case CLUTTER_SCROLL_LEFT:
      *discrete_dx = -1.0;
      *discrete_dy = 0.0;
      break;
    case CLUTTER_SCROLL_RIGHT:
      *discrete_dx = 1.0;
      *discrete_dy = 0.0;
      break;
    case CLUTTER_SCROLL_SMOOTH:
      g_assert_not_reached ();
      break;
    }
}

static gboolean
notify_discrete_scroll_in_impl (GTask *task)
{
  MetaVirtualInputDeviceNative *virtual_native =
    g_task_get_source_object (task);
  MetaSeatNative *seat_native =
    meta_virtual_input_device_native_get_seat_native (virtual_native);
  MetaSeatImpl *seat = seat_native->impl;
  MetaVirtualEventScroll *event = g_task_get_task_data (task);
  double discrete_dx = 0.0, discrete_dy = 0.0;

  if (event->time_us == CLUTTER_CURRENT_TIME)
    event->time_us = g_get_monotonic_time ();

  direction_to_discrete (event->direction, &discrete_dx, &discrete_dy);

  meta_seat_impl_notify_discrete_scroll_in_impl (seat,
                                                 virtual_native->impl_state->device,
                                                 event->time_us,
                                                 discrete_dx * 120.0,
                                                 discrete_dy * 120.0,
                                                 event->scroll_source);

  g_task_return_boolean (task, TRUE);
  return G_SOURCE_REMOVE;
}

static void
meta_virtual_input_device_native_notify_discrete_scroll (ClutterVirtualInputDevice *virtual_device,
                                                         uint64_t                   time_us,
                                                         ClutterScrollDirection     direction,
                                                         ClutterScrollSource        scroll_source)
{
  MetaVirtualEventScroll *event;
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  MetaSeatNative *seat_native =
    meta_virtual_input_device_native_get_seat_native (virtual_native);
  GTask *task;

  g_return_if_fail (virtual_native->impl_state != NULL);

  event = g_new0 (MetaVirtualEventScroll, 1);
  event->time_us = time_us;
  event->direction = direction;
  event->scroll_source = scroll_source;

  task = g_task_new (virtual_device, NULL, NULL, NULL);
  g_task_set_task_data (task, event, g_free);
  meta_seat_impl_run_input_task (seat_native->impl, task,
                                 (GSourceFunc) notify_discrete_scroll_in_impl);
  g_object_unref (task);
}

static gboolean
notify_scroll_continuous_in_impl (GTask *task)
{
  MetaVirtualInputDeviceNative *virtual_native =
    g_task_get_source_object (task);
  MetaSeatNative *seat_native =
    meta_virtual_input_device_native_get_seat_native (virtual_native);
  MetaSeatImpl *seat = seat_native->impl;
  MetaVirtualEventScroll *event = g_task_get_task_data (task);

  if (event->time_us == CLUTTER_CURRENT_TIME)
    event->time_us = g_get_monotonic_time ();

  if (event->scroll_source == CLUTTER_SCROLL_SOURCE_WHEEL)
    {
      meta_seat_impl_notify_discrete_scroll_in_impl (seat,
                                                     virtual_native->impl_state->device,
                                                     event->time_us,
                                                     event->dx * (120.0 / 10.0),
                                                     event->dy * (120.0 / 10.0),
                                                     event->scroll_source);
    }
  else
    {
      meta_seat_impl_notify_scroll_continuous_in_impl (seat,
                                                       virtual_native->impl_state->device,
                                                       event->time_us,
                                                       event->dx, event->dy,
                                                       event->scroll_source,
                                                       event->finish_flags);
    }

  g_task_return_boolean (task, TRUE);
  return G_SOURCE_REMOVE;
}

static void
meta_virtual_input_device_native_notify_scroll_continuous (ClutterVirtualInputDevice *virtual_device,
                                                           uint64_t                   time_us,
                                                           double                     dx,
                                                           double                     dy,
                                                           ClutterScrollSource        scroll_source,
                                                           ClutterScrollFinishFlags   finish_flags)
{
  MetaVirtualEventScroll *event;
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  MetaSeatNative *seat_native =
    meta_virtual_input_device_native_get_seat_native (virtual_native);
  GTask *task;

  g_return_if_fail (virtual_native->impl_state != NULL);

  event = g_new0 (MetaVirtualEventScroll, 1);
  event->time_us = time_us;
  event->dx = dx;
  event->dy = dy;
  event->scroll_source = scroll_source;
  event->finish_flags = finish_flags;

  task = g_task_new (virtual_device, NULL, NULL, NULL);
  g_task_set_task_data (task, event, g_free);
  meta_seat_impl_run_input_task (seat_native->impl, task,
                                 (GSourceFunc) notify_scroll_continuous_in_impl);
  g_object_unref (task);
}

static gboolean
notify_touch_down_in_impl (GTask *task)
{
  MetaVirtualInputDeviceNative *virtual_native =
    g_task_get_source_object (task);
  MetaSeatNative *seat_native =
    meta_virtual_input_device_native_get_seat_native (virtual_native);
  MetaSeatImpl *seat = seat_native->impl;
  MetaVirtualEventTouch *event = g_task_get_task_data (task);

  if (event->time_us == CLUTTER_CURRENT_TIME)
    event->time_us = g_get_monotonic_time ();

  meta_seat_impl_notify_touch_event_in_impl (seat,
                                             virtual_native->impl_state->device,
                                             CLUTTER_TOUCH_BEGIN,
                                             event->time_us,
                                             event->device_slot,
                                             (float) event->x,
                                             (float) event->y);

  g_task_return_boolean (task, TRUE);
  return G_SOURCE_REMOVE;
}

static void
meta_virtual_input_device_native_notify_touch_down (ClutterVirtualInputDevice *virtual_device,
                                                    uint64_t                   time_us,
                                                    int                        device_slot,
                                                    double                     x,
                                                    double                     y)
{
  MetaVirtualEventTouch *event;
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  MetaSeatNative *seat_native =
    meta_virtual_input_device_native_get_seat_native (virtual_native);
  GTask *task;

  g_return_if_fail (virtual_native->impl_state != NULL);

  event = g_new0 (MetaVirtualEventTouch, 1);
  event->time_us = time_us;
  event->device_slot = virtual_native->slot_base + (guint) device_slot;
  event->x = x;
  event->y = y;

  task = g_task_new (virtual_device, NULL, NULL, NULL);
  g_task_set_task_data (task, event, g_free);
  meta_seat_impl_run_input_task (seat_native->impl, task,
                                 (GSourceFunc) notify_touch_down_in_impl);
  g_object_unref (task);
}

static gboolean
notify_touch_motion_in_impl (GTask *task)
{
  MetaVirtualInputDeviceNative *virtual_native =
    g_task_get_source_object (task);
  MetaSeatNative *seat_native =
    meta_virtual_input_device_native_get_seat_native (virtual_native);
  MetaSeatImpl *seat = seat_native->impl;
  MetaVirtualEventTouch *event = g_task_get_task_data (task);

  if (event->time_us == CLUTTER_CURRENT_TIME)
    event->time_us = g_get_monotonic_time ();

  meta_seat_impl_notify_touch_event_in_impl (seat,
                                             virtual_native->impl_state->device,
                                             CLUTTER_TOUCH_UPDATE,
                                             event->time_us,
                                             event->device_slot,
                                             (float) event->x,
                                             (float) event->y);

  g_task_return_boolean (task, TRUE);
  return G_SOURCE_REMOVE;
}

static void
meta_virtual_input_device_native_notify_touch_motion (ClutterVirtualInputDevice *virtual_device,
                                                      uint64_t                   time_us,
                                                      int                        device_slot,
                                                      double                     x,
                                                      double                     y)
{
  MetaVirtualEventTouch *event;
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  MetaSeatNative *seat_native =
    meta_virtual_input_device_native_get_seat_native (virtual_native);
  GTask *task;

  g_return_if_fail (virtual_native->impl_state != NULL);

  event = g_new0 (MetaVirtualEventTouch, 1);
  event->time_us = time_us;
  event->device_slot = virtual_native->slot_base + (guint) device_slot;
  event->x = x;
  event->y = y;

  task = g_task_new (virtual_device, NULL, NULL, NULL);
  g_task_set_task_data (task, event, g_free);
  meta_seat_impl_run_input_task (seat_native->impl, task,
                                 (GSourceFunc) notify_touch_motion_in_impl);
  g_object_unref (task);
}

static gboolean
notify_touch_up_in_impl (GTask *task)
{
  MetaVirtualInputDeviceNative *virtual_native =
    g_task_get_source_object (task);
  MetaSeatNative *seat_native =
    meta_virtual_input_device_native_get_seat_native (virtual_native);
  MetaSeatImpl *seat = seat_native->impl;
  MetaVirtualEventTouch *event = g_task_get_task_data (task);

  if (event->time_us == CLUTTER_CURRENT_TIME)
    event->time_us = g_get_monotonic_time ();

  meta_seat_impl_notify_touch_event_in_impl (seat,
                                             virtual_native->impl_state->device,
                                             CLUTTER_TOUCH_END,
                                             event->time_us,
                                             event->device_slot,
                                             (float) event->x,
                                             (float) event->y);

  g_task_return_boolean (task, TRUE);
  return G_SOURCE_REMOVE;
}

static void
meta_virtual_input_device_native_notify_touch_up (ClutterVirtualInputDevice *virtual_device,
                                                  uint64_t                   time_us,
                                                  int                        device_slot)
{
  MetaVirtualEventTouch *event;
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  MetaSeatNative *seat_native =
    meta_virtual_input_device_native_get_seat_native (virtual_native);
  GTask *task;

  g_return_if_fail (virtual_native->impl_state != NULL);

  event = g_new0 (MetaVirtualEventTouch, 1);
  event->time_us = time_us;
  event->device_slot = virtual_native->slot_base + (guint) device_slot;

  task = g_task_new (virtual_device, NULL, NULL, NULL);
  g_task_set_task_data (task, event, g_free);
  meta_seat_impl_run_input_task (seat_native->impl, task,
                                 (GSourceFunc) notify_touch_up_in_impl);
  g_object_unref (task);
}

static void
meta_virtual_input_device_native_get_property (GObject    *object,
                                               guint       prop_id,
                                               GValue     *value,
                                               GParamSpec *pspec)
{
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (object);

  switch (prop_id)
    {
    case PROP_SLOT_BASE:
      g_value_set_uint (value, virtual_native->slot_base);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_virtual_input_device_native_set_property (GObject      *object,
                                               guint         prop_id,
                                               const GValue *value,
                                               GParamSpec   *pspec)
{
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (object);

  switch (prop_id)
    {
    case PROP_SLOT_BASE:
      virtual_native->slot_base = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static gboolean
create_device_in_impl (GTask *task)
{
  ImplState *impl_state = g_task_get_task_data (task);
  MetaVirtualInputDeviceNative *virtual_native =
    g_task_get_source_object (task);
  MetaSeatNative *seat_native =
    meta_virtual_input_device_native_get_seat_native (virtual_native);
  MetaSeatImpl *seat_impl = seat_native->impl;
  ClutterVirtualInputDevice *virtual_device =
    CLUTTER_VIRTUAL_INPUT_DEVICE (virtual_native);
  ClutterInputDeviceType device_type =
    clutter_virtual_input_device_get_device_type (virtual_device);

  impl_state->seat_impl = seat_impl;
  impl_state->device =
    meta_input_device_native_new_virtual_in_impl (seat_impl,
                                                  device_type,
                                                  CLUTTER_INPUT_MODE_PHYSICAL);
  meta_seat_impl_add_virtual_input_device (seat_impl, impl_state->device);

  g_task_return_boolean (task, TRUE);
  return G_SOURCE_REMOVE;
}

static void
meta_virtual_input_device_native_constructed (GObject *object)
{
  ClutterVirtualInputDevice *virtual_device =
    CLUTTER_VIRTUAL_INPUT_DEVICE (object);
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (object);
  MetaSeatNative *seat_native =
    meta_virtual_input_device_native_get_seat_native (virtual_native);
  ClutterInputDeviceType device_type;
  g_autoptr (GTask) task = NULL;

  device_type = clutter_virtual_input_device_get_device_type (virtual_device);

  meta_topic (META_DEBUG_INPUT,
              "Creating new virtual input device of type %d (%p)",
              device_type, virtual_device);

  virtual_native->impl_state = g_new0 (ImplState, 1);

  task = g_task_new (virtual_device, NULL, NULL, NULL);
  g_task_set_task_data (task, virtual_native->impl_state, NULL);
  meta_seat_impl_run_input_task (seat_native->impl, task,
                                 (GSourceFunc) create_device_in_impl);
}

static void
impl_state_free (ImplState *impl_state)
{
  g_warn_if_fail (!impl_state->device);
  g_free (impl_state);
}

static void
meta_virtual_input_device_native_dispose (GObject *object)
{
  ClutterVirtualInputDevice *virtual_device =
    CLUTTER_VIRTUAL_INPUT_DEVICE (object);
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (object);
  MetaSeatNative *seat_native =
    meta_virtual_input_device_native_get_seat_native (virtual_native);
  GObjectClass *object_class =
    G_OBJECT_CLASS (meta_virtual_input_device_native_parent_class);

  if (virtual_native->impl_state)
    {
      GTask *task;

      task = g_task_new (virtual_device, NULL, NULL, NULL);
      g_task_set_task_data (task, virtual_native->impl_state,
                            (GDestroyNotify) impl_state_free);
      meta_seat_impl_run_input_task (seat_native->impl, task,
                                     (GSourceFunc) release_device_in_impl);
      g_object_unref (task);

      virtual_native->impl_state = NULL;
    }

  meta_seat_native_release_touch_slots (seat_native,
                                        virtual_native->slot_base);

  object_class->dispose (object);
}

static void
meta_virtual_input_device_native_init (MetaVirtualInputDeviceNative *virtual_device_evdev)
{
}

static void
meta_virtual_input_device_native_class_init (MetaVirtualInputDeviceNativeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterVirtualInputDeviceClass *virtual_input_device_class =
    CLUTTER_VIRTUAL_INPUT_DEVICE_CLASS (klass);

  object_class->get_property = meta_virtual_input_device_native_get_property;
  object_class->set_property = meta_virtual_input_device_native_set_property;
  object_class->constructed = meta_virtual_input_device_native_constructed;
  object_class->dispose = meta_virtual_input_device_native_dispose;

  virtual_input_device_class->notify_relative_motion = meta_virtual_input_device_native_notify_relative_motion;
  virtual_input_device_class->notify_absolute_motion = meta_virtual_input_device_native_notify_absolute_motion;
  virtual_input_device_class->notify_button = meta_virtual_input_device_native_notify_button;
  virtual_input_device_class->notify_key = meta_virtual_input_device_native_notify_key;
  virtual_input_device_class->notify_keyval = meta_virtual_input_device_native_notify_keyval;
  virtual_input_device_class->notify_discrete_scroll = meta_virtual_input_device_native_notify_discrete_scroll;
  virtual_input_device_class->notify_scroll_continuous = meta_virtual_input_device_native_notify_scroll_continuous;
  virtual_input_device_class->notify_touch_down = meta_virtual_input_device_native_notify_touch_down;
  virtual_input_device_class->notify_touch_motion = meta_virtual_input_device_native_notify_touch_motion;
  virtual_input_device_class->notify_touch_up = meta_virtual_input_device_native_notify_touch_up;

  obj_props[PROP_SLOT_BASE] = g_param_spec_uint ("slot-base", NULL, NULL,
                                                 0, G_MAXUINT, 0,
                                                 G_PARAM_READWRITE |
                                                 G_PARAM_STATIC_STRINGS |
                                                 G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_properties (object_class, PROP_LAST, obj_props);
}
