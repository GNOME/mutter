/*
 * Copyright (C) 2019 Red Hat
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
 * Author: Olivier Fourdan <ofourdan@redhat.com>
 *
 * This reimplements in Clutter the same behavior as mousetweaks original
 * implementation by Gerd Kohlberger <gerdko gmail com>
 * mousetweaks Copyright (C) 2007-2010 Gerd Kohlberger <gerdko gmail com>
 */

#include "config.h"

#include "clutter/clutter-backend-private.h"
#include "clutter/clutter-context-private.h"
#include "clutter/clutter-enum-types.h"
#include "clutter/clutter-input-device.h"
#include "clutter/clutter-input-device-private.h"
#include "clutter/clutter-input-pointer-a11y-private.h"
#include "clutter/clutter-main.h"
#include "clutter/clutter-private.h"
#include "clutter/clutter-seat-private.h"
#include "clutter/clutter-virtual-input-device.h"

typedef struct _ClutterPtrA11yData
{
  int n_btn_pressed;
  float current_x;
  float current_y;

  float dwell_x;
  float dwell_y;
  gboolean dwell_drag_started;
  gboolean dwell_gesture_started;
  guint dwell_timer;
  guint dwell_position_timer;

  guint secondary_click_timer;
  gboolean secondary_click_triggered;
} ClutterPtrA11yData;

static GQuark quark_ptr_a11y_data;
static GQuark quark_a11y_device;

static ClutterPtrA11yData *
ptr_a11y_data_from_seat (ClutterSeat *seat)
{
  return g_object_get_qdata (G_OBJECT (seat), quark_ptr_a11y_data);
}

static ClutterVirtualInputDevice *
a11y_virtual_input_device_from_seat (ClutterSeat *seat)
{
  return g_object_get_qdata (G_OBJECT (seat), quark_a11y_device);
}

static gboolean
is_secondary_click_enabled (ClutterSeat *seat)
{
  ClutterPointerA11ySettings settings;

  clutter_seat_get_pointer_a11y_settings (seat, &settings);

  return (settings.controls & CLUTTER_A11Y_SECONDARY_CLICK_ENABLED);
}

static gboolean
is_dwell_click_enabled (ClutterSeat *seat)
{
  ClutterPointerA11ySettings settings;

  clutter_seat_get_pointer_a11y_settings (seat, &settings);

  return (settings.controls & CLUTTER_A11Y_DWELL_ENABLED);
}

static unsigned int
get_secondary_click_delay (ClutterSeat *seat)
{
  ClutterPointerA11ySettings settings;

  clutter_seat_get_pointer_a11y_settings (seat, &settings);

  return settings.secondary_click_delay;
}

static unsigned int
get_dwell_delay (ClutterSeat *seat)
{
  ClutterPointerA11ySettings settings;

  clutter_seat_get_pointer_a11y_settings (seat, &settings);

  return settings.dwell_delay;
}

static unsigned int
get_dwell_threshold (ClutterSeat *seat)
{
  ClutterPointerA11ySettings settings;

  clutter_seat_get_pointer_a11y_settings (seat, &settings);

  return settings.dwell_threshold;
}

static ClutterPointerA11yDwellMode
get_dwell_mode (ClutterSeat *seat)
{
  ClutterPointerA11ySettings settings;

  clutter_seat_get_pointer_a11y_settings (seat, &settings);

  return settings.dwell_mode;
}

static ClutterPointerA11yDwellClickType
get_dwell_click_type (ClutterSeat *seat)
{
  ClutterPointerA11ySettings settings;

  clutter_seat_get_pointer_a11y_settings (seat, &settings);

  return settings.dwell_click_type;
}

static ClutterPointerA11yDwellClickType
get_dwell_click_type_for_direction (ClutterSeat                      *seat,
                                    ClutterPointerA11yDwellDirection  direction)
{
  ClutterPointerA11ySettings settings;

  clutter_seat_get_pointer_a11y_settings (seat, &settings);

  if (direction == settings.dwell_gesture_single)
    return CLUTTER_A11Y_DWELL_CLICK_TYPE_PRIMARY;
  else if (direction == settings.dwell_gesture_double)
    return CLUTTER_A11Y_DWELL_CLICK_TYPE_DOUBLE;
  else if (direction == settings.dwell_gesture_drag)
    return CLUTTER_A11Y_DWELL_CLICK_TYPE_DRAG;
  else if (direction == settings.dwell_gesture_secondary)
    return CLUTTER_A11Y_DWELL_CLICK_TYPE_SECONDARY;

  return CLUTTER_A11Y_DWELL_CLICK_TYPE_NONE;
}

static void
emit_button_press (ClutterSeat *seat,
                   gint         button)
{
  ClutterVirtualInputDevice *a11y_virtual_device =
    a11y_virtual_input_device_from_seat (seat);

  clutter_virtual_input_device_notify_button (a11y_virtual_device,
                                              g_get_monotonic_time (),
                                              button,
                                              CLUTTER_BUTTON_STATE_PRESSED);
}

static void
emit_button_release (ClutterSeat *seat,
                     gint         button)
{
  ClutterVirtualInputDevice *a11y_virtual_device =
    a11y_virtual_input_device_from_seat (seat);

  clutter_virtual_input_device_notify_button (a11y_virtual_device,
                                              g_get_monotonic_time (),
                                              button,
                                              CLUTTER_BUTTON_STATE_RELEASED);
}

static void
emit_button_click (ClutterSeat *seat,
                   gint         button)
{
  emit_button_press (seat, button);
  emit_button_release (seat, button);
}

static void
restore_dwell_position (ClutterSeat *seat)
{
  ClutterPtrA11yData *ptr_a11y_data = ptr_a11y_data_from_seat (seat);
  ClutterVirtualInputDevice *a11y_virtual_device =
    a11y_virtual_input_device_from_seat (seat);

  clutter_virtual_input_device_notify_absolute_motion (a11y_virtual_device,
                                                       g_get_monotonic_time (),
                                                       ptr_a11y_data->dwell_x,
                                                       ptr_a11y_data->dwell_y);
}

static void
trigger_secondary_click (gpointer data)
{
  ClutterSeat *seat = data;
  ClutterPtrA11yData *ptr_a11y_data = ptr_a11y_data_from_seat (seat);

  ptr_a11y_data->secondary_click_triggered = TRUE;
  ptr_a11y_data->secondary_click_timer = 0;

  g_signal_emit_by_name (seat,
                         "ptr-a11y-timeout-stopped",
                         CLUTTER_A11Y_TIMEOUT_TYPE_SECONDARY_CLICK,
                         TRUE);
}

static void
start_secondary_click_timeout (ClutterSeat *seat)
{
  ClutterPtrA11yData *ptr_a11y_data = ptr_a11y_data_from_seat (seat);
  unsigned int delay = get_secondary_click_delay (seat);

  ptr_a11y_data->secondary_click_timer =
    g_timeout_add_once (delay, trigger_secondary_click, seat);

  g_signal_emit_by_name (seat,
                         "ptr-a11y-timeout-started",
                         CLUTTER_A11Y_TIMEOUT_TYPE_SECONDARY_CLICK,
                         delay);
}

static void
stop_secondary_click_timeout (ClutterSeat *seat)
{
  ClutterPtrA11yData *ptr_a11y_data = ptr_a11y_data_from_seat (seat);

  if (ptr_a11y_data->secondary_click_timer)
    {
      g_clear_handle_id (&ptr_a11y_data->secondary_click_timer,
                         g_source_remove);

      g_signal_emit_by_name (seat,
                             "ptr-a11y-timeout-stopped",
                             CLUTTER_A11Y_TIMEOUT_TYPE_SECONDARY_CLICK,
                             FALSE);
    }
  ptr_a11y_data->secondary_click_triggered = FALSE;
}

static gboolean
pointer_has_moved (ClutterSeat *seat)
{
  ClutterPtrA11yData *ptr_a11y_data = ptr_a11y_data_from_seat (seat);
  float dx, dy;
  gint threshold;

  dx = ptr_a11y_data->dwell_x - ptr_a11y_data->current_x;
  dy = ptr_a11y_data->dwell_y - ptr_a11y_data->current_y;
  threshold = get_dwell_threshold (seat);

  /* Pythagorean theorem */
  return ((dx * dx) + (dy * dy)) > (threshold * threshold);
}

static gboolean
is_secondary_click_pending (ClutterSeat *seat)
{
  ClutterPtrA11yData *ptr_a11y_data = ptr_a11y_data_from_seat (seat);

  return ptr_a11y_data->secondary_click_timer != 0;
}

static gboolean
is_secondary_click_triggered (ClutterSeat *seat)
{
  ClutterPtrA11yData *ptr_a11y_data = ptr_a11y_data_from_seat (seat);

  return ptr_a11y_data->secondary_click_triggered;
}

static gboolean
is_dwell_click_pending (ClutterSeat *seat)
{
  ClutterPtrA11yData *ptr_a11y_data = ptr_a11y_data_from_seat (seat);

  return ptr_a11y_data->dwell_timer != 0;
}

static gboolean
is_dwell_dragging (ClutterSeat *seat)
{
  ClutterPtrA11yData *ptr_a11y_data = ptr_a11y_data_from_seat (seat);

  return ptr_a11y_data->dwell_drag_started;
}

static gboolean
is_dwell_gesturing (ClutterSeat *seat)
{
  ClutterPtrA11yData *ptr_a11y_data = ptr_a11y_data_from_seat (seat);

  return ptr_a11y_data->dwell_gesture_started;
}

static gboolean
has_button_pressed (ClutterSeat *seat)
{
  ClutterPtrA11yData *ptr_a11y_data = ptr_a11y_data_from_seat (seat);

  return ptr_a11y_data->n_btn_pressed > 0;
}

static gboolean
should_start_secondary_click_timeout (ClutterSeat *seat)
{
  return !is_dwell_dragging (seat);
}

static gboolean
should_start_dwell (ClutterSeat *seat)
{
  /* We should trigger a dwell if we've not already started one, and if
   * no button is currently pressed or we are in the middle of a dwell
   * drag action.
   */
  return !is_dwell_click_pending (seat) &&
         (is_dwell_dragging (seat) ||
          !has_button_pressed (seat));
}

static gboolean
should_stop_dwell (ClutterSeat *seat)
{
  /* We should stop a dwell if the motion exceeds the threshold, unless
   * we've started a gesture, because we want to keep the original dwell
   * location to both detect a gesture and restore the original pointer
   * location once the gesture is finished.
   */
  return pointer_has_moved (seat) &&
         !is_dwell_gesturing (seat);
}


static gboolean
should_update_dwell_position (ClutterSeat *seat)
{
  return !is_dwell_gesturing (seat) &&
         !is_dwell_click_pending (seat) &&
         !is_secondary_click_pending (seat);
}

static void
update_dwell_click_type (ClutterSeat *seat)
{
  ClutterPointerA11ySettings settings;
  ClutterPointerA11yDwellClickType dwell_click_type;

  clutter_seat_get_pointer_a11y_settings (seat, &settings);

  dwell_click_type = settings.dwell_click_type;
  switch (dwell_click_type)
    {
    case CLUTTER_A11Y_DWELL_CLICK_TYPE_DOUBLE:
    case CLUTTER_A11Y_DWELL_CLICK_TYPE_SECONDARY:
    case CLUTTER_A11Y_DWELL_CLICK_TYPE_MIDDLE:
      dwell_click_type = CLUTTER_A11Y_DWELL_CLICK_TYPE_PRIMARY;
      break;

    case CLUTTER_A11Y_DWELL_CLICK_TYPE_DRAG:
      if (!is_dwell_dragging (seat))
        dwell_click_type = CLUTTER_A11Y_DWELL_CLICK_TYPE_PRIMARY;
      break;

    case CLUTTER_A11Y_DWELL_CLICK_TYPE_PRIMARY:
    case CLUTTER_A11Y_DWELL_CLICK_TYPE_NONE:
    default:
      break;
    }

  if (dwell_click_type != settings.dwell_click_type)
    {
      settings.dwell_click_type = dwell_click_type;
      clutter_seat_set_pointer_a11y_settings (seat, &settings);

      g_signal_emit_by_name (seat,
                             "ptr-a11y-dwell-click-type-changed",
                             dwell_click_type);
    }
}

static void
emit_dwell_click (ClutterSeat                      *seat,
                  ClutterPointerA11yDwellClickType  dwell_click_type)
{
  ClutterPtrA11yData *ptr_a11y_data = ptr_a11y_data_from_seat (seat);

  switch (dwell_click_type)
    {
    case CLUTTER_A11Y_DWELL_CLICK_TYPE_PRIMARY:
      emit_button_click (seat, CLUTTER_BUTTON_PRIMARY);
      break;

    case CLUTTER_A11Y_DWELL_CLICK_TYPE_DOUBLE:
      emit_button_click (seat, CLUTTER_BUTTON_PRIMARY);
      emit_button_click (seat, CLUTTER_BUTTON_PRIMARY);
      break;

    case CLUTTER_A11Y_DWELL_CLICK_TYPE_DRAG:
      if (is_dwell_dragging (seat))
        {
          emit_button_release (seat, CLUTTER_BUTTON_PRIMARY);
          ptr_a11y_data->dwell_drag_started = FALSE;
        }
      else
        {
          emit_button_press (seat, CLUTTER_BUTTON_PRIMARY);
          ptr_a11y_data->dwell_drag_started = TRUE;
        }
      break;

    case CLUTTER_A11Y_DWELL_CLICK_TYPE_SECONDARY:
      emit_button_click (seat, CLUTTER_BUTTON_SECONDARY);
      break;

    case CLUTTER_A11Y_DWELL_CLICK_TYPE_MIDDLE:
      emit_button_click (seat, CLUTTER_BUTTON_MIDDLE);
      break;

    case CLUTTER_A11Y_DWELL_CLICK_TYPE_NONE:
    default:
      break;
    }
}

static ClutterPointerA11yDwellDirection
get_dwell_direction (ClutterSeat *seat)
{
  ClutterPtrA11yData *ptr_a11y_data = ptr_a11y_data_from_seat (seat);
  float dx, dy;

  dx = ABS (ptr_a11y_data->dwell_x - ptr_a11y_data->current_x);
  dy = ABS (ptr_a11y_data->dwell_y - ptr_a11y_data->current_y);

  /* The pointer hasn't moved */
  if (!pointer_has_moved (seat))
    return CLUTTER_A11Y_DWELL_DIRECTION_NONE;

  if (ptr_a11y_data->dwell_x < ptr_a11y_data->current_x)
    {
      if (dx > dy)
        return CLUTTER_A11Y_DWELL_DIRECTION_LEFT;
    }
  else
    {
      if (dx > dy)
        return CLUTTER_A11Y_DWELL_DIRECTION_RIGHT;
    }

  if (ptr_a11y_data->dwell_y < ptr_a11y_data->current_y)
    return CLUTTER_A11Y_DWELL_DIRECTION_UP;

  return CLUTTER_A11Y_DWELL_DIRECTION_DOWN;
}

static void
trigger_clear_dwell_gesture (gpointer data)
{
  ClutterSeat *seat = data;
  ClutterPtrA11yData *ptr_a11y_data = ptr_a11y_data_from_seat (seat);

  ptr_a11y_data->dwell_timer = 0;
  ptr_a11y_data->dwell_gesture_started = FALSE;
}

static void
trigger_dwell_gesture (gpointer data)
{
  ClutterSeat *seat = data;
  ClutterPtrA11yData *ptr_a11y_data = ptr_a11y_data_from_seat (seat);
  ClutterPointerA11yDwellDirection direction;
  unsigned int delay = get_dwell_delay (seat);

  restore_dwell_position (seat);
  direction = get_dwell_direction (seat);
  emit_dwell_click (seat,
                    get_dwell_click_type_for_direction (seat,
                                                        direction));

  /* Do not clear the gesture right away, otherwise we'll start another one */
  ptr_a11y_data->dwell_timer =
    g_timeout_add_once (delay, trigger_clear_dwell_gesture, seat);

  g_signal_emit_by_name (seat,
                         "ptr-a11y-timeout-stopped",
                         CLUTTER_A11Y_TIMEOUT_TYPE_GESTURE,
                         TRUE);
}

static void
start_dwell_gesture_timeout (ClutterSeat *seat)
{
  ClutterPtrA11yData *ptr_a11y_data = ptr_a11y_data_from_seat (seat);
  unsigned int delay = get_dwell_delay (seat);

  ptr_a11y_data->dwell_timer =
    g_timeout_add_once (delay, trigger_dwell_gesture, seat);
  ptr_a11y_data->dwell_gesture_started = TRUE;

  g_signal_emit_by_name (seat,
                         "ptr-a11y-timeout-started",
                         CLUTTER_A11Y_TIMEOUT_TYPE_GESTURE,
                         delay);
}

static void
trigger_dwell_click (gpointer data)
{
  ClutterSeat *seat = data;
  ClutterPtrA11yData *ptr_a11y_data = ptr_a11y_data_from_seat (seat);

  ptr_a11y_data->dwell_timer = 0;

  g_signal_emit_by_name (seat,
                         "ptr-a11y-timeout-stopped",
                         CLUTTER_A11Y_TIMEOUT_TYPE_DWELL,
                         TRUE);

  if (get_dwell_mode (seat) == CLUTTER_A11Y_DWELL_MODE_GESTURE)
    {
      if (is_dwell_dragging (seat))
        emit_dwell_click (seat, CLUTTER_A11Y_DWELL_CLICK_TYPE_DRAG);
      else
        start_dwell_gesture_timeout (seat);
    }
  else
    {
      emit_dwell_click (seat, get_dwell_click_type (seat));
      update_dwell_click_type (seat);
    }
}

static void
start_dwell_timeout (ClutterSeat *seat)
{
  ClutterPtrA11yData *ptr_a11y_data = ptr_a11y_data_from_seat (seat);
  unsigned int delay = get_dwell_delay (seat);

  ptr_a11y_data->dwell_timer =
    g_timeout_add_once (delay, trigger_dwell_click, seat);

  g_signal_emit_by_name (seat,
                         "ptr-a11y-timeout-started",
                         CLUTTER_A11Y_TIMEOUT_TYPE_DWELL,
                         delay);
}

static void
stop_dwell_timeout (ClutterSeat *seat)
{
  ClutterPtrA11yData *ptr_a11y_data = ptr_a11y_data_from_seat (seat);

  if (ptr_a11y_data->dwell_timer)
    {
      g_clear_handle_id (&ptr_a11y_data->dwell_timer, g_source_remove);
      ptr_a11y_data->dwell_gesture_started = FALSE;

      g_signal_emit_by_name (seat,
                             "ptr-a11y-timeout-stopped",
                             CLUTTER_A11Y_TIMEOUT_TYPE_DWELL,
                             FALSE);
    }
}

static void
trigger_dwell_position_timeout (gpointer data)
{
  ClutterSeat *seat = data;
  ClutterPtrA11yData *ptr_a11y_data = ptr_a11y_data_from_seat (seat);

  ptr_a11y_data->dwell_position_timer = 0;

  if (is_dwell_click_enabled (seat))
    {
      if (!pointer_has_moved (seat))
        start_dwell_timeout (seat);
    }
}

static void
start_dwell_position_timeout (ClutterSeat *seat)
{
  ClutterPtrA11yData *ptr_a11y_data = ptr_a11y_data_from_seat (seat);

  ptr_a11y_data->dwell_position_timer =
    g_timeout_add_once (100, trigger_dwell_position_timeout, seat);
}

static void
stop_dwell_position_timeout (ClutterSeat *seat)
{
  ClutterPtrA11yData *ptr_a11y_data = ptr_a11y_data_from_seat (seat);

  g_clear_handle_id (&ptr_a11y_data->dwell_position_timer,
                     g_source_remove);
}

static void
update_dwell_position (ClutterSeat *seat)
{
  ClutterPtrA11yData *ptr_a11y_data = ptr_a11y_data_from_seat (seat);

  ptr_a11y_data->dwell_x = ptr_a11y_data->current_x;
  ptr_a11y_data->dwell_y = ptr_a11y_data->current_y;
}

static void
update_current_position (ClutterSeat *seat,
                         float        x,
                         float        y)
{
  ClutterPtrA11yData *ptr_a11y_data = ptr_a11y_data_from_seat (seat);

  ptr_a11y_data->current_x = x;
  ptr_a11y_data->current_y = y;
}

void
_clutter_seat_init_a11y (ClutterSeat *seat)
{
  ClutterPtrA11yData *ptr_a11y_data;
  ClutterVirtualInputDevice *a11y_virtual_device;

  quark_ptr_a11y_data = g_quark_from_static_string ("-clutter-seat-ptr-a11y-data");
  ptr_a11y_data = g_new0 (ClutterPtrA11yData, 1);
  g_object_set_qdata_full (G_OBJECT (seat),
                           quark_ptr_a11y_data,
                           ptr_a11y_data,
                           g_free);

  quark_a11y_device = g_quark_from_static_string ("-clutter-seat-a11y-device");
  a11y_virtual_device =
    clutter_seat_create_virtual_device (seat,
                                        CLUTTER_POINTER_DEVICE);
  g_object_set_qdata_full (G_OBJECT (seat),
                           quark_a11y_device,
                           a11y_virtual_device,
                           g_object_unref);
}

void
_clutter_seat_shutdown_a11y (ClutterSeat *seat)
{
  /* Terminate a drag if started */
  if (is_dwell_dragging (seat))
    emit_dwell_click (seat, CLUTTER_A11Y_DWELL_CLICK_TYPE_DRAG);

  stop_dwell_position_timeout (seat);
  stop_dwell_timeout (seat);
  stop_secondary_click_timeout (seat);

  g_object_set_qdata (G_OBJECT (seat), quark_ptr_a11y_data, NULL);
  g_object_set_qdata (G_OBJECT (seat), quark_a11y_device, NULL);
}

void
_clutter_seat_a11y_on_motion_event (ClutterSeat *seat,
                                    float        x,
                                    float        y)
{
  if (!_clutter_seat_is_pointer_a11y_enabled (seat))
    return;

  update_current_position (seat, x, y);

  if (is_secondary_click_enabled (seat))
    {
      if (pointer_has_moved (seat))
        stop_secondary_click_timeout (seat);
    }

  if (is_dwell_click_enabled (seat))
    {
      stop_dwell_position_timeout (seat);

      if (should_stop_dwell (seat))
        stop_dwell_timeout (seat);

      if (should_start_dwell (seat))
        start_dwell_position_timeout (seat);
    }

  if (should_update_dwell_position (seat))
    update_dwell_position (seat);
}

void
_clutter_seat_a11y_on_button_event (ClutterSeat *seat,
                                    int          button,
                                    gboolean     pressed)
{
  ClutterPtrA11yData *ptr_a11y_data = ptr_a11y_data_from_seat (seat);

  if (!_clutter_seat_is_pointer_a11y_enabled (seat))
    return;

  if (pressed)
    {
      ptr_a11y_data->n_btn_pressed++;

      stop_dwell_position_timeout (seat);

      if (is_dwell_click_enabled (seat))
        stop_dwell_timeout (seat);

      if (is_dwell_dragging (seat))
        stop_dwell_timeout (seat);

      if (is_secondary_click_enabled (seat))
        {
          if (button == CLUTTER_BUTTON_PRIMARY)
            {
              if (should_start_secondary_click_timeout (seat))
                start_secondary_click_timeout (seat);
            }
          else if (is_secondary_click_pending (seat))
            {
              stop_secondary_click_timeout (seat);
            }
        }
    }
  else
    {
      if (has_button_pressed (seat))
        ptr_a11y_data->n_btn_pressed--;

      if (is_secondary_click_triggered (seat))
        {
          emit_button_click (seat, CLUTTER_BUTTON_SECONDARY);
          stop_secondary_click_timeout (seat);
        }

      if (is_secondary_click_pending (seat))
        stop_secondary_click_timeout (seat);

      if (is_dwell_dragging (seat))
        emit_dwell_click (seat, CLUTTER_A11Y_DWELL_CLICK_TYPE_DRAG);
    }
}

gboolean
_clutter_seat_is_pointer_a11y_enabled (ClutterSeat *seat)
{
  g_return_val_if_fail (CLUTTER_IS_SEAT (seat), FALSE);

  return (is_secondary_click_enabled (seat) || is_dwell_click_enabled (seat));
}

void
clutter_seat_a11y_update (ClutterSeat        *seat,
                          const ClutterEvent *event)
{
  ClutterContext *context;
  ClutterBackend *backend;
  ClutterEventType event_type;

  g_return_if_fail (CLUTTER_IS_SEAT (seat));

  if (!_clutter_seat_is_pointer_a11y_enabled (seat))
    return;

  if ((clutter_event_get_flags (event) & CLUTTER_EVENT_FLAG_SYNTHETIC) != 0)
    return;

  context = clutter_seat_get_context (seat);
  backend = context->backend;

  if (!clutter_backend_is_display_server (backend))
    return;

  event_type = clutter_event_type (event);

  if (event_type == CLUTTER_MOTION)
    {
      float x, y;

      clutter_event_get_coords (event, &x, &y);
      _clutter_seat_a11y_on_motion_event (seat, x, y);
    }
  else if (event_type == CLUTTER_BUTTON_PRESS ||
           event_type == CLUTTER_BUTTON_RELEASE)
    {
      _clutter_seat_a11y_on_button_event (seat,
                                          clutter_event_get_button (event),
                                          event_type == CLUTTER_BUTTON_PRESS);
    }
}
