/*
 * Copyright (C) 2010  Intel Corporation.
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

 * Authors:
 *  Kristian Høgsberg
 *  Damien Lespiau <damien.lespiau@intel.com>
 */

#include "config.h"

#include "backends/native/meta-xkb-utils.h"
#include "clutter/clutter-keysyms.h"
#include "clutter/clutter-mutter.h"

/*
 * _clutter_event_new_from_evdev: Create a new Clutter ClutterKeyEvent
 * @device: a ClutterInputDevice
 * @xkb: XKB rules to translate the event
 * @_time: timestamp of the event
 * @key: a key code coming from a Linux input device
 * @state: TRUE if a press event, FALSE if a release event
 * @modifer_state: in/out
 *
 * Translate @key to a #ClutterKeyEvent using rules from xbbcommon.
 *
 * Return value: the new #ClutterEvent
 */
ClutterEvent *
meta_key_event_new_from_evdev (ClutterInputDevice *device,
                               ClutterInputDevice *core_device,
                               ClutterEventFlags   flags,
                               struct xkb_state   *xkb_state,
                               uint32_t            button_state,
                               uint64_t            time_us,
                               xkb_keycode_t       key,
                               uint32_t            state)
{
  ClutterEvent *event;
  xkb_keysym_t sym;
  char buffer[8];
  gunichar unicode_value;
  ClutterModifierType modifiers;
  int n;

  /* We use a fixed offset of 8 because evdev starts KEY_* numbering from
   * 0, whereas X11's minimum keycode, for really stupid reasons, is 8.
   * So the evdev XKB rules are based on the keycodes all being shifted
   * upwards by 8. */
  key = meta_xkb_evdev_to_keycode (key);

  sym = xkb_state_key_get_one_sym (xkb_state, key);

  modifiers = xkb_state_serialize_mods (xkb_state, XKB_STATE_MODS_EFFECTIVE) |
    button_state;

  n = xkb_keysym_to_utf8 (sym, buffer, sizeof (buffer));

  if (n == 0)
    {
      /* not printable */
      unicode_value = (gunichar) '\0';
    }
  else
    {
      unicode_value = g_utf8_get_char_validated (buffer, n);
      if (unicode_value == -1 || unicode_value == -2)
        unicode_value = (gunichar) '\0';
    }

  event = clutter_event_key_new (state ?
                                 CLUTTER_KEY_PRESS : CLUTTER_KEY_RELEASE,
                                 flags,
                                 time_us,
                                 device,
                                 modifiers,
                                 sym,
                                 key - 8,
                                 key,
                                 unicode_value);
  return event;
}

ClutterModifierType
meta_xkb_translate_modifiers (struct xkb_state    *state,
                              ClutterModifierType  button_state)
{
  ClutterModifierType modifiers;

  modifiers = xkb_state_serialize_mods (state, XKB_STATE_MODS_EFFECTIVE);
  modifiers |= button_state;

  return modifiers;
}

uint32_t
meta_xkb_keycode_to_evdev (uint32_t xkb_keycode)
{
  /* The keycodes from the evdev backend are almost evdev
   * keycodes: we use the evdev keycode file, but xkb rules have an
   *  offset by 8. See the comment in _clutter_key_event_new_from_evdev()
   */
  return xkb_keycode - 8;
}

uint32_t
meta_xkb_evdev_to_keycode (uint32_t evcode)
{
  /* The inverse of meta_xkb_keycode_to_evdev */
  return evcode + 8;
}
