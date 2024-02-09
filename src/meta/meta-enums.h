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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

typedef enum _MetaCompositorType
{
  META_COMPOSITOR_TYPE_WAYLAND,
  META_COMPOSITOR_TYPE_X11,
} MetaCompositorType;

/**
 * MetaGrabOp:
 * @META_GRAB_OP_NONE: None
 * @META_GRAB_OP_MOVING: Moving with pointer
 * @META_GRAB_OP_RESIZING_SE: Resizing SE with pointer
 * @META_GRAB_OP_RESIZING_S: Resizing S with pointer
 * @META_GRAB_OP_RESIZING_SW: Resizing SW with pointer
 * @META_GRAB_OP_RESIZING_N: Resizing N with pointer
 * @META_GRAB_OP_RESIZING_NE: Resizing NE with pointer
 * @META_GRAB_OP_RESIZING_NW: Resizing NW with pointer
 * @META_GRAB_OP_RESIZING_W: Resizing W with pointer
 * @META_GRAB_OP_RESIZING_E: Resizing E with pointer
 * @META_GRAB_OP_KEYBOARD_MOVING: Moving with keyboard
 * @META_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN: Resizing with keyboard
 * @META_GRAB_OP_KEYBOARD_RESIZING_S: Resizing S with keyboard
 * @META_GRAB_OP_KEYBOARD_RESIZING_N: Resizing N with keyboard
 * @META_GRAB_OP_KEYBOARD_RESIZING_W: Resizing W with keyboard
 * @META_GRAB_OP_KEYBOARD_RESIZING_E: Resizing E with keyboard
 * @META_GRAB_OP_KEYBOARD_RESIZING_SE: Resizing SE with keyboard
 * @META_GRAB_OP_KEYBOARD_RESIZING_NE: Resizing NE with keyboard
 * @META_GRAB_OP_KEYBOARD_RESIZING_SW: Resizing SW with keyboard
 * @META_GRAB_OP_KEYBOARD_RESIZING_NW: Resizing NS with keyboard
 */

/* The lower 16 bits of the grab operation is its type.
 *
 * Window grab operations have the following layout:
 *
 * 0000  0000  | 0000 0011
 * NSEW  flags | type
 *
 * Flags contains whether the operation is a keyboard operation,
 * and whether the keyboard operation is "unknown".
 *
 * The rest of the flags tell you which direction the resize is
 * going in.
 *
 * If the directions field is 0000, then the operation is a move,
 * not a resize.
 */
enum
{
  META_GRAB_OP_WINDOW_FLAG_KEYBOARD = 0x0100,
  META_GRAB_OP_WINDOW_FLAG_UNKNOWN  = 0x0200,
  META_GRAB_OP_WINDOW_FLAG_UNCONSTRAINED = 0x0400,
  META_GRAB_OP_WINDOW_DIR_WEST      = 0x1000,
  META_GRAB_OP_WINDOW_DIR_EAST      = 0x2000,
  META_GRAB_OP_WINDOW_DIR_SOUTH     = 0x4000,
  META_GRAB_OP_WINDOW_DIR_NORTH     = 0x8000,
  META_GRAB_OP_WINDOW_DIR_MASK      = 0xF000,

  /* WGO = "window grab op". shorthand for below */
  _WGO_K = META_GRAB_OP_WINDOW_FLAG_KEYBOARD,
  _WGO_U = META_GRAB_OP_WINDOW_FLAG_UNKNOWN,
  _WGO_C = META_GRAB_OP_WINDOW_FLAG_UNCONSTRAINED,
  _WGO_W = META_GRAB_OP_WINDOW_DIR_WEST,
  _WGO_E = META_GRAB_OP_WINDOW_DIR_EAST,
  _WGO_S = META_GRAB_OP_WINDOW_DIR_SOUTH,
  _WGO_N = META_GRAB_OP_WINDOW_DIR_NORTH,
};

typedef enum
{
  META_GRAB_OP_NONE,

  /* Window grab ops. */
  META_GRAB_OP_WINDOW_BASE,

  META_GRAB_OP_MOVING                     = META_GRAB_OP_WINDOW_BASE,
  META_GRAB_OP_MOVING_UNCONSTRAINED       = META_GRAB_OP_WINDOW_BASE | _WGO_C,
  META_GRAB_OP_RESIZING_NW                = META_GRAB_OP_WINDOW_BASE | _WGO_N | _WGO_W,
  META_GRAB_OP_RESIZING_N                 = META_GRAB_OP_WINDOW_BASE | _WGO_N,
  META_GRAB_OP_RESIZING_NE                = META_GRAB_OP_WINDOW_BASE | _WGO_N | _WGO_E,
  META_GRAB_OP_RESIZING_E                 = META_GRAB_OP_WINDOW_BASE |          _WGO_E,
  META_GRAB_OP_RESIZING_SW                = META_GRAB_OP_WINDOW_BASE | _WGO_S | _WGO_W,
  META_GRAB_OP_RESIZING_S                 = META_GRAB_OP_WINDOW_BASE | _WGO_S,
  META_GRAB_OP_RESIZING_SE                = META_GRAB_OP_WINDOW_BASE | _WGO_S | _WGO_E,
  META_GRAB_OP_RESIZING_W                 = META_GRAB_OP_WINDOW_BASE |          _WGO_W,
  META_GRAB_OP_KEYBOARD_MOVING            = META_GRAB_OP_WINDOW_BASE |                   _WGO_K,
  META_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN  = META_GRAB_OP_WINDOW_BASE |                   _WGO_K | _WGO_U,
  META_GRAB_OP_KEYBOARD_RESIZING_NW       = META_GRAB_OP_WINDOW_BASE | _WGO_N | _WGO_W | _WGO_K,
  META_GRAB_OP_KEYBOARD_RESIZING_N        = META_GRAB_OP_WINDOW_BASE | _WGO_N |          _WGO_K,
  META_GRAB_OP_KEYBOARD_RESIZING_NE       = META_GRAB_OP_WINDOW_BASE | _WGO_N | _WGO_E | _WGO_K,
  META_GRAB_OP_KEYBOARD_RESIZING_E        = META_GRAB_OP_WINDOW_BASE |          _WGO_E | _WGO_K,
  META_GRAB_OP_KEYBOARD_RESIZING_SW       = META_GRAB_OP_WINDOW_BASE | _WGO_S | _WGO_W | _WGO_K,
  META_GRAB_OP_KEYBOARD_RESIZING_S        = META_GRAB_OP_WINDOW_BASE | _WGO_S |          _WGO_K,
  META_GRAB_OP_KEYBOARD_RESIZING_SE       = META_GRAB_OP_WINDOW_BASE | _WGO_S | _WGO_E | _WGO_K,
  META_GRAB_OP_KEYBOARD_RESIZING_W        = META_GRAB_OP_WINDOW_BASE |          _WGO_W | _WGO_K,
} MetaGrabOp;

/**
 * MetaCursor:
 * @META_CURSOR_DEFAULT: Default cursor
 * @META_CURSOR_NORTH_RESIZE: Resize northern edge cursor
 * @META_CURSOR_SOUTH_RESIZE: Resize southern edge cursor
 * @META_CURSOR_WEST_RESIZE: Resize western edge cursor
 * @META_CURSOR_EAST_RESIZE: Resize eastern edge cursor
 * @META_CURSOR_SE_RESIZE: Resize south-eastern corner cursor
 * @META_CURSOR_SW_RESIZE: Resize south-western corner cursor
 * @META_CURSOR_NE_RESIZE: Resize north-eastern corner cursor
 * @META_CURSOR_NW_RESIZE: Resize north-western corner cursor
 * @META_CURSOR_MOVE_OR_RESIZE_WINDOW: Move or resize cursor
 * @META_CURSOR_BUSY: Busy cursor
 * @META_CURSOR_DND_IN_DRAG: DND in drag cursor
 * @META_CURSOR_DND_MOVE: DND move cursor
 * @META_CURSOR_DND_COPY: DND copy cursor
 * @META_CURSOR_DND_UNSUPPORTED_TARGET: DND unsupported target
 * @META_CURSOR_POINTING_HAND: pointing hand
 * @META_CURSOR_CROSSHAIR: crosshair (action forbidden)
 * @META_CURSOR_IBEAM: I-beam (text input)
 * @META_CURSOR_BLANK: Invisible cursor
 */
typedef enum
{
  META_CURSOR_NONE = 0,
  META_CURSOR_DEFAULT,
  META_CURSOR_NORTH_RESIZE,
  META_CURSOR_SOUTH_RESIZE,
  META_CURSOR_WEST_RESIZE,
  META_CURSOR_EAST_RESIZE,
  META_CURSOR_SE_RESIZE,
  META_CURSOR_SW_RESIZE,
  META_CURSOR_NE_RESIZE,
  META_CURSOR_NW_RESIZE,
  META_CURSOR_MOVE_OR_RESIZE_WINDOW,
  META_CURSOR_BUSY,
  META_CURSOR_DND_IN_DRAG,
  META_CURSOR_DND_MOVE,
  META_CURSOR_DND_COPY,
  META_CURSOR_DND_UNSUPPORTED_TARGET,
  META_CURSOR_POINTING_HAND,
  META_CURSOR_CROSSHAIR,
  META_CURSOR_IBEAM,
  META_CURSOR_BLANK,
  META_CURSOR_LAST
} MetaCursor;

/**
 * MetaFrameType:
 * @META_FRAME_TYPE_NORMAL: Normal frame
 * @META_FRAME_TYPE_DIALOG: Dialog frame
 * @META_FRAME_TYPE_MODAL_DIALOG: Modal dialog frame
 * @META_FRAME_TYPE_UTILITY: Utility frame
 * @META_FRAME_TYPE_MENU: Menu frame
 * @META_FRAME_TYPE_BORDER: Border frame
 * @META_FRAME_TYPE_ATTACHED: Attached frame
 * @META_FRAME_TYPE_LAST: Marks the end of the #MetaFrameType enumeration
 */
typedef enum
{
  META_FRAME_TYPE_NORMAL,
  META_FRAME_TYPE_DIALOG,
  META_FRAME_TYPE_MODAL_DIALOG,
  META_FRAME_TYPE_UTILITY,
  META_FRAME_TYPE_MENU,
  META_FRAME_TYPE_BORDER,
  META_FRAME_TYPE_ATTACHED,
  META_FRAME_TYPE_LAST
} MetaFrameType;

/**
 * MetaDirection:
 * @META_DIRECTION_LEFT: Left
 * @META_DIRECTION_RIGHT: Right
 * @META_DIRECTION_TOP: Top
 * @META_DIRECTION_BOTTOM: Bottom
 * @META_DIRECTION_UP: Up
 * @META_DIRECTION_DOWN: Down
 * @META_DIRECTION_HORIZONTAL: Horizontal
 * @META_DIRECTION_VERTICAL: Vertical
 */

/* Relative directions or sides seem to come up all over the place... */
/* FIXME: Replace
 *   display.[ch]:MetaDisplayDirection,
 *   workspace.[ch]:MetaMotionDirection,
 * with the use of MetaDirection.
 */
typedef enum
{
  META_DIRECTION_LEFT       = 1 << 0,
  META_DIRECTION_RIGHT      = 1 << 1,
  META_DIRECTION_TOP        = 1 << 2,
  META_DIRECTION_BOTTOM     = 1 << 3,

  /* Some aliases for making code more readable for various circumstances. */
  META_DIRECTION_UP         = META_DIRECTION_TOP,
  META_DIRECTION_DOWN       = META_DIRECTION_BOTTOM,

  /* A few more definitions using aliases */
  META_DIRECTION_HORIZONTAL = META_DIRECTION_LEFT | META_DIRECTION_RIGHT,
  META_DIRECTION_VERTICAL   = META_DIRECTION_UP   | META_DIRECTION_DOWN,
} MetaDirection;

/**
 * MetaMotionDirection:
 * @META_MOTION_UP: Upwards motion
 * @META_MOTION_DOWN: Downwards motion
 * @META_MOTION_LEFT: Motion to the left
 * @META_MOTION_RIGHT: Motion to the right
 * @META_MOTION_UP_LEFT: Motion up and to the left
 * @META_MOTION_UP_RIGHT: Motion up and to the right
 * @META_MOTION_DOWN_LEFT: Motion down and to the left
 * @META_MOTION_DOWN_RIGHT: Motion down and to the right
 */

/* Negative to avoid conflicting with real workspace
 * numbers
 */
typedef enum
{
  META_MOTION_UP = -1,
  META_MOTION_DOWN = -2,
  META_MOTION_LEFT = -3,
  META_MOTION_RIGHT = -4,
  /* These are only used for effects */
  META_MOTION_UP_LEFT = -5,
  META_MOTION_UP_RIGHT = -6,
  META_MOTION_DOWN_LEFT = -7,
  META_MOTION_DOWN_RIGHT = -8
} MetaMotionDirection;

/**
 * MetaSide:
 * @META_SIDE_LEFT: Left side
 * @META_SIDE_RIGHT: Right side
 * @META_SIDE_TOP: Top side
 * @META_SIDE_BOTTOM: Bottom side
 */

/* Sometimes we want to talk about sides instead of directions; note
 * that the values must be as follows or meta_window_update_struts()
 * won't work. Using these values also is a safety blanket since
 * MetaDirection used to be used as a side.
 */
typedef enum
{
  META_SIDE_LEFT            = META_DIRECTION_LEFT,
  META_SIDE_RIGHT           = META_DIRECTION_RIGHT,
  META_SIDE_TOP             = META_DIRECTION_TOP,
  META_SIDE_BOTTOM          = META_DIRECTION_BOTTOM
} MetaSide;

/**
 * MetaButtonFunction:
 * @META_BUTTON_FUNCTION_MENU: Menu
 * @META_BUTTON_FUNCTION_MINIMIZE: Minimize
 * @META_BUTTON_FUNCTION_MAXIMIZE: Maximize
 * @META_BUTTON_FUNCTION_CLOSE: Close
 * @META_BUTTON_FUNCTION_LAST: Marks the end of the #MetaButtonFunction enumeration
 *
 * Function a window button can have.
 *
 * Note, you can't add stuff here without extending the theme format
 * to draw a new function and breaking all existing themes.
 */
typedef enum
{
  META_BUTTON_FUNCTION_MENU,
  META_BUTTON_FUNCTION_MINIMIZE,
  META_BUTTON_FUNCTION_MAXIMIZE,
  META_BUTTON_FUNCTION_CLOSE,
  META_BUTTON_FUNCTION_LAST
} MetaButtonFunction;

/**
 * MetaWindowMenuType:
 * @META_WINDOW_MENU_WM: the window manager menu
 * @META_WINDOW_MENU_APP: the (fallback) app menu
 *
 * Menu the compositor should display for a given window
 */
typedef enum
{
  META_WINDOW_MENU_WM,
  META_WINDOW_MENU_APP
} MetaWindowMenuType;

/**
 * MetaStackLayer:
 * @META_LAYER_DESKTOP: Desktop layer
 * @META_LAYER_BOTTOM: Bottom layer
 * @META_LAYER_NORMAL: Normal layer
 * @META_LAYER_TOP: Top layer
 * @META_LAYER_DOCK: Dock layer
 * @META_LAYER_OVERRIDE_REDIRECT: Override-redirect layer
 * @META_LAYER_LAST: Marks the end of the #MetaStackLayer enumeration
 *
 * Layers a window can be in.
 * These MUST be in the order of stacking.
 */
typedef enum
{
  META_LAYER_DESKTOP	       = 0,
  META_LAYER_BOTTOM	       = 1,
  META_LAYER_NORMAL	       = 2,
  META_LAYER_TOP	       = 4, /* Same as DOCK; see EWMH and bug 330717 */
  META_LAYER_DOCK	       = 4,
  META_LAYER_OVERRIDE_REDIRECT = 7,
  META_LAYER_LAST	       = 8
} MetaStackLayer;

/* MetaGravity: (skip)
 *
 * Identical to the corresponding gravity value macros from libX11.
 */
typedef enum _MetaGravity
{
  META_GRAVITY_NONE = 0,
  META_GRAVITY_NORTH_WEST = 1,
  META_GRAVITY_NORTH = 2,
  META_GRAVITY_NORTH_EAST = 3,
  META_GRAVITY_WEST = 4,
  META_GRAVITY_CENTER = 5,
  META_GRAVITY_EAST = 6,
  META_GRAVITY_SOUTH_WEST = 7,
  META_GRAVITY_SOUTH = 8,
  META_GRAVITY_SOUTH_EAST = 9,
  META_GRAVITY_STATIC = 10,
} MetaGravity;

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
