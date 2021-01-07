/*
 * Copyright (C) 2010 Intel Corp.
 * Copyright (C) 2014 Jonas Ådahl
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
#include <cairo-gobject.h>

#include "backends/meta-backend-private.h"
#include "backends/native/meta-input-thread.h"
#include "clutter/clutter-mutter.h"

G_DEFINE_TYPE (MetaInputDeviceNative,
               meta_input_device_native,
               META_TYPE_INPUT_DEVICE)

enum
{
  PROP_0,
  PROP_DEVICE_MATRIX,
  PROP_OUTPUT_ASPECT_RATIO,
  N_PROPS
};

static GParamSpec *obj_props[N_PROPS] = { 0 };

typedef struct _SlowKeysEventPending
{
  MetaInputDeviceNative *device;
  ClutterEvent *event;
  GSource *timer;
} SlowKeysEventPending;

typedef struct _PadFeature PadFeature;

struct _PadFeature
{
  ClutterInputDevicePadFeature feature;
  int n_feature;
  int group;
  gboolean mode_switch;
};

static void clear_slow_keys      (MetaInputDeviceNative *device);
static void stop_bounce_keys     (MetaInputDeviceNative *device);
static void stop_toggle_slowkeys (MetaInputDeviceNative *device);
static void stop_mousekeys_move  (MetaInputDeviceNative *device);

static void
meta_input_device_native_finalize (GObject *object)
{
  MetaInputDeviceNative *device_evdev = META_INPUT_DEVICE_NATIVE (object);

  g_warn_if_fail (!device_evdev->libinput_device);

  clear_slow_keys (device_evdev);
  stop_bounce_keys (device_evdev);
  stop_toggle_slowkeys (device_evdev);
  stop_mousekeys_move (device_evdev);

  g_clear_pointer (&device_evdev->pad_features, g_array_unref);
  g_clear_pointer (&device_evdev->modes, g_array_unref);

  G_OBJECT_CLASS (meta_input_device_native_parent_class)->finalize (object);
}

static void
meta_input_device_native_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  MetaInputDeviceNative *device = META_INPUT_DEVICE_NATIVE (object);

  switch (prop_id)
    {
    case PROP_DEVICE_MATRIX:
      {
        const cairo_matrix_t *matrix = g_value_get_boxed (value);
        cairo_matrix_init_identity (&device->device_matrix);
        cairo_matrix_multiply (&device->device_matrix,
                               &device->device_matrix, matrix);
        break;
      }
    case PROP_OUTPUT_ASPECT_RATIO:
      device->output_ratio = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_input_device_native_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  MetaInputDeviceNative *device = META_INPUT_DEVICE_NATIVE (object);

  switch (prop_id)
    {
    case PROP_DEVICE_MATRIX:
      g_value_set_boxed (value, &device->device_matrix);
      break;
    case PROP_OUTPUT_ASPECT_RATIO:
      g_value_set_double (value, device->output_ratio);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static gboolean
meta_input_device_native_is_mode_switch_button (ClutterInputDevice *device,
                                                uint32_t            group,
                                                uint32_t            button)
{
  MetaInputDeviceNative *device_native = META_INPUT_DEVICE_NATIVE (device);
  int i;

  if (!device_native->pad_features)
    return FALSE;

  for (i = 0; i < device_native->pad_features->len; i++)
    {
      PadFeature *pad_feature;

      pad_feature = &g_array_index (device_native->pad_features, PadFeature, i);

      if (pad_feature->feature == CLUTTER_PAD_FEATURE_BUTTON &&
          pad_feature->group == group &&
          pad_feature->n_feature == button)
        return pad_feature->mode_switch;
    }

  return FALSE;
}

static int
meta_input_device_native_get_group_n_modes (ClutterInputDevice *device,
                                            int                 group)
{
  MetaInputDeviceNative *device_native = META_INPUT_DEVICE_NATIVE (device);

  if (!device_native->modes || group < device_native->modes->len)
    return -1;

  return g_array_index (device_native->modes, int, group);
}

static gboolean
meta_input_device_native_is_grouped (ClutterInputDevice *device,
                                     ClutterInputDevice *other_device)
{
  MetaInputDeviceNative *device_native, *other_device_native;

  device_native = META_INPUT_DEVICE_NATIVE (device);
  other_device_native = META_INPUT_DEVICE_NATIVE (other_device);

  return device_native->group == other_device_native->group;
}

static int
meta_input_device_native_get_pad_feature_group (ClutterInputDevice           *device,
                                                ClutterInputDevicePadFeature  feature,
                                                int                           n_feature)
{
  MetaInputDeviceNative *device_native = META_INPUT_DEVICE_NATIVE (device);
  int i;

  if (!device_native->pad_features)
    return -1;

  for (i = 0; i < device_native->pad_features->len; i++)
    {
      PadFeature *pad_feature;

      pad_feature = &g_array_index (device_native->pad_features, PadFeature, i);

      if (pad_feature->feature == feature &&
          pad_feature->n_feature == n_feature)
        return pad_feature->group;
    }

  return -1;
}

static void
meta_input_device_native_bell_notify (MetaInputDeviceNative *device)
{
  meta_seat_impl_notify_bell_in_impl (device->seat_impl);
}

static void
meta_input_device_native_free_pending_slow_key (gpointer data)
{
  SlowKeysEventPending *slow_keys_event = data;

  clutter_event_free (slow_keys_event->event);
  g_clear_pointer (&slow_keys_event->timer, g_source_destroy);
  g_free (slow_keys_event);
}

static void
clear_slow_keys (MetaInputDeviceNative *device)
{
  g_list_free_full (device->slow_keys_list, meta_input_device_native_free_pending_slow_key);
  g_list_free (device->slow_keys_list);
  device->slow_keys_list = NULL;
}

static guint
get_slow_keys_delay (ClutterInputDevice *device)
{
  MetaInputDeviceNative *device_native = META_INPUT_DEVICE_NATIVE (device);
  MetaKbdA11ySettings a11y_settings;
  MetaInputSettings *input_settings;

  input_settings = meta_seat_impl_get_input_settings (device_native->seat_impl);
  meta_input_settings_get_kbd_a11y_settings (input_settings, &a11y_settings);
  /* Settings use int, we use uint, make sure we dont go negative */
  return MAX (0, a11y_settings.slowkeys_delay);
}

static gboolean
trigger_slow_keys (gpointer data)
{
  SlowKeysEventPending *slow_keys_event = data;
  MetaInputDeviceNative *device = slow_keys_event->device;
  ClutterKeyEvent *key_event = (ClutterKeyEvent *) slow_keys_event->event;

  /* Alter timestamp and emit the event */
  key_event->time = us2ms (g_get_monotonic_time ());
  _clutter_event_push (slow_keys_event->event, TRUE);

  /* Then remote the pending event */
  device->slow_keys_list = g_list_remove (device->slow_keys_list, slow_keys_event);
  meta_input_device_native_free_pending_slow_key (slow_keys_event);

  if (device->a11y_flags & META_A11Y_SLOW_KEYS_BEEP_ACCEPT)
    meta_input_device_native_bell_notify (device);

  return G_SOURCE_REMOVE;
}

static int
find_pending_event_by_keycode (gconstpointer a,
                               gconstpointer b)
{
  const SlowKeysEventPending *pa = a;
  const ClutterKeyEvent *ka = (ClutterKeyEvent *) pa->event;
  const ClutterKeyEvent *kb = b;

  return kb->hardware_keycode - ka->hardware_keycode;
}

static GSource *
timeout_source_new (MetaSeatImpl *seat_impl,
                    guint         interval,
                    GSourceFunc   func,
                    gpointer      user_data)
{
  GSource *source;

  source = g_timeout_source_new (interval);
  g_source_set_callback (source,
                         func,
                         user_data, NULL);
  g_source_attach (source, seat_impl->input_context);
  g_source_unref (source);

  return source;
}

static gboolean
start_slow_keys (ClutterEvent          *event,
                 MetaInputDeviceNative *device)
{
  SlowKeysEventPending *slow_keys_event;
  ClutterKeyEvent *key_event = (ClutterKeyEvent *) event;

  if (key_event->flags & CLUTTER_EVENT_FLAG_REPEATED)
    return TRUE;

  slow_keys_event = g_new0 (SlowKeysEventPending, 1);
  slow_keys_event->device = device;
  slow_keys_event->event = clutter_event_copy (event);
  slow_keys_event->timer =
    timeout_source_new (device->seat_impl,
                        get_slow_keys_delay (CLUTTER_INPUT_DEVICE (device)),
                        trigger_slow_keys,
                        slow_keys_event);
  device->slow_keys_list = g_list_append (device->slow_keys_list, slow_keys_event);

  if (device->a11y_flags & META_A11Y_SLOW_KEYS_BEEP_PRESS)
    meta_input_device_native_bell_notify (device);

  return TRUE;
}

static gboolean
stop_slow_keys (ClutterEvent          *event,
                MetaInputDeviceNative *device)
{
  GList *item;

  /* Check if we have a slow key event queued for this key event */
  item = g_list_find_custom (device->slow_keys_list, event, find_pending_event_by_keycode);
  if (item)
    {
      SlowKeysEventPending *slow_keys_event = item->data;

      device->slow_keys_list = g_list_delete_link (device->slow_keys_list, item);
      meta_input_device_native_free_pending_slow_key (slow_keys_event);

      if (device->a11y_flags & META_A11Y_SLOW_KEYS_BEEP_REJECT)
        meta_input_device_native_bell_notify (device);

      return TRUE;
    }

  /* If no key press event was pending, just emit the key release as-is */
  return FALSE;
}

static guint
get_debounce_delay (ClutterInputDevice *device)
{
  MetaInputDeviceNative *device_native = META_INPUT_DEVICE_NATIVE (device);
  MetaKbdA11ySettings a11y_settings;
  MetaInputSettings *input_settings;

  input_settings = meta_seat_impl_get_input_settings (device_native->seat_impl);
  meta_input_settings_get_kbd_a11y_settings (input_settings, &a11y_settings);
  /* Settings use int, we use uint, make sure we dont go negative */
  return MAX (0, a11y_settings.debounce_delay);
}

static gboolean
clear_bounce_keys (gpointer data)
{
  MetaInputDeviceNative *device = data;

  device->debounce_key = 0;
  device->debounce_timer = 0;

  return G_SOURCE_REMOVE;
}

static void
start_bounce_keys (ClutterEvent          *event,
                   MetaInputDeviceNative *device)
{
  stop_bounce_keys (device);

  device->debounce_key = ((ClutterKeyEvent *) event)->hardware_keycode;
  device->debounce_timer =
    timeout_source_new (device->seat_impl,
                        get_debounce_delay (CLUTTER_INPUT_DEVICE (device)),
                        clear_bounce_keys,
                        device);
}

static void
stop_bounce_keys (MetaInputDeviceNative *device)
{
  g_clear_pointer (&device->debounce_timer, g_source_destroy);
}

static void
notify_bounce_keys_reject (MetaInputDeviceNative *device)
{
  if (device->a11y_flags & META_A11Y_BOUNCE_KEYS_BEEP_REJECT)
    meta_input_device_native_bell_notify (device);
}

static gboolean
debounce_key (ClutterEvent          *event,
              MetaInputDeviceNative *device)
{
  return (device->debounce_key == ((ClutterKeyEvent *) event)->hardware_keycode);
}

static gboolean
key_event_is_modifier (ClutterEvent *event)
{
  switch (event->key.keyval)
    {
    case XKB_KEY_Shift_L:
    case XKB_KEY_Shift_R:
    case XKB_KEY_Control_L:
    case XKB_KEY_Control_R:
    case XKB_KEY_Alt_L:
    case XKB_KEY_Alt_R:
    case XKB_KEY_Meta_L:
    case XKB_KEY_Meta_R:
    case XKB_KEY_Super_L:
    case XKB_KEY_Super_R:
    case XKB_KEY_Hyper_L:
    case XKB_KEY_Hyper_R:
    case XKB_KEY_Caps_Lock:
    case XKB_KEY_Shift_Lock:
      return TRUE;
    default:
      return FALSE;
    }
}

static void
notify_stickykeys_mask (MetaInputDeviceNative *device)
{
  meta_seat_impl_notify_kbd_a11y_mods_state_changed_in_impl (device->seat_impl,
                                                             device->stickykeys_latched_mask,
                                                             device->stickykeys_locked_mask);
}

static void
update_internal_xkb_state (MetaInputDeviceNative *device,
                           xkb_mod_mask_t         new_latched_mask,
                           xkb_mod_mask_t         new_locked_mask)
{
  MetaSeatImpl *seat_impl = device->seat_impl;
  xkb_mod_mask_t depressed_mods;
  xkb_mod_mask_t latched_mods;
  xkb_mod_mask_t locked_mods;
  xkb_mod_mask_t group_mods;
  struct xkb_state *xkb_state;

  g_rw_lock_writer_lock (&seat_impl->state_lock);

  xkb_state = meta_seat_impl_get_xkb_state_in_impl (seat_impl);
  depressed_mods = xkb_state_serialize_mods (xkb_state, XKB_STATE_MODS_DEPRESSED);
  latched_mods = xkb_state_serialize_mods (xkb_state, XKB_STATE_MODS_LATCHED);
  locked_mods = xkb_state_serialize_mods (xkb_state, XKB_STATE_MODS_LOCKED);

  latched_mods &= ~device->stickykeys_latched_mask;
  locked_mods &= ~device->stickykeys_locked_mask;

  device->stickykeys_latched_mask = new_latched_mask;
  device->stickykeys_locked_mask = new_locked_mask;

  latched_mods |= device->stickykeys_latched_mask;
  locked_mods |= device->stickykeys_locked_mask;

  group_mods = xkb_state_serialize_layout (xkb_state, XKB_STATE_LAYOUT_EFFECTIVE);

  xkb_state_update_mask (xkb_state,
                         depressed_mods,
                         latched_mods,
                         locked_mods,
                         0, 0, group_mods);
  notify_stickykeys_mask (device);

  g_rw_lock_writer_unlock (&seat_impl->state_lock);
}

static void
update_stickykeys_event (ClutterEvent          *event,
                         MetaInputDeviceNative *device,
                         xkb_mod_mask_t         new_latched_mask,
                         xkb_mod_mask_t         new_locked_mask)
{
  MetaSeatImpl *seat_impl = device->seat_impl;
  xkb_mod_mask_t effective_mods;
  xkb_mod_mask_t latched_mods;
  xkb_mod_mask_t locked_mods;
  struct xkb_state *xkb_state;

  update_internal_xkb_state (device, new_latched_mask, new_locked_mask);

  xkb_state = meta_seat_impl_get_xkb_state_in_impl (seat_impl);
  effective_mods = xkb_state_serialize_mods (xkb_state, XKB_STATE_MODS_EFFECTIVE);
  latched_mods = xkb_state_serialize_mods (xkb_state, XKB_STATE_MODS_LATCHED);
  locked_mods = xkb_state_serialize_mods (xkb_state, XKB_STATE_MODS_LOCKED);

  _clutter_event_set_state_full (event,
                                 seat_impl->button_state,
                                 device->stickykeys_depressed_mask,
                                 latched_mods,
                                 locked_mods,
                                 effective_mods | seat_impl->button_state);
}

static void
notify_stickykeys_change (MetaInputDeviceNative *device)
{
  /* Every time sticky keys setting is changed, clear the masks */
  device->stickykeys_depressed_mask = 0;
  update_internal_xkb_state (device, 0, 0);

  meta_seat_impl_notify_kbd_a11y_flags_changed_in_impl (device->seat_impl,
                                                        device->a11y_flags,
                                                        META_A11Y_STICKY_KEYS_ENABLED);
}

static void
set_stickykeys_off (MetaInputDeviceNative *device)
{
  device->a11y_flags &= ~META_A11Y_STICKY_KEYS_ENABLED;
  notify_stickykeys_change (device);
}

static void
set_stickykeys_on (MetaInputDeviceNative *device)
{
  device->a11y_flags |= META_A11Y_STICKY_KEYS_ENABLED;
  notify_stickykeys_change (device);
}

static void
clear_stickykeys_event (ClutterEvent          *event,
                        MetaInputDeviceNative *device)
{
  set_stickykeys_off (device);
  update_stickykeys_event (event, device, 0, 0);
}

static void
set_slowkeys_off (MetaInputDeviceNative *device)
{
  device->a11y_flags &= ~META_A11Y_SLOW_KEYS_ENABLED;

  meta_seat_impl_notify_kbd_a11y_flags_changed_in_impl (device->seat_impl,
                                                        device->a11y_flags,
                                                        META_A11Y_SLOW_KEYS_ENABLED);
}

static void
set_slowkeys_on (MetaInputDeviceNative *device)
{
  device->a11y_flags |= META_A11Y_SLOW_KEYS_ENABLED;

  meta_seat_impl_notify_kbd_a11y_flags_changed_in_impl (device->seat_impl,
                                                        device->a11y_flags,
                                                        META_A11Y_SLOW_KEYS_ENABLED);
}

static void
handle_stickykeys_press (ClutterEvent          *event,
                         MetaInputDeviceNative *device)
{
  MetaSeatImpl *seat_impl = device->seat_impl;
  xkb_mod_mask_t depressed_mods;
  xkb_mod_mask_t new_latched_mask;
  xkb_mod_mask_t new_locked_mask;
  struct xkb_state *xkb_state;

  if (!key_event_is_modifier (event))
    return;

  if (device->stickykeys_depressed_mask &&
      (device->a11y_flags & META_A11Y_STICKY_KEYS_TWO_KEY_OFF))
    {
      clear_stickykeys_event (event, device);
      return;
    }

  xkb_state = meta_seat_impl_get_xkb_state_in_impl (seat_impl);
  depressed_mods = xkb_state_serialize_mods (xkb_state, XKB_STATE_MODS_DEPRESSED);
  /* Ignore the lock modifier mask, that one cannot be sticky, yet the
   * CAPS_LOCK key itself counts as a modifier as it might be remapped
   * to some other modifier which can be sticky.
   */
  depressed_mods &= ~CLUTTER_LOCK_MASK;

  new_latched_mask = device->stickykeys_latched_mask;
  new_locked_mask = device->stickykeys_locked_mask;

  device->stickykeys_depressed_mask = depressed_mods;

  if (new_locked_mask & depressed_mods)
    {
      new_locked_mask &= ~depressed_mods;
    }
  else if (new_latched_mask & depressed_mods)
    {
      new_locked_mask |= depressed_mods;
      new_latched_mask &= ~depressed_mods;
    }
  else
    {
      new_latched_mask |= depressed_mods;
    }

  update_stickykeys_event (event, device, new_latched_mask, new_locked_mask);
}

static void
handle_stickykeys_release (ClutterEvent          *event,
                           MetaInputDeviceNative *device)
{
  MetaSeatImpl *seat_impl = device->seat_impl;
  struct xkb_state *xkb_state;

  xkb_state = meta_seat_impl_get_xkb_state_in_impl (seat_impl);
  device->stickykeys_depressed_mask =
    xkb_state_serialize_mods (xkb_state, XKB_STATE_MODS_DEPRESSED);

  if (key_event_is_modifier (event))
    {
      if (device->a11y_flags & META_A11Y_STICKY_KEYS_BEEP)
        meta_input_device_native_bell_notify (device);

      return;
    }

  if (device->stickykeys_latched_mask == 0)
    return;

  update_stickykeys_event (event, device, 0, device->stickykeys_locked_mask);
}

static gboolean
trigger_toggle_slowkeys (gpointer data)
{
  MetaInputDeviceNative *device = data;

  device->toggle_slowkeys_timer = 0;

  if (device->a11y_flags & META_A11Y_FEATURE_STATE_CHANGE_BEEP)
    meta_input_device_native_bell_notify (device);

  if (device->a11y_flags & META_A11Y_SLOW_KEYS_ENABLED)
    set_slowkeys_off (device);
  else
    set_slowkeys_on (device);

  return G_SOURCE_REMOVE;
}

static void
start_toggle_slowkeys (MetaInputDeviceNative *device)
{
  if (device->toggle_slowkeys_timer != 0)
    return;

  device->toggle_slowkeys_timer =
    timeout_source_new (device->seat_impl,
                        8 * 1000 /* 8 secs */,
                        trigger_toggle_slowkeys,
                        device);
}

static void
stop_toggle_slowkeys (MetaInputDeviceNative *device)
{
  g_clear_pointer (&device->toggle_slowkeys_timer, g_source_destroy);
}

static void
handle_enablekeys_press (ClutterEvent          *event,
                         MetaInputDeviceNative *device)
{
  if (event->key.keyval == XKB_KEY_Shift_L || event->key.keyval == XKB_KEY_Shift_R)
    {
      start_toggle_slowkeys (device);

      if (event->key.time > device->last_shift_time + 15 * 1000 /* 15 secs  */)
        device->shift_count = 1;
      else
        device->shift_count++;

      device->last_shift_time = event->key.time;
    }
  else
    {
      device->shift_count = 0;
      stop_toggle_slowkeys (device);
    }
}

static void
handle_enablekeys_release (ClutterEvent          *event,
                           MetaInputDeviceNative *device)
{
  if (event->key.keyval == XKB_KEY_Shift_L || event->key.keyval == XKB_KEY_Shift_R)
    {
      stop_toggle_slowkeys (device);
      if (device->shift_count >= 5)
        {
          device->shift_count = 0;

          if (device->a11y_flags & META_A11Y_FEATURE_STATE_CHANGE_BEEP)
            meta_input_device_native_bell_notify (device);

          if (device->a11y_flags & META_A11Y_STICKY_KEYS_ENABLED)
            set_stickykeys_off (device);
          else
            set_stickykeys_on (device);
        }
    }
}

static int
get_button_index (int button)
{
  switch (button)
    {
    case CLUTTER_BUTTON_PRIMARY:
      return 0;
    case CLUTTER_BUTTON_MIDDLE:
      return 1;
    case CLUTTER_BUTTON_SECONDARY:
      return 2;
    default:
      break;
    }

  g_warn_if_reached ();
  return 0;
}

static void
emulate_button_press (MetaInputDeviceNative *device_evdev)
{
  ClutterInputDevice *device = CLUTTER_INPUT_DEVICE (device_evdev);
  int btn = device_evdev->mousekeys_btn;

  if (device_evdev->mousekeys_btn_states[get_button_index (btn)])
    return;

  clutter_virtual_input_device_notify_button (device->accessibility_virtual_device,
                                              g_get_monotonic_time (), btn,
                                              CLUTTER_BUTTON_STATE_PRESSED);
  device_evdev->mousekeys_btn_states[get_button_index (btn)] = CLUTTER_BUTTON_STATE_PRESSED;
}

static void
emulate_button_release (MetaInputDeviceNative *device_evdev)
{
  ClutterInputDevice *device = CLUTTER_INPUT_DEVICE (device_evdev);
  int btn = device_evdev->mousekeys_btn;

  if (device_evdev->mousekeys_btn_states[get_button_index (btn)] == CLUTTER_BUTTON_STATE_RELEASED)
    return;

  clutter_virtual_input_device_notify_button (device->accessibility_virtual_device,
                                              g_get_monotonic_time (), btn,
                                              CLUTTER_BUTTON_STATE_RELEASED);
  device_evdev->mousekeys_btn_states[get_button_index (btn)] = CLUTTER_BUTTON_STATE_RELEASED;
}

static void
emulate_button_click (MetaInputDeviceNative *device)
{
  emulate_button_press (device);
  emulate_button_release (device);
}

#define MOUSEKEYS_CURVE (1.0 + (((double) 50.0) * 0.001))

static void
update_mousekeys_params (MetaInputDeviceNative *device,
                         MetaKbdA11ySettings   *settings)
{
  /* Prevent us from broken settings values */
  device->mousekeys_max_speed = MAX (1, settings->mousekeys_max_speed);
  device->mousekeys_accel_time = MAX (1, settings->mousekeys_accel_time);
  device->mousekeys_init_delay = MAX (0, settings->mousekeys_init_delay);

  device->mousekeys_curve_factor =
    (((double) device->mousekeys_max_speed) /
      pow ((double) device->mousekeys_accel_time, MOUSEKEYS_CURVE));
}

static double
mousekeys_get_speed_factor (MetaInputDeviceNative *device,
                            uint64_t               time_us)
{
  uint32_t time;
  int64_t delta_t;
  int64_t init_time;
  double speed;

  time = us2ms (time_us);

  if (device->mousekeys_first_motion_time == 0)
    {
      /* Start acceleration _after_ the first move, so take
       * mousekeys_init_delay into account for t0
       */
      device->mousekeys_first_motion_time = time + device->mousekeys_init_delay;
      device->mousekeys_last_motion_time = device->mousekeys_first_motion_time;
      return 1.0;
    }

  init_time = time - device->mousekeys_first_motion_time;
  delta_t = time - device->mousekeys_last_motion_time;

  if (delta_t < 0)
    return 0.0;

  if (init_time < device->mousekeys_accel_time)
    speed = (double) (device->mousekeys_curve_factor *
                      pow ((double) init_time, MOUSEKEYS_CURVE) * delta_t / 1000.0);
  else
    speed = (double) (device->mousekeys_max_speed * delta_t / 1000.0);

  device->mousekeys_last_motion_time = time;

  return speed;
}

#undef MOUSEKEYS_CURVE

static void
emulate_pointer_motion (MetaInputDeviceNative *device_evdev,
                        int                    dx,
                        int                    dy)
{
  ClutterInputDevice *device = CLUTTER_INPUT_DEVICE (device_evdev);
  double dx_motion;
  double dy_motion;
  double speed;
  int64_t time_us;

  time_us = g_get_monotonic_time ();
  speed = mousekeys_get_speed_factor (device_evdev, time_us);

  if (dx < 0)
    dx_motion = floor (((double) dx) * speed);
  else
    dx_motion = ceil (((double) dx) * speed);

  if (dy < 0)
    dy_motion = floor (((double) dy) * speed);
  else
    dy_motion = ceil (((double) dy) * speed);

  clutter_virtual_input_device_notify_relative_motion (device->accessibility_virtual_device,
                                                       time_us, dx_motion, dy_motion);
}
static gboolean
is_numlock_active (MetaInputDeviceNative *device)
{
  MetaSeatImpl *seat_impl = device->seat_impl;
  struct xkb_state *xkb_state;

  xkb_state = meta_seat_impl_get_xkb_state_in_impl (seat_impl);

  return xkb_state_mod_name_is_active (xkb_state,
                                       "Mod2",
                                       XKB_STATE_MODS_LOCKED);
}

static void
enable_mousekeys (MetaInputDeviceNative *device_evdev)
{
  ClutterInputDevice *device = CLUTTER_INPUT_DEVICE (device_evdev);

  device_evdev->mousekeys_btn = CLUTTER_BUTTON_PRIMARY;
  device_evdev->move_mousekeys_timer = 0;
  device_evdev->mousekeys_first_motion_time = 0;
  device_evdev->mousekeys_last_motion_time = 0;
  device_evdev->last_mousekeys_key = 0;

  if (device->accessibility_virtual_device)
    return;

  device->accessibility_virtual_device =
    clutter_seat_create_virtual_device (clutter_input_device_get_seat (device),
                                        CLUTTER_POINTER_DEVICE);
}

static void
disable_mousekeys (MetaInputDeviceNative *device_evdev)
{
  ClutterInputDevice *device = CLUTTER_INPUT_DEVICE (device_evdev);

  stop_mousekeys_move (device_evdev);

  /* Make sure we don't leave button pressed behind... */
  if (device_evdev->mousekeys_btn_states[get_button_index (CLUTTER_BUTTON_PRIMARY)])
    {
      device_evdev->mousekeys_btn = CLUTTER_BUTTON_PRIMARY;
      emulate_button_release (device_evdev);
    }

  if (device_evdev->mousekeys_btn_states[get_button_index (CLUTTER_BUTTON_MIDDLE)])
    {
      device_evdev->mousekeys_btn = CLUTTER_BUTTON_MIDDLE;
      emulate_button_release (device_evdev);
    }

  if (device_evdev->mousekeys_btn_states[get_button_index (CLUTTER_BUTTON_SECONDARY)])
    {
      device_evdev->mousekeys_btn = CLUTTER_BUTTON_SECONDARY;
      emulate_button_release (device_evdev);
    }

  if (device->accessibility_virtual_device)
    g_clear_object (&device->accessibility_virtual_device);
}

static gboolean
trigger_mousekeys_move (gpointer data)
{
  MetaInputDeviceNative *device = data;
  int dx = 0;
  int dy = 0;

  if (device->mousekeys_first_motion_time == 0)
    {
      /* This is the first move, Secdule at mk_init_delay */
      device->move_mousekeys_timer =
        timeout_source_new (device->seat_impl,
                            device->mousekeys_init_delay,
                            trigger_mousekeys_move,
                            device);

    }
  else
    {
      /* More moves, reschedule at mk_interval */
      device->move_mousekeys_timer =
        timeout_source_new (device->seat_impl,
                            100, /* msec between mousekey events */
                            trigger_mousekeys_move,
                            device);
    }

  /* Pointer motion */
  switch (device->last_mousekeys_key)
    {
    case XKB_KEY_KP_Home:
    case XKB_KEY_KP_7:
    case XKB_KEY_KP_Up:
    case XKB_KEY_KP_8:
    case XKB_KEY_KP_Page_Up:
    case XKB_KEY_KP_9:
       dy = -1;
       break;
    case XKB_KEY_KP_End:
    case XKB_KEY_KP_1:
    case XKB_KEY_KP_Down:
    case XKB_KEY_KP_2:
    case XKB_KEY_KP_Page_Down:
    case XKB_KEY_KP_3:
       dy = 1;
       break;
    default:
       break;
    }

  switch (device->last_mousekeys_key)
    {
    case XKB_KEY_KP_Home:
    case XKB_KEY_KP_7:
    case XKB_KEY_KP_Left:
    case XKB_KEY_KP_4:
    case XKB_KEY_KP_End:
    case XKB_KEY_KP_1:
       dx = -1;
       break;
    case XKB_KEY_KP_Page_Up:
    case XKB_KEY_KP_9:
    case XKB_KEY_KP_Right:
    case XKB_KEY_KP_6:
    case XKB_KEY_KP_Page_Down:
    case XKB_KEY_KP_3:
       dx = 1;
       break;
    default:
       break;
    }

  if (dx != 0 || dy != 0)
    emulate_pointer_motion (device, dx, dy);

  /* We reschedule each time */
  return G_SOURCE_REMOVE;
}

static void
stop_mousekeys_move (MetaInputDeviceNative *device)
{
  device->mousekeys_first_motion_time = 0;
  device->mousekeys_last_motion_time = 0;

  g_clear_pointer (&device->move_mousekeys_timer, g_source_destroy);
}

static void
start_mousekeys_move (ClutterEvent          *event,
                      MetaInputDeviceNative *device)
{
  device->last_mousekeys_key = event->key.keyval;

  if (device->move_mousekeys_timer != 0)
    return;

  trigger_mousekeys_move (device);
}

static gboolean
handle_mousekeys_press (ClutterEvent          *event,
                        MetaInputDeviceNative *device)
{
  if (!(event->key.flags & CLUTTER_EVENT_FLAG_SYNTHETIC))
    stop_mousekeys_move (device);

  /* Do not handle mousekeys if NumLock is ON */
  if (is_numlock_active (device))
    return FALSE;

  /* Button selection */
  switch (event->key.keyval)
    {
    case XKB_KEY_KP_Divide:
      device->mousekeys_btn = CLUTTER_BUTTON_PRIMARY;
      return TRUE;
    case XKB_KEY_KP_Multiply:
      device->mousekeys_btn = CLUTTER_BUTTON_MIDDLE;
      return TRUE;
    case XKB_KEY_KP_Subtract:
      device->mousekeys_btn = CLUTTER_BUTTON_SECONDARY;
      return TRUE;
    default:
      break;
    }

  /* Button events */
  switch (event->key.keyval)
    {
    case XKB_KEY_KP_Begin:
    case XKB_KEY_KP_5:
      emulate_button_click (device);
      return TRUE;
    case XKB_KEY_KP_Insert:
    case XKB_KEY_KP_0:
      emulate_button_press (device);
      return TRUE;
    case XKB_KEY_KP_Decimal:
    case XKB_KEY_KP_Delete:
      emulate_button_release (device);
      return TRUE;
    case XKB_KEY_KP_Add:
      emulate_button_click (device);
      emulate_button_click (device);
      return TRUE;
    default:
      break;
    }

  /* Pointer motion */
  switch (event->key.keyval)
    {
    case XKB_KEY_KP_1:
    case XKB_KEY_KP_2:
    case XKB_KEY_KP_3:
    case XKB_KEY_KP_4:
    case XKB_KEY_KP_6:
    case XKB_KEY_KP_7:
    case XKB_KEY_KP_8:
    case XKB_KEY_KP_9:
    case XKB_KEY_KP_Down:
    case XKB_KEY_KP_End:
    case XKB_KEY_KP_Home:
    case XKB_KEY_KP_Left:
    case XKB_KEY_KP_Page_Down:
    case XKB_KEY_KP_Page_Up:
    case XKB_KEY_KP_Right:
    case XKB_KEY_KP_Up:
      start_mousekeys_move (event, device);
      return TRUE;
    default:
      break;
    }

  return FALSE;
}

static gboolean
handle_mousekeys_release (ClutterEvent          *event,
                          MetaInputDeviceNative *device)
{
  /* Do not handle mousekeys if NumLock is ON */
  if (is_numlock_active (device))
    return FALSE;

  switch (event->key.keyval)
    {
    case XKB_KEY_KP_0:
    case XKB_KEY_KP_1:
    case XKB_KEY_KP_2:
    case XKB_KEY_KP_3:
    case XKB_KEY_KP_4:
    case XKB_KEY_KP_5:
    case XKB_KEY_KP_6:
    case XKB_KEY_KP_7:
    case XKB_KEY_KP_8:
    case XKB_KEY_KP_9:
    case XKB_KEY_KP_Add:
    case XKB_KEY_KP_Begin:
    case XKB_KEY_KP_Decimal:
    case XKB_KEY_KP_Delete:
    case XKB_KEY_KP_Divide:
    case XKB_KEY_KP_Down:
    case XKB_KEY_KP_End:
    case XKB_KEY_KP_Home:
    case XKB_KEY_KP_Insert:
    case XKB_KEY_KP_Left:
    case XKB_KEY_KP_Multiply:
    case XKB_KEY_KP_Page_Down:
    case XKB_KEY_KP_Page_Up:
    case XKB_KEY_KP_Right:
    case XKB_KEY_KP_Subtract:
    case XKB_KEY_KP_Up:
      stop_mousekeys_move (device);
      return TRUE;
    default:
       break;
    }

  return FALSE;
}

gboolean
meta_input_device_native_process_kbd_a11y_event_in_impl (ClutterInputDevice *device,
                                                         ClutterEvent       *event)
{
  MetaInputDeviceNative *device_evdev = META_INPUT_DEVICE_NATIVE (device);

  if (device_evdev->a11y_flags & META_A11Y_KEYBOARD_ENABLED)
    {
      if (event->type == CLUTTER_KEY_PRESS)
        handle_enablekeys_press (event, device_evdev);
      else
        handle_enablekeys_release (event, device_evdev);
    }

  if (device_evdev->a11y_flags & META_A11Y_MOUSE_KEYS_ENABLED)
    {
      if (event->type == CLUTTER_KEY_PRESS &&
          handle_mousekeys_press (event, device_evdev))
        return TRUE; /* swallow event */
      if (event->type == CLUTTER_KEY_RELEASE &&
          handle_mousekeys_release (event, device_evdev))
        return TRUE; /* swallow event */
    }

  if ((device_evdev->a11y_flags & META_A11Y_BOUNCE_KEYS_ENABLED) &&
      (get_debounce_delay (device) != 0))
    {
      if ((event->type == CLUTTER_KEY_PRESS) && debounce_key (event, device_evdev))
        {
          notify_bounce_keys_reject (device_evdev);

          return TRUE;
        }
      else if (event->type == CLUTTER_KEY_RELEASE)
        start_bounce_keys (event, device_evdev);
    }

  if ((device_evdev->a11y_flags & META_A11Y_SLOW_KEYS_ENABLED) &&
      (get_slow_keys_delay (device) != 0))
    {
      if (event->type == CLUTTER_KEY_PRESS)
        return start_slow_keys (event, device_evdev);
      else if (event->type == CLUTTER_KEY_RELEASE)
        return stop_slow_keys (event, device_evdev);
    }

  if (device_evdev->a11y_flags & META_A11Y_STICKY_KEYS_ENABLED)
    {
      if (event->type == CLUTTER_KEY_PRESS)
        handle_stickykeys_press (event, device_evdev);
      else if (event->type == CLUTTER_KEY_RELEASE)
        handle_stickykeys_release (event, device_evdev);
    }

  return FALSE;
}

void
meta_input_device_native_apply_kbd_a11y_settings_in_impl (MetaInputDeviceNative *device,
                                                          MetaKbdA11ySettings   *settings)
{
  MetaKeyboardA11yFlags changed_flags = (device->a11y_flags ^ settings->controls);

  if (changed_flags & (META_A11Y_KEYBOARD_ENABLED | META_A11Y_SLOW_KEYS_ENABLED))
    clear_slow_keys (device);

  if (changed_flags & (META_A11Y_KEYBOARD_ENABLED | META_A11Y_BOUNCE_KEYS_ENABLED))
    device->debounce_key = 0;

  if (changed_flags & (META_A11Y_KEYBOARD_ENABLED | META_A11Y_STICKY_KEYS_ENABLED))
    {
      device->stickykeys_depressed_mask = 0;
      update_internal_xkb_state (device, 0, 0);
    }

  if (changed_flags & META_A11Y_KEYBOARD_ENABLED)
    {
      device->toggle_slowkeys_timer = 0;
      device->shift_count = 0;
      device->last_shift_time = 0;
    }

  if (changed_flags & (META_A11Y_KEYBOARD_ENABLED | META_A11Y_MOUSE_KEYS_ENABLED))
    {
      if (settings->controls &
          (META_A11Y_KEYBOARD_ENABLED | META_A11Y_MOUSE_KEYS_ENABLED))
        enable_mousekeys (device);
      else
        disable_mousekeys (device);
    }
  update_mousekeys_params (device, settings);

  /* Keep our own copy of keyboard a11y features flags to see what changes */
  device->a11y_flags = settings->controls;
}

void
meta_input_device_native_a11y_maybe_notify_toggle_keys_in_impl (MetaInputDeviceNative *device)
{
  if (device->a11y_flags & META_A11Y_TOGGLE_KEYS_ENABLED)
    meta_input_device_native_bell_notify (device);
}

static void
meta_input_device_native_class_init (MetaInputDeviceNativeClass *klass)
{
  ClutterInputDeviceClass *device_class = CLUTTER_INPUT_DEVICE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_input_device_native_finalize;
  object_class->set_property = meta_input_device_native_set_property;
  object_class->get_property = meta_input_device_native_get_property;

  device_class->is_mode_switch_button = meta_input_device_native_is_mode_switch_button;
  device_class->get_group_n_modes = meta_input_device_native_get_group_n_modes;
  device_class->is_grouped = meta_input_device_native_is_grouped;
  device_class->get_pad_feature_group = meta_input_device_native_get_pad_feature_group;

  obj_props[PROP_DEVICE_MATRIX] =
    g_param_spec_boxed ("device-matrix",
                        "Device input matrix",
                        "Device input matrix",
                        CAIRO_GOBJECT_TYPE_MATRIX,
                        CLUTTER_PARAM_READWRITE);
  obj_props[PROP_OUTPUT_ASPECT_RATIO] =
    g_param_spec_double ("output-aspect-ratio",
                         "Output aspect ratio",
                         "Output aspect ratio",
                         0, G_MAXDOUBLE, 0,
                         CLUTTER_PARAM_READWRITE);

  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
meta_input_device_native_init (MetaInputDeviceNative *self)
{
  cairo_matrix_init_identity (&self->device_matrix);
  self->device_aspect_ratio = 0;
  self->output_ratio = 0;
}

static void
update_pad_features (MetaInputDeviceNative *device_native)
{
  ClutterInputDevice *device = CLUTTER_INPUT_DEVICE (device_native);
  struct libinput_device *libinput_device;
  struct libinput_tablet_pad_mode_group *mode_group;
  int n_groups, n_buttons, n_rings, n_strips, n_modes, i, j;

  libinput_device = meta_input_device_native_get_libinput_device (device);
  n_rings = libinput_device_tablet_pad_get_num_rings (libinput_device);
  n_strips = libinput_device_tablet_pad_get_num_strips (libinput_device);
  n_groups = libinput_device_tablet_pad_get_num_mode_groups (libinput_device);
  n_buttons = libinput_device_tablet_pad_get_num_buttons (libinput_device);

  device_native->pad_features = g_array_new (FALSE, FALSE, sizeof (PadFeature));
  device_native->modes = g_array_sized_new (FALSE, FALSE, sizeof (int), n_groups);

  for (i = 0; i < n_groups; i++)
    {
      mode_group =
        libinput_device_tablet_pad_get_mode_group (libinput_device, i);

      n_modes = libinput_tablet_pad_mode_group_get_num_modes (mode_group);
      g_array_append_val (device_native->modes, n_modes);

      for (j = 0; j < n_buttons; j++)
        {
          gboolean is_mode_switch =
            libinput_tablet_pad_mode_group_button_is_toggle (mode_group, j) != 0;
          PadFeature feature = { CLUTTER_PAD_FEATURE_BUTTON, j, i, is_mode_switch };

          if (libinput_tablet_pad_mode_group_has_button (mode_group, j))
            g_array_append_val (device_native->pad_features, feature);
        }

      for (j = 0; j < n_rings; j++)
        {
          PadFeature feature = { CLUTTER_PAD_FEATURE_RING, j, i };

          if (libinput_tablet_pad_mode_group_has_ring (mode_group, j))
            g_array_append_val (device_native->pad_features, feature);
        }

      for (j = 0; j < n_strips; j++)
        {
          PadFeature feature = { CLUTTER_PAD_FEATURE_STRIP, j, i };

          if (libinput_tablet_pad_mode_group_has_strip (mode_group, j))
            g_array_append_val (device_native->pad_features, feature);
        }
    }
}

/*
 * meta_input_device_native_new:
 * @manager: the device manager
 * @seat: the seat the device will belong to
 * @libinput_device: the libinput device
 *
 * Create a new ClutterInputDevice given a libinput device and associate
 * it with the provided seat.
 */
ClutterInputDevice *
meta_input_device_native_new_in_impl (MetaSeatImpl           *seat_impl,
                                      struct libinput_device *libinput_device)
{
  MetaInputDeviceNative *device;
  ClutterInputDeviceType type;
  char *vendor, *product;
  int n_rings = 0, n_strips = 0, n_groups = 1, n_buttons = 0;
  char *node_path;
  double width, height;

  type = meta_input_device_native_determine_type_in_impl (libinput_device);
  vendor = g_strdup_printf ("%.4x", libinput_device_get_id_vendor (libinput_device));
  product = g_strdup_printf ("%.4x", libinput_device_get_id_product (libinput_device));
  node_path = g_strdup_printf ("/dev/input/%s", libinput_device_get_sysname (libinput_device));

  if (libinput_device_has_capability (libinput_device,
                                      LIBINPUT_DEVICE_CAP_TABLET_PAD))
    {
      n_rings = libinput_device_tablet_pad_get_num_rings (libinput_device);
      n_strips = libinput_device_tablet_pad_get_num_strips (libinput_device);
      n_groups = libinput_device_tablet_pad_get_num_mode_groups (libinput_device);
      n_buttons = libinput_device_tablet_pad_get_num_buttons (libinput_device);
    }

  device = g_object_new (META_TYPE_INPUT_DEVICE_NATIVE,
                         "name", libinput_device_get_name (libinput_device),
                         "device-type", type,
                         "device-mode", CLUTTER_INPUT_MODE_PHYSICAL,
                         "vendor-id", vendor,
                         "product-id", product,
                         "n-rings", n_rings,
                         "n-strips", n_strips,
                         "n-mode-groups", n_groups,
                         "n-buttons", n_buttons,
                         "device-node", node_path,
                         "seat", seat_impl->seat_native,
                         NULL);

  device->seat_impl = seat_impl;
  device->libinput_device = libinput_device;

  libinput_device_set_user_data (libinput_device, device);
  libinput_device_ref (libinput_device);
  g_free (vendor);
  g_free (product);
  g_free (node_path);

  if (libinput_device_has_capability (libinput_device,
                                      LIBINPUT_DEVICE_CAP_TABLET_PAD))
    update_pad_features (device);

  if (libinput_device_get_size (libinput_device, &width, &height) == 0)
    device->device_aspect_ratio = width / height;

  device->group = (intptr_t) libinput_device_get_device_group (libinput_device);

  return CLUTTER_INPUT_DEVICE (device);
}

/*
 * meta_input_device_native_new_virtual:
 * @seat: the seat the device will belong to
 * @type: the input device type
 *
 * Create a new virtual ClutterInputDevice of the given type.
 */
ClutterInputDevice *
meta_input_device_native_new_virtual (MetaSeatImpl           *seat_impl,
                                      ClutterInputDeviceType  type,
                                      ClutterInputMode        mode)
{
  MetaInputDeviceNative *device;
  const char *name;

  switch (type)
    {
    case CLUTTER_KEYBOARD_DEVICE:
      name = "Virtual keyboard device for seat";
      break;
    case CLUTTER_POINTER_DEVICE:
      name = "Virtual pointer device for seat";
      break;
    case CLUTTER_TOUCHSCREEN_DEVICE:
      name = "Virtual touchscreen device for seat";
      break;
    default:
      name = "Virtual device for seat";
      break;
    };

  device = g_object_new (META_TYPE_INPUT_DEVICE_NATIVE,
                         "name", name,
                         "device-type", type,
                         "device-mode", mode,
                         "seat", seat_impl->seat_native,
                         NULL);

  device->seat_impl = seat_impl;

  return CLUTTER_INPUT_DEVICE (device);
}

MetaSeatImpl *
meta_input_device_native_get_seat_impl (MetaInputDeviceNative *device)
{
  return device->seat_impl;
}

void
meta_input_device_native_update_leds_in_impl (MetaInputDeviceNative *device,
                                              enum libinput_led      leds)
{
  if (!device->libinput_device)
    return;

  libinput_device_led_update (device->libinput_device, leds);
}

ClutterInputDeviceType
meta_input_device_native_determine_type_in_impl (struct libinput_device *ldev)
{
  /* This setting is specific to touchpads and alike, only in these
   * devices there is this additional layer of touch event interpretation.
   */
  if (libinput_device_config_tap_get_finger_count (ldev) > 0)
    return CLUTTER_TOUCHPAD_DEVICE;
  else if (libinput_device_has_capability (ldev, LIBINPUT_DEVICE_CAP_TABLET_TOOL))
    return CLUTTER_TABLET_DEVICE;
  else if (libinput_device_has_capability (ldev, LIBINPUT_DEVICE_CAP_TABLET_PAD))
    return CLUTTER_PAD_DEVICE;
  else if (libinput_device_has_capability (ldev, LIBINPUT_DEVICE_CAP_POINTER))
    return CLUTTER_POINTER_DEVICE;
  else if (libinput_device_has_capability (ldev, LIBINPUT_DEVICE_CAP_TOUCH))
    return CLUTTER_TOUCHSCREEN_DEVICE;
  else if (libinput_device_has_capability (ldev, LIBINPUT_DEVICE_CAP_KEYBOARD))
    return CLUTTER_KEYBOARD_DEVICE;
  else
    return CLUTTER_EXTENSION_DEVICE;
}

/**
 * meta_input_device_native_get_libinput_device:
 * @device: a #ClutterInputDevice
 *
 * Retrieves the libinput_device struct held in @device.
 *
 * Returns: The libinput_device struct
 *
 * Since: 1.20
 * Stability: unstable
 **/
struct libinput_device *
meta_input_device_native_get_libinput_device (ClutterInputDevice *device)
{
  MetaInputDeviceNative *device_evdev;

  g_return_val_if_fail (META_IS_INPUT_DEVICE_NATIVE (device), NULL);

  device_evdev = META_INPUT_DEVICE_NATIVE (device);

  return device_evdev->libinput_device;
}

void
meta_input_device_native_translate_coordinates_in_impl (ClutterInputDevice *device,
                                                        MetaViewportInfo   *viewports,
                                                        float              *x,
                                                        float              *y)
{
  MetaInputDeviceNative *device_evdev = META_INPUT_DEVICE_NATIVE (device);
  double min_x = 0, min_y = 0, max_x = 1, max_y = 1;
  float stage_width, stage_height;
  double x_d, y_d;

  meta_viewport_info_get_extents (viewports, &stage_width, &stage_height);
  x_d = *x / stage_width;
  y_d = *y / stage_height;

  /* Apply aspect ratio */
  if (device_evdev->output_ratio > 0 &&
      device_evdev->device_aspect_ratio > 0)
    {
      double ratio = device_evdev->device_aspect_ratio / device_evdev->output_ratio;

      if (ratio > 1)
        x_d *= ratio;
      else if (ratio < 1)
        y_d *= 1 / ratio;
    }

  cairo_matrix_transform_point (&device_evdev->device_matrix, &min_x, &min_y);
  cairo_matrix_transform_point (&device_evdev->device_matrix, &max_x, &max_y);
  cairo_matrix_transform_point (&device_evdev->device_matrix, &x_d, &y_d);

  *x = CLAMP (x_d, MIN (min_x, max_x), MAX (min_x, max_x)) * stage_width;
  *y = CLAMP (y_d, MIN (min_y, max_y), MAX (min_y, max_y)) * stage_height;
}

MetaInputDeviceMapping
meta_input_device_native_get_mapping_mode_in_impl (ClutterInputDevice *device)
{
  MetaInputDeviceNative *device_native = META_INPUT_DEVICE_NATIVE (device);
  ClutterInputDeviceType device_type;

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device),
                        META_INPUT_DEVICE_MAPPING_ABSOLUTE);

  device_type = clutter_input_device_get_device_type (device);
  g_return_val_if_fail (device_type == CLUTTER_TABLET_DEVICE ||
                        device_type == CLUTTER_PEN_DEVICE ||
                        device_type == CLUTTER_ERASER_DEVICE,
                        META_INPUT_DEVICE_MAPPING_ABSOLUTE);

  return device_native->mapping_mode;
}

void
meta_input_device_native_set_mapping_mode_in_impl (ClutterInputDevice     *device,
                                                   MetaInputDeviceMapping  mapping)
{
  MetaInputDeviceNative *device_native = META_INPUT_DEVICE_NATIVE (device);
  ClutterInputDeviceType device_type;

  g_return_if_fail (CLUTTER_IS_INPUT_DEVICE (device));

  device_type = clutter_input_device_get_device_type (device);
  g_return_if_fail (device_type == CLUTTER_TABLET_DEVICE ||
                    device_type == CLUTTER_PEN_DEVICE ||
                    device_type == CLUTTER_ERASER_DEVICE);

  device_native->mapping_mode = mapping;
}

void
meta_input_device_native_set_coords_in_impl (MetaInputDeviceNative *device_native,
                                             float                  x,
                                             float                  y)
{
  device_native->pointer_x = x;
  device_native->pointer_y = y;
}

void
meta_input_device_native_get_coords_in_impl (MetaInputDeviceNative *device_native,
                                             float                 *x,
                                             float                 *y)
{
  if (x)
    *x = device_native->pointer_x;
  if (y)
    *y = device_native->pointer_y;
}

void
meta_input_device_native_detach_libinput_in_impl (MetaInputDeviceNative *device_native)
{
  g_clear_pointer (&device_native->libinput_device, libinput_device_unref);
}
