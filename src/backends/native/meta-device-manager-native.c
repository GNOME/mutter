/*
 * Copyright (C) 2010  Intel Corp.
 * Copyright (C) 2014  Jonas Ådahl
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
#include <float.h>
#include <linux/input.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <libinput.h>

#include "backends/native/meta-device-manager-native.h"
#include "backends/native/meta-event-native.h"
#include "backends/native/meta-input-device-native.h"
#include "backends/native/meta-input-device-tool-native.h"
#include "backends/native/meta-keymap-native.h"
#include "backends/native/meta-seat-native.h"
#include "backends/native/meta-virtual-input-device-native.h"
#include "backends/native/meta-xkb-utils.h"
#include "clutter/clutter-mutter.h"

/*
 * Clutter makes the assumption that two core devices have ID's 2 and 3 (core
 * pointer and core keyboard).
 *
 * Since the two first devices that will ever be created will be the virtual
 * pointer and virtual keyboard of the first seat, we fulfill the made
 * assumptions by having the first device having ID 2 and following 3.
 */
#define INITIAL_DEVICE_ID 2

typedef struct _MetaEventFilter MetaEventFilter;

struct _MetaEventFilter
{
  MetaEvdevFilterFunc func;
  gpointer data;
  GDestroyNotify destroy_notify;
};

typedef struct _MetaEventSource  MetaEventSource;

struct _MetaDeviceManagerNativePrivate
{
  struct libinput *libinput;

  ClutterStage *stage;
  gboolean released;

  MetaEventSource *event_source;

  GSList *devices;
  GSList *seats;

  MetaSeatNative *main_seat;

  MetaPointerConstrainCallback constrain_callback;
  gpointer constrain_data;
  GDestroyNotify constrain_data_notify;

  MetaRelativeMotionFilter relative_motion_filter;
  gpointer relative_motion_filter_user_data;

  ClutterStageManager *stage_manager;
  guint stage_added_handler;
  guint stage_removed_handler;

  GSList *event_filters;

  gint device_id_next;
  GList *free_device_ids;
};

G_DEFINE_TYPE_WITH_CODE (MetaDeviceManagerNative,
                         meta_device_manager_native,
                         CLUTTER_TYPE_DEVICE_MANAGER,
                         G_ADD_PRIVATE (MetaDeviceManagerNative))

static MetaOpenDeviceCallback  device_open_callback;
static MetaCloseDeviceCallback device_close_callback;
static gpointer                device_callback_data;
static gchar *                 evdev_seat_id;

#ifdef CLUTTER_ENABLE_DEBUG
static const char *device_type_str[] = {
  "pointer",            /* CLUTTER_POINTER_DEVICE */
  "keyboard",           /* CLUTTER_KEYBOARD_DEVICE */
  "extension",          /* CLUTTER_EXTENSION_DEVICE */
  "joystick",           /* CLUTTER_JOYSTICK_DEVICE */
  "tablet",             /* CLUTTER_TABLET_DEVICE */
  "touchpad",           /* CLUTTER_TOUCHPAD_DEVICE */
  "touchscreen",        /* CLUTTER_TOUCHSCREEN_DEVICE */
  "pen",                /* CLUTTER_PEN_DEVICE */
  "eraser",             /* CLUTTER_ERASER_DEVICE */
  "cursor",             /* CLUTTER_CURSOR_DEVICE */
  "pad",                /* CLUTTER_PAD_DEVICE */
};
#endif /* CLUTTER_ENABLE_DEBUG */

/*
 * MetaEventSource management
 *
 * The device manager is responsible for managing the GSource when devices
 * appear and disappear from the system.
 */

static void
meta_device_manager_native_copy_event_data (ClutterDeviceManager *device_manager,
                                            const ClutterEvent   *src,
                                            ClutterEvent         *dest)
{
  MetaEventNative *event_evdev;

  event_evdev = _clutter_event_get_platform_data (src);
  if (event_evdev != NULL)
    _clutter_event_set_platform_data (dest, meta_event_native_copy (event_evdev));
}

static void
meta_device_manager_native_free_event_data (ClutterDeviceManager *device_manager,
                                            ClutterEvent         *event)
{
  MetaEventNative *event_evdev;

  event_evdev = _clutter_event_get_platform_data (event);
  if (event_evdev != NULL)
    meta_event_native_free (event_evdev);
}

/*
 * MetaEventSource for reading input devices
 */

struct _MetaEventSource
{
  GSource source;

  MetaDeviceManagerNative *manager_evdev;
  GPollFD event_poll_fd;
};

static void
process_events (MetaDeviceManagerNative *manager_evdev);

static gboolean
meta_event_prepare (GSource *source,
                    gint    *timeout)
{
  gboolean retval;

  _clutter_threads_acquire_lock ();

  *timeout = -1;
  retval = clutter_events_pending ();

  _clutter_threads_release_lock ();

  return retval;
}

static gboolean
meta_event_check (GSource *source)
{
  MetaEventSource *event_source = (MetaEventSource *) source;
  gboolean retval;

  _clutter_threads_acquire_lock ();

  retval = ((event_source->event_poll_fd.revents & G_IO_IN) ||
            clutter_events_pending ());

  _clutter_threads_release_lock ();

  return retval;
}

static void
queue_event (ClutterEvent *event)
{
  _clutter_event_push (event, FALSE);
}

void
meta_device_manager_native_constrain_pointer (MetaDeviceManagerNative *manager_evdev,
                                              ClutterInputDevice      *core_pointer,
                                              uint64_t                 time_us,
                                              float                    x,
                                              float                    y,
                                              float                   *new_x,
                                              float                   *new_y)
{
  if (manager_evdev->priv->constrain_callback)
    {
      manager_evdev->priv->constrain_callback (core_pointer,
                                               us2ms (time_us),
                                               x, y,
                                               new_x, new_y,
					       manager_evdev->priv->constrain_data);
    }
  else
    {
      ClutterActor *stage = CLUTTER_ACTOR (manager_evdev->priv->stage);
      float stage_width = clutter_actor_get_width (stage);
      float stage_height = clutter_actor_get_height (stage);

      *new_x = CLAMP (x, 0.f, stage_width - 1);
      *new_y = CLAMP (y, 0.f, stage_height - 1);
    }
}

void
meta_device_manager_native_filter_relative_motion (MetaDeviceManagerNative *manager_evdev,
                                                   ClutterInputDevice      *device,
                                                   float                    x,
                                                   float                    y,
                                                   float                   *dx,
                                                   float                   *dy)
{
  MetaDeviceManagerNativePrivate *priv = manager_evdev->priv;

  if (!priv->relative_motion_filter)
    return;

  priv->relative_motion_filter (device, x, y, dx, dy,
                                priv->relative_motion_filter_user_data);
}

static ClutterEvent *
new_absolute_motion_event (ClutterInputDevice *input_device,
                           uint64_t            time_us,
                           float               x,
                           float               y,
                           double             *axes)
{
  gfloat stage_width, stage_height;
  MetaDeviceManagerNative *manager_evdev;
  MetaInputDeviceNative *device_evdev;
  MetaSeatNative *seat;
  ClutterStage *stage;
  ClutterEvent *event = NULL;

  stage = _clutter_input_device_get_stage (input_device);
  device_evdev = META_INPUT_DEVICE_NATIVE (input_device);
  manager_evdev = META_DEVICE_MANAGER_NATIVE (input_device->device_manager);
  seat = meta_input_device_native_get_seat (device_evdev);

  stage_width = clutter_actor_get_width (CLUTTER_ACTOR (stage));
  stage_height = clutter_actor_get_height (CLUTTER_ACTOR (stage));

  event = clutter_event_new (CLUTTER_MOTION);

  if (manager_evdev->priv->constrain_callback &&
      clutter_input_device_get_device_type (input_device) != CLUTTER_TABLET_DEVICE)
    {
      manager_evdev->priv->constrain_callback (seat->core_pointer,
                                               us2ms (time_us),
                                               seat->pointer_x,
                                               seat->pointer_y,
                                               &x, &y,
					       manager_evdev->priv->constrain_data);
    }
  else
    {
      x = CLAMP (x, 0.f, stage_width - 1);
      y = CLAMP (y, 0.f, stage_height - 1);
    }

  meta_event_native_set_time_usec (event, time_us);
  event->motion.time = us2ms (time_us);
  event->motion.stage = stage;
  meta_xkb_translate_state (event, seat->xkb, seat->button_state);
  event->motion.x = x;
  event->motion.y = y;
  meta_input_device_native_translate_coordinates (input_device, stage,
                                                  &event->motion.x,
                                                  &event->motion.y);
  event->motion.axes = axes;
  clutter_event_set_device (event, seat->core_pointer);
  clutter_event_set_source_device (event, input_device);

  if (clutter_input_device_get_device_type (input_device) == CLUTTER_TABLET_DEVICE)
    {
      clutter_event_set_device_tool (event, device_evdev->last_tool);
      clutter_event_set_device (event, input_device);
    }
  else
    clutter_event_set_device (event, seat->core_pointer);

  _clutter_input_device_set_stage (seat->core_pointer, stage);

  if (clutter_input_device_get_device_type (input_device) != CLUTTER_TABLET_DEVICE)
    {
      seat->pointer_x = x;
      seat->pointer_y = y;
    }

  return event;
}

static void
notify_absolute_motion (ClutterInputDevice *input_device,
                        uint64_t            time_us,
                        float               x,
                        float               y,
                        double             *axes)
{
  ClutterEvent *event;

  event = new_absolute_motion_event (input_device, time_us, x, y, axes);

  queue_event (event);
}

static void
notify_relative_tool_motion (ClutterInputDevice *input_device,
                             uint64_t            time_us,
                             float               dx,
                             float               dy,
                             double             *axes)
{
  MetaInputDeviceNative *device_evdev;
  ClutterEvent *event;
  MetaSeatNative *seat;
  gfloat x, y;

  device_evdev = META_INPUT_DEVICE_NATIVE (input_device);
  seat = meta_input_device_native_get_seat (device_evdev);
  x = input_device->current_x + dx;
  y = input_device->current_y + dy;

  meta_device_manager_native_filter_relative_motion (seat->manager_evdev,
                                                        input_device,
                                                        seat->pointer_x,
                                                        seat->pointer_y,
                                                        &dx,
                                                        &dy);

  event = new_absolute_motion_event (input_device, time_us, x, y, axes);
  meta_event_native_set_relative_motion (event, dx, dy, 0, 0);

  queue_event (event);
}

static void
notify_pinch_gesture_event (ClutterInputDevice          *input_device,
                            ClutterTouchpadGesturePhase  phase,
                            uint64_t                     time_us,
                            double                       dx,
                            double                       dy,
                            double                       angle_delta,
                            double                       scale,
                            uint32_t                     n_fingers)
{
  MetaInputDeviceNative *device_evdev;
  MetaSeatNative *seat;
  ClutterStage *stage;
  ClutterEvent *event = NULL;
  ClutterPoint pos;

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  stage = _clutter_input_device_get_stage (input_device);
  if (stage == NULL)
    return;

  device_evdev = META_INPUT_DEVICE_NATIVE (input_device);
  seat = meta_input_device_native_get_seat (device_evdev);

  event = clutter_event_new (CLUTTER_TOUCHPAD_PINCH);

  clutter_input_device_get_coords (seat->core_pointer, NULL, &pos);

  meta_event_native_set_time_usec (event, time_us);
  event->touchpad_pinch.phase = phase;
  event->touchpad_pinch.time = us2ms (time_us);
  event->touchpad_pinch.stage = CLUTTER_STAGE (stage);
  event->touchpad_pinch.x = pos.x;
  event->touchpad_pinch.y = pos.y;
  event->touchpad_pinch.dx = dx;
  event->touchpad_pinch.dy = dy;
  event->touchpad_pinch.angle_delta = angle_delta;
  event->touchpad_pinch.scale = scale;
  event->touchpad_pinch.n_fingers = n_fingers;

  meta_xkb_translate_state (event, seat->xkb, seat->button_state);

  clutter_event_set_device (event, seat->core_pointer);
  clutter_event_set_source_device (event, input_device);

  queue_event (event);
}

static void
notify_swipe_gesture_event (ClutterInputDevice          *input_device,
                            ClutterTouchpadGesturePhase  phase,
                            uint64_t                     time_us,
                            uint32_t                     n_fingers,
                            double                       dx,
                            double                       dy)
{
  MetaInputDeviceNative *device_evdev;
  MetaSeatNative *seat;
  ClutterStage *stage;
  ClutterEvent *event = NULL;
  ClutterPoint pos;

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  stage = _clutter_input_device_get_stage (input_device);
  if (stage == NULL)
    return;

  device_evdev = META_INPUT_DEVICE_NATIVE (input_device);
  seat = meta_input_device_native_get_seat (device_evdev);

  event = clutter_event_new (CLUTTER_TOUCHPAD_SWIPE);

  meta_event_native_set_time_usec (event, time_us);
  event->touchpad_swipe.phase = phase;
  event->touchpad_swipe.time = us2ms (time_us);
  event->touchpad_swipe.stage = CLUTTER_STAGE (stage);

  clutter_input_device_get_coords (seat->core_pointer, NULL, &pos);
  event->touchpad_swipe.x = pos.x;
  event->touchpad_swipe.y = pos.y;
  event->touchpad_swipe.dx = dx;
  event->touchpad_swipe.dy = dy;
  event->touchpad_swipe.n_fingers = n_fingers;

  meta_xkb_translate_state (event, seat->xkb, seat->button_state);

  clutter_event_set_device (event, seat->core_pointer);
  clutter_event_set_source_device (event, input_device);

  queue_event (event);
}

static void
notify_proximity (ClutterInputDevice *input_device,
                  uint64_t            time_us,
                  gboolean            in)
{
  MetaInputDeviceNative *device_evdev;
  MetaSeatNative *seat;
  ClutterStage *stage;
  ClutterEvent *event = NULL;

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  stage = _clutter_input_device_get_stage (input_device);
  if (stage == NULL)
    return;

  device_evdev = META_INPUT_DEVICE_NATIVE (input_device);
  seat = meta_input_device_native_get_seat (device_evdev);

  if (in)
    event = clutter_event_new (CLUTTER_PROXIMITY_IN);
  else
    event = clutter_event_new (CLUTTER_PROXIMITY_OUT);

  meta_event_native_set_time_usec (event, time_us);

  event->proximity.time = us2ms (time_us);
  event->proximity.stage = CLUTTER_STAGE (stage);
  clutter_event_set_device_tool (event, device_evdev->last_tool);
  clutter_event_set_device (event, seat->core_pointer);
  clutter_event_set_source_device (event, input_device);

  _clutter_input_device_set_stage (seat->core_pointer, stage);

  queue_event (event);
}

static void
notify_pad_button (ClutterInputDevice *input_device,
                   uint64_t            time_us,
                   uint32_t            button,
                   uint32_t            mode_group,
                   uint32_t            mode,
                   uint32_t            pressed)
{
  MetaInputDeviceNative *device_evdev;
  MetaSeatNative *seat;
  ClutterStage *stage;
  ClutterEvent *event;

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  stage = _clutter_input_device_get_stage (input_device);
  if (stage == NULL)
    return;

  if (pressed)
    event = clutter_event_new (CLUTTER_PAD_BUTTON_PRESS);
  else
    event = clutter_event_new (CLUTTER_PAD_BUTTON_RELEASE);

  device_evdev = META_INPUT_DEVICE_NATIVE (input_device);
  seat = meta_input_device_native_get_seat (device_evdev);

  meta_event_native_set_time_usec (event, time_us);
  event->pad_button.stage = stage;
  event->pad_button.button = button;
  event->pad_button.group = mode_group;
  event->pad_button.mode = mode;
  clutter_event_set_device (event, input_device);
  clutter_event_set_source_device (event, input_device);
  clutter_event_set_time (event, us2ms (time_us));

  _clutter_input_device_set_stage (seat->core_pointer, stage);

  queue_event (event);
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
  MetaInputDeviceNative *device_evdev;
  ClutterInputDevicePadSource source;
  MetaSeatNative *seat;
  ClutterStage *stage;
  ClutterEvent *event;

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  stage = _clutter_input_device_get_stage (input_device);
  if (stage == NULL)
    return;

  if (strip_source == LIBINPUT_TABLET_PAD_STRIP_SOURCE_FINGER)
    source = CLUTTER_INPUT_DEVICE_PAD_SOURCE_FINGER;
  else
    source = CLUTTER_INPUT_DEVICE_PAD_SOURCE_UNKNOWN;

  device_evdev = META_INPUT_DEVICE_NATIVE (input_device);
  seat = meta_input_device_native_get_seat (device_evdev);

  event = clutter_event_new (CLUTTER_PAD_STRIP);
  meta_event_native_set_time_usec (event, time_us);
  event->pad_strip.strip_source = source;
  event->pad_strip.stage = stage;
  event->pad_strip.strip_number = strip_number;
  event->pad_strip.value = value;
  event->pad_strip.group = mode_group;
  event->pad_strip.mode = mode;
  clutter_event_set_device (event, input_device);
  clutter_event_set_source_device (event, input_device);
  clutter_event_set_time (event, us2ms (time_us));

  _clutter_input_device_set_stage (seat->core_pointer, stage);

  queue_event (event);
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
  MetaInputDeviceNative *device_evdev;
  ClutterInputDevicePadSource source;
  MetaSeatNative *seat;
  ClutterStage *stage;
  ClutterEvent *event;

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  stage = _clutter_input_device_get_stage (input_device);
  if (stage == NULL)
    return;

  if (ring_source == LIBINPUT_TABLET_PAD_RING_SOURCE_FINGER)
    source = CLUTTER_INPUT_DEVICE_PAD_SOURCE_FINGER;
  else
    source = CLUTTER_INPUT_DEVICE_PAD_SOURCE_UNKNOWN;

  device_evdev = META_INPUT_DEVICE_NATIVE (input_device);
  seat = meta_input_device_native_get_seat (device_evdev);

  event = clutter_event_new (CLUTTER_PAD_RING);
  meta_event_native_set_time_usec (event, time_us);
  event->pad_ring.ring_source = source;
  event->pad_ring.stage = stage;
  event->pad_ring.ring_number = ring_number;
  event->pad_ring.angle = angle;
  event->pad_ring.group = mode_group;
  event->pad_ring.mode = mode;
  clutter_event_set_device (event, input_device);
  clutter_event_set_source_device (event, input_device);
  clutter_event_set_time (event, us2ms (time_us));

  _clutter_input_device_set_stage (seat->core_pointer, stage);

  queue_event (event);
}

static void
dispatch_libinput (MetaDeviceManagerNative *manager_evdev)
{
  MetaDeviceManagerNativePrivate *priv = manager_evdev->priv;

  libinput_dispatch (priv->libinput);
  process_events (manager_evdev);
}

static gboolean
meta_event_dispatch (GSource     *g_source,
                     GSourceFunc  callback,
                     gpointer     user_data)
{
  MetaEventSource *source = (MetaEventSource *) g_source;
  MetaDeviceManagerNative *manager_evdev;
  ClutterEvent *event;

  _clutter_threads_acquire_lock ();

  manager_evdev = source->manager_evdev;

  /* Don't queue more events if we haven't finished handling the previous batch
   */
  if (clutter_events_pending ())
    goto queue_event;

  dispatch_libinput (manager_evdev);

 queue_event:
  event = clutter_event_get ();

  if (event)
    {
      ClutterModifierType event_state;
      ClutterInputDevice *input_device =
        clutter_event_get_source_device (event);
      MetaInputDeviceNative *device_evdev =
        META_INPUT_DEVICE_NATIVE (input_device);
      MetaSeatNative *seat =
        meta_input_device_native_get_seat (device_evdev);

      /* Drop events if we don't have any stage to forward them to */
      if (!_clutter_input_device_get_stage (input_device))
        goto out;

      /* update the device states *before* the event */
      event_state = seat->button_state |
        xkb_state_serialize_mods (seat->xkb, XKB_STATE_MODS_EFFECTIVE);
      _clutter_input_device_set_state (seat->core_pointer, event_state);
      _clutter_input_device_set_state (seat->core_keyboard, event_state);

      /* forward the event into clutter for emission etc. */
      _clutter_stage_queue_event (event->any.stage, event, FALSE);
    }

out:
  _clutter_threads_release_lock ();

  return TRUE;
}
static GSourceFuncs event_funcs = {
  meta_event_prepare,
  meta_event_check,
  meta_event_dispatch,
  NULL
};

static MetaEventSource *
meta_event_source_new (MetaDeviceManagerNative *manager_evdev)
{
  MetaDeviceManagerNativePrivate *priv = manager_evdev->priv;
  GSource *source;
  MetaEventSource *event_source;
  gint fd;

  source = g_source_new (&event_funcs, sizeof (MetaEventSource));
  event_source = (MetaEventSource *) source;

  /* setup the source */
  event_source->manager_evdev = manager_evdev;

  fd = libinput_get_fd (priv->libinput);
  event_source->event_poll_fd.fd = fd;
  event_source->event_poll_fd.events = G_IO_IN;

  /* and finally configure and attach the GSource */
  g_source_set_priority (source, CLUTTER_PRIORITY_EVENTS);
  g_source_add_poll (source, &event_source->event_poll_fd);
  g_source_set_can_recurse (source, TRUE);
  g_source_attach (source, NULL);

  return event_source;
}

static void
meta_event_source_free (MetaEventSource *source)
{
  GSource *g_source = (GSource *) source;

  /* ignore the return value of close, it's not like we can do something
   * about it */
  close (source->event_poll_fd.fd);

  g_source_destroy (g_source);
  g_source_unref (g_source);
}

static void
evdev_add_device (MetaDeviceManagerNative *manager_evdev,
                  struct libinput_device  *libinput_device)
{
  ClutterDeviceManager *manager = (ClutterDeviceManager *) manager_evdev;
  MetaDeviceManagerNativePrivate *priv = manager_evdev->priv;
  ClutterInputDeviceType type;
  struct libinput_seat *libinput_seat;
  MetaSeatNative *seat;
  ClutterInputDevice *device;

  libinput_seat = libinput_device_get_seat (libinput_device);
  seat = libinput_seat_get_user_data (libinput_seat);
  if (seat == NULL)
    {
      /* Clutter has the notion of global "core" pointers and keyboard devices,
       * which are located on the main seat. Make whatever seat comes first the
       * main seat. */
      if (priv->main_seat->libinput_seat == NULL)
        seat = priv->main_seat;
      else
        {
          seat = meta_seat_native_new (manager_evdev);
          priv->seats = g_slist_append (priv->seats, seat);
        }

      meta_seat_native_set_libinput_seat (seat, libinput_seat);
    }

  device = meta_input_device_native_new (manager, seat, libinput_device);
  _clutter_input_device_set_stage (device, manager_evdev->priv->stage);

  _clutter_device_manager_add_device (manager, device);

  /* Clutter assumes that device types are exclusive in the
   * ClutterInputDevice API */
  type = meta_input_device_native_determine_type (libinput_device);

  if (type == CLUTTER_KEYBOARD_DEVICE)
    {
      _clutter_input_device_set_associated_device (device, seat->core_keyboard);
      _clutter_input_device_add_slave (seat->core_keyboard, device);
    }
  else if (type == CLUTTER_POINTER_DEVICE)
    {
      _clutter_input_device_set_associated_device (device, seat->core_pointer);
      _clutter_input_device_add_slave (seat->core_pointer, device);
    }
}

static void
evdev_remove_device (MetaDeviceManagerNative *manager_evdev,
                     MetaInputDeviceNative   *device_evdev)
{
  ClutterDeviceManager *manager = CLUTTER_DEVICE_MANAGER (manager_evdev);
  ClutterInputDevice *input_device = CLUTTER_INPUT_DEVICE (device_evdev);

  _clutter_device_manager_remove_device (manager, input_device);
}

/*
 * ClutterDeviceManager implementation
 */

static void
meta_device_manager_native_add_device (ClutterDeviceManager *manager,
                                       ClutterInputDevice   *device)
{
  MetaDeviceManagerNative *manager_evdev;
  MetaDeviceManagerNativePrivate *priv;
  MetaInputDeviceNative *device_evdev;
  MetaSeatNative *seat;

  manager_evdev = META_DEVICE_MANAGER_NATIVE (manager);
  priv = manager_evdev->priv;
  device_evdev = META_INPUT_DEVICE_NATIVE (device);
  seat = meta_input_device_native_get_seat (device_evdev);

  seat->devices = g_slist_prepend (seat->devices, device);
  priv->devices = g_slist_prepend (priv->devices, device);
}

static void
meta_device_manager_native_remove_device (ClutterDeviceManager *manager,
                                          ClutterInputDevice   *device)
{
  MetaDeviceManagerNative *manager_evdev;
  MetaDeviceManagerNativePrivate *priv;
  MetaInputDeviceNative *device_evdev;
  MetaSeatNative *seat;

  device_evdev = META_INPUT_DEVICE_NATIVE (device);
  seat = meta_input_device_native_get_seat (device_evdev);
  manager_evdev = META_DEVICE_MANAGER_NATIVE (manager);
  priv = manager_evdev->priv;

  /* Remove the device */
  seat->devices = g_slist_remove (seat->devices, device);
  priv->devices = g_slist_remove (priv->devices, device);

  if (seat->repeat_timer && seat->repeat_device == device)
    meta_seat_native_clear_repeat_timer (seat);

  g_object_unref (device);
}

static const GSList *
meta_device_manager_native_get_devices (ClutterDeviceManager *manager)
{
  return META_DEVICE_MANAGER_NATIVE (manager)->priv->devices;
}

static ClutterInputDevice *
meta_device_manager_native_get_core_device (ClutterDeviceManager   *manager,
                                            ClutterInputDeviceType  type)
{
  MetaDeviceManagerNative *manager_evdev;
  MetaDeviceManagerNativePrivate *priv;

  manager_evdev = META_DEVICE_MANAGER_NATIVE (manager);
  priv = manager_evdev->priv;

  switch (type)
    {
    case CLUTTER_POINTER_DEVICE:
      return priv->main_seat->core_pointer;

    case CLUTTER_KEYBOARD_DEVICE:
      return priv->main_seat->core_keyboard;

    case CLUTTER_EXTENSION_DEVICE:
    default:
      return NULL;
    }

  return NULL;
}

static ClutterInputDevice *
meta_device_manager_native_get_device (ClutterDeviceManager *manager,
                                       gint                  id)
{
  MetaDeviceManagerNative *manager_evdev;
  MetaDeviceManagerNativePrivate *priv;
  GSList *l;

  manager_evdev = META_DEVICE_MANAGER_NATIVE (manager);
  priv = manager_evdev->priv;

  for (l = priv->seats; l; l = l->next)
    {
      MetaSeatNative *seat = l->data;
      ClutterInputDevice *device = meta_seat_native_get_device (seat, id);

      if (device)
        return device;
    }

  return NULL;
}

static void
flush_event_queue (void)
{
  ClutterEvent *event;

  while ((event = clutter_event_get ()) != NULL)
    {
      _clutter_process_event (event);
      clutter_event_free (event);
    }
}

static gboolean
process_base_event (MetaDeviceManagerNative *manager_evdev,
                    struct libinput_event   *event)
{
  ClutterInputDevice *device;
  struct libinput_device *libinput_device;
  gboolean handled = TRUE;

  switch (libinput_event_get_type (event))
    {
    case LIBINPUT_EVENT_DEVICE_ADDED:
      libinput_device = libinput_event_get_device (event);

      evdev_add_device (manager_evdev, libinput_device);
      break;

    case LIBINPUT_EVENT_DEVICE_REMOVED:
      /* Flush all queued events, there
       * might be some from this device.
       */
      flush_event_queue ();

      libinput_device = libinput_event_get_device (event);

      device = libinput_device_get_user_data (libinput_device);
      evdev_remove_device (manager_evdev,
                           META_INPUT_DEVICE_NATIVE (device));
      break;

    default:
      handled = FALSE;
    }

  return handled;
}

static ClutterScrollSource
translate_scroll_source (enum libinput_pointer_axis_source source)
{
  switch (source)
    {
    case LIBINPUT_POINTER_AXIS_SOURCE_WHEEL:
      return CLUTTER_SCROLL_SOURCE_WHEEL;
    case LIBINPUT_POINTER_AXIS_SOURCE_FINGER:
      return CLUTTER_SCROLL_SOURCE_FINGER;
    case LIBINPUT_POINTER_AXIS_SOURCE_CONTINUOUS:
      return CLUTTER_SCROLL_SOURCE_CONTINUOUS;
    default:
      return CLUTTER_SCROLL_SOURCE_UNKNOWN;
    }
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
input_device_update_tool (ClutterInputDevice          *input_device,
                          struct libinput_tablet_tool *libinput_tool)
{
  MetaInputDeviceNative *evdev_device = META_INPUT_DEVICE_NATIVE (input_device);
  ClutterInputDeviceTool *tool = NULL;
  ClutterInputDeviceToolType tool_type;
  uint64_t tool_serial;

  if (libinput_tool)
    {
      tool_serial = libinput_tablet_tool_get_serial (libinput_tool);
      tool_type = translate_tool_type (libinput_tool);
      tool = clutter_input_device_lookup_tool (input_device,
                                               tool_serial, tool_type);

      if (!tool)
        {
          tool = meta_input_device_tool_native_new (libinput_tool,
                                                    tool_serial, tool_type);
          clutter_input_device_add_tool (input_device, tool);
        }
    }

  if (evdev_device->last_tool != tool)
    {
      evdev_device->last_tool = tool;
      g_signal_emit_by_name (clutter_device_manager_get_default (),
                             "tool-changed", input_device, tool);
    }
}

static gdouble *
translate_tablet_axes (struct libinput_event_tablet_tool *tablet_event,
                       ClutterInputDeviceTool            *tool)
{
  GArray *axes = g_array_new (FALSE, FALSE, sizeof (gdouble));
  struct libinput_tablet_tool *libinput_tool;
  gdouble value;

  libinput_tool = libinput_event_tablet_tool_get_tool (tablet_event);

  value = libinput_event_tablet_tool_get_x (tablet_event);
  g_array_append_val (axes, value);
  value = libinput_event_tablet_tool_get_y (tablet_event);
  g_array_append_val (axes, value);

  if (libinput_tablet_tool_has_distance (libinput_tool))
    {
      value = libinput_event_tablet_tool_get_distance (tablet_event);
      g_array_append_val (axes, value);
    }

  if (libinput_tablet_tool_has_pressure (libinput_tool))
    {
      value = libinput_event_tablet_tool_get_pressure (tablet_event);
      value = meta_input_device_tool_native_translate_pressure (tool, value);
      g_array_append_val (axes, value);
    }

  if (libinput_tablet_tool_has_tilt (libinput_tool))
    {
      value = libinput_event_tablet_tool_get_tilt_x (tablet_event);
      g_array_append_val (axes, value);
      value = libinput_event_tablet_tool_get_tilt_y (tablet_event);
      g_array_append_val (axes, value);
    }

  if (libinput_tablet_tool_has_rotation (libinput_tool))
    {
      value = libinput_event_tablet_tool_get_rotation (tablet_event);
      g_array_append_val (axes, value);
    }

  if (libinput_tablet_tool_has_slider (libinput_tool))
    {
      value = libinput_event_tablet_tool_get_slider_position (tablet_event);
      g_array_append_val (axes, value);
    }

  if (libinput_tablet_tool_has_wheel (libinput_tool))
    {
      value = libinput_event_tablet_tool_get_wheel_delta (tablet_event);
      g_array_append_val (axes, value);
    }

  if (axes->len == 0)
    {
      g_array_free (axes, TRUE);
      return NULL;
    }
  else
    return (gdouble *) g_array_free (axes, FALSE);
}

static MetaSeatNative *
seat_from_device (ClutterInputDevice *device)
{
  MetaInputDeviceNative *device_evdev = META_INPUT_DEVICE_NATIVE (device);

  return meta_input_device_native_get_seat (device_evdev);
}

static void
notify_continuous_axis (MetaSeatNative                *seat,
                        ClutterInputDevice            *device,
                        uint64_t                       time_us,
                        ClutterScrollSource            scroll_source,
                        struct libinput_event_pointer *axis_event)
{
  gdouble dx = 0.0, dy = 0.0;
  ClutterScrollFinishFlags finish_flags = CLUTTER_SCROLL_FINISHED_NONE;

  if (libinput_event_pointer_has_axis (axis_event,
                                       LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL))
    {
      dx = libinput_event_pointer_get_axis_value (
          axis_event, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);

      if (fabs (dx) < DBL_EPSILON)
        finish_flags |= CLUTTER_SCROLL_FINISHED_HORIZONTAL;
    }
  if (libinput_event_pointer_has_axis (axis_event,
                                       LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL))
    {
      dy = libinput_event_pointer_get_axis_value (
          axis_event, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);

      if (fabs (dy) < DBL_EPSILON)
        finish_flags |= CLUTTER_SCROLL_FINISHED_VERTICAL;
    }

  meta_seat_native_notify_scroll_continuous (seat, device, time_us,
                                             dx, dy,
                                             scroll_source, finish_flags);
}

static void
notify_discrete_axis (MetaSeatNative                *seat,
                      ClutterInputDevice            *device,
                      uint64_t                       time_us,
                      ClutterScrollSource            scroll_source,
                      struct libinput_event_pointer *axis_event)
{
  gdouble discrete_dx = 0.0, discrete_dy = 0.0;

  if (libinput_event_pointer_has_axis (axis_event,
                                       LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL))
    {
      discrete_dx = libinput_event_pointer_get_axis_value_discrete (
          axis_event, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
    }
  if (libinput_event_pointer_has_axis (axis_event,
                                       LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL))
    {
      discrete_dy = libinput_event_pointer_get_axis_value_discrete (
          axis_event, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
    }

  meta_seat_native_notify_discrete_scroll (seat, device,
                                           time_us,
                                           discrete_dx, discrete_dy,
                                           scroll_source);
}

static void
process_tablet_axis (MetaDeviceManagerNative *manager_evdev,
                     struct libinput_event   *event)
{
  struct libinput_device *libinput_device = libinput_event_get_device (event);
  uint64_t time;
  double x, y, dx, dy, *axes;
  float stage_width, stage_height;
  ClutterStage *stage;
  ClutterInputDevice *device;
  struct libinput_event_tablet_tool *tablet_event =
    libinput_event_get_tablet_tool_event (event);
  MetaInputDeviceNative *evdev_device;

  device = libinput_device_get_user_data (libinput_device);
  evdev_device = META_INPUT_DEVICE_NATIVE (device);

  stage = _clutter_input_device_get_stage (device);
  if (!stage)
    return;

  axes = translate_tablet_axes (tablet_event,
                                evdev_device->last_tool);
  if (!axes)
    return;

  stage_width = clutter_actor_get_width (CLUTTER_ACTOR (stage));
  stage_height = clutter_actor_get_height (CLUTTER_ACTOR (stage));

  time = libinput_event_tablet_tool_get_time_usec (tablet_event);

  if (clutter_input_device_get_mapping_mode (device) == CLUTTER_INPUT_DEVICE_MAPPING_RELATIVE ||
      clutter_input_device_tool_get_tool_type (evdev_device->last_tool) == CLUTTER_INPUT_DEVICE_TOOL_MOUSE ||
      clutter_input_device_tool_get_tool_type (evdev_device->last_tool) == CLUTTER_INPUT_DEVICE_TOOL_LENS)
    {
      dx = libinput_event_tablet_tool_get_dx (tablet_event);
      dy = libinput_event_tablet_tool_get_dy (tablet_event);
      notify_relative_tool_motion (device, time, dx, dy, axes);
    }
  else
    {
      x = libinput_event_tablet_tool_get_x_transformed (tablet_event, stage_width);
      y = libinput_event_tablet_tool_get_y_transformed (tablet_event, stage_height);
      notify_absolute_motion (device, time, x, y, axes);
    }
}

static gboolean
process_device_event (MetaDeviceManagerNative *manager_evdev,
                      struct libinput_event   *event)
{
  gboolean handled = TRUE;
  struct libinput_device *libinput_device = libinput_event_get_device(event);
  ClutterInputDevice *device;
  MetaInputDeviceNative *device_evdev;

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
          break;

        meta_seat_native_notify_key (seat_from_device (device),
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

        meta_seat_native_notify_relative_motion (seat_from_device (device),
                                                 device,
                                                 time_us,
                                                 dx, dy,
                                                 dx_unaccel, dy_unaccel);

        break;
      }

    case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
      {
        uint64_t time_us;
        double x, y;
        float stage_width, stage_height;
        ClutterStage *stage;
        struct libinput_event_pointer *motion_event =
          libinput_event_get_pointer_event (event);
        device = libinput_device_get_user_data (libinput_device);

        stage = _clutter_input_device_get_stage (device);
        if (stage == NULL)
          break;

        stage_width = clutter_actor_get_width (CLUTTER_ACTOR (stage));
        stage_height = clutter_actor_get_height (CLUTTER_ACTOR (stage));

        time_us = libinput_event_pointer_get_time_usec (motion_event);
        x = libinput_event_pointer_get_absolute_x_transformed (motion_event,
                                                               stage_width);
        y = libinput_event_pointer_get_absolute_y_transformed (motion_event,
                                                               stage_height);

        meta_seat_native_notify_absolute_motion (seat_from_device (device),
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
          break;

        meta_seat_native_notify_button (seat_from_device (device), device,
                                        time_us, button, button_state);
        break;
      }

    case LIBINPUT_EVENT_POINTER_AXIS:
      {
        uint64_t time_us;
        enum libinput_pointer_axis_source source;
        struct libinput_event_pointer *axis_event =
          libinput_event_get_pointer_event (event);
        MetaSeatNative *seat;
        ClutterScrollSource scroll_source;

        device = libinput_device_get_user_data (libinput_device);
        seat = meta_input_device_native_get_seat (META_INPUT_DEVICE_NATIVE (device));

        time_us = libinput_event_pointer_get_time_usec (axis_event);
        source = libinput_event_pointer_get_axis_source (axis_event);
        scroll_source = translate_scroll_source (source);

        /* libinput < 0.8 sent wheel click events with value 10. Since 0.8
           the value is the angle of the click in degrees. To keep
           backwards-compat with existing clients, we just send multiples of
           the click count. */

        switch (scroll_source)
          {
          case CLUTTER_SCROLL_SOURCE_WHEEL:
            notify_discrete_axis (seat, device, time_us, scroll_source,
                                  axis_event);
            break;
          case CLUTTER_SCROLL_SOURCE_FINGER:
          case CLUTTER_SCROLL_SOURCE_CONTINUOUS:
          case CLUTTER_SCROLL_SOURCE_UNKNOWN:
            notify_continuous_axis (seat, device, time_us, scroll_source,
                                    axis_event);
            break;
          }
        break;
      }

    case LIBINPUT_EVENT_TOUCH_DOWN:
      {
        int device_slot;
        uint64_t time_us;
        double x, y;
        float stage_width, stage_height;
        MetaSeatNative *seat;
        ClutterStage *stage;
        MetaTouchState *touch_state;
        struct libinput_event_touch *touch_event =
          libinput_event_get_touch_event (event);

        device = libinput_device_get_user_data (libinput_device);
        device_evdev = META_INPUT_DEVICE_NATIVE (device);
        seat = meta_input_device_native_get_seat (device_evdev);

        stage = _clutter_input_device_get_stage (device);
        if (stage == NULL)
          break;

        stage_width = clutter_actor_get_width (CLUTTER_ACTOR (stage));
        stage_height = clutter_actor_get_height (CLUTTER_ACTOR (stage));

        device_slot = libinput_event_touch_get_slot (touch_event);
        time_us = libinput_event_touch_get_time_usec (touch_event);
        x = libinput_event_touch_get_x_transformed (touch_event,
                                                    stage_width);
        y = libinput_event_touch_get_y_transformed (touch_event,
                                                    stage_height);

        touch_state =
          meta_input_device_native_acquire_touch_state (device_evdev,
                                                        device_slot);
        touch_state->coords.x = x;
        touch_state->coords.y = y;

        meta_seat_native_notify_touch_event (seat, device,
                                             CLUTTER_TOUCH_BEGIN,
                                             time_us,
                                             touch_state->seat_slot,
                                             touch_state->coords.x,
                                             touch_state->coords.y);
        break;
      }

    case LIBINPUT_EVENT_TOUCH_UP:
      {
        int device_slot;
        uint64_t time_us;
        MetaSeatNative *seat;
        MetaTouchState *touch_state;
        struct libinput_event_touch *touch_event =
          libinput_event_get_touch_event (event);

        device = libinput_device_get_user_data (libinput_device);
        device_evdev = META_INPUT_DEVICE_NATIVE (device);
        seat = meta_input_device_native_get_seat (device_evdev);

        device_slot = libinput_event_touch_get_slot (touch_event);
        time_us = libinput_event_touch_get_time_usec (touch_event);
        touch_state =
          meta_input_device_native_lookup_touch_state (device_evdev,
                                                       device_slot);
        if (!touch_state)
          break;

        meta_seat_native_notify_touch_event (seat, device,
                                             CLUTTER_TOUCH_END, time_us,
                                             touch_state->seat_slot,
                                             touch_state->coords.x,
                                             touch_state->coords.y);
        meta_input_device_native_release_touch_state (device_evdev,
                                                      touch_state);
        break;
      }

    case LIBINPUT_EVENT_TOUCH_MOTION:
      {
        int device_slot;
        uint64_t time_us;
        double x, y;
        float stage_width, stage_height;
        MetaSeatNative *seat;
        ClutterStage *stage;
        MetaTouchState *touch_state;
        struct libinput_event_touch *touch_event =
          libinput_event_get_touch_event (event);

        device = libinput_device_get_user_data (libinput_device);
        device_evdev = META_INPUT_DEVICE_NATIVE (device);
        seat = meta_input_device_native_get_seat (device_evdev);

        stage = _clutter_input_device_get_stage (device);
        if (stage == NULL)
          break;

        stage_width = clutter_actor_get_width (CLUTTER_ACTOR (stage));
        stage_height = clutter_actor_get_height (CLUTTER_ACTOR (stage));

        device_slot = libinput_event_touch_get_slot (touch_event);
        time_us = libinput_event_touch_get_time_usec (touch_event);
        x = libinput_event_touch_get_x_transformed (touch_event,
                                                    stage_width);
        y = libinput_event_touch_get_y_transformed (touch_event,
                                                    stage_height);

        touch_state =
          meta_input_device_native_lookup_touch_state (device_evdev,
                                                       device_slot);
        if (!touch_state)
          break;

        touch_state->coords.x = x;
        touch_state->coords.y = y;

        meta_seat_native_notify_touch_event (seat, device,
                                             CLUTTER_TOUCH_UPDATE,
                                             time_us,
                                             touch_state->seat_slot,
                                             touch_state->coords.x,
                                             touch_state->coords.y);
        break;
      }
    case LIBINPUT_EVENT_TOUCH_CANCEL:
      {
        uint64_t time_us;
        struct libinput_event_touch *touch_event =
          libinput_event_get_touch_event (event);

        device = libinput_device_get_user_data (libinput_device);
        device_evdev = META_INPUT_DEVICE_NATIVE (device);
        time_us = libinput_event_touch_get_time_usec (touch_event);

        meta_input_device_native_release_touch_slots (device_evdev, time_us);

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
        notify_pinch_gesture_event (device, phase, time_us, 0, 0, 0, 0, n_fingers);
        break;
      }
    case LIBINPUT_EVENT_GESTURE_PINCH_UPDATE:
      {
        struct libinput_event_gesture *gesture_event =
          libinput_event_get_gesture_event (event);
        gdouble angle_delta, scale, dx, dy;
        uint32_t n_fingers;
        uint64_t time_us;

        n_fingers = libinput_event_gesture_get_finger_count (gesture_event);
        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_gesture_get_time_usec (gesture_event);
        angle_delta = libinput_event_gesture_get_angle_delta (gesture_event);
        scale = libinput_event_gesture_get_scale (gesture_event);
        dx = libinput_event_gesture_get_dx (gesture_event);
        dy = libinput_event_gesture_get_dx (gesture_event);

        notify_pinch_gesture_event (device,
                                    CLUTTER_TOUCHPAD_GESTURE_PHASE_UPDATE,
                                    time_us, dx, dy, angle_delta, scale, n_fingers);
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

        notify_swipe_gesture_event (device, phase, time_us, n_fingers, 0, 0);
        break;
      }
    case LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE:
      {
        struct libinput_event_gesture *gesture_event =
          libinput_event_get_gesture_event (event);
        uint32_t n_fingers;
        uint64_t time_us;
        double dx, dy;

        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_gesture_get_time_usec (gesture_event);
        n_fingers = libinput_event_gesture_get_finger_count (gesture_event);
        dx = libinput_event_gesture_get_dx (gesture_event);
        dy = libinput_event_gesture_get_dy (gesture_event);

        notify_swipe_gesture_event (device,
                                    CLUTTER_TOUCHPAD_GESTURE_PHASE_UPDATE,
                                    time_us, n_fingers, dx, dy);
        break;
      }
    case LIBINPUT_EVENT_TABLET_TOOL_AXIS:
      {
        process_tablet_axis (manager_evdev, event);
        break;
      }
    case LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY:
      {
        uint64_t time;
        struct libinput_event_tablet_tool *tablet_event =
          libinput_event_get_tablet_tool_event (event);
        struct libinput_tablet_tool *libinput_tool = NULL;
        enum libinput_tablet_tool_proximity_state state;

        state = libinput_event_tablet_tool_get_proximity_state (tablet_event);
        time = libinput_event_tablet_tool_get_time_usec (tablet_event);
        device = libinput_device_get_user_data (libinput_device);

        libinput_tool = libinput_event_tablet_tool_get_tool (tablet_event);

        if (state == LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_IN)
          input_device_update_tool (device, libinput_tool);
        notify_proximity (device, time, state == LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_IN);
        if (state == LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT)
          input_device_update_tool (device, NULL);
        break;
      }
    case LIBINPUT_EVENT_TABLET_TOOL_BUTTON:
      {
        uint64_t time_us;
        uint32_t button_state;
        struct libinput_event_tablet_tool *tablet_event =
          libinput_event_get_tablet_tool_event (event);
        uint32_t tablet_button;

        process_tablet_axis (manager_evdev, event);

        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_tablet_tool_get_time_usec (tablet_event);
        tablet_button = libinput_event_tablet_tool_get_button (tablet_event);

        button_state = libinput_event_tablet_tool_get_button_state (tablet_event) ==
                       LIBINPUT_BUTTON_STATE_PRESSED;

        meta_seat_native_notify_button (seat_from_device (device), device,
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
          process_tablet_axis (manager_evdev, event);

        meta_seat_native_notify_button (seat_from_device (device), device,
                                        time_us, BTN_TOUCH, button_state);
        if (!button_state)
          process_tablet_axis (manager_evdev, event);
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
    default:
      handled = FALSE;
    }

  return handled;
}

static gboolean
filter_event (MetaDeviceManagerNative *manager_evdev,
              struct libinput_event   *event)
{
  gboolean retval = CLUTTER_EVENT_PROPAGATE;
  MetaEventFilter *filter;
  GSList *tmp_list;

  tmp_list = manager_evdev->priv->event_filters;

  while (tmp_list)
    {
      filter = tmp_list->data;
      retval = filter->func (event, filter->data);
      tmp_list = tmp_list->next;

      if (retval != CLUTTER_EVENT_PROPAGATE)
        break;
    }

  return retval;
}

static void
process_event (MetaDeviceManagerNative *manager_evdev,
               struct libinput_event   *event)
{
  gboolean retval;

  retval = filter_event (manager_evdev, event);

  if (retval != CLUTTER_EVENT_PROPAGATE)
    return;

  if (process_base_event (manager_evdev, event))
    return;
  if (process_device_event (manager_evdev, event))
    return;
}

static void
process_events (MetaDeviceManagerNative *manager_evdev)
{
  MetaDeviceManagerNativePrivate *priv = manager_evdev->priv;
  struct libinput_event *event;

  while ((event = libinput_get_event (priv->libinput)))
    {
      process_event(manager_evdev, event);
      libinput_event_destroy(event);
    }
}

static int
open_restricted (const char *path,
                 int flags,
                 void *user_data)
{
  gint fd;

  if (device_open_callback)
    {
      GError *error = NULL;

      fd = device_open_callback (path, flags, device_callback_data, &error);

      if (fd < 0)
        {
          g_warning ("Could not open device %s: %s", path, error->message);
          g_error_free (error);
        }
    }
  else
    {
      fd = open (path, O_RDWR | O_NONBLOCK);
      if (fd < 0)
        {
          g_warning ("Could not open device %s: %s", path, strerror (errno));
        }
    }

  return fd;
}

static void
close_restricted (int fd,
                  void *user_data)
{
  if (device_close_callback)
    device_close_callback (fd, device_callback_data);
  else
    close (fd);
}

static const struct libinput_interface libinput_interface = {
  open_restricted,
  close_restricted
};

static ClutterVirtualInputDevice *
meta_device_manager_native_create_virtual_device (ClutterDeviceManager  *manager,
                                                  ClutterInputDeviceType device_type)
{
  MetaDeviceManagerNative *manager_evdev =
    META_DEVICE_MANAGER_NATIVE (manager);
  MetaDeviceManagerNativePrivate *priv = manager_evdev->priv;

  return g_object_new (META_TYPE_VIRTUAL_INPUT_DEVICE_NATIVE,
                       "device-manager", manager,
                       "seat", priv->main_seat,
                       "device-type", device_type,
                       NULL);
}

static ClutterVirtualDeviceType
meta_device_manager_native_get_supported_virtual_device_types (ClutterDeviceManager *device_manager)
{
  return (CLUTTER_VIRTUAL_DEVICE_TYPE_KEYBOARD |
          CLUTTER_VIRTUAL_DEVICE_TYPE_POINTER |
          CLUTTER_VIRTUAL_DEVICE_TYPE_TOUCHSCREEN);
}

static void
meta_device_manager_native_compress_motion (ClutterDeviceManager *device_manger,
                                            ClutterEvent         *event,
                                            const ClutterEvent   *to_discard)
{
  double dx, dy;
  double dx_unaccel, dy_unaccel;
  double dst_dx = 0.0, dst_dy = 0.0;
  double dst_dx_unaccel = 0.0, dst_dy_unaccel = 0.0;

  if (!meta_event_native_get_relative_motion (to_discard,
                                              &dx, &dy,
                                              &dx_unaccel, &dy_unaccel))
    return;

  meta_event_native_get_relative_motion (event,
                                         &dst_dx, &dst_dy,
                                         &dst_dx_unaccel, &dst_dy_unaccel);
  meta_event_native_set_relative_motion (event,
                                         dx + dst_dx,
                                         dy + dst_dy,
                                         dx_unaccel + dst_dx_unaccel,
                                         dy_unaccel + dst_dy_unaccel);
}

static void
meta_device_manager_native_apply_kbd_a11y_settings (ClutterDeviceManager   *device_manager,
                                                    ClutterKbdA11ySettings *settings)
{
  ClutterInputDevice *device;

  device = meta_device_manager_native_get_core_device (device_manager, CLUTTER_KEYBOARD_DEVICE);
  if (device)
    meta_input_device_native_apply_kbd_a11y_settings (META_INPUT_DEVICE_NATIVE (device),
                                                      settings);
}

/*
 * GObject implementation
 */

static void
meta_device_manager_native_constructed (GObject *object)
{
  MetaDeviceManagerNative *manager_evdev;
  MetaDeviceManagerNativePrivate *priv;
  MetaEventSource *source;
  struct udev *udev;

  udev = udev_new ();
  if (G_UNLIKELY (udev == NULL))
    {
      g_warning ("Failed to create udev object");
      return;
    }

  manager_evdev = META_DEVICE_MANAGER_NATIVE (object);
  priv = manager_evdev->priv;

  priv->libinput = libinput_udev_create_context (&libinput_interface,
                                                 manager_evdev,
                                                 udev);
  if (priv->libinput == NULL)
    {
      g_critical ("Failed to create the libinput object.");
      return;
    }

  if (libinput_udev_assign_seat (priv->libinput,
                                 evdev_seat_id ? evdev_seat_id : "seat0") == -1)
    {
      g_critical ("Failed to assign a seat to the libinput object.");
      libinput_unref (priv->libinput);
      priv->libinput = NULL;
      return;
    }

  udev_unref (udev);

  priv->main_seat = meta_seat_native_new (manager_evdev);
  priv->seats = g_slist_append (priv->seats, priv->main_seat);

  dispatch_libinput (manager_evdev);

  source = meta_event_source_new (manager_evdev);
  priv->event_source = source;
}

static void
meta_device_manager_native_dispose (GObject *object)
{
  MetaDeviceManagerNative *manager_evdev;
  MetaDeviceManagerNativePrivate *priv;

  manager_evdev = META_DEVICE_MANAGER_NATIVE (object);
  priv = manager_evdev->priv;

  if (priv->stage_added_handler)
    {
      g_signal_handler_disconnect (priv->stage_manager,
                                   priv->stage_added_handler);
      priv->stage_added_handler = 0;
    }

  if (priv->stage_removed_handler)
    {
      g_signal_handler_disconnect (priv->stage_manager,
                                   priv->stage_removed_handler);
      priv->stage_removed_handler = 0;
    }

  if (priv->stage_manager)
    {
      g_object_unref (priv->stage_manager);
      priv->stage_manager = NULL;
    }

  G_OBJECT_CLASS (meta_device_manager_native_parent_class)->dispose (object);
}

static void
meta_device_manager_native_finalize (GObject *object)
{
  MetaDeviceManagerNative *manager_evdev;
  MetaDeviceManagerNativePrivate *priv;

  manager_evdev = META_DEVICE_MANAGER_NATIVE (object);
  priv = manager_evdev->priv;

  g_slist_free_full (priv->seats, (GDestroyNotify) meta_seat_native_free);
  g_slist_free (priv->devices);

  if (priv->event_source != NULL)
    meta_event_source_free (priv->event_source);

  if (priv->constrain_data_notify != NULL)
    priv->constrain_data_notify (priv->constrain_data);

  if (priv->libinput != NULL)
    libinput_unref (priv->libinput);

  g_list_free (priv->free_device_ids);

  G_OBJECT_CLASS (meta_device_manager_native_parent_class)->finalize (object);
}

static void
meta_device_manager_native_class_init (MetaDeviceManagerNativeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterDeviceManagerClass *manager_class;

  object_class->constructed = meta_device_manager_native_constructed;
  object_class->finalize = meta_device_manager_native_finalize;
  object_class->dispose = meta_device_manager_native_dispose;

  manager_class = CLUTTER_DEVICE_MANAGER_CLASS (klass);
  manager_class->add_device = meta_device_manager_native_add_device;
  manager_class->remove_device = meta_device_manager_native_remove_device;
  manager_class->get_devices = meta_device_manager_native_get_devices;
  manager_class->get_core_device = meta_device_manager_native_get_core_device;
  manager_class->get_device = meta_device_manager_native_get_device;
  manager_class->create_virtual_device = meta_device_manager_native_create_virtual_device;
  manager_class->get_supported_virtual_device_types = meta_device_manager_native_get_supported_virtual_device_types;
  manager_class->compress_motion = meta_device_manager_native_compress_motion;
  manager_class->apply_kbd_a11y_settings = meta_device_manager_native_apply_kbd_a11y_settings;
  manager_class->copy_event_data = meta_device_manager_native_copy_event_data;
  manager_class->free_event_data = meta_device_manager_native_free_event_data;
}

static void
meta_device_manager_native_stage_added_cb (ClutterStageManager     *manager,
                                           ClutterStage            *stage,
                                           MetaDeviceManagerNative *self)
{
  MetaDeviceManagerNativePrivate *priv = self->priv;
  GSList *l;

  /* NB: Currently we can only associate a single stage with all evdev
   * devices.
   *
   * We save a pointer to the stage so if we release/reclaim input
   * devices due to switching virtual terminals then we know what
   * stage to re associate the devices with.
   */
  priv->stage = stage;

  /* Set the stage of any devices that don't already have a stage */
  for (l = priv->seats; l; l = l->next)
    {
      MetaSeatNative *seat = l->data;

      meta_seat_native_set_stage (seat, stage);
    }

  /* We only want to do this once so we can catch the default
     stage. If the application has multiple stages then it will need
     to manage the stage of the input devices itself */
  g_signal_handler_disconnect (priv->stage_manager,
                               priv->stage_added_handler);
  priv->stage_added_handler = 0;
}

static void
meta_device_manager_native_stage_removed_cb (ClutterStageManager     *manager,
                                             ClutterStage            *stage,
                                             MetaDeviceManagerNative *self)
{
  MetaDeviceManagerNativePrivate *priv = self->priv;
  GSList *l;

  /* Remove the stage of any input devices that were pointing to this
     stage so we don't send events to invalid stages */
  for (l = priv->seats; l; l = l->next)
    {
      MetaSeatNative *seat = l->data;

      meta_seat_native_set_stage (seat, NULL);
    }
}

static void
meta_device_manager_native_init (MetaDeviceManagerNative *self)
{
  MetaDeviceManagerNativePrivate *priv;

  priv = self->priv = meta_device_manager_native_get_instance_private (self);

  priv->stage_manager = clutter_stage_manager_get_default ();
  g_object_ref (priv->stage_manager);

  /* evdev doesn't have any way to link an event to a particular stage
     so we'll have to leave it up to applications to set the
     corresponding stage for an input device. However to make it
     easier for applications that are only using one fullscreen stage
     (which is probably the most frequent use-case for the evdev
     backend) we'll associate any input devices that don't have a
     stage with the first stage created. */
  priv->stage_added_handler =
    g_signal_connect (priv->stage_manager,
                      "stage-added",
                      G_CALLBACK (meta_device_manager_native_stage_added_cb),
                      self);
  priv->stage_removed_handler =
    g_signal_connect (priv->stage_manager,
                      "stage-removed",
                      G_CALLBACK (meta_device_manager_native_stage_removed_cb),
                      self);

  priv->device_id_next = INITIAL_DEVICE_ID;
}

gint
meta_device_manager_native_acquire_device_id (MetaDeviceManagerNative *manager_evdev)
{
  MetaDeviceManagerNativePrivate *priv = manager_evdev->priv;
  GList *first;
  gint next_id;

  if (priv->free_device_ids == NULL)
    {
      gint i;

      /* We ran out of free ID's, so append 10 new ones. */
      for (i = 0; i < 10; i++)
        priv->free_device_ids =
          g_list_append (priv->free_device_ids,
                         GINT_TO_POINTER (priv->device_id_next++));
    }

  first = g_list_first (priv->free_device_ids);
  next_id = GPOINTER_TO_INT (first->data);
  priv->free_device_ids = g_list_delete_link (priv->free_device_ids, first);

  return next_id;
}

void
meta_device_manager_native_dispatch (MetaDeviceManagerNative *manager_evdev)
{
  dispatch_libinput (manager_evdev);
}

static int
compare_ids (gconstpointer a,
             gconstpointer b)
{
  return GPOINTER_TO_INT (a) - GPOINTER_TO_INT (b);
}

void
meta_device_manager_native_release_device_id (MetaDeviceManagerNative *manager_evdev,
                                              ClutterInputDevice      *device)
{
  MetaDeviceManagerNativePrivate *priv = manager_evdev->priv;
  gint device_id;

  device_id = clutter_input_device_get_device_id (device);
  priv->free_device_ids = g_list_insert_sorted (priv->free_device_ids,
                                                GINT_TO_POINTER (device_id),
                                                compare_ids);
}

ClutterStage *
meta_device_manager_native_get_stage (MetaDeviceManagerNative *manager_evdev)
{
  MetaDeviceManagerNativePrivate *priv = manager_evdev->priv;

  return priv->stage;
}

/**
 * meta_device_manager_native_release_devices:
 *
 * Releases all the evdev devices that Clutter is currently managing. This api
 * is typically used when switching away from the Clutter application when
 * switching tty. The devices can be reclaimed later with a call to
 * meta_device_manager_native_reclaim_devices().
 *
 * This function should only be called after clutter has been initialized.
 *
 * Since: 1.10
 * Stability: unstable
 */
void
meta_device_manager_native_release_devices (void)
{
  ClutterDeviceManager *manager = clutter_device_manager_get_default ();
  MetaDeviceManagerNative *manager_evdev;
  MetaDeviceManagerNativePrivate *priv;

  if (!manager)
    {
      g_warning ("clutter_evdev_release_devices shouldn't be called "
                 "before clutter_init()");
      return;
    }

  g_return_if_fail (META_IS_DEVICE_MANAGER_NATIVE (manager));

  manager_evdev = META_DEVICE_MANAGER_NATIVE (manager);
  priv = manager_evdev->priv;

  if (priv->released)
    {
      g_warning ("clutter_evdev_release_devices() shouldn't be called "
                 "multiple times without a corresponding call to "
                 "clutter_evdev_reclaim_devices() first");
      return;
    }

  libinput_suspend (priv->libinput);
  process_events (manager_evdev);

  priv->released = TRUE;
}

static void
update_xkb_state (MetaDeviceManagerNative *manager_evdev)
{
  MetaDeviceManagerNativePrivate *priv;
  GSList *iter;
  MetaSeatNative *seat;
  xkb_mod_mask_t latched_mods;
  xkb_mod_mask_t locked_mods;
  struct xkb_keymap *xkb_keymap;
  ClutterKeymap *keymap;

  keymap = clutter_backend_get_keymap (clutter_get_default_backend ());
  xkb_keymap = meta_keymap_native_get_keyboard_map (META_KEYMAP_NATIVE (keymap));

  priv = manager_evdev->priv;

  for (iter = priv->seats; iter; iter = iter->next)
    {
      seat = iter->data;

      latched_mods = xkb_state_serialize_mods (seat->xkb,
                                               XKB_STATE_MODS_LATCHED);
      locked_mods = xkb_state_serialize_mods (seat->xkb,
                                              XKB_STATE_MODS_LOCKED);
      xkb_state_unref (seat->xkb);
      seat->xkb = xkb_state_new (xkb_keymap);

      xkb_state_update_mask (seat->xkb,
                             0, /* depressed */
                             latched_mods,
                             locked_mods,
                             0, 0, seat->layout_idx);

      seat->caps_lock_led = xkb_keymap_led_get_index (xkb_keymap, XKB_LED_NAME_CAPS);
      seat->num_lock_led = xkb_keymap_led_get_index (xkb_keymap, XKB_LED_NAME_NUM);
      seat->scroll_lock_led = xkb_keymap_led_get_index (xkb_keymap, XKB_LED_NAME_SCROLL);

      meta_seat_native_sync_leds (seat);
    }
}

/**
 * meta_device_manager_native_reclaim_devices:
 *
 * This causes Clutter to re-probe for evdev devices. This is must only be
 * called after a corresponding call to clutter_evdev_release_devices()
 * was previously used to release all evdev devices. This API is typically
 * used when a clutter application using evdev has regained focus due to
 * switching ttys.
 *
 * This function should only be called after clutter has been initialized.
 *
 * Since: 1.10
 * Stability: unstable
 */
void
meta_device_manager_native_reclaim_devices (void)
{
  ClutterDeviceManager *manager = clutter_device_manager_get_default ();
  MetaDeviceManagerNative *manager_evdev =
    META_DEVICE_MANAGER_NATIVE (manager);
  MetaDeviceManagerNativePrivate *priv = manager_evdev->priv;

  if (!priv->released)
    {
      g_warning ("Spurious call to clutter_evdev_reclaim_devices() without "
                 "previous call to clutter_evdev_release_devices");
      return;
    }

  libinput_resume (priv->libinput);
  update_xkb_state (manager_evdev);
  process_events (manager_evdev);

  priv->released = FALSE;
}

/**
 * meta_device_manager_native_set_device_callbacks: (skip)
 * @open_callback: the user replacement for open()
 * @close_callback: the user replacement for close()
 * @user_data: user data for @callback
 *
 * Through this function, the application can set a custom callback
 * to invoked when Clutter is about to open an evdev device. It can do
 * so if special handling is needed, for example to circumvent permission
 * problems.
 *
 * Setting @callback to %NULL will reset the default behavior.
 *
 * For reliable effects, this function must be called before clutter_init().
 *
 * Since: 1.16
 * Stability: unstable
 */
void
meta_device_manager_native_set_device_callbacks (MetaOpenDeviceCallback  open_callback,
                                                 MetaCloseDeviceCallback close_callback,
                                                 gpointer                user_data)
{
  device_open_callback = open_callback;
  device_close_callback = close_callback;
  device_callback_data = user_data;
}

/**
 * meta_device_manager_native_set_keyboard_map: (skip)
 * @evdev: the #ClutterDeviceManager created by the evdev backend
 * @keymap: the new keymap
 *
 * Instructs @evdev to use the speficied keyboard map. This will cause
 * the backend to drop the state and create a new one with the new
 * map. To avoid state being lost, callers should ensure that no key
 * is pressed when calling this function.
 *
 * Since: 1.16
 * Stability: unstable
 */
void
meta_device_manager_native_set_keyboard_map (ClutterDeviceManager *evdev,
                                             struct xkb_keymap    *xkb_keymap)
{
  MetaDeviceManagerNative *manager_evdev;
  ClutterKeymap *keymap;

  g_return_if_fail (META_IS_DEVICE_MANAGER_NATIVE (evdev));

  keymap = clutter_backend_get_keymap (clutter_get_default_backend ());
  meta_keymap_native_set_keyboard_map (META_KEYMAP_NATIVE (keymap),
                                       xkb_keymap);

  manager_evdev = META_DEVICE_MANAGER_NATIVE (evdev);
  update_xkb_state (manager_evdev);
}

/**
 * meta_device_manager_native_get_keyboard_map: (skip)
 * @evdev: the #ClutterDeviceManager created by the evdev backend
 *
 * Retrieves the #xkb_keymap in use by the evdev backend.
 *
 * Return value: the #xkb_keymap.
 *
 * Since: 1.18
 * Stability: unstable
 */
struct xkb_keymap *
meta_device_manager_native_get_keyboard_map (ClutterDeviceManager *evdev)
{
  MetaDeviceManagerNative *manager_evdev;

  g_return_val_if_fail (META_IS_DEVICE_MANAGER_NATIVE (evdev), NULL);

  manager_evdev = META_DEVICE_MANAGER_NATIVE (evdev);

  return xkb_state_get_keymap (manager_evdev->priv->main_seat->xkb);
}

/**
 * meta_device_manager_set_keyboard_layout_index: (skip)
 * @evdev: the #ClutterDeviceManager created by the evdev backend
 * @idx: the xkb layout index to set
 *
 * Sets the xkb layout index on the backend's #xkb_state .
 *
 * Since: 1.20
 * Stability: unstable
 */
void
meta_device_manager_native_set_keyboard_layout_index (ClutterDeviceManager *evdev,
                                                      xkb_layout_index_t    idx)
{
  MetaDeviceManagerNative *manager_evdev;
  xkb_mod_mask_t depressed_mods;
  xkb_mod_mask_t latched_mods;
  xkb_mod_mask_t locked_mods;
  struct xkb_state *state;
  GSList *l;

  g_return_if_fail (META_IS_DEVICE_MANAGER_NATIVE (evdev));

  manager_evdev = META_DEVICE_MANAGER_NATIVE (evdev);
  state = manager_evdev->priv->main_seat->xkb;

  depressed_mods = xkb_state_serialize_mods (state, XKB_STATE_MODS_DEPRESSED);
  latched_mods = xkb_state_serialize_mods (state, XKB_STATE_MODS_LATCHED);
  locked_mods = xkb_state_serialize_mods (state, XKB_STATE_MODS_LOCKED);

  xkb_state_update_mask (state, depressed_mods, latched_mods, locked_mods, 0, 0, idx);
  for (l = manager_evdev->priv->seats; l; l = l->next)
    {
      MetaSeatNative *seat = l->data;

      seat->layout_idx = idx;
    }
}

/**
 * clutter_evdev_get_keyboard_layout_index: (skip)
 */
xkb_layout_index_t
meta_device_manager_native_get_keyboard_layout_index (ClutterDeviceManager *evdev)
{
  MetaDeviceManagerNative *manager_evdev;

  manager_evdev = META_DEVICE_MANAGER_NATIVE (evdev);
  return manager_evdev->priv->main_seat->layout_idx;
}

/**
 * meta_device_manager_native_set_keyboard_numlock: (skip)
 * @evdev: the #ClutterDeviceManager created by the evdev backend
 * @numlock_set: TRUE to set NumLock ON, FALSE otherwise.
 *
 * Sets the NumLock state on the backend's #xkb_state .
 *
 * Stability: unstable
 */
void
meta_device_manager_native_set_keyboard_numlock (ClutterDeviceManager *evdev,
                                                 gboolean              numlock_state)
{
  MetaDeviceManagerNative *manager_evdev;
  MetaDeviceManagerNativePrivate *priv;
  GSList *iter;
  xkb_mod_mask_t numlock;
  struct xkb_keymap *xkb_keymap;
  ClutterKeymap *keymap;

  g_return_if_fail (META_IS_DEVICE_MANAGER_NATIVE (evdev));

  manager_evdev = META_DEVICE_MANAGER_NATIVE (evdev);
  priv = manager_evdev->priv;

  keymap = clutter_backend_get_keymap (clutter_get_default_backend ());
  xkb_keymap = meta_keymap_native_get_keyboard_map (META_KEYMAP_NATIVE (keymap));

  numlock = (1 << xkb_keymap_mod_get_index (xkb_keymap, "Mod2"));

  for (iter = priv->seats; iter; iter = iter->next)
    {
      MetaSeatNative *seat = iter->data;
      xkb_mod_mask_t depressed_mods;
      xkb_mod_mask_t latched_mods;
      xkb_mod_mask_t locked_mods;
      xkb_mod_mask_t group_mods;

      depressed_mods = xkb_state_serialize_mods (seat->xkb, XKB_STATE_MODS_DEPRESSED);
      latched_mods = xkb_state_serialize_mods (seat->xkb, XKB_STATE_MODS_LATCHED);
      locked_mods = xkb_state_serialize_mods (seat->xkb, XKB_STATE_MODS_LOCKED);
      group_mods = xkb_state_serialize_layout (seat->xkb, XKB_STATE_LAYOUT_EFFECTIVE);

      if (numlock_state)
        locked_mods |= numlock;
      else
        locked_mods &= ~numlock;

      xkb_state_update_mask (seat->xkb,
                             depressed_mods,
                             latched_mods,
                             locked_mods,
                             0, 0,
                             group_mods);

      meta_seat_native_sync_leds (seat);
    }
}


/**
 * meta_device_manager_native_set_pointer_constrain_callback:
 * @evdev: the #ClutterDeviceManager created by the evdev backend
 * @callback: the callback
 * @user_data: data to pass to the callback
 * @user_data_notify: function to be called when removing the callback
 *
 * Sets a callback to be invoked for every pointer motion. The callback
 * can then modify the new pointer coordinates to constrain movement within
 * a specific region.
 *
 * Since: 1.16
 * Stability: unstable
 */
void
meta_device_manager_native_set_pointer_constrain_callback (ClutterDeviceManager         *evdev,
                                                           MetaPointerConstrainCallback  callback,
                                                           gpointer                      user_data,
                                                           GDestroyNotify                user_data_notify)
{
  MetaDeviceManagerNative *manager_evdev;
  MetaDeviceManagerNativePrivate *priv;

  g_return_if_fail (META_IS_DEVICE_MANAGER_NATIVE (evdev));

  manager_evdev = META_DEVICE_MANAGER_NATIVE (evdev);
  priv = manager_evdev->priv;

  if (priv->constrain_data_notify)
    priv->constrain_data_notify (priv->constrain_data);

  priv->constrain_callback = callback;
  priv->constrain_data = user_data;
  priv->constrain_data_notify = user_data_notify;
}

void
meta_device_manager_native_set_relative_motion_filter (ClutterDeviceManager     *evdev,
                                                       MetaRelativeMotionFilter  filter,
                                                       gpointer                  user_data)
{
  MetaDeviceManagerNative *manager_evdev;
  MetaDeviceManagerNativePrivate *priv;

  g_return_if_fail (META_IS_DEVICE_MANAGER_NATIVE (evdev));

  manager_evdev = META_DEVICE_MANAGER_NATIVE (evdev);
  priv = manager_evdev->priv;

  priv->relative_motion_filter = filter;
  priv->relative_motion_filter_user_data = user_data;
}

/**
 * meta_device_manager_native_set_keyboard_repeat:
 * @evdev: the #ClutterDeviceManager created by the evdev backend
 * @repeat: whether to enable or disable keyboard repeat events
 * @delay: the delay in ms between the hardware key press event and
 * the first synthetic event
 * @interval: the period in ms between consecutive synthetic key
 * press events
 *
 * Enables or disables sythetic key press events, allowing for initial
 * delay and interval period to be specified.
 *
 * Since: 1.18
 * Stability: unstable
 */
void
meta_device_manager_native_set_keyboard_repeat (ClutterDeviceManager *evdev,
                                                gboolean              repeat,
                                                uint32_t              delay,
                                                uint32_t              interval)
{
  MetaDeviceManagerNative *manager_evdev;
  MetaSeatNative *seat;

  g_return_if_fail (META_IS_DEVICE_MANAGER_NATIVE (evdev));

  manager_evdev = META_DEVICE_MANAGER_NATIVE (evdev);
  seat = manager_evdev->priv->main_seat;

  seat->repeat = repeat;
  seat->repeat_delay = delay;
  seat->repeat_interval = interval;
}

/**
 * meta_device_manager_native_add_filter: (skip)
 * @func: (closure data): a filter function
 * @data: (allow-none): user data to be passed to the filter function, or %NULL
 * @destroy_notify: (allow-none): function to call on @data when the filter is removed, or %NULL
 *
 * Adds an event filter function.
 *
 * Since: 1.20
 * Stability: unstable
 */
void
meta_device_manager_native_add_filter (MetaEvdevFilterFunc func,
                                       gpointer            data,
                                       GDestroyNotify      destroy_notify)
{
  MetaDeviceManagerNative *manager_evdev;
  ClutterDeviceManager *manager;
  MetaEventFilter *filter;

  g_return_if_fail (func != NULL);

  manager = clutter_device_manager_get_default ();

  if (!META_IS_DEVICE_MANAGER_NATIVE (manager))
    {
      g_critical ("The Clutter input backend is not a evdev backend");
      return;
    }

  manager_evdev = META_DEVICE_MANAGER_NATIVE (manager);

  filter = g_new0 (MetaEventFilter, 1);
  filter->func = func;
  filter->data = data;
  filter->destroy_notify = destroy_notify;

  manager_evdev->priv->event_filters =
    g_slist_append (manager_evdev->priv->event_filters, filter);
}

/**
 * meta_device_manager_native_remove_filter: (skip)
 * @func: a filter function
 * @data: (allow-none): user data to be passed to the filter function, or %NULL
 *
 * Removes the given filter function.
 *
 * Since: 1.20
 * Stability: unstable
 */
void
meta_device_manager_native_remove_filter (MetaEvdevFilterFunc func,
                                          gpointer            data)
{
  MetaDeviceManagerNative *manager_evdev;
  ClutterDeviceManager *manager;
  MetaEventFilter *filter;
  GSList *tmp_list;

  g_return_if_fail (func != NULL);

  manager = clutter_device_manager_get_default ();

  if (!META_IS_DEVICE_MANAGER_NATIVE (manager))
    {
      g_critical ("The Clutter input backend is not a evdev backend");
      return;
    }

  manager_evdev = META_DEVICE_MANAGER_NATIVE (manager);
  tmp_list = manager_evdev->priv->event_filters;

  while (tmp_list)
    {
      filter = tmp_list->data;

      if (filter->func == func && filter->data == data)
        {
          if (filter->destroy_notify)
            filter->destroy_notify (filter->data);
          g_free (filter);
          manager_evdev->priv->event_filters =
            g_slist_delete_link (manager_evdev->priv->event_filters, tmp_list);
          return;
        }

      tmp_list = tmp_list->next;
    }
}

/**
 * meta_device_manager_native_warp_pointer:
 * @pointer_device: the pointer device to warp
 * @time: the timestamp for the warp event
 * @x: the new X position of the pointer
 * @y: the new Y position of the pointer
 *
 * Warps the pointer to a new location. Technically, this is
 * processed the same way as an absolute motion event from
 * libinput: it simply generates an absolute motion event that
 * will be processed on the next iteration of the mainloop.
 *
 * The intended use for this is for display servers that need
 * to warp cursor the cursor to a new location.
 *
 * Since: 1.20
 * Stability: unstable
 */
void
meta_device_manager_native_warp_pointer (ClutterInputDevice   *pointer_device,
                                         uint32_t              time_,
                                         int                   x,
                                         int                   y)
{
  notify_absolute_motion (pointer_device, ms2us(time_), x, y, NULL);
}

/**
 * meta_device_manager_native_set_seat_id:
 * @seat_id: The seat ID
 *
 * Sets the seat to assign to the libinput context.
 *
 * For reliable effects, this function must be called before clutter_init().
 */
void
meta_device_manager_native_set_seat_id (const gchar *seat_id)
{
  g_free (evdev_seat_id);
  evdev_seat_id = g_strdup (seat_id);
}

struct xkb_state *
meta_device_manager_native_get_xkb_state (MetaDeviceManagerNative *manager_evdev)
{
  return manager_evdev->priv->main_seat->xkb;
}
