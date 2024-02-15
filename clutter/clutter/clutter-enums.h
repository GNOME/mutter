/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2011  Intel Corporation
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
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * ClutterRotateAxis:
 * @CLUTTER_X_AXIS: Rotate around the X axis
 * @CLUTTER_Y_AXIS: Rotate around the Y axis
 * @CLUTTER_Z_AXIS: Rotate around the Z axis
 *
 * Axis of a rotation.
 */
typedef enum /*< prefix=CLUTTER >*/
{
  CLUTTER_X_AXIS,
  CLUTTER_Y_AXIS,
  CLUTTER_Z_AXIS
} ClutterRotateAxis;

/**
 * ClutterRequestMode:
 * @CLUTTER_REQUEST_HEIGHT_FOR_WIDTH: Height for width requests
 * @CLUTTER_REQUEST_WIDTH_FOR_HEIGHT: Width for height requests
 * @CLUTTER_REQUEST_CONTENT_SIZE: Use the preferred size of the
 *   #ClutterContent, if it has any (available since 1.22)
 *
 * Specifies the type of requests for a #ClutterActor.
 */
typedef enum /*< prefix=CLUTTER_REQUEST >*/
{
  CLUTTER_REQUEST_HEIGHT_FOR_WIDTH,
  CLUTTER_REQUEST_WIDTH_FOR_HEIGHT,
  CLUTTER_REQUEST_CONTENT_SIZE
} ClutterRequestMode;

/**
 * ClutterAnimationMode:
 * @CLUTTER_CUSTOM_MODE: custom progress function
 * @CLUTTER_LINEAR: linear tweening
 * @CLUTTER_EASE_IN_QUAD: quadratic tweening
 * @CLUTTER_EASE_OUT_QUAD: quadratic tweening, inverse of
 *    %CLUTTER_EASE_IN_QUAD
 * @CLUTTER_EASE_IN_OUT_QUAD: quadratic tweening, combininig
 *    %CLUTTER_EASE_IN_QUAD and %CLUTTER_EASE_OUT_QUAD
 * @CLUTTER_EASE_IN_CUBIC: cubic tweening
 * @CLUTTER_EASE_OUT_CUBIC: cubic tweening, inverse of
 *    %CLUTTER_EASE_IN_CUBIC
 * @CLUTTER_EASE_IN_OUT_CUBIC: cubic tweening, combining
 *    %CLUTTER_EASE_IN_CUBIC and %CLUTTER_EASE_OUT_CUBIC
 * @CLUTTER_EASE_IN_QUART: quartic tweening
 * @CLUTTER_EASE_OUT_QUART: quartic tweening, inverse of
 *    %CLUTTER_EASE_IN_QUART
 * @CLUTTER_EASE_IN_OUT_QUART: quartic tweening, combining
 *    %CLUTTER_EASE_IN_QUART and %CLUTTER_EASE_OUT_QUART
 * @CLUTTER_EASE_IN_QUINT: quintic tweening
 * @CLUTTER_EASE_OUT_QUINT: quintic tweening, inverse of
 *    %CLUTTER_EASE_IN_QUINT
 * @CLUTTER_EASE_IN_OUT_QUINT: fifth power tweening, combining
 *    %CLUTTER_EASE_IN_QUINT and %CLUTTER_EASE_OUT_QUINT
 * @CLUTTER_EASE_IN_SINE: sinusoidal tweening
 * @CLUTTER_EASE_OUT_SINE: sinusoidal tweening, inverse of
 *    %CLUTTER_EASE_IN_SINE
 * @CLUTTER_EASE_IN_OUT_SINE: sine wave tweening, combining
 *    %CLUTTER_EASE_IN_SINE and %CLUTTER_EASE_OUT_SINE
 * @CLUTTER_EASE_IN_EXPO: exponential tweening
 * @CLUTTER_EASE_OUT_EXPO: exponential tweening, inverse of
 *    %CLUTTER_EASE_IN_EXPO
 * @CLUTTER_EASE_IN_OUT_EXPO: exponential tweening, combining
 *    %CLUTTER_EASE_IN_EXPO and %CLUTTER_EASE_OUT_EXPO
 * @CLUTTER_EASE_IN_CIRC: circular tweening
 * @CLUTTER_EASE_OUT_CIRC: circular tweening, inverse of
 *    %CLUTTER_EASE_IN_CIRC
 * @CLUTTER_EASE_IN_OUT_CIRC: circular tweening, combining
 *    %CLUTTER_EASE_IN_CIRC and %CLUTTER_EASE_OUT_CIRC
 * @CLUTTER_EASE_IN_ELASTIC: elastic tweening, with offshoot on start
 * @CLUTTER_EASE_OUT_ELASTIC: elastic tweening, with offshoot on end
 * @CLUTTER_EASE_IN_OUT_ELASTIC: elastic tweening with offshoot on both ends
 * @CLUTTER_EASE_IN_BACK: overshooting cubic tweening, with
 *   backtracking on start
 * @CLUTTER_EASE_OUT_BACK: overshooting cubic tweening, with
 *   backtracking on end
 * @CLUTTER_EASE_IN_OUT_BACK: overshooting cubic tweening, with
 *   backtracking on both ends
 * @CLUTTER_EASE_IN_BOUNCE: exponentially decaying parabolic (bounce)
 *   tweening, with bounce on start
 * @CLUTTER_EASE_OUT_BOUNCE: exponentially decaying parabolic (bounce)
 *   tweening, with bounce on end
 * @CLUTTER_EASE_IN_OUT_BOUNCE: exponentially decaying parabolic (bounce)
 *   tweening, with bounce on both ends
 * @CLUTTER_STEPS: parametrized step function; see clutter_timeline_set_step_progress()
 *   for further details. (Since 1.12)
 * @CLUTTER_STEP_START: equivalent to %CLUTTER_STEPS with a number of steps
 *   equal to 1, and a step mode of %CLUTTER_STEP_MODE_START. (Since 1.12)
 * @CLUTTER_STEP_END: equivalent to %CLUTTER_STEPS with a number of steps
 *   equal to 1, and a step mode of %CLUTTER_STEP_MODE_END. (Since 1.12)
 * @CLUTTER_CUBIC_BEZIER: cubic bezier between (0, 0) and (1, 1) with two
 *   control points; see clutter_timeline_set_cubic_bezier_progress(). (Since 1.12)
 * @CLUTTER_EASE: equivalent to %CLUTTER_CUBIC_BEZIER with control points
 *   in (0.25, 0.1) and (0.25, 1.0). (Since 1.12)
 * @CLUTTER_EASE_IN: equivalent to %CLUTTER_CUBIC_BEZIER with control points
 *   in (0.42, 0) and (1.0, 1.0). (Since 1.12)
 * @CLUTTER_EASE_OUT: equivalent to %CLUTTER_CUBIC_BEZIER with control points
 *   in (0, 0) and (0.58, 1.0). (Since 1.12)
 * @CLUTTER_EASE_IN_OUT: equivalent to %CLUTTER_CUBIC_BEZIER with control points
 *   in (0.42, 0) and (0.58, 1.0). (Since 1.12)
 * @CLUTTER_ANIMATION_LAST: last animation mode, used as a guard for
 *   registered global alpha functions
 *
 * The animation modes used by [iface@Animatable]. 
 * 
 * This enumeration can be expanded in later versions of Clutter.
 *
 * <figure id="easing-modes">
 *   <title>Easing modes provided by Clutter</title>
 *   <graphic fileref="easing-modes.png" format="PNG"/>
 * </figure>
 *
 * Every global alpha function registered using clutter_alpha_register_func()
 * or clutter_alpha_register_closure() will have a logical id greater than
 * %CLUTTER_ANIMATION_LAST.
 */
typedef enum
{
  CLUTTER_CUSTOM_MODE = 0,

  /* linear */
  CLUTTER_LINEAR,

  /* quadratic */
  CLUTTER_EASE_IN_QUAD,
  CLUTTER_EASE_OUT_QUAD,
  CLUTTER_EASE_IN_OUT_QUAD,

  /* cubic */
  CLUTTER_EASE_IN_CUBIC,
  CLUTTER_EASE_OUT_CUBIC,
  CLUTTER_EASE_IN_OUT_CUBIC,

  /* quartic */
  CLUTTER_EASE_IN_QUART,
  CLUTTER_EASE_OUT_QUART,
  CLUTTER_EASE_IN_OUT_QUART,

  /* quintic */
  CLUTTER_EASE_IN_QUINT,
  CLUTTER_EASE_OUT_QUINT,
  CLUTTER_EASE_IN_OUT_QUINT,

  /* sinusoidal */
  CLUTTER_EASE_IN_SINE,
  CLUTTER_EASE_OUT_SINE,
  CLUTTER_EASE_IN_OUT_SINE,

  /* exponential */
  CLUTTER_EASE_IN_EXPO,
  CLUTTER_EASE_OUT_EXPO,
  CLUTTER_EASE_IN_OUT_EXPO,

  /* circular */
  CLUTTER_EASE_IN_CIRC,
  CLUTTER_EASE_OUT_CIRC,
  CLUTTER_EASE_IN_OUT_CIRC,

  /* elastic */
  CLUTTER_EASE_IN_ELASTIC,
  CLUTTER_EASE_OUT_ELASTIC,
  CLUTTER_EASE_IN_OUT_ELASTIC,

  /* overshooting cubic */
  CLUTTER_EASE_IN_BACK,
  CLUTTER_EASE_OUT_BACK,
  CLUTTER_EASE_IN_OUT_BACK,

  /* exponentially decaying parabolic */
  CLUTTER_EASE_IN_BOUNCE,
  CLUTTER_EASE_OUT_BOUNCE,
  CLUTTER_EASE_IN_OUT_BOUNCE,

  /* step functions (see css3-transitions) */
  CLUTTER_STEPS,
  CLUTTER_STEP_START, /* steps(1, start) */
  CLUTTER_STEP_END, /* steps(1, end) */

  /* cubic bezier (see css3-transitions) */
  CLUTTER_CUBIC_BEZIER,
  CLUTTER_EASE,
  CLUTTER_EASE_IN,
  CLUTTER_EASE_OUT,
  CLUTTER_EASE_IN_OUT,

  /* guard, before registered alpha functions */
  CLUTTER_ANIMATION_LAST
} ClutterAnimationMode;

/**
 * ClutterTextDirection:
 * @CLUTTER_TEXT_DIRECTION_DEFAULT: Use the default setting, as returned
 *   by clutter_get_default_text_direction()
 * @CLUTTER_TEXT_DIRECTION_LTR: Use left-to-right text direction
 * @CLUTTER_TEXT_DIRECTION_RTL: Use right-to-left text direction
 *
 * The text direction to be used by [class@Actor]s
 */
typedef enum
{
  CLUTTER_TEXT_DIRECTION_DEFAULT,
  CLUTTER_TEXT_DIRECTION_LTR,
  CLUTTER_TEXT_DIRECTION_RTL
} ClutterTextDirection;

/**
 * ClutterShaderType:
 * @CLUTTER_VERTEX_SHADER: a vertex shader
 * @CLUTTER_FRAGMENT_SHADER: a fragment shader
 *
 * The type of GLSL shader program
 */
typedef enum
{
  CLUTTER_VERTEX_SHADER,
  CLUTTER_FRAGMENT_SHADER
} ClutterShaderType;

/**
 * ClutterModifierType:
 * @CLUTTER_SHIFT_MASK: Mask applied by the Shift key
 * @CLUTTER_LOCK_MASK: Mask applied by the Caps Lock key
 * @CLUTTER_CONTROL_MASK: Mask applied by the Control key
 * @CLUTTER_MOD1_MASK: Mask applied by the first Mod key
 * @CLUTTER_MOD2_MASK: Mask applied by the second Mod key
 * @CLUTTER_MOD3_MASK: Mask applied by the third Mod key
 * @CLUTTER_MOD4_MASK: Mask applied by the fourth Mod key
 * @CLUTTER_MOD5_MASK: Mask applied by the fifth Mod key
 * @CLUTTER_BUTTON1_MASK: Mask applied by the first pointer button
 * @CLUTTER_BUTTON2_MASK: Mask applied by the second pointer button
 * @CLUTTER_BUTTON3_MASK: Mask applied by the third pointer button
 * @CLUTTER_BUTTON4_MASK: Mask applied by the fourth pointer button
 * @CLUTTER_BUTTON5_MASK: Mask applied by the fifth pointer button
 * @CLUTTER_SUPER_MASK: Mask applied by the Super key
 * @CLUTTER_HYPER_MASK: Mask applied by the Hyper key
 * @CLUTTER_META_MASK: Mask applied by the Meta key
 * @CLUTTER_RELEASE_MASK: Mask applied during release
 * @CLUTTER_MODIFIER_MASK: A mask covering all modifier types
 *
 * Masks applied to a #ClutterEvent by modifiers.
 *
 * Note that Clutter may add internal values to events which include
 * reserved values such as %CLUTTER_MODIFIER_RESERVED_13_MASK.  Your code
 * should preserve and ignore them.  You can use %CLUTTER_MODIFIER_MASK to
 * remove all reserved values.
 */
typedef enum
{
  CLUTTER_SHIFT_MASK    = 1 << 0,
  CLUTTER_LOCK_MASK     = 1 << 1,
  CLUTTER_CONTROL_MASK  = 1 << 2,
  CLUTTER_MOD1_MASK     = 1 << 3,
  CLUTTER_MOD2_MASK     = 1 << 4,
  CLUTTER_MOD3_MASK     = 1 << 5,
  CLUTTER_MOD4_MASK     = 1 << 6,
  CLUTTER_MOD5_MASK     = 1 << 7,
  CLUTTER_BUTTON1_MASK  = 1 << 8,
  CLUTTER_BUTTON2_MASK  = 1 << 9,
  CLUTTER_BUTTON3_MASK  = 1 << 10,
  CLUTTER_BUTTON4_MASK  = 1 << 11,
  CLUTTER_BUTTON5_MASK  = 1 << 12,

#ifndef __GTK_DOC_IGNORE__
  CLUTTER_MODIFIER_RESERVED_13_MASK  = 1 << 13,
  CLUTTER_MODIFIER_RESERVED_14_MASK  = 1 << 14,
  CLUTTER_MODIFIER_RESERVED_15_MASK  = 1 << 15,
  CLUTTER_MODIFIER_RESERVED_16_MASK  = 1 << 16,
  CLUTTER_MODIFIER_RESERVED_17_MASK  = 1 << 17,
  CLUTTER_MODIFIER_RESERVED_18_MASK  = 1 << 18,
  CLUTTER_MODIFIER_RESERVED_19_MASK  = 1 << 19,
  CLUTTER_MODIFIER_RESERVED_20_MASK  = 1 << 20,
  CLUTTER_MODIFIER_RESERVED_21_MASK  = 1 << 21,
  CLUTTER_MODIFIER_RESERVED_22_MASK  = 1 << 22,
  CLUTTER_MODIFIER_RESERVED_23_MASK  = 1 << 23,
  CLUTTER_MODIFIER_RESERVED_24_MASK  = 1 << 24,
  CLUTTER_MODIFIER_RESERVED_25_MASK  = 1 << 25,
#endif

  CLUTTER_SUPER_MASK    = 1 << 26,
  CLUTTER_HYPER_MASK    = 1 << 27,
  CLUTTER_META_MASK     = 1 << 28,

#ifndef __GTK_DOC_IGNORE__
  CLUTTER_MODIFIER_RESERVED_29_MASK  = 1 << 29,
#endif

  CLUTTER_RELEASE_MASK  = 1 << 30,

  /* Combination of CLUTTER_SHIFT_MASK..CLUTTER_BUTTON5_MASK + CLUTTER_SUPER_MASK
     + CLUTTER_HYPER_MASK + CLUTTER_META_MASK + CLUTTER_RELEASE_MASK */
  CLUTTER_MODIFIER_MASK = 0x5c001fff
} ClutterModifierType;

/**
 * ClutterPointerA11yFlags:
 * @CLUTTER_A11Y_POINTER_ENABLED:
 * @CLUTTER_A11Y_SECONDARY_CLICK_ENABLED:
 * @CLUTTER_A11Y_DWELL_ENABLED:
 *
 * Pointer accessibility features applied to a ClutterInputDevice pointer.
 *
 */
typedef enum {
  CLUTTER_A11Y_SECONDARY_CLICK_ENABLED   = 1 << 0,
  CLUTTER_A11Y_DWELL_ENABLED             = 1 << 1,
} ClutterPointerA11yFlags;

/**
 * ClutterPointerA11yDwellClickType:
 * @CLUTTER_A11Y_DWELL_CLICK_TYPE_NONE: Internal use only
 * @CLUTTER_A11Y_DWELL_CLICK_TYPE_PRIMARY:
 * @CLUTTER_A11Y_DWELL_CLICK_TYPE_SECONDARY:
 * @CLUTTER_A11Y_DWELL_CLICK_TYPE_MIDDLE:
 * @CLUTTER_A11Y_DWELL_CLICK_TYPE_DOUBLE:
 * @CLUTTER_A11Y_DWELL_CLICK_TYPE_DRAG:
 *
 * Dwell click types.
 *
 */
typedef enum {
  CLUTTER_A11Y_DWELL_CLICK_TYPE_NONE,
  CLUTTER_A11Y_DWELL_CLICK_TYPE_PRIMARY,
  CLUTTER_A11Y_DWELL_CLICK_TYPE_SECONDARY,
  CLUTTER_A11Y_DWELL_CLICK_TYPE_MIDDLE,
  CLUTTER_A11Y_DWELL_CLICK_TYPE_DOUBLE,
  CLUTTER_A11Y_DWELL_CLICK_TYPE_DRAG,
} ClutterPointerA11yDwellClickType;

/**
 * ClutterPointerA11yDwellDirection:
 * @CLUTTER_A11Y_DWELL_DIRECTION_NONE:
 * @CLUTTER_A11Y_DWELL_DIRECTION_LEFT:
 * @CLUTTER_A11Y_DWELL_DIRECTION_RIGHT:
 * @CLUTTER_A11Y_DWELL_DIRECTION_UP:
 * @CLUTTER_A11Y_DWELL_DIRECTION_DOWN:
 *
 * Dwell gesture directions.
 *
 */
typedef enum {
  CLUTTER_A11Y_DWELL_DIRECTION_NONE,
  CLUTTER_A11Y_DWELL_DIRECTION_LEFT,
  CLUTTER_A11Y_DWELL_DIRECTION_RIGHT,
  CLUTTER_A11Y_DWELL_DIRECTION_UP,
  CLUTTER_A11Y_DWELL_DIRECTION_DOWN,
} ClutterPointerA11yDwellDirection;

/**
 * ClutterPointerA11yDwellMode:
 * @CLUTTER_A11Y_DWELL_MODE_WINDOW:
 * @CLUTTER_A11Y_DWELL_MODE_GESTURE:
 *
 * Dwell mode.
 *
 */
typedef enum {
  CLUTTER_A11Y_DWELL_MODE_WINDOW,
  CLUTTER_A11Y_DWELL_MODE_GESTURE,
} ClutterPointerA11yDwellMode;

/**
 * ClutterPointerA11yTimeoutType:
 * @CLUTTER_A11Y_TIMEOUT_TYPE_SECONDARY_CLICK:
 * @CLUTTER_A11Y_TIMEOUT_TYPE_DWELL:
 * @CLUTTER_A11Y_TIMEOUT_TYPE_GESTURE:
 *
 * Pointer accessibility timeout type.
 *
 */
typedef enum {
  CLUTTER_A11Y_TIMEOUT_TYPE_SECONDARY_CLICK,
  CLUTTER_A11Y_TIMEOUT_TYPE_DWELL,
  CLUTTER_A11Y_TIMEOUT_TYPE_GESTURE,
} ClutterPointerA11yTimeoutType;

/**
 * ClutterActorFlags:
 * @CLUTTER_ACTOR_MAPPED: the actor will be painted (is visible, and inside
 *   a toplevel, and all parents visible)
 * @CLUTTER_ACTOR_REALIZED: the resources associated to the actor have been
 *   allocated
 * @CLUTTER_ACTOR_REACTIVE: the actor 'reacts' to mouse events emitting event
 *   signals
 * @CLUTTER_ACTOR_VISIBLE: the actor has been shown by the application program
 * @CLUTTER_ACTOR_NO_LAYOUT: the actor provides an explicit layout management
 *   policy for its children; this flag will prevent Clutter from automatic
 *   queueing of relayout and will defer all layouting to the actor itself
 *
 * Flags used to signal the state of an actor.
 */
typedef enum /*< prefix=CLUTTER_ACTOR >*/
{
  CLUTTER_ACTOR_MAPPED    = 1 << 1,
  CLUTTER_ACTOR_REALIZED  = 1 << 2,
  CLUTTER_ACTOR_REACTIVE  = 1 << 3,
  CLUTTER_ACTOR_VISIBLE   = 1 << 4,
  CLUTTER_ACTOR_NO_LAYOUT = 1 << 5
} ClutterActorFlags;

/**
 * ClutterOffscreenRedirect:
 * @CLUTTER_OFFSCREEN_REDIRECT_AUTOMATIC_FOR_OPACITY: Only redirect
 *   the actor if it is semi-transparent and its has_overlaps()
 *   virtual returns %TRUE.
 * @CLUTTER_OFFSCREEN_REDIRECT_ALWAYS: Always redirect the actor to an
 *   offscreen buffer even if it is fully opaque.
 * @CLUTTER_OFFSCREEN_REDIRECT_ON_IDLE: Only redirect the actor if it is the
 *   most efficient thing to do based on its recent repaint behaviour. That
 *   means when its contents are changing less frequently than it's being used
 *   on stage.
 *
 * Possible flags to pass to clutter_actor_set_offscreen_redirect().
 */
typedef enum /*< prefix=CLUTTER_OFFSCREEN_REDIRECT >*/
{
  CLUTTER_OFFSCREEN_REDIRECT_AUTOMATIC_FOR_OPACITY = 1 << 0,
  CLUTTER_OFFSCREEN_REDIRECT_ALWAYS                = 1 << 1,
  CLUTTER_OFFSCREEN_REDIRECT_ON_IDLE               = 1 << 2
} ClutterOffscreenRedirect;

/**
 * ClutterAlignAxis:
 * @CLUTTER_ALIGN_X_AXIS: Maintain the alignment on the X axis
 * @CLUTTER_ALIGN_Y_AXIS: Maintain the alignment on the Y axis
 * @CLUTTER_ALIGN_BOTH: Maintain the alignment on both the X and Y axis
 *
 * Specifies the axis on which #ClutterAlignConstraint should maintain
 * the alignment.
 */
typedef enum /*< prefix=CLUTTER_ALIGN >*/
{
  CLUTTER_ALIGN_X_AXIS,
  CLUTTER_ALIGN_Y_AXIS,
  CLUTTER_ALIGN_BOTH
} ClutterAlignAxis;

/**
 * ClutterBindCoordinate:
 * @CLUTTER_BIND_X: Bind the X coordinate
 * @CLUTTER_BIND_Y: Bind the Y coordinate
 * @CLUTTER_BIND_WIDTH: Bind the width
 * @CLUTTER_BIND_HEIGHT: Bind the height
 * @CLUTTER_BIND_POSITION: Equivalent to to %CLUTTER_BIND_X and
 *   %CLUTTER_BIND_Y
 * @CLUTTER_BIND_SIZE: Equivalent to %CLUTTER_BIND_WIDTH and
 *   %CLUTTER_BIND_HEIGHT
 * @CLUTTER_BIND_ALL: Equivalent to %CLUTTER_BIND_POSITION and
 *   %CLUTTER_BIND_SIZE
 *
 * Specifies which property should be used in a binding
 */
typedef enum /*< prefix=CLUTTER_BIND >*/
{
  CLUTTER_BIND_X,
  CLUTTER_BIND_Y,
  CLUTTER_BIND_WIDTH,
  CLUTTER_BIND_HEIGHT,
  CLUTTER_BIND_POSITION,
  CLUTTER_BIND_SIZE,
  CLUTTER_BIND_ALL
} ClutterBindCoordinate;

/**
 * ClutterEffectPaintFlags:
 * @CLUTTER_EFFECT_PAINT_ACTOR_DIRTY: The actor or one of its children
 *   has queued a redraw before this paint. This implies that the effect
 *   should call clutter_actor_continue_paint() to chain to the next
 *   effect and can not cache any results from a previous paint.
 * @CLUTTER_EFFECT_PAINT_BYPASS_EFFECT: The effect should not be used
 *   on this frame, but it will be asked to paint the actor still.
 *
 * Flags passed to the ‘paint’ or ‘pick’ method of #ClutterEffect.
 */
typedef enum /*< prefix=CLUTTER_EFFECT_PAINT >*/
{
  CLUTTER_EFFECT_PAINT_ACTOR_DIRTY   = (1 << 0),
  CLUTTER_EFFECT_PAINT_BYPASS_EFFECT = (1 << 1)
} ClutterEffectPaintFlags;


/**
 * ClutterLongPressState:
 * @CLUTTER_LONG_PRESS_QUERY: Queries the action whether it supports
 *   long presses
 * @CLUTTER_LONG_PRESS_ACTIVATE: Activates the action on a long press
 * @CLUTTER_LONG_PRESS_CANCEL: The long press was cancelled
 *
 * The states for the #ClutterClickAction::long-press signal.
 */
typedef enum /*< prefix=CLUTTER_LONG_PRESS >*/
{
  CLUTTER_LONG_PRESS_QUERY,
  CLUTTER_LONG_PRESS_ACTIVATE,
  CLUTTER_LONG_PRESS_CANCEL
} ClutterLongPressState;

/**
 * ClutterEventFlags:
 * @CLUTTER_EVENT_NONE: No flag set
 * @CLUTTER_EVENT_FLAG_SYNTHETIC: Synthetic event
 * @CLUTTER_EVENT_FLAG_REPEATED: Auto-repeated event
 *
 * Flags for the #ClutterEvent
 */
typedef enum /*< flags prefix=CLUTTER_EVENT >*/
{
  CLUTTER_EVENT_NONE              = 0,
  CLUTTER_EVENT_FLAG_SYNTHETIC    = 1 << 0,
  CLUTTER_EVENT_FLAG_INPUT_METHOD = 1 << 1,
  CLUTTER_EVENT_FLAG_REPEATED     = 1 << 2,
  CLUTTER_EVENT_FLAG_RELATIVE_MOTION = 1 << 3,
  CLUTTER_EVENT_FLAG_GRAB_NOTIFY  = 1 << 4,
  CLUTTER_EVENT_FLAG_POINTER_EMULATED = 1 << 5,
} ClutterEventFlags;

/**
 * ClutterEventType:
 * @CLUTTER_NOTHING: Empty event
 * @CLUTTER_KEY_PRESS: Key press event
 * @CLUTTER_KEY_RELEASE: Key release event
 * @CLUTTER_MOTION: Pointer motion event
 * @CLUTTER_ENTER: Actor enter event
 * @CLUTTER_LEAVE: Actor leave event
 * @CLUTTER_BUTTON_PRESS: Pointer button press event
 * @CLUTTER_BUTTON_RELEASE: Pointer button release event
 * @CLUTTER_SCROLL: Pointer scroll event
 * @CLUTTER_TOUCH_BEGIN: A new touch event sequence has started;
 * @CLUTTER_TOUCH_UPDATE: A touch event sequence has been updated;
 * @CLUTTER_TOUCH_END: A touch event sequence has finished;
 * @CLUTTER_TOUCH_CANCEL: A touch event sequence has been canceled;
 * @CLUTTER_TOUCHPAD_PINCH: A pinch gesture event, the current state is
 *   determined by its phase field;
 * @CLUTTER_TOUCHPAD_SWIPE: A swipe gesture event, the current state is
 *   determined by its phase field;
 * @CLUTTER_TOUCHPAD_HOLD: A hold gesture event, the current state is
 *   determined by its phase field. A hold gesture starts when the user places a
 *   finger on the touchpad and ends when all fingers are lifted. It is
 *   cancelled when the finger(s) move past a certain threshold.
 * @CLUTTER_PROXIMITY_IN: A tool entered in proximity to a tablet;
 * @CLUTTER_PROXIMITY_OUT: A tool left from the proximity area of a tablet;
 * @CLUTTER_EVENT_LAST: Marks the end of the #ClutterEventType enumeration;
 *
 * Types of events.
 */
typedef enum /*< prefix=CLUTTER >*/
{
  CLUTTER_NOTHING = 0,
  CLUTTER_KEY_PRESS,
  CLUTTER_KEY_RELEASE,
  CLUTTER_MOTION,
  CLUTTER_ENTER,
  CLUTTER_LEAVE,
  CLUTTER_BUTTON_PRESS,
  CLUTTER_BUTTON_RELEASE,
  CLUTTER_SCROLL,
  CLUTTER_TOUCH_BEGIN,
  CLUTTER_TOUCH_UPDATE,
  CLUTTER_TOUCH_END,
  CLUTTER_TOUCH_CANCEL,
  CLUTTER_TOUCHPAD_PINCH,
  CLUTTER_TOUCHPAD_SWIPE,
  CLUTTER_TOUCHPAD_HOLD,
  CLUTTER_PROXIMITY_IN,
  CLUTTER_PROXIMITY_OUT,
  CLUTTER_PAD_BUTTON_PRESS,
  CLUTTER_PAD_BUTTON_RELEASE,
  CLUTTER_PAD_STRIP,
  CLUTTER_PAD_RING,
  CLUTTER_DEVICE_ADDED,
  CLUTTER_DEVICE_REMOVED,
  CLUTTER_IM_COMMIT,
  CLUTTER_IM_DELETE,
  CLUTTER_IM_PREEDIT,

  CLUTTER_EVENT_LAST            /* helper */
} ClutterEventType;

/**
 * ClutterScrollDirection:
 * @CLUTTER_SCROLL_UP: Scroll up
 * @CLUTTER_SCROLL_DOWN: Scroll down
 * @CLUTTER_SCROLL_LEFT: Scroll left
 * @CLUTTER_SCROLL_RIGHT: Scroll right
 * @CLUTTER_SCROLL_SMOOTH: Precise scrolling delta (available in 1.10)
 *
 * Direction of a pointer scroll event.
 *
 * The %CLUTTER_SCROLL_SMOOTH value implies that the #ClutterScrollEvent
 * has precise scrolling delta information.
 */
typedef enum /*< prefix=CLUTTER_SCROLL >*/
{
  CLUTTER_SCROLL_UP,
  CLUTTER_SCROLL_DOWN,
  CLUTTER_SCROLL_LEFT,
  CLUTTER_SCROLL_RIGHT,
  CLUTTER_SCROLL_SMOOTH
} ClutterScrollDirection;

/**
 * ClutterInputDeviceCapabilities:
 * @CLUTTER_INPUT_CAPABILITY_NONE: No capabilities
 * @CLUTTER_INPUT_CAPABILITY_POINTER: Pointer capability
 * @CLUTTER_INPUT_CAPABILITY_KEYBOARD: Keyboard capability
 * @CLUTTER_INPUT_CAPABILITY_TOUCHPAD: Touchpad gesture and scroll capability
 * @CLUTTER_INPUT_CAPABILITY_TOUCH: Touch capability
 * @CLUTTER_INPUT_CAPABILITY_TABLET_TOOL: Tablet tool capability
 * @CLUTTER_INPUT_CAPABILITY_TABLET_PAD: Tablet pad capability
 *
 * Describes the capabilities of an input device.
 **/
typedef enum /*< prefix=CLUTTER_INPUT_CAPABILITY >*/
{
  CLUTTER_INPUT_CAPABILITY_NONE = 0,
  CLUTTER_INPUT_CAPABILITY_POINTER = 1 << 0,
  CLUTTER_INPUT_CAPABILITY_KEYBOARD = 1 << 1,
  CLUTTER_INPUT_CAPABILITY_TOUCHPAD = 1 << 2,
  CLUTTER_INPUT_CAPABILITY_TOUCH = 1 << 3,
  CLUTTER_INPUT_CAPABILITY_TABLET_TOOL = 1 << 4,
  CLUTTER_INPUT_CAPABILITY_TABLET_PAD = 1 << 5,
  CLUTTER_INPUT_CAPABILITY_TRACKBALL = 1 << 6,
  CLUTTER_INPUT_CAPABILITY_TRACKPOINT = 1 << 7,
} ClutterInputCapabilities;

/**
 * ClutterInputDeviceType:
 * @CLUTTER_POINTER_DEVICE: A pointer device
 * @CLUTTER_KEYBOARD_DEVICE: A keyboard device
 * @CLUTTER_EXTENSION_DEVICE: A generic extension device
 * @CLUTTER_JOYSTICK_DEVICE: A joystick device
 * @CLUTTER_TABLET_DEVICE: A tablet device
 * @CLUTTER_TOUCHPAD_DEVICE: A touchpad device
 * @CLUTTER_TOUCHSCREEN_DEVICE: A touch screen device
 * @CLUTTER_PEN_DEVICE: A pen device
 * @CLUTTER_ERASER_DEVICE: An eraser device
 * @CLUTTER_CURSOR_DEVICE: A cursor device
 * @CLUTTER_PAD_DEVICE: A tablet pad
 * @CLUTTER_N_DEVICE_TYPES: The number of device types
 *
 * The types of input devices available.
 *
 * The #ClutterInputDeviceType enumeration can be extended at later
 * date; not every platform supports every input device type.
 */
typedef enum
{
  CLUTTER_POINTER_DEVICE,
  CLUTTER_KEYBOARD_DEVICE,
  CLUTTER_EXTENSION_DEVICE,
  CLUTTER_JOYSTICK_DEVICE,
  CLUTTER_TABLET_DEVICE,
  CLUTTER_TOUCHPAD_DEVICE,
  CLUTTER_TOUCHSCREEN_DEVICE,
  CLUTTER_PEN_DEVICE,
  CLUTTER_ERASER_DEVICE,
  CLUTTER_CURSOR_DEVICE,
  CLUTTER_PAD_DEVICE,

  CLUTTER_N_DEVICE_TYPES
} ClutterInputDeviceType;

/**
 * ClutterInputMode:
 * @CLUTTER_INPUT_MODE_LOGICAL: A logical, virtual device
 * @CLUTTER_INPUT_MODE_PHYSICAL: A physical device, attached to
 *   a logical device
 * @CLUTTER_INPUT_MODE_FLOATING: A physical device, not attached
 *   to a logical device
 *
 * The mode for input devices available.
 */
typedef enum
{
  CLUTTER_INPUT_MODE_LOGICAL,
  CLUTTER_INPUT_MODE_PHYSICAL,
  CLUTTER_INPUT_MODE_FLOATING
} ClutterInputMode;

/**
 * ClutterInputAxis:
 * @CLUTTER_INPUT_AXIS_IGNORE: Unused axis
 * @CLUTTER_INPUT_AXIS_X: The position on the X axis
 * @CLUTTER_INPUT_AXIS_Y: The position of the Y axis
 * @CLUTTER_INPUT_AXIS_PRESSURE: The pressure information
 * @CLUTTER_INPUT_AXIS_XTILT: The tilt on the X axis
 * @CLUTTER_INPUT_AXIS_YTILT: The tile on the Y axis
 * @CLUTTER_INPUT_AXIS_WHEEL: A wheel
 * @CLUTTER_INPUT_AXIS_DISTANCE: Distance (Since 1.12)
 * @CLUTTER_INPUT_AXIS_ROTATION: Rotation along the z-axis (Since 1.28)
 * @CLUTTER_INPUT_AXIS_SLIDER: A slider (Since 1.28)
 * @CLUTTER_INPUT_AXIS_LAST: Last value of the enumeration; this value is
 *   useful when iterating over the enumeration values (Since 1.12)
 *
 * The type of axes Clutter recognizes on a #ClutterInputDevice
 */
typedef enum
{
  CLUTTER_INPUT_AXIS_IGNORE,

  CLUTTER_INPUT_AXIS_X,
  CLUTTER_INPUT_AXIS_Y,
  CLUTTER_INPUT_AXIS_PRESSURE,
  CLUTTER_INPUT_AXIS_XTILT,
  CLUTTER_INPUT_AXIS_YTILT,
  CLUTTER_INPUT_AXIS_WHEEL,
  CLUTTER_INPUT_AXIS_DISTANCE,
  CLUTTER_INPUT_AXIS_ROTATION,
  CLUTTER_INPUT_AXIS_SLIDER,

  CLUTTER_INPUT_AXIS_LAST
} ClutterInputAxis;

typedef enum
{
  CLUTTER_INPUT_AXIS_FLAG_NONE = 0,
  CLUTTER_INPUT_AXIS_FLAG_X = 1 << CLUTTER_INPUT_AXIS_X,
  CLUTTER_INPUT_AXIS_FLAG_Y = 1 << CLUTTER_INPUT_AXIS_Y,
  CLUTTER_INPUT_AXIS_FLAG_PRESSURE = 1 << CLUTTER_INPUT_AXIS_PRESSURE,
  CLUTTER_INPUT_AXIS_FLAG_XTILT = 1 << CLUTTER_INPUT_AXIS_XTILT,
  CLUTTER_INPUT_AXIS_FLAG_YTILT = 1 << CLUTTER_INPUT_AXIS_YTILT,
  CLUTTER_INPUT_AXIS_FLAG_WHEEL = 1 << CLUTTER_INPUT_AXIS_WHEEL,
  CLUTTER_INPUT_AXIS_FLAG_DISTANCE = 1 << CLUTTER_INPUT_AXIS_DISTANCE,
  CLUTTER_INPUT_AXIS_FLAG_ROTATION = 1 << CLUTTER_INPUT_AXIS_ROTATION,
  CLUTTER_INPUT_AXIS_FLAG_SLIDER = 1 << CLUTTER_INPUT_AXIS_SLIDER,
} ClutterInputAxisFlags;

/**
 * ClutterSnapEdge:
 * @CLUTTER_SNAP_EDGE_TOP: the top edge
 * @CLUTTER_SNAP_EDGE_RIGHT: the right edge
 * @CLUTTER_SNAP_EDGE_BOTTOM: the bottom edge
 * @CLUTTER_SNAP_EDGE_LEFT: the left edge
 *
 * The edge to snap
 */
typedef enum
{
  CLUTTER_SNAP_EDGE_TOP,
  CLUTTER_SNAP_EDGE_RIGHT,
  CLUTTER_SNAP_EDGE_BOTTOM,
  CLUTTER_SNAP_EDGE_LEFT
} ClutterSnapEdge;

/**
 * ClutterPickMode:
 * @CLUTTER_PICK_NONE: Do not paint any actor
 * @CLUTTER_PICK_REACTIVE: Paint only the reactive actors
 * @CLUTTER_PICK_ALL: Paint all actors
 *
 * Controls the paint cycle of the scene graph when in pick mode
 */
typedef enum
{
  CLUTTER_PICK_NONE = 0,
  CLUTTER_PICK_REACTIVE,
  CLUTTER_PICK_ALL
} ClutterPickMode;

/**
 * ClutterSwipeDirection:
 * @CLUTTER_SWIPE_DIRECTION_UP: Upwards swipe gesture
 * @CLUTTER_SWIPE_DIRECTION_DOWN: Downwards swipe gesture
 * @CLUTTER_SWIPE_DIRECTION_LEFT: Leftwards swipe gesture
 * @CLUTTER_SWIPE_DIRECTION_RIGHT: Rightwards swipe gesture
 *
 * The main direction of the swipe gesture
 */
typedef enum /*< prefix=CLUTTER_SWIPE_DIRECTION >*/
{
  CLUTTER_SWIPE_DIRECTION_UP    = 1 << 0,
  CLUTTER_SWIPE_DIRECTION_DOWN  = 1 << 1,
  CLUTTER_SWIPE_DIRECTION_LEFT  = 1 << 2,
  CLUTTER_SWIPE_DIRECTION_RIGHT = 1 << 3
} ClutterSwipeDirection;

/**
 * ClutterPanAxis:
 * @CLUTTER_PAN_AXIS_NONE: No constraint
 * @CLUTTER_PAN_X_AXIS: Set a constraint on the X axis
 * @CLUTTER_PAN_Y_AXIS: Set a constraint on the Y axis
 * @CLUTTER_PAN_AXIS_AUTO: Constrain panning automatically based on initial
 *   movement (available since 1.24)
 *
 * The axis of the constraint that should be applied on the
 * panning action
 */
typedef enum /*< prefix=CLUTTER_PAN >*/
{
  CLUTTER_PAN_AXIS_NONE = 0,

  CLUTTER_PAN_X_AXIS,
  CLUTTER_PAN_Y_AXIS,

  CLUTTER_PAN_AXIS_AUTO
} ClutterPanAxis;

/**
 * ClutterTimelineDirection:
 * @CLUTTER_TIMELINE_FORWARD: forward direction for a timeline
 * @CLUTTER_TIMELINE_BACKWARD: backward direction for a timeline
 *
 * The direction of a #ClutterTimeline
 */
typedef enum
{
  CLUTTER_TIMELINE_FORWARD,
  CLUTTER_TIMELINE_BACKWARD
} ClutterTimelineDirection;

/**
 * ClutterActorAlign:
 * @CLUTTER_ACTOR_ALIGN_FILL: Stretch to cover the whole allocated space
 * @CLUTTER_ACTOR_ALIGN_START: Snap to left or top side, leaving space
 *   to the right or bottom. For horizontal layouts, in right-to-left
 *   locales this should be reversed.
 * @CLUTTER_ACTOR_ALIGN_CENTER: Center the actor inside the allocation
 * @CLUTTER_ACTOR_ALIGN_END: Snap to right or bottom side, leaving space
 *   to the left or top. For horizontal layouts, in right-to-left locales
 *   this should be reversed.
 *
 * Controls how a #ClutterActor should align itself inside the extra space
 * assigned to it during the allocation.
 *
 * Alignment only matters if the allocated space given to an actor is
 * bigger than its natural size; for example, when
 * the [property@Clutter.Actor:x-expand] or the [property@Clutter.Actor:y-expand]
 * properties of #ClutterActor are set to %TRUE.
 */
typedef enum
{
  CLUTTER_ACTOR_ALIGN_FILL,
  CLUTTER_ACTOR_ALIGN_START,
  CLUTTER_ACTOR_ALIGN_CENTER,
  CLUTTER_ACTOR_ALIGN_END
} ClutterActorAlign;

/**
 * ClutterRepaintFlags:
 * @CLUTTER_REPAINT_FLAGS_PRE_PAINT: Run the repaint function prior to
 *   painting the stages
 * @CLUTTER_REPAINT_FLAGS_POST_PAINT: Run the repaint function after
 *   painting the stages
 *
 * Flags to pass to clutter_threads_add_repaint_func_full().
 */
typedef enum
{
  CLUTTER_REPAINT_FLAGS_PRE_PAINT = 1 << 0,
  CLUTTER_REPAINT_FLAGS_POST_PAINT = 1 << 1,
} ClutterRepaintFlags;

/**
 * ClutterContentGravity:
 * @CLUTTER_CONTENT_GRAVITY_TOP_LEFT: Align the content to the top left corner
 * @CLUTTER_CONTENT_GRAVITY_TOP: Align the content to the top edge
 * @CLUTTER_CONTENT_GRAVITY_TOP_RIGHT: Align the content to the top right corner
 * @CLUTTER_CONTENT_GRAVITY_LEFT: Align the content to the left edge
 * @CLUTTER_CONTENT_GRAVITY_CENTER: Align the content to the center
 * @CLUTTER_CONTENT_GRAVITY_RIGHT: Align the content to the right edge
 * @CLUTTER_CONTENT_GRAVITY_BOTTOM_LEFT: Align the content to the bottom left corner
 * @CLUTTER_CONTENT_GRAVITY_BOTTOM: Align the content to the bottom edge
 * @CLUTTER_CONTENT_GRAVITY_BOTTOM_RIGHT: Align the content to the bottom right corner
 * @CLUTTER_CONTENT_GRAVITY_RESIZE_FILL: Resize the content to fill the allocation
 * @CLUTTER_CONTENT_GRAVITY_RESIZE_ASPECT: Resize the content to remain within the
 *   allocation, while maintaining the aspect ratio
 *
 * Controls the alignment of the #ClutterContent inside a #ClutterActor.
 */
typedef enum
{
  CLUTTER_CONTENT_GRAVITY_TOP_LEFT,
  CLUTTER_CONTENT_GRAVITY_TOP,
  CLUTTER_CONTENT_GRAVITY_TOP_RIGHT,

  CLUTTER_CONTENT_GRAVITY_LEFT,
  CLUTTER_CONTENT_GRAVITY_CENTER,
  CLUTTER_CONTENT_GRAVITY_RIGHT,

  CLUTTER_CONTENT_GRAVITY_BOTTOM_LEFT,
  CLUTTER_CONTENT_GRAVITY_BOTTOM,
  CLUTTER_CONTENT_GRAVITY_BOTTOM_RIGHT,

  CLUTTER_CONTENT_GRAVITY_RESIZE_FILL,
  CLUTTER_CONTENT_GRAVITY_RESIZE_ASPECT
} ClutterContentGravity;

/**
 * ClutterScalingFilter:
 * @CLUTTER_SCALING_FILTER_LINEAR: Linear interpolation filter
 * @CLUTTER_SCALING_FILTER_NEAREST: Nearest neighbor interpolation filter
 * @CLUTTER_SCALING_FILTER_TRILINEAR: Trilinear minification filter, with
 *   mipmap generation; this filter linearly interpolates on every axis,
 *   as well as between mipmap levels.
 *
 * The scaling filters to be used with the [property@Actor:minification-filter]
 * and [property@Actor:magnification-filter] properties.
 */
typedef enum
{
  CLUTTER_SCALING_FILTER_LINEAR,
  CLUTTER_SCALING_FILTER_NEAREST,
  CLUTTER_SCALING_FILTER_TRILINEAR
} ClutterScalingFilter;

/**
 * ClutterOrientation:
 * @CLUTTER_ORIENTATION_HORIZONTAL: An horizontal orientation
 * @CLUTTER_ORIENTATION_VERTICAL: A vertical orientation
 *
 * Represents the orientation of actors or layout managers.
 */
typedef enum
{
  CLUTTER_ORIENTATION_HORIZONTAL,
  CLUTTER_ORIENTATION_VERTICAL
} ClutterOrientation;

/**
 * ClutterScrollMode:
 * @CLUTTER_SCROLL_NONE: Ignore scrolling
 * @CLUTTER_SCROLL_HORIZONTALLY: Scroll only horizontally
 * @CLUTTER_SCROLL_VERTICALLY: Scroll only vertically
 * @CLUTTER_SCROLL_BOTH: Scroll in both directions
 *
 * Scroll modes.
 */
typedef enum /*< prefix=CLUTTER_SCROLL >*/
{
  CLUTTER_SCROLL_NONE         = 0,

  CLUTTER_SCROLL_HORIZONTALLY = 1 << 0,
  CLUTTER_SCROLL_VERTICALLY   = 1 << 1,

  CLUTTER_SCROLL_BOTH         = CLUTTER_SCROLL_HORIZONTALLY | CLUTTER_SCROLL_VERTICALLY
} ClutterScrollMode;

/**
 * ClutterGridPosition:
 * @CLUTTER_GRID_POSITION_LEFT: left position
 * @CLUTTER_GRID_POSITION_RIGHT: right position
 * @CLUTTER_GRID_POSITION_TOP: top position
 * @CLUTTER_GRID_POSITION_BOTTOM: bottom position
 *
 * Grid position modes.
 */
typedef enum
{
  CLUTTER_GRID_POSITION_LEFT,
  CLUTTER_GRID_POSITION_RIGHT,
  CLUTTER_GRID_POSITION_TOP,
  CLUTTER_GRID_POSITION_BOTTOM
} ClutterGridPosition;

/**
 * ClutterContentRepeat:
 * @CLUTTER_REPEAT_NONE: No repeat
 * @CLUTTER_REPEAT_X_AXIS: Repeat the content on the X axis
 * @CLUTTER_REPEAT_Y_AXIS: Repeat the content on the Y axis
 * @CLUTTER_REPEAT_BOTH: Repeat the content on both axis
 *
 * Content repeat modes.
 */
typedef enum
{
  CLUTTER_REPEAT_NONE   = 0,
  CLUTTER_REPEAT_X_AXIS = 1 << 0,
  CLUTTER_REPEAT_Y_AXIS = 1 << 1,
  CLUTTER_REPEAT_BOTH   = CLUTTER_REPEAT_X_AXIS | CLUTTER_REPEAT_Y_AXIS
} ClutterContentRepeat;

/**
 * ClutterColorspace:
 * @CLUTTER_COLORSPACE_UNKNOWN: Unknown colorspace
 * @CLUTTER_COLORSPACE_SRGB: Default sRGB colorspace
 * @CLUTTER_COLORSPACE_BT2020: BT2020 colorspace
 *
 * Colorspace information.
 */
typedef enum
{
  CLUTTER_COLORSPACE_UNKNOWN,
  CLUTTER_COLORSPACE_SRGB,
  CLUTTER_COLORSPACE_BT2020,
} ClutterColorspace;

/**
 * ClutterStepMode:
 * @CLUTTER_STEP_MODE_START: The change in the value of a
 *   %CLUTTER_STEP progress mode should occur at the start of
 *   the transition
 * @CLUTTER_STEP_MODE_END: The change in the value of a
 *   %CLUTTER_STEP progress mode should occur at the end of
 *   the transition
 *
 * Change the value transition of a step function.
 *
 * See clutter_timeline_set_step_progress().
 */
typedef enum
{
  CLUTTER_STEP_MODE_START,
  CLUTTER_STEP_MODE_END
} ClutterStepMode;

/**
 * ClutterGestureTriggerEdge:
 * @CLUTTER_GESTURE_TRIGGER_EDGE_NONE: Tell #ClutterGestureAction that
 * the gesture must begin immediately and there's no drag limit that
 * will cause its cancellation;
 * @CLUTTER_GESTURE_TRIGGER_EDGE_AFTER: Tell #ClutterGestureAction that
 * it needs to wait until the drag threshold has been exceeded before
 * considering that the gesture has begun;
 * @CLUTTER_GESTURE_TRIGGER_EDGE_BEFORE: Tell #ClutterGestureAction that
 * the gesture must begin immediately and that it must be cancelled
 * once the drag exceed the configured threshold.
 *
 * Enum passed to the [method@GestureAction.set_threshold_trigger_edge]
 * function.
 */
typedef enum
{
  CLUTTER_GESTURE_TRIGGER_EDGE_NONE  = 0,
  CLUTTER_GESTURE_TRIGGER_EDGE_AFTER,
  CLUTTER_GESTURE_TRIGGER_EDGE_BEFORE
} ClutterGestureTriggerEdge;

/**
 * ClutterTouchpadGesturePhase:
 * @CLUTTER_TOUCHPAD_GESTURE_PHASE_BEGIN: The gesture has begun.
 * @CLUTTER_TOUCHPAD_GESTURE_PHASE_UPDATE: The gesture has been updated.
 * @CLUTTER_TOUCHPAD_GESTURE_PHASE_END: The gesture was finished, changes
 *   should be permanently applied.
 * @CLUTTER_TOUCHPAD_GESTURE_PHASE_CANCEL: The gesture was cancelled, all
 *   changes should be undone.
 *
 * The phase of a touchpad gesture event. 
 * 
 * All gestures are guaranteed to begin with an event of type 
 * %CLUTTER_TOUCHPAD_GESTURE_PHASE_BEGIN, followed by a number
 * of %CLUTTER_TOUCHPAD_GESTURE_PHASE_UPDATE (possibly 0).
 *
 * A finished gesture may have 2 possible outcomes, an event with phase
 * %CLUTTER_TOUCHPAD_GESTURE_PHASE_END will be emitted when the gesture is
 * considered successful, this should be used as the hint to perform any
 * permanent changes.

 * Cancelled gestures may be so for a variety of reasons, due to hardware,
 * or due to the gesture recognition layers hinting the gesture did not
 * finish resolutely (eg. a 3rd finger being added during a pinch gesture).
 * In these cases, the last event with report the phase
 * %CLUTTER_TOUCHPAD_GESTURE_PHASE_CANCEL, this should be used as a hint
 * to undo any visible/permanent changes that were done throughout the
 * progress of the gesture.
 *
 * See also #ClutterTouchpadPinchEvent and #ClutterTouchpadPinchEvent.4
 */
typedef enum
{
  CLUTTER_TOUCHPAD_GESTURE_PHASE_BEGIN,
  CLUTTER_TOUCHPAD_GESTURE_PHASE_UPDATE,
  CLUTTER_TOUCHPAD_GESTURE_PHASE_END,
  CLUTTER_TOUCHPAD_GESTURE_PHASE_CANCEL
} ClutterTouchpadGesturePhase;

/**
 * ClutterScrollSource:
 * @CLUTTER_SCROLL_SOURCE_UNKNOWN: Source of scroll events is unknown.
 * @CLUTTER_SCROLL_SOURCE_WHEEL: The scroll event is originated by a mouse wheel.
 * @CLUTTER_SCROLL_SOURCE_FINGER: The scroll event is originated by one or more
 *   fingers on the device (eg. touchpads).
 * @CLUTTER_SCROLL_SOURCE_CONTINUOUS: The scroll event is originated by the
 *   motion of some device (eg. a scroll button is set).
 *
 * The scroll source determines the source of the scroll event. 
 * 
 * Keep in mind that the source device #ClutterInputDeviceType is not enough
 * to infer the scroll source.6
 */
typedef enum
{
  CLUTTER_SCROLL_SOURCE_UNKNOWN,
  CLUTTER_SCROLL_SOURCE_WHEEL,
  CLUTTER_SCROLL_SOURCE_FINGER,
  CLUTTER_SCROLL_SOURCE_CONTINUOUS
} ClutterScrollSource;

/**
 * ClutterScrollFinishFlags:
 * @CLUTTER_SCROLL_FINISHED_NONE: no axis was stopped.
 * @CLUTTER_SCROLL_FINISHED_HORIZONTAL: The horizontal axis stopped.
 * @CLUTTER_SCROLL_FINISHED_VERTICAL: The vertical axis stopped.
 *
 * Flags used to notify the axes that were stopped in a #ClutterScrollEvent.
 * 
 * These can be used to trigger post-scroll effects like kinetic scrolling.6
 */
typedef enum
{
  CLUTTER_SCROLL_FINISHED_NONE       = 0,
  CLUTTER_SCROLL_FINISHED_HORIZONTAL = 1 << 0,
  CLUTTER_SCROLL_FINISHED_VERTICAL   = 1 << 1
} ClutterScrollFinishFlags;

/**
 * ClutterInputDeviceToolType:
 * @CLUTTER_INPUT_DEVICE_TOOL_NONE: No tool
 * @CLUTTER_INPUT_DEVICE_TOOL_PEN: The tool is a pen
 * @CLUTTER_INPUT_DEVICE_TOOL_ERASER: The tool is an eraser
 * @CLUTTER_INPUT_DEVICE_TOOL_BRUSH: The tool is a brush
 * @CLUTTER_INPUT_DEVICE_TOOL_PENCIL: The tool is a pencil
 * @CLUTTER_INPUT_DEVICE_TOOL_AIRBRUSH: The tool is an airbrush
 * @CLUTTER_INPUT_DEVICE_TOOL_MOUSE: The tool is a mouse
 * @CLUTTER_INPUT_DEVICE_TOOL_LENS: The tool is a lens
 *
 * Defines the type of tool that a #ClutterInputDeviceTool represents.8
 */
typedef enum
{
  CLUTTER_INPUT_DEVICE_TOOL_NONE,
  CLUTTER_INPUT_DEVICE_TOOL_PEN,
  CLUTTER_INPUT_DEVICE_TOOL_ERASER,
  CLUTTER_INPUT_DEVICE_TOOL_BRUSH,
  CLUTTER_INPUT_DEVICE_TOOL_PENCIL,
  CLUTTER_INPUT_DEVICE_TOOL_AIRBRUSH,
  CLUTTER_INPUT_DEVICE_TOOL_MOUSE,
  CLUTTER_INPUT_DEVICE_TOOL_LENS
} ClutterInputDeviceToolType;

typedef enum
{
  CLUTTER_INPUT_DEVICE_PAD_SOURCE_UNKNOWN,
  CLUTTER_INPUT_DEVICE_PAD_SOURCE_FINGER,
} ClutterInputDevicePadSource;

typedef enum
{
  CLUTTER_PAD_FEATURE_BUTTON,
  CLUTTER_PAD_FEATURE_RING,
  CLUTTER_PAD_FEATURE_STRIP,
} ClutterInputDevicePadFeature;

typedef enum
{
  CLUTTER_INPUT_CONTENT_HINT_COMPLETION          = 1 << 0,
  CLUTTER_INPUT_CONTENT_HINT_SPELLCHECK          = 1 << 1,
  CLUTTER_INPUT_CONTENT_HINT_AUTO_CAPITALIZATION = 1 << 2,
  CLUTTER_INPUT_CONTENT_HINT_LOWERCASE           = 1 << 3,
  CLUTTER_INPUT_CONTENT_HINT_UPPERCASE           = 1 << 4,
  CLUTTER_INPUT_CONTENT_HINT_TITLECASE           = 1 << 5,
  CLUTTER_INPUT_CONTENT_HINT_HIDDEN_TEXT         = 1 << 6,
  CLUTTER_INPUT_CONTENT_HINT_SENSITIVE_DATA      = 1 << 7,
  CLUTTER_INPUT_CONTENT_HINT_LATIN               = 1 << 8,
  CLUTTER_INPUT_CONTENT_HINT_MULTILINE           = 1 << 9,
} ClutterInputContentHintFlags;

typedef enum
{
  CLUTTER_INPUT_CONTENT_PURPOSE_NORMAL,
  CLUTTER_INPUT_CONTENT_PURPOSE_ALPHA,
  CLUTTER_INPUT_CONTENT_PURPOSE_DIGITS,
  CLUTTER_INPUT_CONTENT_PURPOSE_NUMBER,
  CLUTTER_INPUT_CONTENT_PURPOSE_PHONE,
  CLUTTER_INPUT_CONTENT_PURPOSE_URL,
  CLUTTER_INPUT_CONTENT_PURPOSE_EMAIL,
  CLUTTER_INPUT_CONTENT_PURPOSE_NAME,
  CLUTTER_INPUT_CONTENT_PURPOSE_PASSWORD,
  CLUTTER_INPUT_CONTENT_PURPOSE_DATE,
  CLUTTER_INPUT_CONTENT_PURPOSE_TIME,
  CLUTTER_INPUT_CONTENT_PURPOSE_DATETIME,
  CLUTTER_INPUT_CONTENT_PURPOSE_TERMINAL,
} ClutterInputContentPurpose;

typedef enum
{
  CLUTTER_INPUT_PANEL_STATE_OFF,
  CLUTTER_INPUT_PANEL_STATE_ON,
  CLUTTER_INPUT_PANEL_STATE_TOGGLE,
} ClutterInputPanelState;

typedef enum
{
  CLUTTER_PREEDIT_RESET_CLEAR,
  CLUTTER_PREEDIT_RESET_COMMIT,
} ClutterPreeditResetMode;

typedef enum
{
  CLUTTER_PHASE_CAPTURE,
  CLUTTER_PHASE_BUBBLE,
} ClutterEventPhase;

typedef enum
{
  CLUTTER_GRAB_STATE_NONE = 0,
  CLUTTER_GRAB_STATE_POINTER = 1 << 0,
  CLUTTER_GRAB_STATE_KEYBOARD = 1 << 1,
  CLUTTER_GRAB_STATE_ALL = (CLUTTER_GRAB_STATE_POINTER |
                            CLUTTER_GRAB_STATE_KEYBOARD),
} ClutterGrabState;

G_END_DECLS
