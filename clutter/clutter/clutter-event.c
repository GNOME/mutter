/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *
 * Copyright (C) 2006 OpenedHand
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
 *
 */

#include "config.h"

#include "clutter/clutter-backend-private.h"
#include "clutter/clutter-context-private.h"
#include "clutter/clutter-debug.h"
#include "clutter/clutter-event-private.h"
#include "clutter/clutter-keysyms.h"
#include "clutter/clutter-input-device-tool.h"
#include "clutter/clutter-private.h"

#include <math.h>

struct _ClutterAnyEvent
{
  ClutterEventType type;
  int64_t time_us;
  ClutterEventFlags flags;
  ClutterInputDevice *device;
  ClutterInputDevice *source_device;
};

struct _ClutterKeyEvent
{
  ClutterEventType type;
  int64_t time_us;
  ClutterEventFlags flags;
  ClutterInputDevice *device;
  ClutterInputDevice *source_device;

  ClutterModifierSet raw_modifiers;
  ClutterModifierType modifier_state;
  uint32_t keyval;
  uint16_t hardware_keycode;
  gunichar unicode_value;
  uint32_t evdev_code;
};

struct _ClutterButtonEvent
{
  ClutterEventType type;
  int64_t time_us;
  ClutterEventFlags flags;
  ClutterInputDevice *device;
  ClutterInputDevice *source_device;

  float x;
  float y;
  ClutterModifierType modifier_state;
  uint32_t button;
  double *axes;
  ClutterInputDeviceTool *tool;
  uint32_t evdev_code;
};

struct _ClutterProximityEvent
{
  ClutterEventType type;
  int64_t time_us;
  ClutterEventFlags flags;
  ClutterInputDevice *device;
  ClutterInputDevice *source_device;
  ClutterInputDeviceTool *tool;
};

struct _ClutterCrossingEvent
{
  ClutterEventType type;
  int64_t time_us;
  ClutterEventFlags flags;
  ClutterInputDevice *device;
  ClutterInputDevice *source_device;

  float x;
  float y;
  ClutterEventSequence *sequence;
  ClutterActor *source;
  ClutterActor *related;
};

struct _ClutterMotionEvent
{
  ClutterEventType type;
  int64_t time_us;
  ClutterEventFlags flags;
  ClutterInputDevice *device;
  ClutterInputDevice *source_device;

  float x;
  float y;
  ClutterModifierType modifier_state;
  double *axes;
  ClutterInputDeviceTool *tool;

  double dx;
  double dy;
  double dx_unaccel;
  double dy_unaccel;
  double dx_constrained;
  double dy_constrained;
};

struct _ClutterScrollEvent
{
  ClutterEventType type;
  int64_t time_us;
  ClutterEventFlags flags;
  ClutterInputDevice *device;
  ClutterInputDevice *source_device;

  float x;
  float y;
  double delta_x;
  double delta_y;
  ClutterScrollDirection direction;
  ClutterModifierType modifier_state;
  double *axes;
  ClutterInputDeviceTool *tool;
  ClutterScrollSource scroll_source;
  ClutterScrollFinishFlags finish_flags;
};

struct _ClutterTouchEvent
{
  ClutterEventType type;
  int64_t time_us;
  ClutterEventFlags flags;
  ClutterInputDevice *device;
  ClutterInputDevice *source_device;

  float x;
  float y;
  ClutterEventSequence *sequence;
  ClutterModifierType modifier_state;
  double *axes; /* reserved */
};

struct _ClutterTouchpadPinchEvent
{
  ClutterEventType type;
  int64_t time_us;
  ClutterEventFlags flags;
  ClutterInputDevice *device;
  ClutterInputDevice *source_device;

  ClutterTouchpadGesturePhase phase;
  float x;
  float y;
  float dx;
  float dy;
  float dx_unaccel;
  float dy_unaccel;
  float angle_delta;
  float scale;
  uint32_t n_fingers;
};

struct _ClutterTouchpadSwipeEvent
{
  ClutterEventType type;
  int64_t time_us;
  ClutterEventFlags flags;
  ClutterInputDevice *device;
  ClutterInputDevice *source_device;

  ClutterTouchpadGesturePhase phase;
  uint32_t n_fingers;
  float x;
  float y;
  float dx;
  float dy;
  float dx_unaccel;
  float dy_unaccel;
};

struct _ClutterTouchpadHoldEvent
{
  ClutterEventType type;
  int64_t time_us;
  ClutterEventFlags flags;
  ClutterInputDevice *device;
  ClutterInputDevice *source_device;

  ClutterTouchpadGesturePhase phase;
  uint32_t n_fingers;
  float x;
  float y;
};

struct _ClutterPadButtonEvent
{
  ClutterEventType type;
  int64_t time_us;
  ClutterEventFlags flags;
  ClutterInputDevice *device;
  ClutterInputDevice *source_device;

  uint32_t button;
  uint32_t group;
  uint32_t mode;
};

struct _ClutterPadStripEvent
{
  ClutterEventType type;
  int64_t time_us;
  ClutterEventFlags flags;
  ClutterInputDevice *device;
  ClutterInputDevice *source_device;

  ClutterInputDevicePadSource strip_source;
  uint32_t strip_number;
  uint32_t group;
  double value;
  uint32_t mode;
};

struct _ClutterPadRingEvent
{
  ClutterEventType type;
  int64_t time_us;
  ClutterEventFlags flags;
  ClutterInputDevice *device;
  ClutterInputDevice *source_device;

  ClutterInputDevicePadSource ring_source;
  uint32_t ring_number;
  uint32_t group;
  double angle;
  uint32_t mode;
};

struct _ClutterDeviceEvent
{
  ClutterEventType type;
  int64_t time_us;
  ClutterEventFlags flags;
  ClutterInputDevice *device;
  ClutterInputDevice *source_device;
};

struct _ClutterIMEvent
{
  ClutterEventType type;
  int64_t time_us;
  ClutterEventFlags flags;
  ClutterInputDevice *device;
  ClutterInputDevice *source_device;

  char *text;
  int32_t offset;
  int32_t anchor;
  uint32_t len;
  ClutterPreeditResetMode mode;
};

union _ClutterEvent
{
  /*< private >*/
  ClutterEventType type;

  ClutterAnyEvent any;
  ClutterButtonEvent button;
  ClutterKeyEvent key;
  ClutterMotionEvent motion;
  ClutterScrollEvent scroll;
  ClutterCrossingEvent crossing;
  ClutterTouchEvent touch;
  ClutterTouchpadPinchEvent touchpad_pinch;
  ClutterTouchpadSwipeEvent touchpad_swipe;
  ClutterTouchpadHoldEvent touchpad_hold;
  ClutterProximityEvent proximity;
  ClutterPadButtonEvent pad_button;
  ClutterPadStripEvent pad_strip;
  ClutterPadRingEvent pad_ring;
  ClutterDeviceEvent device;
  ClutterIMEvent im;
};

typedef struct _ClutterEventFilter {
  int id;

  ClutterStage *stage;
  ClutterEventFilterFunc func;
  GDestroyNotify notify;
  gpointer user_data;
} ClutterEventFilter;

G_DEFINE_BOXED_TYPE (ClutterEvent, clutter_event,
                     clutter_event_copy,
                     clutter_event_free);

static ClutterEventSequence *
clutter_event_sequence_copy (ClutterEventSequence *sequence)
{
  /* Nothing to copy here */
  return sequence;
}

static void
clutter_event_sequence_free (ClutterEventSequence *sequence)
{
  /* Nothing to free here */
}

G_DEFINE_BOXED_TYPE (ClutterEventSequence, clutter_event_sequence,
                     clutter_event_sequence_copy,
                     clutter_event_sequence_free);

/**
 * clutter_event_type:
 * @event: a #ClutterEvent
 *
 * Retrieves the type of the event.
 *
 * Return value: a #ClutterEventType
 */
ClutterEventType
clutter_event_type (const ClutterEvent *event)
{
  g_return_val_if_fail (event != NULL, CLUTTER_NOTHING);

  return event->type;
}

/**
 * clutter_event_get_time:
 * @event: a #ClutterEvent
 *
 * Retrieves the time of the event.
 *
 * Return value: the time of the event, or %CLUTTER_CURRENT_TIME
 */
guint32
clutter_event_get_time (const ClutterEvent *event)
{
  g_return_val_if_fail (event != NULL, CLUTTER_CURRENT_TIME);

  return us2ms (event->any.time_us);
}

/**
 * clutter_event_get_state:
 * @event: a #ClutterEvent
 *
 * Retrieves the modifier state of the event. In case the window system
 * supports reporting latched and locked modifiers, this function returns
 * the effective state.
 *
 * Return value: the modifier state parameter, or 0
 */
ClutterModifierType
clutter_event_get_state (const ClutterEvent *event)
{
  g_return_val_if_fail (event != NULL, 0);

  switch (event->type)
    {
    case CLUTTER_KEY_PRESS:
    case CLUTTER_KEY_RELEASE:
      return event->key.modifier_state;

    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_BUTTON_RELEASE:
      return event->button.modifier_state;

    case CLUTTER_TOUCH_BEGIN:
    case CLUTTER_TOUCH_UPDATE:
    case CLUTTER_TOUCH_END:
    case CLUTTER_TOUCH_CANCEL:
      return event->touch.modifier_state;

    case CLUTTER_MOTION:
      return event->motion.modifier_state;

    case CLUTTER_SCROLL:
      return event->scroll.modifier_state;

    default:
      break;
    }

  return 0;
}

/**
 * clutter_event_get_coords:
 * @event: a #ClutterEvent
 * @x: (out): return location for the X coordinate, or %NULL
 * @y: (out): return location for the Y coordinate, or %NULL
 *
 * Retrieves the coordinates of @event and puts them into @x and @y.
 */
void
clutter_event_get_coords (const ClutterEvent *event,
                          gfloat             *x,
                          gfloat             *y)
{
  graphene_point_t coords;

  g_return_if_fail (event != NULL);

  clutter_event_get_position (event, &coords);

  if (x != NULL)
    *x = coords.x;

  if (y != NULL)
    *y = coords.y;
}

/**
 * clutter_event_get_position:
 * @event: a #ClutterEvent
 * @position: a #graphene_point_t
 *
 * Retrieves the event coordinates as a #graphene_point_t.
 */
void
clutter_event_get_position (const ClutterEvent *event,
                            graphene_point_t   *position)
{
  g_return_if_fail (event != NULL);
  g_return_if_fail (position != NULL);

  switch (event->type)
    {
    case CLUTTER_NOTHING:
    case CLUTTER_KEY_PRESS:
    case CLUTTER_KEY_RELEASE:
    case CLUTTER_EVENT_LAST:
    case CLUTTER_PROXIMITY_IN:
    case CLUTTER_PROXIMITY_OUT:
    case CLUTTER_PAD_BUTTON_PRESS:
    case CLUTTER_PAD_BUTTON_RELEASE:
    case CLUTTER_PAD_STRIP:
    case CLUTTER_PAD_RING:
    case CLUTTER_DEVICE_ADDED:
    case CLUTTER_DEVICE_REMOVED:
    case CLUTTER_IM_COMMIT:
    case CLUTTER_IM_DELETE:
    case CLUTTER_IM_PREEDIT:
      graphene_point_init (position, 0.f, 0.f);
      break;

    case CLUTTER_ENTER:
    case CLUTTER_LEAVE:
      graphene_point_init (position, event->crossing.x, event->crossing.y);
      break;

    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_BUTTON_RELEASE:
      graphene_point_init (position, event->button.x, event->button.y);
      break;

    case CLUTTER_MOTION:
      graphene_point_init (position, event->motion.x, event->motion.y);
      break;

    case CLUTTER_TOUCH_BEGIN:
    case CLUTTER_TOUCH_UPDATE:
    case CLUTTER_TOUCH_END:
    case CLUTTER_TOUCH_CANCEL:
      graphene_point_init (position, event->touch.x, event->touch.y);
      break;

    case CLUTTER_SCROLL:
      graphene_point_init (position, event->scroll.x, event->scroll.y);
      break;

    case CLUTTER_TOUCHPAD_PINCH:
      graphene_point_init (position, event->touchpad_pinch.x,
                           event->touchpad_pinch.y);
      break;

    case CLUTTER_TOUCHPAD_SWIPE:
      graphene_point_init (position, event->touchpad_swipe.x,
                           event->touchpad_swipe.y);
      break;

    case CLUTTER_TOUCHPAD_HOLD:
      graphene_point_init (position, event->touchpad_hold.x,
                           event->touchpad_hold.y);
      break;
    }

}

/**
 * clutter_event_get_flags:
 * @event: a #ClutterEvent
 *
 * Retrieves the #ClutterEventFlags of @event
 *
 * Return value: the event flags
 */
ClutterEventFlags
clutter_event_get_flags (const ClutterEvent *event)
{
  g_return_val_if_fail (event != NULL, CLUTTER_EVENT_NONE);

  return event->any.flags;
}

/**
 * clutter_event_get_related:
 * @event: a #ClutterEvent of type %CLUTTER_ENTER or of
 *   type %CLUTTER_LEAVE
 *
 * Retrieves the related actor of a crossing event.
 *
 * Return value: (transfer none): the related #ClutterActor, or %NULL
 */
ClutterActor *
clutter_event_get_related (const ClutterEvent *event)
{
  g_return_val_if_fail (event != NULL, NULL);
  g_return_val_if_fail (event->type == CLUTTER_ENTER ||
                        event->type == CLUTTER_LEAVE, NULL);

  return event->crossing.related;
}

/**
 * clutter_event_get_scroll_delta:
 * @event: a #ClutterEvent of type %CLUTTER_SCROLL
 * @dx: (out): return location for the delta on the horizontal axis
 * @dy: (out): return location for the delta on the vertical axis
 *
 * Retrieves the precise scrolling information of @event.
 *
 * The @event has to have a #ClutterScrollEvent.direction value
 * of %CLUTTER_SCROLL_SMOOTH.
 */
void
clutter_event_get_scroll_delta (const ClutterEvent *event,
                                gdouble            *dx,
                                gdouble            *dy)
{
  g_return_if_fail (event != NULL);
  g_return_if_fail (event->type == CLUTTER_SCROLL);
  g_return_if_fail (event->scroll.direction == CLUTTER_SCROLL_SMOOTH);

  if (dx != NULL)
    *dx = event->scroll.delta_x;

  if (dy != NULL)
    *dy = event->scroll.delta_y;
}

/**
 * clutter_event_get_scroll_direction:
 * @event: a #ClutterEvent of type %CLUTTER_SCROLL
 *
 * Retrieves the direction of the scrolling of @event
 *
 * Return value: the scrolling direction
 */
ClutterScrollDirection
clutter_event_get_scroll_direction (const ClutterEvent *event)
{
  g_return_val_if_fail (event != NULL, CLUTTER_SCROLL_UP);
  g_return_val_if_fail (event->type == CLUTTER_SCROLL, CLUTTER_SCROLL_UP);

  return event->scroll.direction;
}

/**
 * clutter_event_get_button:
 * @event: a #ClutterEvent of type %CLUTTER_BUTTON_PRESS or
 *   of type %CLUTTER_BUTTON_RELEASE
 *
 * Retrieves the button number of @event
 *
 * Return value: the button number
 */
guint32
clutter_event_get_button (const ClutterEvent *event)
{
  g_return_val_if_fail (event != NULL, 0);
  g_return_val_if_fail (event->type == CLUTTER_BUTTON_PRESS ||
                        event->type == CLUTTER_BUTTON_RELEASE ||
			event->type == CLUTTER_PAD_BUTTON_PRESS ||
			event->type == CLUTTER_PAD_BUTTON_RELEASE, 0);

  if (event->type == CLUTTER_BUTTON_PRESS ||
      event->type == CLUTTER_BUTTON_RELEASE)
    return event->button.button;
  else
    return event->pad_button.button;
}

/* keys */

/**
 * clutter_event_get_key_symbol:
 * @event: a #ClutterEvent of type %CLUTTER_KEY_PRESS or
 *   of type %CLUTTER_KEY_RELEASE
 *
 * Retrieves the key symbol of @event
 *
 * Return value: the key symbol representing the key
 */
guint
clutter_event_get_key_symbol (const ClutterEvent *event)
{
  g_return_val_if_fail (event != NULL, 0);
  g_return_val_if_fail (event->type == CLUTTER_KEY_PRESS ||
                        event->type == CLUTTER_KEY_RELEASE, 0);

  return event->key.keyval;
}

/**
 * clutter_event_get_key_code:
 * @event: a #ClutterEvent of type %CLUTTER_KEY_PRESS or
 *    of type %CLUTTER_KEY_RELEASE
 *
 * Retrieves the keycode of the key that caused @event
 *
 * Return value: The keycode representing the key
 */
guint16
clutter_event_get_key_code (const ClutterEvent *event)
{
  g_return_val_if_fail (event != NULL, 0);
  g_return_val_if_fail (event->type == CLUTTER_KEY_PRESS ||
                        event->type == CLUTTER_KEY_RELEASE, 0);

  return event->key.hardware_keycode;
}

/**
 * clutter_event_get_key_unicode:
 * @event: a #ClutterEvent of type %CLUTTER_KEY_PRESS
 *   or %CLUTTER_KEY_RELEASE
 *
 * Retrieves the unicode value for the key that caused @keyev.
 *
 * Return value: The unicode value representing the key
 */
gunichar
clutter_event_get_key_unicode (const ClutterEvent *event)
{
  g_return_val_if_fail (event != NULL, 0);
  g_return_val_if_fail (event->type == CLUTTER_KEY_PRESS ||
                        event->type == CLUTTER_KEY_RELEASE, 0);

  if (event->key.unicode_value)
    return event->key.unicode_value;
  else
    return clutter_keysym_to_unicode (event->key.keyval);
}

/**
 * clutter_event_get_key_state:
 * @event: a #ClutterEvent of type %CLUTTER_KEY_PRESS
 *   or %CLUTTER_KEY_RELEASE
 * @pressed: (out): Return location for pressed modifiers
 * @latched: (out): Return location for latched modifiers
 * @locked: (out): Return location for locked modifiers
 *
 * Returns the modifier state decomposed into independent
 * pressed/latched/locked states. The effective state is a
 * composition of these 3 states, see [method@Clutter.Event.get_state].
 **/
void
clutter_event_get_key_state (const ClutterEvent  *event,
                             ClutterModifierType *pressed,
                             ClutterModifierType *latched,
                             ClutterModifierType *locked)
{
  g_return_if_fail (event != NULL);
  g_return_if_fail (event->type == CLUTTER_KEY_PRESS ||
                    event->type == CLUTTER_KEY_RELEASE);

  if (pressed)
    *pressed = event->key.raw_modifiers.pressed;
  if (latched)
    *latched = event->key.raw_modifiers.latched;
  if (locked)
    *locked = event->key.raw_modifiers.locked;
}

/**
 * clutter_event_get_event_sequence:
 * @event: a #ClutterEvent of type %CLUTTER_TOUCH_BEGIN,
 *   %CLUTTER_TOUCH_UPDATE, %CLUTTER_TOUCH_END, or
 *   %CLUTTER_TOUCH_CANCEL
 *
 * Retrieves the #ClutterEventSequence of @event.
 *
 * Return value: (transfer none): the event sequence, or %NULL
 */
ClutterEventSequence *
clutter_event_get_event_sequence (const ClutterEvent *event)
{
  g_return_val_if_fail (event != NULL, NULL);

  if (event->type == CLUTTER_TOUCH_BEGIN ||
      event->type == CLUTTER_TOUCH_UPDATE ||
      event->type == CLUTTER_TOUCH_END ||
      event->type == CLUTTER_TOUCH_CANCEL)
    return event->touch.sequence;
  else if (event->type == CLUTTER_ENTER ||
           event->type == CLUTTER_LEAVE)
    return event->crossing.sequence;

  return NULL;
}

/**
 * clutter_event_get_device_type:
 * @event: a #ClutterEvent
 *
 * Retrieves the type of the device for @event
 *
 * Return value: the #ClutterInputDeviceType for the device, if
 *   any is set
 */
ClutterInputDeviceType
clutter_event_get_device_type (const ClutterEvent *event)
{
  ClutterInputDevice *device = NULL;

  g_return_val_if_fail (event != NULL, CLUTTER_POINTER_DEVICE);

  device = clutter_event_get_device (event);
  if (device != NULL)
    return clutter_input_device_get_device_type (device);

  return CLUTTER_POINTER_DEVICE;
}

/**
 * clutter_event_get_device:
 * @event: a #ClutterEvent
 *
 * Retrieves the #ClutterInputDevice for the event.
 * If you want the physical device the event originated from, use
 * [method@Clutter.Event.get_source_device].
 *
 * The #ClutterInputDevice structure is completely opaque and should
 * be cast to the platform-specific implementation.
 *
 * Return value: (transfer none): the #ClutterInputDevice or %NULL. The
 *   returned device is owned by the #ClutterEvent and it should not
 *   be unreferenced
 */
ClutterInputDevice *
clutter_event_get_device (const ClutterEvent *event)
{
  g_return_val_if_fail (event != NULL, NULL);

  return event->any.device;
}

/**
 * clutter_event_get_device_tool:
 * @event: a #ClutterEvent
 *
 * Returns the device tool that originated this event
 *
 * Returns: (transfer none): The tool of this event8
 **/
ClutterInputDeviceTool *
clutter_event_get_device_tool (const ClutterEvent *event)
{
  g_return_val_if_fail (event != NULL, NULL);

  switch (event->any.type)
    {
    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_BUTTON_RELEASE:
      return event->button.tool;
    case CLUTTER_MOTION:
      return event->motion.tool;
    case CLUTTER_SCROLL:
      return event->scroll.tool;
    case CLUTTER_PROXIMITY_IN:
    case CLUTTER_PROXIMITY_OUT:
      return event->proximity.tool;
    default:
      return NULL;
    }
}

static ClutterEvent *
clutter_event_new (ClutterEventType type)
{
  ClutterEvent *new_event;

  new_event = g_new0 (ClutterEvent, 1);
  new_event->any.type = type;

  return new_event;
}

/**
 * clutter_event_copy:
 * @event: A #ClutterEvent.
 *
 * Copies @event.
 *
 * Return value: (transfer full): A newly allocated #ClutterEvent
 */
ClutterEvent *
clutter_event_copy (const ClutterEvent *event)
{
  ClutterEvent *new_event;

  g_return_val_if_fail (event != NULL, NULL);

  new_event = clutter_event_new (CLUTTER_NOTHING);

  g_set_object (&new_event->any.device, event->any.device);
  g_set_object (&new_event->any.source_device, event->any.source_device);
  *new_event = *event;

  switch (event->type)
    {
    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_BUTTON_RELEASE:
      if (event->button.axes != NULL)
        {
          new_event->button.axes =
            g_memdup2 (event->button.axes,
                       sizeof (double) * CLUTTER_INPUT_AXIS_LAST);
        }
      break;

    case CLUTTER_SCROLL:
      if (event->scroll.axes != NULL)
        {
          new_event->scroll.axes =
            g_memdup2 (event->scroll.axes,
                       sizeof (double) * CLUTTER_INPUT_AXIS_LAST);
        }
      break;

    case CLUTTER_MOTION:
      if (event->motion.axes != NULL)
        {
          new_event->motion.axes =
            g_memdup2 (event->motion.axes,
                       sizeof (double) * CLUTTER_INPUT_AXIS_LAST);
        }
      break;

    case CLUTTER_TOUCH_BEGIN:
    case CLUTTER_TOUCH_UPDATE:
    case CLUTTER_TOUCH_END:
    case CLUTTER_TOUCH_CANCEL:
      if (event->touch.axes != NULL)
        {
          new_event->touch.axes =
            g_memdup2 (event->touch.axes,
                      sizeof (double) * CLUTTER_INPUT_AXIS_LAST);
        }
      break;

    case CLUTTER_IM_COMMIT:
    case CLUTTER_IM_PREEDIT:
      new_event->im.text = g_strdup (event->im.text);
      break;

    default:
      break;
    }

  return new_event;
}

/**
 * clutter_event_free:
 * @event: A #ClutterEvent.
 *
 * Frees all resources used by @event.
 */
void
clutter_event_free (ClutterEvent *event)
{
  if (G_LIKELY (event != NULL))
    {
      g_clear_object (&event->any.device);
      g_clear_object (&event->any.source_device);

      switch (event->type)
        {
        case CLUTTER_BUTTON_PRESS:
        case CLUTTER_BUTTON_RELEASE:
          g_free (event->button.axes);
          break;

        case CLUTTER_MOTION:
          g_free (event->motion.axes);
          break;

        case CLUTTER_SCROLL:
          g_free (event->scroll.axes);
          break;

        case CLUTTER_TOUCH_BEGIN:
        case CLUTTER_TOUCH_UPDATE:
        case CLUTTER_TOUCH_END:
        case CLUTTER_TOUCH_CANCEL:
          g_free (event->touch.axes);
          break;

        case CLUTTER_IM_COMMIT:
        case CLUTTER_IM_PREEDIT:
          g_free (event->im.text);
          break;

        default:
          break;
        }

      g_free (event);
    }
}

/**
 * clutter_event_get:
 *
 * Pops an event off the event queue. Applications should not need to call 
 * this.
 *
 * Return value: A #ClutterEvent or NULL if queue empty
 */
ClutterEvent *
clutter_event_get (void)
{
  ClutterContext *context = _clutter_context_get_default ();
  ClutterEvent *event;

  event = g_async_queue_try_pop (context->events_queue);

  return event;
}

void
_clutter_event_push (const ClutterEvent *event,
                     gboolean            do_copy)
{
  ClutterContext *context = _clutter_context_get_default ();

  g_assert (context != NULL);

  if (do_copy)
    {
      ClutterEvent *copy;

      copy = clutter_event_copy (event);
      event = copy;
    }

  g_async_queue_push (context->events_queue, (gpointer) event);
  g_main_context_wakeup (NULL);
}

/**
 * clutter_event_put:
 * @event: a #ClutterEvent
 *
 * Puts a copy of the event on the back of the event queue. The event will
 * have the %CLUTTER_EVENT_FLAG_SYNTHETIC flag set. If the source is set
 * event signals will be emitted for this source and capture/bubbling for
 * its ancestors. If the source is not set it will be generated by picking
 * or use the actor that currently has keyboard focus
 */
void
clutter_event_put (const ClutterEvent *event)
{
  _clutter_event_push (event, TRUE);
}

/**
 * clutter_events_pending:
 *
 * Checks if events are pending in the event queue.
 *
 * Return value: TRUE if there are pending events, FALSE otherwise.
 */
gboolean
clutter_events_pending (void)
{
  ClutterContext *context = _clutter_context_get_default ();

  g_return_val_if_fail (context != NULL, FALSE);

  return g_async_queue_length (context->events_queue) > 0;
}

/**
 * clutter_get_current_event_time:
 *
 * Retrieves the timestamp of the last event, if there is an
 * event or if the event has a timestamp.
 *
 * Return value: the event timestamp, or %CLUTTER_CURRENT_TIME
 */
guint32
clutter_get_current_event_time (void)
{
  const ClutterEvent* event;

  event = clutter_get_current_event ();

  if (event != NULL)
    return clutter_event_get_time (event);

  return CLUTTER_CURRENT_TIME;
}

/**
 * clutter_get_current_event:
 *
 * If an event is currently being processed, return that event.
 * This function is intended to be used to access event state
 * that might not be exposed by higher-level widgets.  For
 * example, to get the key modifier state from a Button 'clicked'
 * event.
 *
 * Return value: (transfer none): The current ClutterEvent, or %NULL if none
 */
const ClutterEvent *
clutter_get_current_event (void)
{
  ClutterContext *context = _clutter_context_get_default ();

  g_return_val_if_fail (context != NULL, NULL);

  return context->current_event != NULL ? context->current_event->data : NULL;
}

/**
 * clutter_event_get_source_device:
 * @event: a #ClutterEvent
 *
 * Retrieves the hardware device that originated the event.
 *
 * If you need the virtual device, use [method@Clutter.Event.get_device].
 *
 * If no hardware device originated this event, this function will
 * return the same device as [method@Clutter.Event.get_device].
 *
 * Return value: (transfer none): a pointer to a #ClutterInputDevice
 *   or %NULL
 */
ClutterInputDevice *
clutter_event_get_source_device (const ClutterEvent *event)
{
  g_return_val_if_fail (event != NULL, NULL);

  if (event->any.source_device)
    return event->any.source_device;
  else
    return clutter_event_get_device (event);
}

/**
 * clutter_event_get_axes:
 * @event: a #ClutterEvent
 * @n_axes: (out): return location for the number of axes returned
 *
 * Retrieves the array of axes values attached to the event.
 *
 * Return value: (transfer none): an array of axis values
 */
gdouble *
clutter_event_get_axes (const ClutterEvent *event,
                        guint              *n_axes)
{
  gdouble *retval = NULL;

  switch (event->type)
    {
    case CLUTTER_NOTHING:
    case CLUTTER_ENTER:
    case CLUTTER_LEAVE:
    case CLUTTER_KEY_PRESS:
    case CLUTTER_KEY_RELEASE:
    case CLUTTER_EVENT_LAST:
    case CLUTTER_PROXIMITY_IN:
    case CLUTTER_PROXIMITY_OUT:
    case CLUTTER_DEVICE_ADDED:
    case CLUTTER_DEVICE_REMOVED:
      break;

    case CLUTTER_SCROLL:
      retval = event->scroll.axes;
      break;

    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_BUTTON_RELEASE:
      retval = event->button.axes;
      break;

    case CLUTTER_TOUCH_BEGIN:
    case CLUTTER_TOUCH_UPDATE:
    case CLUTTER_TOUCH_END:
    case CLUTTER_TOUCH_CANCEL:
      retval = event->touch.axes;
      break;

    case CLUTTER_MOTION:
      retval = event->motion.axes;
      break;

    case CLUTTER_TOUCHPAD_PINCH:
    case CLUTTER_TOUCHPAD_SWIPE:
    case CLUTTER_TOUCHPAD_HOLD:
    case CLUTTER_PAD_BUTTON_PRESS:
    case CLUTTER_PAD_BUTTON_RELEASE:
    case CLUTTER_PAD_STRIP:
    case CLUTTER_PAD_RING:
    case CLUTTER_IM_COMMIT:
    case CLUTTER_IM_DELETE:
    case CLUTTER_IM_PREEDIT:
      break;
    }

  if (n_axes)
    *n_axes = CLUTTER_INPUT_AXIS_LAST;

  return retval;
}

/**
 * clutter_event_get_distance:
 * @source: a #ClutterEvent
 * @target: a #ClutterEvent
 *
 * Retrieves the distance between two events, a @source and a @target.
 *
 * Return value: the distance between two #ClutterEvent
 */
float
clutter_event_get_distance (const ClutterEvent *source,
                            const ClutterEvent *target)
{
  graphene_point_t p0, p1;

  clutter_event_get_position (source, &p0);
  clutter_event_get_position (source, &p1);

  return graphene_point_distance (&p0, &p1, NULL, NULL);
}

/**
 * clutter_event_get_angle:
 * @source: a #ClutterEvent
 * @target: a #ClutterEvent
 *
 * Retrieves the angle relative from @source to @target.
 *
 * The direction of the angle is from the position X axis towards
 * the positive Y axis.
 *
 * Return value: the angle between two #ClutterEvent
 */
double
clutter_event_get_angle (const ClutterEvent *source,
                         const ClutterEvent *target)
{
  graphene_point_t p0, p1;
  float x_distance, y_distance;
  double angle;

  clutter_event_get_position (source, &p0);
  clutter_event_get_position (target, &p1);

  if (graphene_point_equal (&p0, &p1))
    return 0;

  graphene_point_distance (&p0, &p1, &x_distance, &y_distance);

  angle = atan2 (x_distance, y_distance);

  /* invert the angle, and shift it by 90 degrees */
  angle = (2.0 * G_PI) - angle;
  angle += G_PI / 2.0;

  /* keep the angle within the [ 0, 360 ] interval */
  angle = fmod (angle, 2.0 * G_PI);

  return angle;
}

/**
 * clutter_event_has_shift_modifier:
 * @event: a #ClutterEvent
 *
 * Checks whether @event has the Shift modifier mask set.
 *
 * Return value: %TRUE if the event has the Shift modifier mask set
 */
gboolean
clutter_event_has_shift_modifier (const ClutterEvent *event)
{
  return (clutter_event_get_state (event) & CLUTTER_SHIFT_MASK) != FALSE;
}

/**
 * clutter_event_has_control_modifier:
 * @event: a #ClutterEvent
 *
 * Checks whether @event has the Control modifier mask set.
 *
 * Return value: %TRUE if the event has the Control modifier mask set
 */
gboolean
clutter_event_has_control_modifier (const ClutterEvent *event)
{
  return (clutter_event_get_state (event) & CLUTTER_CONTROL_MASK) != FALSE;
}

/**
 * clutter_event_is_pointer_emulated:
 * @event: a #ClutterEvent
 *
 * Checks whether a pointer @event has been generated by the windowing
 * system. The returned value can be used to distinguish between events
 * synthesized by the windowing system itself (as opposed by Clutter).
 *
 * Return value: %TRUE if the event is pointer emulated
 */
gboolean
clutter_event_is_pointer_emulated (const ClutterEvent *event)
{
  g_return_val_if_fail (event != NULL, FALSE);

  return !!(event->any.flags & CLUTTER_EVENT_FLAG_POINTER_EMULATED);
}

gboolean
_clutter_event_process_filters (ClutterEvent *event,
                                ClutterActor *event_actor)
{
  ClutterContext *context = _clutter_context_get_default ();
  GList *l, *next;

  /* Event filters are handled in order from least recently added to
   * most recently added */

  for (l = context->event_filters; l; l = next)
    {
      ClutterEventFilter *event_filter = l->data;

      next = l->next;

      if (event_filter->stage &&
          event_filter->stage != CLUTTER_STAGE (clutter_actor_get_stage (event_actor)))
        continue;

      if (event_filter->func (event, event_actor, event_filter->user_data) == CLUTTER_EVENT_STOP)
        return CLUTTER_EVENT_STOP;
    }

  return CLUTTER_EVENT_PROPAGATE;
}

/**
 * clutter_event_add_filter:
 * @stage: (allow-none): The #ClutterStage to capture events for
 * @func: The callback function which will be passed all events.
 * @notify: A #GDestroyNotify
 * @user_data: A data pointer to pass to the function.
 *
 * Adds a function which will be called for all events that Clutter
 * processes. The function will be called before any signals are
 * emitted for the event and it will take precedence over any grabs.
 *
 * Return value: an identifier for the event filter, to be used
 *   with [func@Clutter.Event.remove_filter].
 */
guint
clutter_event_add_filter (ClutterStage          *stage,
                          ClutterEventFilterFunc func,
                          GDestroyNotify         notify,
                          gpointer               user_data)
{
  ClutterContext *context = _clutter_context_get_default ();
  ClutterEventFilter *event_filter = g_new0 (ClutterEventFilter, 1);
  static guint event_filter_id = 0;

  event_filter->stage = stage;
  event_filter->id = ++event_filter_id;
  event_filter->func = func;
  event_filter->notify = notify;
  event_filter->user_data = user_data;

  /* The event filters are kept in order from least recently added to
   * most recently added so we must add it to the end */
  context->event_filters = g_list_append (context->event_filters, event_filter);

  return event_filter->id;
}

/**
 * clutter_event_remove_filter:
 * @id: The ID of the event filter, as returned from [func@Clutter.Event.add_filter]
 *
 * Removes an event filter that was previously added with
 * [func@Clutter.Event.add_filter].
 */
void
clutter_event_remove_filter (guint id)
{
  ClutterContext *context = _clutter_context_get_default ();
  GList *l;

  for (l = context->event_filters; l; l = l->next)
    {
      ClutterEventFilter *event_filter = l->data;

      if (event_filter->id == id)
        {
          if (event_filter->notify)
            event_filter->notify (event_filter->user_data);

          context->event_filters = g_list_delete_link (context->event_filters, l);
          g_free (event_filter);
          return;
        }
    }

  g_warning ("No event filter found for id: %d\n", id);
}

/**
 * clutter_event_get_touchpad_gesture_finger_count:
 * @event: a touchpad swipe/pinch event
 *
 * Returns the number of fingers that is triggering the touchpad gesture.
 *
 * Returns: the number of fingers in the gesture.4
 **/
guint
clutter_event_get_touchpad_gesture_finger_count (const ClutterEvent *event)
{
  g_return_val_if_fail (event != NULL, 0);
  g_return_val_if_fail (event->type == CLUTTER_TOUCHPAD_SWIPE ||
                        event->type == CLUTTER_TOUCHPAD_PINCH ||
                        event->type == CLUTTER_TOUCHPAD_HOLD, 0);

  if (event->type == CLUTTER_TOUCHPAD_SWIPE)
    return event->touchpad_swipe.n_fingers;
  else if (event->type == CLUTTER_TOUCHPAD_PINCH)
    return event->touchpad_pinch.n_fingers;
  else if (event->type == CLUTTER_TOUCHPAD_HOLD)
    return event->touchpad_hold.n_fingers;

  return 0;
}

/**
 * clutter_event_get_gesture_pinch_angle_delta:
 * @event: a touchpad pinch event
 *
 * Returns the angle delta reported by this specific event.
 *
 * Returns: The angle delta relative to the previous event.4
 **/
gdouble
clutter_event_get_gesture_pinch_angle_delta (const ClutterEvent *event)
{
  g_return_val_if_fail (event != NULL, 0);
  g_return_val_if_fail (event->type == CLUTTER_TOUCHPAD_PINCH, 0);

  return event->touchpad_pinch.angle_delta;
}

/**
 * clutter_event_get_gesture_pinch_scale:
 * @event: a touchpad pinch event
 *
 * Returns the current scale as reported by @event, 1.0 being the original
 * distance at the time the corresponding event with phase
 * %CLUTTER_TOUCHPAD_GESTURE_PHASE_BEGIN is received.
 * is received.
 *
 * Returns: the current pinch gesture scale4
 **/
gdouble
clutter_event_get_gesture_pinch_scale (const ClutterEvent *event)
{
  g_return_val_if_fail (event != NULL, 0);
  g_return_val_if_fail (event->type == CLUTTER_TOUCHPAD_PINCH, 0);

  return event->touchpad_pinch.scale;
}

/**
 * clutter_event_get_gesture_phase:
 * @event: a touchpad gesture event
 *
 * Returns the phase of the event, See #ClutterTouchpadGesturePhase.
 *
 * Returns: the phase of the gesture event.
 **/
ClutterTouchpadGesturePhase
clutter_event_get_gesture_phase (const ClutterEvent *event)
{
  g_return_val_if_fail (event != NULL, 0);
  g_return_val_if_fail (event->type == CLUTTER_TOUCHPAD_PINCH ||
                        event->type == CLUTTER_TOUCHPAD_SWIPE ||
                        event->type == CLUTTER_TOUCHPAD_HOLD, 0);

  if (event->type == CLUTTER_TOUCHPAD_PINCH)
    return event->touchpad_pinch.phase;
  else if (event->type == CLUTTER_TOUCHPAD_SWIPE)
    return event->touchpad_swipe.phase;
  else if (event->type == CLUTTER_TOUCHPAD_HOLD)
    return event->touchpad_hold.phase;

  /* Shouldn't ever happen */
  return CLUTTER_TOUCHPAD_GESTURE_PHASE_BEGIN;
};

/**
 * clutter_event_get_gesture_motion_delta:
 * @event: A clutter touchpad gesture event
 * @dx: (out) (allow-none): the displacement relative to the pointer
 *      position in the X axis, or %NULL
 * @dy: (out) (allow-none): the displacement relative to the pointer
 *      position in the Y axis, or %NULL
 *
 * Returns the gesture motion deltas relative to the current pointer
 * position.4
 **/
void
clutter_event_get_gesture_motion_delta (const ClutterEvent *event,
                                        gdouble            *dx,
                                        gdouble            *dy)
{
  g_return_if_fail (event != NULL);
  g_return_if_fail (event->type == CLUTTER_TOUCHPAD_PINCH ||
                    event->type == CLUTTER_TOUCHPAD_SWIPE ||
                    event->type == CLUTTER_TOUCHPAD_HOLD);

  if (event->type == CLUTTER_TOUCHPAD_PINCH)
    {
      if (dx)
        *dx = event->touchpad_pinch.dx;
      if (dy)
        *dy = event->touchpad_pinch.dy;
    }
  else if (event->type == CLUTTER_TOUCHPAD_SWIPE)
    {
      if (dx)
        *dx = event->touchpad_swipe.dx;
      if (dy)
        *dy = event->touchpad_swipe.dy;
    }
  else if (event->type == CLUTTER_TOUCHPAD_HOLD)
    {
      if (dx)
        *dx = 0;
      if (dy)
        *dy = 0;
    }
}

/**
 * clutter_event_get_gesture_motion_delta_unaccelerated:
 * @event: A clutter touchpad gesture event
 * @dx: (out) (allow-none): the displacement relative to the pointer
 *      position in the X axis, or %NULL
 * @dy: (out) (allow-none): the displacement relative to the pointer
 *      position in the Y axis, or %NULL
 *
 * Returns the unaccelerated gesture motion deltas relative to the current
 * pointer position. Unlike [method@Clutter.Event.get_gesture_motion_delta],
 * pointer acceleration is ignored.
 **/
void
clutter_event_get_gesture_motion_delta_unaccelerated (const ClutterEvent *event,
                                                      gdouble            *dx,
                                                      gdouble            *dy)
{
  g_return_if_fail (event != NULL);
  g_return_if_fail (event->type == CLUTTER_TOUCHPAD_PINCH ||
                    event->type == CLUTTER_TOUCHPAD_SWIPE ||
                    event->type == CLUTTER_TOUCHPAD_HOLD);

  if (event->type == CLUTTER_TOUCHPAD_PINCH)
    {
      if (dx)
        *dx = event->touchpad_pinch.dx_unaccel;
      if (dy)
        *dy = event->touchpad_pinch.dy_unaccel;
    }
  else if (event->type == CLUTTER_TOUCHPAD_SWIPE)
    {
      if (dx)
        *dx = event->touchpad_swipe.dx_unaccel;
      if (dy)
        *dy = event->touchpad_swipe.dy_unaccel;
    }
  else if (event->type == CLUTTER_TOUCHPAD_HOLD)
    {
      if (dx)
        *dx = 0;
      if (dy)
        *dy = 0;
    }
}
/**
 * clutter_event_get_scroll_source:
 * @event: an scroll event
 *
 * Returns the #ClutterScrollSource that applies to an scroll event.
 *
 * Returns: The source of scroll events6
 **/
ClutterScrollSource
clutter_event_get_scroll_source (const ClutterEvent *event)
{
  g_return_val_if_fail (event != NULL, CLUTTER_SCROLL_SOURCE_UNKNOWN);
  g_return_val_if_fail (event->type == CLUTTER_SCROLL,
                        CLUTTER_SCROLL_SOURCE_UNKNOWN);

  return event->scroll.scroll_source;
}

/**
 * clutter_event_get_scroll_finish_flags:
 * @event: an scroll event
 *
 * Returns the #ClutterScrollFinishFlags of an scroll event. Those
 * can be used to determine whether post-scroll effects like kinetic
 * scrolling should be applied.
 *
 * Returns: The scroll finish flags6
 **/
ClutterScrollFinishFlags
clutter_event_get_scroll_finish_flags (const ClutterEvent *event)
{
  g_return_val_if_fail (event != NULL, CLUTTER_SCROLL_FINISHED_NONE);
  g_return_val_if_fail (event->type == CLUTTER_SCROLL,
                        CLUTTER_SCROLL_FINISHED_NONE);

  return event->scroll.finish_flags;
}

guint
clutter_event_get_mode_group (const ClutterEvent *event)
{
  g_return_val_if_fail (event->type == CLUTTER_PAD_BUTTON_PRESS ||
                        event->type == CLUTTER_PAD_BUTTON_RELEASE ||
                        event->type == CLUTTER_PAD_RING ||
                        event->type == CLUTTER_PAD_STRIP, 0);
  switch (event->type)
    {
    case CLUTTER_PAD_BUTTON_PRESS:
    case CLUTTER_PAD_BUTTON_RELEASE:
      return event->pad_button.group;
    case CLUTTER_PAD_RING:
      return event->pad_ring.group;
    case CLUTTER_PAD_STRIP:
      return event->pad_strip.group;
    default:
      return 0;
    }
}

/**
 * clutter_event_get_pad_details:
 * @event: a pad event
 * @number: (out) (optional): ring/strip/button number
 * @mode: (out) (optional): pad mode as per the event
 * @source: (out) (optional): source of the event
 * @value: (out) (optional): event axis value
 *
 * Returns the details of a pad event.
 *
 * Returns: #TRUE if event details could be obtained
 **/
gboolean
clutter_event_get_pad_details (const ClutterEvent          *event,
                               guint                       *number,
                               guint                       *mode,
                               ClutterInputDevicePadSource *source,
                               gdouble                     *value)
{
  ClutterInputDevicePadSource s;
  guint n, m;
  gdouble v;

  g_return_val_if_fail (event != NULL, FALSE);
  g_return_val_if_fail (event->type == CLUTTER_PAD_BUTTON_PRESS ||
                        event->type == CLUTTER_PAD_BUTTON_RELEASE ||
                        event->type == CLUTTER_PAD_RING ||
                        event->type == CLUTTER_PAD_STRIP, FALSE);

  switch (event->type)
    {
    case CLUTTER_PAD_BUTTON_PRESS:
    case CLUTTER_PAD_BUTTON_RELEASE:
      n = event->pad_button.button;
      m = event->pad_button.mode;
      s = CLUTTER_INPUT_DEVICE_PAD_SOURCE_UNKNOWN;
      v = 0.0;
      break;
    case CLUTTER_PAD_RING:
      n = event->pad_ring.ring_number;
      m = event->pad_ring.mode;
      s = event->pad_ring.ring_source;
      v = event->pad_ring.angle;
      break;
    case CLUTTER_PAD_STRIP:
      n = event->pad_strip.strip_number;
      m = event->pad_strip.mode;
      s = event->pad_strip.strip_source;
      v = event->pad_strip.value;
      break;
    default:
      return FALSE;
    }

  if (number)
    *number = n;
  if (mode)
    *mode = m;
  if (source)
    *source = s;
  if (value)
    *value = v;

  return TRUE;
}

uint32_t
clutter_event_get_event_code (const ClutterEvent *event)
{
  if (event->type == CLUTTER_KEY_PRESS ||
      event->type == CLUTTER_KEY_RELEASE)
    return event->key.evdev_code;
  else if (event->type == CLUTTER_BUTTON_PRESS ||
           event->type == CLUTTER_BUTTON_RELEASE)
    return event->button.evdev_code;

  return 0;
}

int32_t
clutter_event_sequence_get_slot (const ClutterEventSequence *sequence)
{
  g_return_val_if_fail (sequence != NULL, -1);

  return GPOINTER_TO_INT (sequence) - 1;
}

int64_t
clutter_event_get_time_us (const ClutterEvent *event)
{
  return event->any.time_us;
}

gboolean
clutter_event_get_relative_motion (const ClutterEvent *event,
                                   double             *dx,
                                   double             *dy,
                                   double             *dx_unaccel,
                                   double             *dy_unaccel,
                                   double             *dx_constrained,
                                   double             *dy_constrained)
{
  if (event->type == CLUTTER_MOTION &&
      event->motion.flags & CLUTTER_EVENT_FLAG_RELATIVE_MOTION)
    {
      if (dx)
        *dx = event->motion.dx;
      if (dy)
        *dy = event->motion.dy;
      if (dx_unaccel)
        *dx_unaccel = event->motion.dx_unaccel;
      if (dy_unaccel)
        *dy_unaccel = event->motion.dy_unaccel;
      if (dx_constrained)
        *dx_constrained = event->motion.dx_constrained;
      if (dy_constrained)
        *dy_constrained = event->motion.dy_constrained;

      return TRUE;
    }
  else
    return FALSE;
}

const char *
clutter_event_get_im_text (const ClutterEvent *event)
{
  g_return_val_if_fail (event != NULL, NULL);
  g_return_val_if_fail (event->type == CLUTTER_IM_COMMIT ||
                        event->type == CLUTTER_IM_PREEDIT, NULL);

  return event->im.text;
}

gboolean
clutter_event_get_im_location (const ClutterEvent  *event,
                               int32_t             *offset,
                               int32_t             *anchor)
{
  g_return_val_if_fail (event != NULL, FALSE);
  g_return_val_if_fail (event->type == CLUTTER_IM_DELETE ||
                        event->type == CLUTTER_IM_PREEDIT, FALSE);

  if (offset)
    *offset = event->im.offset;
  if (anchor)
    *anchor = event->im.anchor;

  return TRUE;
}

uint32_t
clutter_event_get_im_delete_length (const ClutterEvent  *event)
{
  g_return_val_if_fail (event != NULL, 0);
  g_return_val_if_fail (event->type == CLUTTER_IM_DELETE, 0);

  return event->im.len;
}

ClutterPreeditResetMode
clutter_event_get_im_preedit_reset_mode (const ClutterEvent *event)
{
  g_return_val_if_fail (event != NULL, CLUTTER_PREEDIT_RESET_CLEAR);
  g_return_val_if_fail (event->type == CLUTTER_IM_COMMIT ||
                        event->type == CLUTTER_IM_PREEDIT,
                        CLUTTER_PREEDIT_RESET_CLEAR);

  return event->im.mode;
}

const char *
clutter_event_get_name (const ClutterEvent *event)
{
  switch (event->type)
    {
    case CLUTTER_KEY_PRESS:
      return "key-press";
    case CLUTTER_KEY_RELEASE:
      return "key-release";
    case CLUTTER_MOTION:
      return "motion";
    case CLUTTER_ENTER:
      return "enter";
    case CLUTTER_LEAVE:
      return "leave";
    case CLUTTER_BUTTON_PRESS:
      return "button-press";
    case CLUTTER_BUTTON_RELEASE:
      return "button-release";
    case CLUTTER_SCROLL:
      return "scroll";
    case CLUTTER_TOUCH_BEGIN:
      return "touch-begin";
    case CLUTTER_TOUCH_UPDATE:
      return "touch-update";
    case CLUTTER_TOUCH_END:
      return "touch-end";
    case CLUTTER_TOUCH_CANCEL:
      return "touch-cancel";
    case CLUTTER_TOUCHPAD_PINCH:
      return "touchpad-pinch";
    case CLUTTER_TOUCHPAD_SWIPE:
      return "touchpad-swipe";
    case CLUTTER_TOUCHPAD_HOLD:
      return "touchpad-hold";
    case CLUTTER_PROXIMITY_IN:
      return "proximity-in";
    case CLUTTER_PROXIMITY_OUT:
      return "proximity-out";
    case CLUTTER_PAD_BUTTON_PRESS:
      return "pad-button-press";
    case CLUTTER_PAD_BUTTON_RELEASE:
      return "pad-button-release";
    case CLUTTER_PAD_STRIP:
      return "pad-strip";
    case CLUTTER_PAD_RING:
      return "pad-ring";
    case CLUTTER_DEVICE_ADDED:
      return "device-added";
    case CLUTTER_DEVICE_REMOVED:
      return "device-removed";
    case CLUTTER_IM_COMMIT:
      return "im-commit";
    case CLUTTER_IM_DELETE:
      return "im-delete";
    case CLUTTER_IM_PREEDIT:
      return "im-preedit";
    case CLUTTER_NOTHING:
    case CLUTTER_EVENT_LAST:
      break;
    }
  g_assert_not_reached ();
}

ClutterEvent *
clutter_event_key_new (ClutterEventType     type,
                       ClutterEventFlags    flags,
                       int64_t              timestamp_us,
                       ClutterInputDevice  *source_device,
                       ClutterModifierSet   raw_modifiers,
                       ClutterModifierType  modifiers,
                       uint32_t             keyval,
                       uint32_t             evcode,
                       uint32_t             keycode,
                       gunichar             unicode_value)
{
  ClutterEvent *event;
  ClutterSeat *seat;

  g_return_val_if_fail (type == CLUTTER_KEY_PRESS ||
                        type == CLUTTER_KEY_RELEASE, NULL);
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (source_device), NULL);

  seat = clutter_input_device_get_seat (source_device);

  event = clutter_event_new (type);

  event->key.time_us = timestamp_us;
  event->key.flags = flags;
  event->key.raw_modifiers = raw_modifiers;
  event->key.modifier_state = modifiers;
  event->key.keyval = keyval;
  event->key.hardware_keycode = keycode;
  event->key.unicode_value = unicode_value;
  event->key.evdev_code = evcode;
  g_set_object (&event->key.device, clutter_seat_get_keyboard (seat));
  g_set_object (&event->key.source_device, source_device);

  return event;
}

ClutterEvent *
clutter_event_button_new (ClutterEventType        type,
                          ClutterEventFlags       flags,
                          int64_t                 timestamp_us,
                          ClutterInputDevice     *source_device,
                          ClutterInputDeviceTool *tool,
                          ClutterModifierType     modifiers,
                          graphene_point_t        coords,
			  int                     button,
                          uint32_t                evcode,
                          double                 *axes)
{
  ClutterEvent *event;

  g_return_val_if_fail (type == CLUTTER_BUTTON_PRESS ||
                        type == CLUTTER_BUTTON_RELEASE, NULL);
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (source_device), NULL);
  g_return_val_if_fail (!tool || CLUTTER_IS_INPUT_DEVICE_TOOL (tool), NULL);

  event = clutter_event_new (type);

  event->button.time_us = timestamp_us;
  event->button.flags = flags;
  event->button.x = coords.x;
  event->button.y = coords.y;
  event->button.modifier_state = modifiers;
  event->button.button = button;
  event->button.axes = axes;
  event->button.evdev_code = evcode;
  event->button.tool = tool;

  g_set_object (&event->button.source_device, source_device);

  if (clutter_input_device_get_device_mode (source_device) ==
      CLUTTER_INPUT_MODE_FLOATING)
    {
      g_set_object (&event->button.device, source_device);
    }
  else
    {
      ClutterSeat *seat;

      seat = clutter_input_device_get_seat (source_device);
      g_set_object (&event->button.device, clutter_seat_get_pointer (seat));
    }

  return event;
}

ClutterEvent *
clutter_event_motion_new (ClutterEventFlags       flags,
                          int64_t                 timestamp_us,
                          ClutterInputDevice     *source_device,
                          ClutterInputDeviceTool *tool,
                          ClutterModifierType     modifiers,
                          graphene_point_t        coords,
                          graphene_point_t        delta,
                          graphene_point_t        delta_unaccel,
                          graphene_point_t        delta_constrained,
                          double                 *axes)
{
  ClutterEvent *event;

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (source_device), NULL);
  g_return_val_if_fail (!tool || CLUTTER_IS_INPUT_DEVICE_TOOL (tool), NULL);

  event = clutter_event_new (CLUTTER_MOTION);

  event->motion.time_us = timestamp_us;
  event->motion.flags = flags;
  event->motion.x = coords.x;
  event->motion.y = coords.y;
  event->motion.modifier_state = modifiers;
  event->motion.axes = axes;
  event->motion.dx = delta.x;
  event->motion.dy = delta.y;
  event->motion.dx_unaccel = delta_unaccel.x;
  event->motion.dy_unaccel = delta_unaccel.y;
  event->motion.dx_constrained = delta_constrained.x;
  event->motion.dy_constrained = delta_constrained.y;
  event->motion.tool = tool;

  g_set_object (&event->motion.source_device, source_device);

  if (clutter_input_device_get_device_mode (source_device) ==
      CLUTTER_INPUT_MODE_FLOATING)
    {
      g_set_object (&event->motion.device, source_device);
    }
  else
    {
      ClutterSeat *seat;

      seat = clutter_input_device_get_seat (source_device);
      g_set_object (&event->motion.device, clutter_seat_get_pointer (seat));
    }

  return event;
}

ClutterEvent *
clutter_event_scroll_smooth_new (ClutterEventFlags         flags,
                                 int64_t                   timestamp_us,
                                 ClutterInputDevice       *source_device,
                                 ClutterInputDeviceTool   *tool,
                                 ClutterModifierType       modifiers,
                                 graphene_point_t          coords,
                                 graphene_point_t          delta,
                                 ClutterScrollSource       scroll_source,
                                 ClutterScrollFinishFlags  finish_flags)
{
  ClutterEvent *event;

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (source_device), NULL);
  g_return_val_if_fail (!tool || CLUTTER_IS_INPUT_DEVICE_TOOL (tool), NULL);

  event = clutter_event_new (CLUTTER_SCROLL);

  event->scroll.time_us = timestamp_us;
  event->scroll.flags = flags;
  event->scroll.x = coords.x;
  event->scroll.y = coords.y;
  event->scroll.delta_x = delta.x;
  event->scroll.delta_y = delta.y;
  event->scroll.direction = CLUTTER_SCROLL_SMOOTH;
  event->scroll.modifier_state = modifiers;
  event->scroll.scroll_source = scroll_source;
  event->scroll.finish_flags = finish_flags;
  event->scroll.tool = tool;

  g_set_object (&event->scroll.source_device, source_device);

  if (clutter_input_device_get_device_mode (source_device) ==
      CLUTTER_INPUT_MODE_FLOATING)
    {
      g_set_object (&event->scroll.device, source_device);
    }
  else
    {
      ClutterSeat *seat;

      seat = clutter_input_device_get_seat (source_device);
      g_set_object (&event->scroll.device, clutter_seat_get_pointer (seat));
    }

  return event;
}

ClutterEvent *
clutter_event_scroll_discrete_new (ClutterEventFlags       flags,
                                   int64_t                 timestamp_us,
                                   ClutterInputDevice     *source_device,
                                   ClutterInputDeviceTool *tool,
                                   ClutterModifierType     modifiers,
                                   graphene_point_t        coords,
                                   ClutterScrollDirection  direction)
{
  ClutterEvent *event;

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (source_device), NULL);
  g_return_val_if_fail (!tool || CLUTTER_IS_INPUT_DEVICE_TOOL (tool), NULL);

  event = clutter_event_new (CLUTTER_SCROLL);

  event->scroll.time_us = timestamp_us;
  event->scroll.flags = flags;
  event->scroll.x = coords.x;
  event->scroll.y = coords.y;
  event->scroll.direction = direction;
  event->scroll.modifier_state = modifiers;
  event->scroll.tool = tool;

  g_set_object (&event->scroll.source_device, source_device);

  if (clutter_input_device_get_device_mode (source_device) ==
      CLUTTER_INPUT_MODE_FLOATING)
    {
      g_set_object (&event->scroll.device, source_device);
    }
  else
    {
      ClutterSeat *seat;

      seat = clutter_input_device_get_seat (source_device);
      g_set_object (&event->scroll.device, clutter_seat_get_pointer (seat));
    }

  return event;
}

ClutterEvent *
clutter_event_crossing_new (ClutterEventType      type,
                            ClutterEventFlags     flags,
                            int64_t               timestamp_us,
                            ClutterInputDevice   *source_device,
                            ClutterEventSequence *sequence,
                            graphene_point_t      coords,
                            ClutterActor         *source,
                            ClutterActor         *related)
{
  ClutterInputDevice *device;
  ClutterEvent *event;

  g_return_val_if_fail (type == CLUTTER_ENTER ||
                        type == CLUTTER_LEAVE, NULL);
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (source_device), NULL);

  if (clutter_input_device_get_device_mode (source_device) ==
      CLUTTER_INPUT_MODE_FLOATING)
    {
      device = source_device;
    }
  else
    {
      ClutterSeat *seat;

      seat = clutter_input_device_get_seat (source_device);
      device = clutter_seat_get_pointer (seat);
    }

  event = clutter_event_new (type);

  event->crossing.time_us = timestamp_us;
  event->crossing.flags = flags;
  event->crossing.x = coords.x;
  event->crossing.y = coords.y;
  event->crossing.sequence = sequence;
  event->crossing.source = source;
  event->crossing.related = related;
  g_set_object (&event->crossing.device, device);
  g_set_object (&event->crossing.source_device, source_device);

  return event;
}

ClutterEvent *
clutter_event_touch_new (ClutterEventType      type,
                         ClutterEventFlags     flags,
                         int64_t               timestamp_us,
                         ClutterInputDevice   *source_device,
                         ClutterEventSequence *sequence,
                         ClutterModifierType   modifiers,
                         graphene_point_t      coords)
{
  ClutterEvent *event;
  ClutterSeat *seat;

  g_return_val_if_fail (type == CLUTTER_TOUCH_BEGIN ||
                        type == CLUTTER_TOUCH_UPDATE ||
                        type == CLUTTER_TOUCH_END, NULL);
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (source_device), NULL);
  g_return_val_if_fail (sequence != NULL, NULL);

  seat = clutter_input_device_get_seat (source_device);

  event = clutter_event_new (type);

  event->touch.time_us = timestamp_us;
  event->touch.flags = flags;
  event->touch.x = coords.x;
  event->touch.y = coords.y;
  event->touch.modifier_state = modifiers;
  event->touch.sequence = sequence;

  /* This has traditionally been the virtual pointer device */
  g_set_object (&event->touch.device, clutter_seat_get_pointer (seat));
  g_set_object (&event->touch.source_device, source_device);

  return event;
}

ClutterEvent *
clutter_event_touch_cancel_new (ClutterEventFlags     flags,
                                int64_t               timestamp_us,
                                ClutterInputDevice   *source_device,
                                ClutterEventSequence *sequence)
{
  ClutterEvent *event;
  ClutterSeat *seat;

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (source_device), NULL);
  g_return_val_if_fail (sequence != NULL, NULL);

  seat = clutter_input_device_get_seat (source_device);

  event = clutter_event_new (CLUTTER_TOUCH_CANCEL);

  event->touch.time_us = timestamp_us;
  event->touch.flags = flags;
  event->touch.sequence = sequence;

  /* This has traditionally been the virtual pointer device */
  g_set_object (&event->touch.device, clutter_seat_get_pointer (seat));
  g_set_object (&event->touch.source_device, source_device);

  return event;
}

ClutterEvent *
clutter_event_proximity_new (ClutterEventType        type,
                             ClutterEventFlags       flags,
                             int64_t                 timestamp_us,
                             ClutterInputDevice     *source_device,
                             ClutterInputDeviceTool *tool)
{
  ClutterEvent *event;

  g_return_val_if_fail (type == CLUTTER_PROXIMITY_IN ||
                        type == CLUTTER_PROXIMITY_OUT, NULL);
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (source_device), NULL);
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE_TOOL (tool), NULL);

  event = clutter_event_new (type);

  event->proximity.time_us = timestamp_us;
  event->proximity.flags = flags;
  event->proximity.tool = tool;

  g_set_object (&event->proximity.device, source_device);
  g_set_object (&event->proximity.source_device, source_device);

  return event;
}

ClutterEvent *
clutter_event_touchpad_pinch_new (ClutterEventFlags            flags,
                                  int64_t                      timestamp_us,
                                  ClutterInputDevice          *source_device,
                                  ClutterTouchpadGesturePhase  phase,
                                  uint32_t                     fingers,
                                  graphene_point_t             coords,
                                  graphene_point_t             delta,
                                  graphene_point_t             delta_unaccel,
                                  float                        angle,
                                  float                        scale)
{
  ClutterEvent *event;
  ClutterSeat *seat;

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (source_device), NULL);

  seat = clutter_input_device_get_seat (source_device);

  event = clutter_event_new (CLUTTER_TOUCHPAD_PINCH);

  event->touchpad_pinch.time_us = timestamp_us;
  event->touchpad_pinch.flags = flags;
  event->touchpad_pinch.phase = phase;
  event->touchpad_pinch.x = coords.x;
  event->touchpad_pinch.y = coords.y;
  event->touchpad_pinch.dx = delta.x;
  event->touchpad_pinch.dy = delta.y;
  event->touchpad_pinch.dx_unaccel = delta_unaccel.x;
  event->touchpad_pinch.dy_unaccel = delta_unaccel.y;
  event->touchpad_pinch.angle_delta = angle;
  event->touchpad_pinch.scale = scale;
  event->touchpad_pinch.n_fingers = fingers;

  g_set_object (&event->touchpad_pinch.device, clutter_seat_get_pointer (seat));
  g_set_object (&event->touchpad_pinch.source_device, source_device);

  return event;
}

ClutterEvent *
clutter_event_touchpad_swipe_new (ClutterEventFlags            flags,
                                  int64_t                      timestamp_us,
                                  ClutterInputDevice          *source_device,
                                  ClutterTouchpadGesturePhase  phase,
                                  uint32_t                     fingers,
                                  graphene_point_t             coords,
                                  graphene_point_t             delta,
                                  graphene_point_t             delta_unaccel)
{
  ClutterEvent *event;
  ClutterSeat *seat;

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (source_device), NULL);

  seat = clutter_input_device_get_seat (source_device);

  event = clutter_event_new (CLUTTER_TOUCHPAD_SWIPE);

  event->touchpad_swipe.time_us = timestamp_us;
  event->touchpad_swipe.flags = flags;
  event->touchpad_swipe.phase = phase;
  event->touchpad_swipe.x = coords.x;
  event->touchpad_swipe.y = coords.y;
  event->touchpad_swipe.dx = delta.x;
  event->touchpad_swipe.dy = delta.y;
  event->touchpad_swipe.dx_unaccel = delta_unaccel.x;
  event->touchpad_swipe.dy_unaccel = delta_unaccel.y;
  event->touchpad_swipe.n_fingers = fingers;

  g_set_object (&event->touchpad_swipe.device, clutter_seat_get_pointer (seat));
  g_set_object (&event->touchpad_swipe.source_device, source_device);

  return event;
}

ClutterEvent *
clutter_event_touchpad_hold_new (ClutterEventFlags            flags,
                                 int64_t                      timestamp_us,
                                 ClutterInputDevice          *source_device,
                                 ClutterTouchpadGesturePhase  phase,
                                 uint32_t                     fingers,
                                 graphene_point_t             coords)
{
  ClutterEvent *event;
  ClutterSeat *seat;

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (source_device), NULL);

  seat = clutter_input_device_get_seat (source_device);

  event = clutter_event_new (CLUTTER_TOUCHPAD_HOLD);

  event->touchpad_hold.time_us = timestamp_us;
  event->touchpad_hold.flags = flags;
  event->touchpad_hold.phase = phase;
  event->touchpad_hold.x = coords.x;
  event->touchpad_hold.y = coords.y;
  event->touchpad_hold.n_fingers = fingers;

  g_set_object (&event->touchpad_hold.device, clutter_seat_get_pointer (seat));
  g_set_object (&event->touchpad_hold.source_device, source_device);

  return event;
}

ClutterEvent *
clutter_event_pad_button_new (ClutterEventType    type,
                              ClutterEventFlags   flags,
                              int64_t             timestamp_us,
                              ClutterInputDevice *source_device,
                              uint32_t            button,
                              uint32_t            group,
                              uint32_t            mode)
{
  ClutterEvent *event;

  g_return_val_if_fail (type == CLUTTER_PAD_BUTTON_PRESS ||
                        type == CLUTTER_PAD_BUTTON_RELEASE, NULL);
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (source_device), NULL);

  event = clutter_event_new (type);

  event->pad_button.time_us = timestamp_us;
  event->pad_button.flags = flags;
  event->pad_button.button = button;
  event->pad_button.group = group;
  event->pad_button.mode = mode;

  g_set_object (&event->pad_button.device, source_device);
  g_set_object (&event->pad_button.source_device, source_device);

  return event;
}

ClutterEvent *
clutter_event_pad_strip_new (ClutterEventFlags            flags,
                             int64_t                      timestamp_us,
                             ClutterInputDevice          *source_device,
                             ClutterInputDevicePadSource  strip_source,
                             uint32_t                     strip,
                             uint32_t                     group,
                             double                       value,
                             uint32_t                     mode)
{
  ClutterEvent *event;

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (source_device), NULL);

  event = clutter_event_new (CLUTTER_PAD_STRIP);

  event->pad_strip.time_us = timestamp_us;
  event->pad_strip.flags = flags;
  event->pad_strip.strip_source = strip_source;
  event->pad_strip.strip_number = strip;
  event->pad_strip.group = group;
  event->pad_strip.value = value;
  event->pad_strip.mode = mode;

  g_set_object (&event->pad_strip.device, source_device);
  g_set_object (&event->pad_strip.source_device, source_device);

  return event;
}

ClutterEvent *
clutter_event_pad_ring_new (ClutterEventFlags            flags,
                            int64_t                      timestamp_us,
                            ClutterInputDevice          *source_device,
                            ClutterInputDevicePadSource  ring_source,
                            uint32_t                     ring,
                            uint32_t                     group,
                            double                       angle,
                            uint32_t                     mode)
{
  ClutterEvent *event;

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (source_device), NULL);

  event = clutter_event_new (CLUTTER_PAD_RING);

  event->pad_ring.time_us = timestamp_us;
  event->pad_ring.flags = flags;
  event->pad_ring.ring_source = ring_source;
  event->pad_ring.ring_number = ring;
  event->pad_ring.group = group;
  event->pad_ring.angle = angle;
  event->pad_ring.mode = mode;

  g_set_object (&event->pad_ring.device, source_device);
  g_set_object (&event->pad_ring.source_device, source_device);

  return event;
}

ClutterEvent *
clutter_event_device_notify_new (ClutterEventType    type,
                                 ClutterEventFlags   flags,
                                 int64_t             timestamp_us,
                                 ClutterInputDevice *source_device)
{
  ClutterEvent *event;

  g_return_val_if_fail (type == CLUTTER_DEVICE_ADDED ||
                        type == CLUTTER_DEVICE_REMOVED, NULL);
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (source_device), NULL);

  event = clutter_event_new (type);

  event->device.time_us = timestamp_us;
  event->device.flags = flags;

  g_set_object (&event->device.device, source_device);

  return event;
}

ClutterEvent *
clutter_event_im_new (ClutterEventType         type,
                      ClutterEventFlags        flags,
                      int64_t                  timestamp_us,
                      ClutterSeat             *seat,
                      const char              *text,
                      int32_t                  offset,
                      int32_t                  anchor,
                      uint32_t                 len,
                      ClutterPreeditResetMode  mode)
{
  ClutterEvent *event;

  g_return_val_if_fail (type == CLUTTER_IM_COMMIT ||
                        type == CLUTTER_IM_DELETE ||
                        type == CLUTTER_IM_PREEDIT, NULL);

  event = clutter_event_new (type);

  event->im.time_us = timestamp_us;
  event->im.flags = flags;
  event->im.text = g_strdup (text);
  event->im.offset = offset;
  event->im.anchor = anchor;
  event->im.len = len;
  event->im.mode = mode;

  g_set_object (&event->im.device, clutter_seat_get_keyboard (seat));

  return event;
}

static const char *
scroll_source_to_string (ClutterScrollSource scroll_source)
{
  switch (scroll_source)
    {
    case CLUTTER_SCROLL_SOURCE_UNKNOWN:
      return "unknown";
    case CLUTTER_SCROLL_SOURCE_WHEEL:
      return "wheel";
    case CLUTTER_SCROLL_SOURCE_FINGER:
      return "finger";
    case CLUTTER_SCROLL_SOURCE_CONTINUOUS:
      return "continuous";
    }
  g_return_val_if_reached ("");
}

static const char *
touchpad_gesture_phase_to_string (ClutterTouchpadGesturePhase phase)
{
  switch (phase)
    {
    case CLUTTER_TOUCHPAD_GESTURE_PHASE_BEGIN:
      return "begin";
    case CLUTTER_TOUCHPAD_GESTURE_PHASE_UPDATE:
      return "update";
    case CLUTTER_TOUCHPAD_GESTURE_PHASE_END:
      return "end";
    case CLUTTER_TOUCHPAD_GESTURE_PHASE_CANCEL:
      return "cancel";
    }
  g_return_val_if_reached ("");
}

static const char *
pad_source_to_string (ClutterInputDevicePadSource pad_source)
{
  switch (pad_source)
    {
    case CLUTTER_INPUT_DEVICE_PAD_SOURCE_UNKNOWN:
      return "unknown";
    case CLUTTER_INPUT_DEVICE_PAD_SOURCE_FINGER:
      return "finger";
    }
  g_return_val_if_reached ("");
}

static const char *
scroll_direction_to_string (ClutterScrollDirection scroll_direction)
{
  switch (scroll_direction)
    {
    case CLUTTER_SCROLL_SMOOTH:
      g_warn_if_reached ();
      return "";
    case CLUTTER_SCROLL_LEFT:
      return "left";
    case CLUTTER_SCROLL_RIGHT:
      return "right";
    case CLUTTER_SCROLL_UP:
      return "up";
    case CLUTTER_SCROLL_DOWN:
      return "down";
    }
  g_return_val_if_reached ("");
}

static char *
generate_event_description (const ClutterEvent *event)
{
  switch (event->type)
    {
    case CLUTTER_KEY_PRESS:
    case CLUTTER_KEY_RELEASE:
      if (g_strcmp0 (g_getenv ("MUTTER_DEBUG_LOG_KEYCODES"), "1") == 0)
        {
          char unicode[7] = {};

          if (event->key.unicode_value)
            g_unichar_to_utf8 (event->key.unicode_value, unicode);
          return g_strdup_printf ("keycode=%u, evdev=%u, "
                                  "keysym=%u, unicode='%s'",
                                  event->key.hardware_keycode,
                                  event->key.evdev_code,
                                  event->key.keyval,
                                  event->key.unicode_value ? unicode : "N\\A");
        }
      else
        {
          return g_strdup ("(hidden)");
        }
    case CLUTTER_MOTION:
      return g_strdup_printf ("abs=(%f, %f), rel=(%f, %f), unaccel-rel=(%f, %f)",
                              event->motion.x,
                              event->motion.y,
                              event->motion.dx,
                              event->motion.dy,
                              event->motion.dx_unaccel,
                              event->motion.dy_unaccel);
    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_BUTTON_RELEASE:
      return g_strdup_printf ("button=%u, evdev=%u",
                              event->button.button,
                              event->button.evdev_code);
    case CLUTTER_SCROLL:
      if (event->scroll.direction == CLUTTER_SCROLL_SMOOTH)
        {
          double dx, dy;
          ClutterScrollSource scroll_source;

          clutter_event_get_scroll_delta (event, &dx, &dy);
          scroll_source = event->scroll.scroll_source;
          return g_strdup_printf ("source=%s, rel: (%f, %f)",
                                  scroll_source_to_string (scroll_source),
                                  dx, dy);
        }
      else
        {
          ClutterScrollDirection direction = event->scroll.direction;

          return g_strdup_printf ("direction=%s",
                                  scroll_direction_to_string (direction));
        }
    case CLUTTER_TOUCH_BEGIN:
    case CLUTTER_TOUCH_UPDATE:
    case CLUTTER_TOUCH_END:
    case CLUTTER_TOUCH_CANCEL:
      return g_strdup_printf ("slot=%d, abs=(%f, %f)",
                              GPOINTER_TO_INT (event->touch.sequence),
                              event->touch.x,
                              event->touch.y);
    case CLUTTER_TOUCHPAD_PINCH:
      return g_strdup_printf ("phase=%s, rel=(%f, %f), unaccel-rel=(%f, %f), "
                              "angle-delta=%f, scale=%f, n-fingers=%d",
                              touchpad_gesture_phase_to_string (event->touchpad_pinch.phase),
                              event->touchpad_pinch.dx,
                              event->touchpad_pinch.dy,
                              event->touchpad_pinch.dx_unaccel,
                              event->touchpad_pinch.dy_unaccel,
                              event->touchpad_pinch.angle_delta,
                              event->touchpad_pinch.scale,
                              event->touchpad_pinch.n_fingers);
    case CLUTTER_TOUCHPAD_SWIPE:
      return g_strdup_printf ("phase=%s, rel=(%f, %f), unaccel-rel=(%f, %f), "
                              "n-fingers=%d",
                              touchpad_gesture_phase_to_string (event->touchpad_pinch.phase),
                              event->touchpad_swipe.dx,
                              event->touchpad_swipe.dy,
                              event->touchpad_swipe.dx_unaccel,
                              event->touchpad_swipe.dy_unaccel,
                              event->touchpad_swipe.n_fingers);
    case CLUTTER_TOUCHPAD_HOLD:
      return g_strdup_printf ("phase=%s, n-fingers=%d",
                              touchpad_gesture_phase_to_string (event->touchpad_pinch.phase),
                              event->touchpad_hold.n_fingers);
    case CLUTTER_PROXIMITY_IN:
    case CLUTTER_PROXIMITY_OUT:
      return g_strdup ("");
    case CLUTTER_PAD_BUTTON_PRESS:
    case CLUTTER_PAD_BUTTON_RELEASE:
      return g_strdup_printf ("button=%u, group=%u, mode=%u",
                              event->pad_button.button,
                              event->pad_button.group,
                              event->pad_button.mode);
    case CLUTTER_PAD_STRIP:
      return g_strdup_printf ("source=%s (%d), value=%f, group=%u, mode=%u",
                              pad_source_to_string (event->pad_strip.strip_source),
                              event->pad_strip.strip_number,
                              event->pad_strip.value,
                              event->pad_strip.group,
                              event->pad_strip.mode);
    case CLUTTER_PAD_RING:
      return g_strdup_printf ("source=%s (%d), angle=%f, group=%u, mode=%u",
                              pad_source_to_string (event->pad_ring.ring_source),
                              event->pad_ring.ring_number,
                              event->pad_ring.angle,
                              event->pad_ring.group,
                              event->pad_ring.mode);
    case CLUTTER_DEVICE_ADDED:
    case CLUTTER_DEVICE_REMOVED:
      {
        ClutterInputDevice *device;

        device = clutter_event_get_device (event);
        return g_strdup_printf ("%s (%s)",
                                clutter_input_device_get_device_name (device),
                                clutter_input_device_get_device_node (device));
      }
    default:
      g_warn_if_reached ();
      return g_strdup ("");
    }
}

static char *
generate_modifiers_description (const ClutterEvent *event)
{
  ClutterModifierType modifiers;
  GString *str;

  modifiers = clutter_event_get_state (event);

  if (modifiers == 0)
    return g_strdup ("none");

  str = g_string_new (NULL);

  if (modifiers & CLUTTER_SHIFT_MASK)
    g_string_append (str, "shift ");
  if (modifiers & CLUTTER_LOCK_MASK)
    g_string_append (str, "lock ");
  if (modifiers & CLUTTER_CONTROL_MASK)
    g_string_append (str, "control ");
  if (modifiers & CLUTTER_MOD1_MASK)
    g_string_append (str, "mod1 ");
  if (modifiers & CLUTTER_MOD2_MASK)
    g_string_append (str, "mod2 ");
  if (modifiers & CLUTTER_MOD3_MASK)
    g_string_append (str, "mod3 ");
  if (modifiers & CLUTTER_MOD4_MASK)
    g_string_append (str, "mod4 ");
  if (modifiers & CLUTTER_MOD5_MASK)
    g_string_append (str, "mod5 ");
  if (modifiers & CLUTTER_BUTTON1_MASK)
    g_string_append (str, "button1 ");
  if (modifiers & CLUTTER_BUTTON2_MASK)
    g_string_append (str, "button2 ");
  if (modifiers & CLUTTER_BUTTON3_MASK)
    g_string_append (str, "button3 ");
  if (modifiers & CLUTTER_BUTTON4_MASK)
    g_string_append (str, "button4 ");
  if (modifiers & CLUTTER_BUTTON5_MASK)
    g_string_append (str, "button5 ");
  if (modifiers & CLUTTER_SUPER_MASK)
    g_string_append (str, "super ");
  if (modifiers & CLUTTER_HYPER_MASK)
    g_string_append (str, "hyper ");
  if (modifiers & CLUTTER_META_MASK)
    g_string_append (str, "meta ");
  if (modifiers & CLUTTER_RELEASE_MASK)
    g_string_append (str, "release ");

  /* Delete trailing space */
  g_string_erase (str, str->len - 1, 1);

  return g_string_free_and_steal (str);
}

char *
clutter_event_describe (const ClutterEvent *event)
{
  g_autofree char *event_description = NULL;
  g_autofree char *modifiers_description = NULL;
  ClutterInputDevice *source_device;

  source_device = clutter_event_get_source_device (event);
  event_description = generate_event_description (event);
  modifiers_description = generate_modifiers_description (event);

  return g_strdup_printf ("'%s'%s%s, time=%" G_GINT64_FORMAT " us, modifiers=%s, %s",
                          clutter_event_get_name (event),
                          source_device ? " from " : "",
                          source_device ?
                          clutter_input_device_get_device_node (source_device) :
                          "",
                          event->any.time_us,
                          modifiers_description,
                          event_description);
}
