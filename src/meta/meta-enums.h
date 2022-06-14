/*
 * Copyright (C) 2016-2021 Red Hat Inc.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#ifndef META_ENUMS_H
#define META_ENUMS_H

typedef enum _MetaCompositorType
{
  META_COMPOSITOR_TYPE_WAYLAND,
  META_COMPOSITOR_TYPE_X11,
} MetaCompositorType;

/**
 * MetaKeyboardA11yFlags:
 * @META_A11Y_KEYBOARD_ENABLED:
 * @META_A11Y_TIMEOUT_ENABLED:
 * @META_A11Y_MOUSE_KEYS_ENABLED:
 * @META_A11Y_SLOW_KEYS_ENABLED:
 * @META_A11Y_SLOW_KEYS_BEEP_PRESS:
 * @META_A11Y_SLOW_KEYS_BEEP_ACCEPT:
 * @META_A11Y_SLOW_KEYS_BEEP_REJECT:
 * @META_A11Y_BOUNCE_KEYS_ENABLED:
 * @META_A11Y_BOUNCE_KEYS_BEEP_REJECT:
 * @META_A11Y_TOGGLE_KEYS_ENABLED:
 * @META_A11Y_STICKY_KEYS_ENABLED:
 * @META_A11Y_STICKY_KEYS_TWO_KEY_OFF:
 * @META_A11Y_STICKY_KEYS_BEEP:
 * @META_A11Y_FEATURE_STATE_CHANGE_BEEP:
 *
 * Keyboard accessibility features.
 *
 */
typedef enum
{
  META_A11Y_KEYBOARD_ENABLED = 1 << 0,
  META_A11Y_TIMEOUT_ENABLED = 1 << 1,
  META_A11Y_MOUSE_KEYS_ENABLED = 1 << 2,
  META_A11Y_SLOW_KEYS_ENABLED = 1 << 3,
  META_A11Y_SLOW_KEYS_BEEP_PRESS = 1 << 4,
  META_A11Y_SLOW_KEYS_BEEP_ACCEPT = 1 << 5,
  META_A11Y_SLOW_KEYS_BEEP_REJECT = 1 << 6,
  META_A11Y_BOUNCE_KEYS_ENABLED = 1 << 7,
  META_A11Y_BOUNCE_KEYS_BEEP_REJECT = 1 << 8,
  META_A11Y_TOGGLE_KEYS_ENABLED = 1 << 9,
  META_A11Y_STICKY_KEYS_ENABLED = 1 << 10,
  META_A11Y_STICKY_KEYS_TWO_KEY_OFF = 1 << 11,
  META_A11Y_STICKY_KEYS_BEEP = 1 << 12,
  META_A11Y_FEATURE_STATE_CHANGE_BEEP = 1 << 13,
} MetaKeyboardA11yFlags;

#endif /* META_ENUMS_H */
