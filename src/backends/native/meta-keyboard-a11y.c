/*
 * Copyright (C) 2025 Red Hat
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "meta-keyboard-a11y-private.h"

#include "meta-input-thread.h"

enum
{
  PROP_0,
  PROP_SEAT_IMPL,
  N_PROPS,
};

static GParamSpec *props[N_PROPS] = { 0, };

typedef struct _SlowKeysEventPending SlowKeysEventPending;

struct _SlowKeysEventPending
{
  MetaKeyboardA11y *keyboard_a11y;
  ClutterEvent *event;
  GSource *timer;
};

struct _MetaKeyboardA11y
{
  GObject parent_instance;

  MetaSeatImpl *seat_impl;

  MetaKeyboardA11yFlags a11y_flags;
  ClutterVirtualInputDevice *mousekeys_pointer;
  GList *slow_keys_list;
  GSource *debounce_timer;
  uint16_t debounce_key;
  xkb_mod_mask_t stickykeys_depressed_mask;
  xkb_mod_mask_t stickykeys_latched_mask;
  xkb_mod_mask_t stickykeys_locked_mask;
  GSource *toggle_slowkeys_timer;
  uint16_t shift_count;
  uint32_t last_shift_time;
  int mousekeys_btn;
  gboolean mousekeys_btn_states[3];
  uint32_t mousekeys_first_motion_time; /* ms */
  uint32_t mousekeys_last_motion_time; /* ms */
  guint mousekeys_init_delay;
  guint mousekeys_accel_time;
  guint mousekeys_max_speed;
  double mousekeys_curve_factor;
  GSource *move_mousekeys_timer;
  uint16_t last_mousekeys_key;
};

G_DEFINE_TYPE (MetaKeyboardA11y, meta_keyboard_a11y, G_TYPE_OBJECT)

static void stop_bounce_keys (MetaKeyboardA11y *keyboard_a11y);

static void stop_mousekeys_move (MetaKeyboardA11y *keyboard_a11y);

static void
meta_keyboard_a11y_bell_notify (MetaKeyboardA11y *keyboard_a11y)
{
  MetaSeatImpl *seat_impl = keyboard_a11y->seat_impl;

  meta_seat_impl_notify_bell_in_impl (seat_impl);
}

static void
free_pending_slow_key (gpointer data)
{
  SlowKeysEventPending *slow_keys_event = data;

  clutter_event_free (slow_keys_event->event);
  g_clear_pointer (&slow_keys_event->timer, g_source_destroy);
  g_free (slow_keys_event);
}

static void
clear_slow_keys (MetaKeyboardA11y *keyboard_a11y)
{
  g_clear_list (&keyboard_a11y->slow_keys_list, free_pending_slow_key);
}

static guint
get_slow_keys_delay (MetaKeyboardA11y *keyboard_a11y)
{
  MetaSeatImpl *seat_impl = keyboard_a11y->seat_impl;
  MetaKbdA11ySettings a11y_settings;
  MetaInputSettings *input_settings;

  input_settings = meta_seat_impl_get_input_settings (seat_impl);
  meta_input_settings_get_kbd_a11y_settings (input_settings, &a11y_settings);
  /* Settings use int, we use uint, make sure we dont go negative */
  return MAX (0, a11y_settings.slowkeys_delay);
}

static gboolean
trigger_slow_keys (gpointer data)
{
  SlowKeysEventPending *slow_keys_event = data;
  MetaKeyboardA11y *keyboard_a11y = slow_keys_event->keyboard_a11y;
  ClutterModifierSet raw_modifiers;
  ClutterEvent *event = slow_keys_event->event;
  ClutterEvent *copy;

  clutter_event_get_key_state (event,
                               &raw_modifiers.pressed,
                               &raw_modifiers.latched,
                               &raw_modifiers.locked);

  /* Alter timestamp and emit the event */
  copy = clutter_event_key_new (clutter_event_type (event),
                                clutter_event_get_flags (event),
                                g_get_monotonic_time (),
                                clutter_event_get_source_device (event),
                                raw_modifiers,
                                clutter_event_get_state (event),
                                clutter_event_get_key_symbol (event),
                                clutter_event_get_event_code (event),
                                clutter_event_get_key_code (event),
                                clutter_event_get_key_unicode (event));
  _clutter_event_push (copy, FALSE);

  /* Then remote the pending event */
  keyboard_a11y->slow_keys_list =
    g_list_remove (keyboard_a11y->slow_keys_list, slow_keys_event);
  free_pending_slow_key (slow_keys_event);

  if (keyboard_a11y->a11y_flags & META_A11Y_SLOW_KEYS_BEEP_ACCEPT)
    meta_keyboard_a11y_bell_notify (keyboard_a11y);

  return G_SOURCE_REMOVE;
}

static int
find_pending_event_by_keycode (gconstpointer a,
                               gconstpointer b)
{
  const SlowKeysEventPending *pa = a;
  const ClutterEvent *ea = pa->event;
  const ClutterEvent *eb = b;

  return clutter_event_get_key_code (eb) - clutter_event_get_key_code (ea);
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
start_slow_keys (ClutterEvent     *event,
                 MetaKeyboardA11y *keyboard_a11y)
{
  SlowKeysEventPending *slow_keys_event;
  MetaSeatImpl *seat_impl = keyboard_a11y->seat_impl;

  if (clutter_event_get_flags (event) & CLUTTER_EVENT_FLAG_REPEATED)
    return TRUE;

  slow_keys_event = g_new0 (SlowKeysEventPending, 1);
  slow_keys_event->keyboard_a11y = keyboard_a11y;
  slow_keys_event->event = clutter_event_copy (event);
  slow_keys_event->timer =
    timeout_source_new (seat_impl,
                        get_slow_keys_delay (keyboard_a11y),
                        trigger_slow_keys,
                        slow_keys_event);
  keyboard_a11y->slow_keys_list =
    g_list_append (keyboard_a11y->slow_keys_list, slow_keys_event);

  if (keyboard_a11y->a11y_flags & META_A11Y_SLOW_KEYS_BEEP_PRESS)
    meta_keyboard_a11y_bell_notify (keyboard_a11y);

  return TRUE;
}

static gboolean
stop_slow_keys (ClutterEvent     *event,
                MetaKeyboardA11y *keyboard_a11y)
{
  GList *item;

  /* Check if we have a slow key event queued for this key event */
  item = g_list_find_custom (keyboard_a11y->slow_keys_list, event,
                             find_pending_event_by_keycode);
  if (item)
    {
      SlowKeysEventPending *slow_keys_event = item->data;

      keyboard_a11y->slow_keys_list =
        g_list_delete_link (keyboard_a11y->slow_keys_list, item);
      free_pending_slow_key (slow_keys_event);

      if (keyboard_a11y->a11y_flags & META_A11Y_SLOW_KEYS_BEEP_REJECT)
        meta_keyboard_a11y_bell_notify (keyboard_a11y);

      return TRUE;
    }

  /* If no key press event was pending, just emit the key release as-is */
  return FALSE;
}

static guint
get_debounce_delay (MetaKeyboardA11y *keyboard_a11y)
{
  MetaSeatImpl *seat_impl = keyboard_a11y->seat_impl;
  MetaKbdA11ySettings a11y_settings;
  MetaInputSettings *input_settings;

  input_settings = meta_seat_impl_get_input_settings (seat_impl);
  meta_input_settings_get_kbd_a11y_settings (input_settings, &a11y_settings);
  /* Settings use int, we use uint, make sure we dont go negative */
  return MAX (0, a11y_settings.debounce_delay);
}

static gboolean
clear_bounce_keys (gpointer user_data)
{
  MetaKeyboardA11y *keyboard_a11y = user_data;

  keyboard_a11y->debounce_key = 0;
  keyboard_a11y->debounce_timer = 0;

  return G_SOURCE_REMOVE;
}

static void
start_bounce_keys (ClutterEvent     *event,
                   MetaKeyboardA11y *keyboard_a11y)
{
  MetaSeatImpl *seat_impl = keyboard_a11y->seat_impl;

  stop_bounce_keys (keyboard_a11y);

  keyboard_a11y->debounce_key = clutter_event_get_key_code (event);
  keyboard_a11y->debounce_timer =
    timeout_source_new (seat_impl,
                        get_debounce_delay (keyboard_a11y),
                        clear_bounce_keys,
                        keyboard_a11y);
}

static void
stop_bounce_keys (MetaKeyboardA11y *keyboard_a11y)
{
  g_clear_pointer (&keyboard_a11y->debounce_timer, g_source_destroy);
}

static void
notify_bounce_keys_reject (MetaKeyboardA11y *keyboard_a11y)
{
  if (keyboard_a11y->a11y_flags & META_A11Y_BOUNCE_KEYS_BEEP_REJECT)
    meta_keyboard_a11y_bell_notify (keyboard_a11y);
}

static gboolean
debounce_key (ClutterEvent     *event,
              MetaKeyboardA11y *keyboard_a11y)
{
  return keyboard_a11y->debounce_key == clutter_event_get_key_code (event);
}

static gboolean
key_event_is_modifier (ClutterEvent *event)
{
  switch (clutter_event_get_key_symbol (event))
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
notify_stickykeys_mask (MetaKeyboardA11y *keyboard_a11y)
{
  MetaSeatImpl *seat_impl = keyboard_a11y->seat_impl;

  meta_seat_impl_notify_kbd_a11y_mods_state_changed_in_impl (seat_impl,
                                                             keyboard_a11y->stickykeys_latched_mask,
                                                             keyboard_a11y->stickykeys_locked_mask);
}

static void
update_internal_xkb_state (MetaKeyboardA11y *keyboard_a11y,
                           xkb_mod_mask_t    new_latched_mask,
                           xkb_mod_mask_t    new_locked_mask)
{
  MetaSeatImpl *seat_impl = keyboard_a11y->seat_impl;
  xkb_mod_mask_t depressed_mods;
  xkb_mod_mask_t latched_mods;
  xkb_mod_mask_t locked_mods;
  xkb_mod_mask_t group_mods;
  struct xkb_state *xkb_state;

  if (keyboard_a11y->stickykeys_latched_mask == new_latched_mask &&
      keyboard_a11y->stickykeys_locked_mask == new_locked_mask)
    return;

  g_rw_lock_writer_lock (&seat_impl->state_lock);

  xkb_state = meta_seat_impl_get_xkb_state_in_impl (seat_impl);
  depressed_mods = xkb_state_serialize_mods (xkb_state, XKB_STATE_MODS_DEPRESSED);
  latched_mods = xkb_state_serialize_mods (xkb_state, XKB_STATE_MODS_LATCHED);
  locked_mods = xkb_state_serialize_mods (xkb_state, XKB_STATE_MODS_LOCKED);

  latched_mods &= ~keyboard_a11y->stickykeys_latched_mask;
  locked_mods &= ~keyboard_a11y->stickykeys_locked_mask;

  keyboard_a11y->stickykeys_latched_mask = new_latched_mask;
  keyboard_a11y->stickykeys_locked_mask = new_locked_mask;

  latched_mods |= keyboard_a11y->stickykeys_latched_mask;
  locked_mods |= keyboard_a11y->stickykeys_locked_mask;

  group_mods = xkb_state_serialize_layout (xkb_state, XKB_STATE_LAYOUT_EFFECTIVE);

  xkb_state_update_mask (xkb_state,
                         depressed_mods,
                         latched_mods,
                         locked_mods,
                         0, 0, group_mods);
  notify_stickykeys_mask (keyboard_a11y);

  g_rw_lock_writer_unlock (&seat_impl->state_lock);
}

static ClutterEvent *
rewrite_stickykeys_event (ClutterEvent     *event,
                          MetaKeyboardA11y *keyboard_a11y,
                          xkb_mod_mask_t    new_latched_mask,
                          xkb_mod_mask_t    new_locked_mask)
{
  MetaSeatImpl *seat_impl = keyboard_a11y->seat_impl;
  struct xkb_state *xkb_state;
  ClutterModifierSet raw_modifiers;
  ClutterEvent *rewritten_event;
  ClutterModifierType modifiers;

  update_internal_xkb_state (keyboard_a11y, new_latched_mask, new_locked_mask);
  xkb_state = meta_seat_impl_get_xkb_state_in_impl (seat_impl);
  modifiers =
    xkb_state_serialize_mods (xkb_state, XKB_STATE_MODS_EFFECTIVE) |
    seat_impl->button_state;

  raw_modifiers = (ClutterModifierSet) {
    .pressed = xkb_state_serialize_mods (xkb_state, XKB_STATE_MODS_DEPRESSED),
    .latched = xkb_state_serialize_mods (xkb_state, XKB_STATE_MODS_LATCHED),
    .locked = xkb_state_serialize_mods (xkb_state, XKB_STATE_MODS_LOCKED),
  };

  rewritten_event =
    clutter_event_key_new (clutter_event_type (event),
                           clutter_event_get_flags (event),
                           clutter_event_get_time_us (event),
                           clutter_event_get_source_device (event),
                           raw_modifiers,
                           modifiers,
                           clutter_event_get_key_symbol (event),
                           clutter_event_get_event_code (event),
                           clutter_event_get_key_code (event),
                           clutter_event_get_key_unicode (event));

  return rewritten_event;
}

static void
notify_stickykeys_change (MetaKeyboardA11y *keyboard_a11y)
{
  MetaSeatImpl *seat_impl = keyboard_a11y->seat_impl;

  /* Every time sticky keys setting is changed, clear the masks */
  keyboard_a11y->stickykeys_depressed_mask = 0;
  update_internal_xkb_state (keyboard_a11y, 0, 0);

  meta_seat_impl_notify_kbd_a11y_flags_changed_in_impl (seat_impl,
                                                        keyboard_a11y->a11y_flags,
                                                        META_A11Y_STICKY_KEYS_ENABLED);
}

static void
set_stickykeys_off (MetaKeyboardA11y *keyboard_a11y)
{
  keyboard_a11y->a11y_flags &= ~META_A11Y_STICKY_KEYS_ENABLED;
  notify_stickykeys_change (keyboard_a11y);
}

static void
set_stickykeys_on (MetaKeyboardA11y *keyboard_a11y)
{
  keyboard_a11y->a11y_flags |= META_A11Y_STICKY_KEYS_ENABLED;
  notify_stickykeys_change (keyboard_a11y);
}

static void
set_slowkeys_off (MetaKeyboardA11y *keyboard_a11y)
{
  MetaSeatImpl *seat_impl = keyboard_a11y->seat_impl;

  keyboard_a11y->a11y_flags &= ~META_A11Y_SLOW_KEYS_ENABLED;

  meta_seat_impl_notify_kbd_a11y_flags_changed_in_impl (seat_impl,
                                                        keyboard_a11y->a11y_flags,
                                                        META_A11Y_SLOW_KEYS_ENABLED);
}

static void
set_slowkeys_on (MetaKeyboardA11y *keyboard_a11y)
{
  MetaSeatImpl *seat_impl = keyboard_a11y->seat_impl;

  keyboard_a11y->a11y_flags |= META_A11Y_SLOW_KEYS_ENABLED;

  meta_seat_impl_notify_kbd_a11y_flags_changed_in_impl (seat_impl,
                                                        keyboard_a11y->a11y_flags,
                                                        META_A11Y_SLOW_KEYS_ENABLED);
}

static ClutterEvent *
handle_stickykeys_press (ClutterEvent     *event,
                         MetaKeyboardA11y *keyboard_a11y)
{
  MetaSeatImpl *seat_impl = keyboard_a11y->seat_impl;
  struct xkb_state *xkb_state;

  if (!key_event_is_modifier (event))
    return NULL;

  if (keyboard_a11y->stickykeys_depressed_mask &&
      (keyboard_a11y->a11y_flags & META_A11Y_STICKY_KEYS_TWO_KEY_OFF))
    {
      set_stickykeys_off (keyboard_a11y);
      return rewrite_stickykeys_event (event, keyboard_a11y, 0, 0);
    }

  xkb_state = meta_seat_impl_get_xkb_state_in_impl (seat_impl);
  keyboard_a11y->stickykeys_depressed_mask =
    xkb_state_serialize_mods (xkb_state, XKB_STATE_MODS_DEPRESSED);

  /* Ignore the lock modifier mask, that one cannot be sticky, yet the
   * CAPS_LOCK key itself counts as a modifier as it might be remapped
   * to some other modifier which can be sticky.
   */
  keyboard_a11y->stickykeys_depressed_mask &= ~CLUTTER_LOCK_MASK;
  return NULL;
}

static ClutterEvent *
handle_stickykeys_release (ClutterEvent     *event,
                           MetaKeyboardA11y *keyboard_a11y)
{
  xkb_mod_mask_t depressed_mods;
  xkb_mod_mask_t new_latched_mask;
  xkb_mod_mask_t new_locked_mask;

  depressed_mods = keyboard_a11y->stickykeys_depressed_mask;

  /* When pressing a modifier and key together, don't make the modifier sticky.
   * When pressing two modifiers together, only latch/lock once.
   */
  keyboard_a11y->stickykeys_depressed_mask = 0;

  if (key_event_is_modifier (event))
    {
      if (!depressed_mods)
        return NULL;

      new_latched_mask = keyboard_a11y->stickykeys_latched_mask;
      new_locked_mask = keyboard_a11y->stickykeys_locked_mask;

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

      if (keyboard_a11y->a11y_flags & META_A11Y_STICKY_KEYS_BEEP)
        meta_keyboard_a11y_bell_notify (keyboard_a11y);
    }
  else
    {
      if (!keyboard_a11y->stickykeys_latched_mask)
        return NULL;

      new_latched_mask = 0;
      new_locked_mask = keyboard_a11y->stickykeys_locked_mask;
    }

  return rewrite_stickykeys_event (event, keyboard_a11y,
                                   new_latched_mask, new_locked_mask);
}

static gboolean
trigger_toggle_slowkeys (gpointer user_data)
{
  MetaKeyboardA11y *keyboard_a11y = user_data;

  keyboard_a11y->toggle_slowkeys_timer = 0;

  if (keyboard_a11y->a11y_flags & META_A11Y_FEATURE_STATE_CHANGE_BEEP)
    meta_keyboard_a11y_bell_notify (keyboard_a11y);

  if (keyboard_a11y->a11y_flags & META_A11Y_SLOW_KEYS_ENABLED)
    set_slowkeys_off (keyboard_a11y);
  else
    set_slowkeys_on (keyboard_a11y);

  return G_SOURCE_REMOVE;
}

static void
start_toggle_slowkeys (MetaKeyboardA11y *keyboard_a11y)
{
  MetaSeatImpl *seat_impl = keyboard_a11y->seat_impl;

  if (keyboard_a11y->toggle_slowkeys_timer != 0)
    return;

  keyboard_a11y->toggle_slowkeys_timer =
    timeout_source_new (seat_impl,
                        8 * 1000 /* 8 secs */,
                        trigger_toggle_slowkeys,
                        keyboard_a11y);
}

static void
stop_toggle_slowkeys (MetaKeyboardA11y *keyboard_a11y)
{
  g_clear_pointer (&keyboard_a11y->toggle_slowkeys_timer,
                   g_source_destroy);
}

static void
handle_enablekeys_press (ClutterEvent     *event,
                         MetaKeyboardA11y *keyboard_a11y)
{
  uint32_t keyval, time_ms;

  keyval = clutter_event_get_key_symbol (event);
  time_ms = clutter_event_get_time (event);

  if (keyval == XKB_KEY_Shift_L || keyval == XKB_KEY_Shift_R)
    {
      start_toggle_slowkeys (keyboard_a11y);

      if (time_ms > keyboard_a11y->last_shift_time + 15 * 1000 /* 15 secs  */)
        keyboard_a11y->shift_count = 1;
      else
        keyboard_a11y->shift_count++;

      keyboard_a11y->last_shift_time = time_ms;
    }
  else
    {
      keyboard_a11y->shift_count = 0;
      stop_toggle_slowkeys (keyboard_a11y);
    }
}

static void
handle_enablekeys_release (ClutterEvent     *event,
                           MetaKeyboardA11y *keyboard_a11y)
{
  uint32_t keyval;

  keyval = clutter_event_get_key_symbol (event);

  if (keyval == XKB_KEY_Shift_L || keyval == XKB_KEY_Shift_R)
    {
      stop_toggle_slowkeys (keyboard_a11y);
      if (keyboard_a11y->shift_count >= 5)
        {
          keyboard_a11y->shift_count = 0;

          if (keyboard_a11y->a11y_flags & META_A11Y_FEATURE_STATE_CHANGE_BEEP)
            meta_keyboard_a11y_bell_notify (keyboard_a11y);

          if (keyboard_a11y->a11y_flags & META_A11Y_STICKY_KEYS_ENABLED)
            set_stickykeys_off (keyboard_a11y);
          else
            set_stickykeys_on (keyboard_a11y);
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
emulate_button_press (MetaKeyboardA11y *keyboard_a11y)
{
  int btn = keyboard_a11y->mousekeys_btn;

  if (keyboard_a11y->mousekeys_btn_states[get_button_index (btn)])
    return;

  clutter_virtual_input_device_notify_button (keyboard_a11y->mousekeys_pointer,
                                              g_get_monotonic_time (), btn,
                                              CLUTTER_BUTTON_STATE_PRESSED);
  keyboard_a11y->mousekeys_btn_states[get_button_index (btn)] = CLUTTER_BUTTON_STATE_PRESSED;
}

static void
emulate_button_release (MetaKeyboardA11y *keyboard_a11y)
{
  int btn = keyboard_a11y->mousekeys_btn;

  if (keyboard_a11y->mousekeys_btn_states[get_button_index (btn)] == CLUTTER_BUTTON_STATE_RELEASED)
    return;

  clutter_virtual_input_device_notify_button (keyboard_a11y->mousekeys_pointer,
                                              g_get_monotonic_time (), btn,
                                              CLUTTER_BUTTON_STATE_RELEASED);
  keyboard_a11y->mousekeys_btn_states[get_button_index (btn)] = CLUTTER_BUTTON_STATE_RELEASED;
}

static void
emulate_button_click (MetaKeyboardA11y *keyboard_a11y)
{
  emulate_button_press (keyboard_a11y);
  emulate_button_release (keyboard_a11y);
}

#define MOUSEKEYS_CURVE (1.0 + (((double) 50.0) * 0.001))

static void
update_mousekeys_params (MetaKeyboardA11y    *keyboard_a11y,
                         MetaKbdA11ySettings *settings)
{
  /* Prevent us from broken settings values */
  keyboard_a11y->mousekeys_max_speed =
    MAX (1, settings->mousekeys_max_speed);
  keyboard_a11y->mousekeys_accel_time =
    MAX (1, settings->mousekeys_accel_time);
  keyboard_a11y->mousekeys_init_delay =
    MAX (0, settings->mousekeys_init_delay);

  keyboard_a11y->mousekeys_curve_factor =
    (((double) keyboard_a11y->mousekeys_max_speed) /
      pow ((double) keyboard_a11y->mousekeys_accel_time, MOUSEKEYS_CURVE));
}

static double
mousekeys_get_speed_factor (MetaKeyboardA11y *keyboard_a11y,
                            uint64_t          time_us)
{
  uint32_t time;
  int64_t delta_t;
  int64_t init_time;
  double speed;

  time = us2ms (time_us);

  if (keyboard_a11y->mousekeys_first_motion_time == 0)
    {
      /* Start acceleration _after_ the first move, so take
       * mousekeys_init_delay into account for t0
       */
      keyboard_a11y->mousekeys_first_motion_time =
        time + keyboard_a11y->mousekeys_init_delay;
      keyboard_a11y->mousekeys_last_motion_time =
        keyboard_a11y->mousekeys_first_motion_time;
      return 1.0;
    }

  init_time = time - keyboard_a11y->mousekeys_first_motion_time;
  delta_t = time - keyboard_a11y->mousekeys_last_motion_time;

  if (delta_t < 0)
    return 0.0;

  if (init_time < keyboard_a11y->mousekeys_accel_time)
    speed = (double) (keyboard_a11y->mousekeys_curve_factor *
                      pow ((double) init_time, MOUSEKEYS_CURVE) * delta_t / 1000.0);
  else
    speed = (double) (keyboard_a11y->mousekeys_max_speed * delta_t / 1000.0);

  keyboard_a11y->mousekeys_last_motion_time = time;

  return speed;
}

#undef MOUSEKEYS_CURVE

static void
emulate_pointer_motion (MetaKeyboardA11y *keyboard_a11y,
                        int               dx,
                        int               dy)
{
  double dx_motion;
  double dy_motion;
  double speed;
  int64_t time_us;

  time_us = g_get_monotonic_time ();
  speed = mousekeys_get_speed_factor (keyboard_a11y, time_us);

  if (dx < 0)
    dx_motion = floor (((double) dx) * speed);
  else
    dx_motion = ceil (((double) dx) * speed);

  if (dy < 0)
    dy_motion = floor (((double) dy) * speed);
  else
    dy_motion = ceil (((double) dy) * speed);

  clutter_virtual_input_device_notify_relative_motion (keyboard_a11y->mousekeys_pointer,
                                                       time_us, dx_motion, dy_motion);
}
static gboolean
is_numlock_active (MetaKeyboardA11y *keyboard_a11y)
{
  MetaSeatImpl *seat_impl = keyboard_a11y->seat_impl;
  struct xkb_state *xkb_state;

  xkb_state = meta_seat_impl_get_xkb_state_in_impl (seat_impl);

  return xkb_state_mod_name_is_active (xkb_state,
                                       "Mod2",
                                       XKB_STATE_MODS_LOCKED);
}

static void
enable_mousekeys (MetaKeyboardA11y *keyboard_a11y)
{
  MetaSeatImpl *seat_impl = keyboard_a11y->seat_impl;

  keyboard_a11y->mousekeys_btn = CLUTTER_BUTTON_PRIMARY;
  keyboard_a11y->move_mousekeys_timer = 0;
  keyboard_a11y->mousekeys_first_motion_time = 0;
  keyboard_a11y->mousekeys_last_motion_time = 0;
  keyboard_a11y->last_mousekeys_key = 0;

  if (keyboard_a11y->mousekeys_pointer)
    return;

  keyboard_a11y->mousekeys_pointer =
    clutter_seat_create_virtual_device (CLUTTER_SEAT (seat_impl->seat_native),
                                        CLUTTER_POINTER_DEVICE);
}

static void
disable_mousekeys (MetaKeyboardA11y *keyboard_a11y)
{
  stop_mousekeys_move (keyboard_a11y);

  /* Make sure we don't leave button pressed behind... */
  if (keyboard_a11y->mousekeys_btn_states[get_button_index (CLUTTER_BUTTON_PRIMARY)])
    {
      keyboard_a11y->mousekeys_btn = CLUTTER_BUTTON_PRIMARY;
      emulate_button_release (keyboard_a11y);
    }

  if (keyboard_a11y->mousekeys_btn_states[get_button_index (CLUTTER_BUTTON_MIDDLE)])
    {
      keyboard_a11y->mousekeys_btn = CLUTTER_BUTTON_MIDDLE;
      emulate_button_release (keyboard_a11y);
    }

  if (keyboard_a11y->mousekeys_btn_states[get_button_index (CLUTTER_BUTTON_SECONDARY)])
    {
      keyboard_a11y->mousekeys_btn = CLUTTER_BUTTON_SECONDARY;
      emulate_button_release (keyboard_a11y);
    }

  g_clear_object (&keyboard_a11y->mousekeys_pointer);
}

static gboolean
trigger_mousekeys_move (gpointer user_data)
{
  MetaKeyboardA11y *keyboard_a11y = user_data;
  MetaSeatImpl *seat_impl = keyboard_a11y->seat_impl;
  int dx = 0;
  int dy = 0;

  if (keyboard_a11y->mousekeys_first_motion_time == 0)
    {
      /* This is the first move, Secdule at mk_init_delay */
      keyboard_a11y->move_mousekeys_timer =
        timeout_source_new (seat_impl,
                            keyboard_a11y->mousekeys_init_delay,
                            trigger_mousekeys_move,
                            keyboard_a11y);

    }
  else
    {
      /* More moves, reschedule at mk_interval */
      keyboard_a11y->move_mousekeys_timer =
        timeout_source_new (seat_impl,
                            100, /* msec between mousekey events */
                            trigger_mousekeys_move,
                            keyboard_a11y);
    }

  /* Pointer motion */
  switch (keyboard_a11y->last_mousekeys_key)
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

  switch (keyboard_a11y->last_mousekeys_key)
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
    emulate_pointer_motion (keyboard_a11y, dx, dy);

  /* We reschedule each time */
  return G_SOURCE_REMOVE;
}

static void
stop_mousekeys_move (MetaKeyboardA11y *keyboard_a11y)
{
  keyboard_a11y->mousekeys_first_motion_time = 0;
  keyboard_a11y->mousekeys_last_motion_time = 0;

  g_clear_pointer (&keyboard_a11y->move_mousekeys_timer, g_source_destroy);
}

static void
start_mousekeys_move (ClutterEvent     *event,
                      MetaKeyboardA11y *keyboard_a11y)
{
  keyboard_a11y->last_mousekeys_key =
    clutter_event_get_key_symbol (event);

  if (keyboard_a11y->move_mousekeys_timer != 0)
    return;

  trigger_mousekeys_move (keyboard_a11y);
}

static gboolean
handle_mousekeys_press (ClutterEvent     *event,
                        MetaKeyboardA11y *keyboard_a11y)
{
  if (!(clutter_event_get_flags (event) & CLUTTER_EVENT_FLAG_SYNTHETIC))
    stop_mousekeys_move (keyboard_a11y);

  /* Do not handle mousekeys if NumLock is ON */
  if (is_numlock_active (keyboard_a11y))
    return FALSE;

  /* Button selection */
  switch (clutter_event_get_key_symbol (event))
    {
    case XKB_KEY_KP_Divide:
      keyboard_a11y->mousekeys_btn = CLUTTER_BUTTON_PRIMARY;
      return TRUE;
    case XKB_KEY_KP_Multiply:
      keyboard_a11y->mousekeys_btn = CLUTTER_BUTTON_MIDDLE;
      return TRUE;
    case XKB_KEY_KP_Subtract:
      keyboard_a11y->mousekeys_btn = CLUTTER_BUTTON_SECONDARY;
      return TRUE;
    default:
      break;
    }

  /* Button events */
  switch (clutter_event_get_key_symbol (event))
    {
    case XKB_KEY_KP_Begin:
    case XKB_KEY_KP_5:
      emulate_button_click (keyboard_a11y);
      return TRUE;
    case XKB_KEY_KP_Insert:
    case XKB_KEY_KP_0:
      emulate_button_press (keyboard_a11y);
      return TRUE;
    case XKB_KEY_KP_Decimal:
    case XKB_KEY_KP_Delete:
      emulate_button_release (keyboard_a11y);
      return TRUE;
    case XKB_KEY_KP_Add:
      emulate_button_click (keyboard_a11y);
      emulate_button_click (keyboard_a11y);
      return TRUE;
    default:
      break;
    }

  /* Pointer motion */
  switch (clutter_event_get_key_symbol (event))
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
      start_mousekeys_move (event, keyboard_a11y);
      return TRUE;
    default:
      break;
    }

  return FALSE;
}

static gboolean
handle_mousekeys_release (ClutterEvent     *event,
                          MetaKeyboardA11y *keyboard_a11y)
{
  /* Do not handle mousekeys if NumLock is ON */
  if (is_numlock_active (keyboard_a11y))
    return FALSE;

  switch (clutter_event_get_key_symbol (event))
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
      stop_mousekeys_move (keyboard_a11y);
      return TRUE;
    default:
       break;
    }

  return FALSE;
}

static void
meta_keyboard_a11y_finalize (GObject *object)
{
  MetaKeyboardA11y *keyboard_a11y = META_KEYBOARD_A11Y (object);

  clear_slow_keys (keyboard_a11y);
  stop_bounce_keys (keyboard_a11y);
  stop_toggle_slowkeys (keyboard_a11y);
  stop_mousekeys_move (keyboard_a11y);
  g_clear_object (&keyboard_a11y->mousekeys_pointer);

  G_OBJECT_CLASS (meta_keyboard_a11y_parent_class)->finalize (object);
}

static void
meta_keyboard_a11y_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  MetaKeyboardA11y *keyboard_a11y = META_KEYBOARD_A11Y (object);

  switch (prop_id)
    {
    case PROP_SEAT_IMPL:
      keyboard_a11y->seat_impl = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_keyboard_a11y_class_init (MetaKeyboardA11yClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_keyboard_a11y_finalize;
  object_class->set_property = meta_keyboard_a11y_set_property;

  props[PROP_SEAT_IMPL] =
    g_param_spec_object ("seat-impl", NULL, NULL,
                         META_TYPE_SEAT_IMPL,
                         G_PARAM_WRITABLE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, props);
}

static void
meta_keyboard_a11y_init (MetaKeyboardA11y *keyboard_aay)
{
}

MetaKeyboardA11y *
meta_keyboard_a11y_new (MetaSeatImpl *seat_impl)
{
  return g_object_new (META_TYPE_KEYBOARD_A11Y,
                       "seat-impl", seat_impl,
                       NULL);
}

void
meta_keyboard_a11y_apply_settings_in_impl (MetaKeyboardA11y    *keyboard_a11y,
                                           MetaKbdA11ySettings *settings)
{
  MetaKeyboardA11yFlags changed_flags =
    (keyboard_a11y->a11y_flags ^ settings->controls);

  if (changed_flags & (META_A11Y_KEYBOARD_ENABLED | META_A11Y_SLOW_KEYS_ENABLED))
    clear_slow_keys (keyboard_a11y);

  if (changed_flags & (META_A11Y_KEYBOARD_ENABLED | META_A11Y_BOUNCE_KEYS_ENABLED))
    keyboard_a11y->debounce_key = 0;

  if (changed_flags & (META_A11Y_KEYBOARD_ENABLED | META_A11Y_STICKY_KEYS_ENABLED))
    {
      keyboard_a11y->stickykeys_depressed_mask = 0;
      update_internal_xkb_state (keyboard_a11y, 0, 0);
    }

  if (changed_flags & META_A11Y_KEYBOARD_ENABLED)
    {
      keyboard_a11y->toggle_slowkeys_timer = 0;
      keyboard_a11y->shift_count = 0;
      keyboard_a11y->last_shift_time = 0;
    }

  if (changed_flags & (META_A11Y_KEYBOARD_ENABLED | META_A11Y_MOUSE_KEYS_ENABLED))
    {
      if (settings->controls &
          (META_A11Y_KEYBOARD_ENABLED | META_A11Y_MOUSE_KEYS_ENABLED))
        enable_mousekeys (keyboard_a11y);
      else
        disable_mousekeys (keyboard_a11y);
    }
  update_mousekeys_params (keyboard_a11y, settings);

  /* Keep our own copy of keyboard a11y features flags to see what changes */
  keyboard_a11y->a11y_flags = settings->controls;
}

void
meta_keyboard_a11y_maybe_notify_toggle_keys_in_impl (MetaKeyboardA11y *keyboard_a11y)
{
  if (keyboard_a11y->a11y_flags & META_A11Y_TOGGLE_KEYS_ENABLED)
    meta_keyboard_a11y_bell_notify (keyboard_a11y);
}

gboolean
meta_keyboard_a11y_process_event_in_impl (MetaKeyboardA11y  *keyboard_a11y,
                                          ClutterEvent      *event,
                                          ClutterEvent     **out_event)
{
  ClutterEventType event_type;

  event_type = clutter_event_type (event);

  if (keyboard_a11y->a11y_flags & META_A11Y_KEYBOARD_ENABLED)
    {
      if (event_type == CLUTTER_KEY_PRESS)
        handle_enablekeys_press (event, keyboard_a11y);
      else
        handle_enablekeys_release (event, keyboard_a11y);
    }

  if (keyboard_a11y->a11y_flags & META_A11Y_MOUSE_KEYS_ENABLED)
    {
      if (event_type == CLUTTER_KEY_PRESS &&
          handle_mousekeys_press (event, keyboard_a11y))
        return TRUE; /* swallow event */
      if (event_type == CLUTTER_KEY_RELEASE &&
          handle_mousekeys_release (event, keyboard_a11y))
        return TRUE; /* swallow event */
    }

  if ((keyboard_a11y->a11y_flags & META_A11Y_BOUNCE_KEYS_ENABLED) &&
      (get_debounce_delay (keyboard_a11y) != 0))
    {
      if ((event_type == CLUTTER_KEY_PRESS) && debounce_key (event, keyboard_a11y))
        {
          notify_bounce_keys_reject (keyboard_a11y);

          return TRUE;
        }
      else if (event_type == CLUTTER_KEY_RELEASE)
        {
          start_bounce_keys (event, keyboard_a11y);
        }
    }

  if ((keyboard_a11y->a11y_flags & META_A11Y_SLOW_KEYS_ENABLED) &&
      (get_slow_keys_delay (keyboard_a11y) != 0))
    {
      if (event_type == CLUTTER_KEY_PRESS)
        return start_slow_keys (event, keyboard_a11y);
      else if (event_type == CLUTTER_KEY_RELEASE)
        return stop_slow_keys (event, keyboard_a11y);
    }

  if (keyboard_a11y->a11y_flags & META_A11Y_STICKY_KEYS_ENABLED)
    {
      if (event_type == CLUTTER_KEY_PRESS)
        {
          *out_event = handle_stickykeys_press (event, keyboard_a11y);
          return *out_event != NULL;
        }
      else if (event_type == CLUTTER_KEY_RELEASE)
        {
          *out_event = handle_stickykeys_release (event, keyboard_a11y);
          return *out_event != NULL;
        }
    }

  return FALSE;
}
