/*
 * Copyright (C) 2021-2024 Red Hat Inc.
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

#include "config.h"

#include "mdk-monitor.h"

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <linux/input-event-codes.h>

#include "mdk-context.h"
#include "mdk-keyboard.h"
#include "mdk-pointer.h"
#include "mdk-session.h"
#include "mdk-stream.h"
#include "mdk-touch.h"

#define DEFAULT_MONITOR_WIDTH 1280
#define DEFAULT_MONITOR_HEIGHT 800

#define BUTTON_BASE (BTN_LEFT - 1)

struct _MdkMonitor
{
  GtkBox parent;

  GtkPicture *picture;

  MdkContext *context;
  MdkStream *stream;

  MdkPointer *pointer;
  MdkKeyboard *keyboard;
  MdkTouch *touch;
};

G_DEFINE_FINAL_TYPE (MdkMonitor, mdk_monitor, GTK_TYPE_BOX)

static MdkPointer *
ensure_pointer (MdkMonitor *monitor)
{
  MdkSession *session = mdk_context_get_session (monitor->context);

  if (monitor->pointer)
    return monitor->pointer;

  monitor->pointer = mdk_session_create_pointer (session, monitor);
  return monitor->pointer;
}

static MdkKeyboard *
ensure_keyboard (MdkMonitor *monitor)
{
  MdkSession *session = mdk_context_get_session (monitor->context);

  if (monitor->keyboard)
    return monitor->keyboard;

  monitor->keyboard = mdk_session_create_keyboard (session);
  return monitor->keyboard;
}

static MdkTouch *
ensure_touch (MdkMonitor *monitor)
{
  MdkSession *session = mdk_context_get_session (monitor->context);

  if (monitor->touch)
    return monitor->touch;

  monitor->touch = mdk_session_create_touch (session, monitor);
  return monitor->touch;
}

static void
on_pointer_motion (GtkEventControllerMotion *controller,
                   double                    x,
                   double                    y,
                   MdkMonitor               *monitor)
{
  MdkPointer *pointer;

  pointer = ensure_pointer (monitor);

  mdk_pointer_notify_motion (pointer, x, y);
}

static uint32_t
gdk_button_code_to_evdev (unsigned int gtk_button_code)
{
  switch (gtk_button_code)
    {
    case GDK_BUTTON_PRIMARY:
      return BTN_LEFT;
    case GDK_BUTTON_MIDDLE:
      return BTN_MIDDLE;
    case GDK_BUTTON_SECONDARY:
      return BTN_RIGHT;
    default:
      return gtk_button_code + BUTTON_BASE - 4;
    }
}

static void
click_pressed_cb (GtkGestureClick *gesture,
                  unsigned int     n_press,
                  double           x,
                  double           y,
                  MdkMonitor      *monitor)
{
  MdkPointer *pointer;
  unsigned int gtk_button_code;
  uint32_t evdev_button_code;

  gtk_button_code =
    gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture));
  evdev_button_code = gdk_button_code_to_evdev (gtk_button_code);

  pointer = ensure_pointer (monitor);

  mdk_pointer_notify_button (pointer,
                             evdev_button_code,
                             1);
}

static void
click_released_cb (GtkGestureClick *gesture,
                   unsigned int     n_press,
                   double           x,
                   double           y,
                   MdkMonitor      *monitor)
{
  MdkPointer *pointer;
  unsigned int gtk_button_code;
  uint32_t evdev_button_code;

  gtk_button_code =
    gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture));
  evdev_button_code = gdk_button_code_to_evdev (gtk_button_code);

  pointer = ensure_pointer (monitor);

  mdk_pointer_notify_button (pointer,
                             evdev_button_code,
                             0);

  gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}

static gboolean
on_scroll (GtkEventControllerScroll *controller,
           double                    dx,
           double                    dy,
           MdkMonitor               *monitor)
{
  MdkPointer *pointer;
  GdkEvent *event;
  GdkScrollDirection direction;

  event =
    gtk_event_controller_get_current_event (GTK_EVENT_CONTROLLER (controller));

  pointer = ensure_pointer (monitor);

  direction = gdk_scroll_event_get_direction (event);
  switch (direction)
    {
    case GDK_SCROLL_UP:
    case GDK_SCROLL_DOWN:
    case GDK_SCROLL_LEFT:
    case GDK_SCROLL_RIGHT:
      mdk_pointer_notify_scroll_discrete (pointer, direction);
      break;
    case GDK_SCROLL_SMOOTH:
      mdk_pointer_notify_scroll (pointer, dx * 10.0, dy * 10.0);
      break;
    }

  return TRUE;
}

static gboolean
on_scroll_end (GtkEventControllerScroll *controller,
               double                    dx,
               double                    dy,
               MdkMonitor               *monitor)
{
  MdkPointer *pointer;

  pointer = ensure_pointer (monitor);
  mdk_pointer_notify_scroll_end (pointer);

  return TRUE;
}

static uint32_t
gdk_key_code_to_evdev (unsigned int gtk_key_code)
{
  return gtk_key_code - 8;
}

static gboolean
on_key_pressed (GtkEventControllerKey *controller,
                unsigned int           keyval,
                unsigned int           keycode,
                GdkModifierType        state,
                MdkMonitor            *monitor)
{
  MdkKeyboard *keyboard;
  uint32_t evdev_key_code;

  evdev_key_code = gdk_key_code_to_evdev (keycode);
  keyboard = ensure_keyboard (monitor);
  mdk_keyboard_notify_key (keyboard, evdev_key_code, 1);

  return TRUE;
}

static gboolean
on_key_released (GtkEventControllerKey *controller,
                 unsigned int           keyval,
                 unsigned int           keycode,
                 GdkModifierType        state,
                 MdkMonitor            *monitor)
{
  MdkKeyboard *keyboard;
  uint32_t evdev_key_code;

  evdev_key_code = gdk_key_code_to_evdev (keycode);
  keyboard = ensure_keyboard (monitor);
  mdk_keyboard_notify_key (keyboard, evdev_key_code, 0);

  return TRUE;
}

static GtkWidget *
get_event_widget (GdkEvent *event)
{
  GdkSurface *surface;

  surface = gdk_event_get_surface (event);
  if (surface && !gdk_surface_is_destroyed (surface))
    return GTK_WIDGET (gtk_native_get_for_surface (surface));

  return NULL;
}

static gboolean
calc_event_widget_coordinates (GdkEvent  *event,
                               double    *x,
                               double    *y,
                               GtkWidget *widget)
{
  GtkWidget *event_widget;
  double event_x, event_y;
  GtkNative *native;
  double nx, ny;
  graphene_point_t point;

  *x = *y = 0;

  if (!gdk_event_get_position (event, &event_x, &event_y))
    return FALSE;

  event_widget = get_event_widget (event);
  native = gtk_widget_get_native (event_widget);
  gtk_native_get_surface_transform (native, &nx, &ny);
  event_x -= nx;
  event_y -= ny;

  if (gtk_widget_compute_point (event_widget,
                                widget,
                                &GRAPHENE_POINT_INIT ((float) event_x,
                                                      (float) event_y),
                                &point))
    {
      *x = point.x;
      *y = point.y;
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

static gboolean
is_touch_event (GdkEvent *event)
{
  switch (gdk_event_get_event_type (event))
    {
    case GDK_TOUCH_BEGIN:
    case GDK_TOUCH_UPDATE:
    case GDK_TOUCH_END:
    case GDK_TOUCH_CANCEL:
      return TRUE;
    default:
      return FALSE;
    }
}

static void
touch_event (GtkEventControllerLegacy *controller,
             GdkEvent                 *event,
             MdkMonitor               *monitor)
{
  GdkEventType event_type;
  MdkTouch *touch;
  double x, y;
  int slot;

  if (!is_touch_event (event))
    return;

  touch = ensure_touch (monitor);

  slot = GPOINTER_TO_INT (gdk_event_get_event_sequence (event)) - 1;

  event_type = gdk_event_get_event_type (event);
  switch (event_type)
    {
    case GDK_TOUCH_BEGIN:
      if (calc_event_widget_coordinates (event, &x, &y, GTK_WIDGET (monitor)))
        mdk_touch_notify_down (touch, slot, x, y);
      break;
    case GDK_TOUCH_UPDATE:
      if (calc_event_widget_coordinates (event, &x, &y, GTK_WIDGET (monitor)))
        mdk_touch_notify_motion (touch, slot, x, y);
      break;
    case GDK_TOUCH_END:
    case GDK_TOUCH_CANCEL:
      mdk_touch_notify_up (touch, slot);
      break;
    default:
      break;
    }
}

static void
maybe_release_all_keys_and_buttons (MdkMonitor *monitor)
{
  GtkWindow *window;

  window = GTK_WINDOW (gtk_widget_get_root (GTK_WIDGET (monitor)));

  if (!gtk_widget_has_focus (GTK_WIDGET (monitor)) ||
      !gtk_window_is_active (window))
    {
      if (monitor->pointer)
        mdk_pointer_release_all (monitor->pointer);
      if (monitor->keyboard)
        mdk_keyboard_release_all (monitor->keyboard);
    }
}

static void
is_active_changed (GtkWindow   *window,
                   GParamSpec  *pspec,
                   MdkMonitor  *monitor)
{
  maybe_release_all_keys_and_buttons (monitor);
}

static void
has_focus_changed (GtkWidget *widget)
{
  maybe_release_all_keys_and_buttons (MDK_MONITOR (widget));
}

static void
mdk_monitor_realize (GtkWidget *widget)
{
  MdkMonitor *monitor = MDK_MONITOR (widget);

  GTK_WIDGET_CLASS (mdk_monitor_parent_class)->realize (widget);

  mdk_stream_realize (monitor->stream);
}

static void
mdk_monitor_unrealize (GtkWidget *widget)
{
  MdkMonitor *monitor = MDK_MONITOR (widget);

  mdk_stream_unrealize (monitor->stream);

  GTK_WIDGET_CLASS (mdk_monitor_parent_class)->unrealize (widget);
}

static void
mdk_monitor_map (GtkWidget *widget)
{
  MdkMonitor *monitor = MDK_MONITOR (widget);
  GtkWindow *window;

  GTK_WIDGET_CLASS (mdk_monitor_parent_class)->map (widget);

  window = GTK_WINDOW (gtk_widget_get_root (GTK_WIDGET (monitor)));
  g_signal_connect_object (window, "notify::is-active",
                           G_CALLBACK (is_active_changed),
                           monitor, 0);
}

static void
mdk_monitor_unmap (GtkWidget *widget)
{
  MdkMonitor *monitor = MDK_MONITOR (widget);
  GtkWindow *window;

  window = GTK_WINDOW (gtk_widget_get_root (GTK_WIDGET (monitor)));
  g_signal_handlers_disconnect_by_func (window, is_active_changed, monitor);

  GTK_WIDGET_CLASS (mdk_monitor_parent_class)->unmap (widget);
}

static gboolean
mdk_monitor_focus (GtkWidget        *widget,
                   GtkDirectionType  direction)
{
  if (!gtk_widget_is_focus (widget))
    {
      gtk_widget_grab_focus (widget);
      return TRUE;
    }

  return FALSE;
}

static void
mdk_monitor_finalize (GObject *object)
{
  MdkMonitor *monitor = MDK_MONITOR (object);

  g_clear_object (&monitor->touch);
  g_clear_object (&monitor->keyboard);
  g_clear_object (&monitor->pointer);
  g_clear_object (&monitor->stream);

  G_OBJECT_CLASS (mdk_monitor_parent_class)->finalize (object);
}

static void
mdk_monitor_class_init (MdkMonitorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = mdk_monitor_finalize;

  widget_class->realize = mdk_monitor_realize;
  widget_class->unrealize = mdk_monitor_unrealize;
  widget_class->focus = mdk_monitor_focus;
  widget_class->map = mdk_monitor_map;
  widget_class->unmap = mdk_monitor_unmap;
}

static void
mdk_monitor_init (MdkMonitor *monitor)
{
  GtkEventController *motion_controller;
  GtkGesture *click_gesture;
  GtkEventController *scroll_controller;
  GtkEventController *key_controller;
  GtkEventController *touch_controller;
  g_autoptr (GdkCursor) none_cursor = NULL;

  motion_controller = gtk_event_controller_motion_new ();
  g_signal_connect (motion_controller,
                    "enter",
                    G_CALLBACK (on_pointer_motion),
                    monitor);
  g_signal_connect (motion_controller,
                    "motion",
                    G_CALLBACK (on_pointer_motion),
                    monitor);
  gtk_widget_add_controller (GTK_WIDGET (monitor), motion_controller);

  click_gesture = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click_gesture), 0);
  g_signal_connect (click_gesture, "pressed",
                    G_CALLBACK (click_pressed_cb), monitor);
  g_signal_connect (click_gesture, "released",
                    G_CALLBACK (click_released_cb), monitor);
  gtk_widget_add_controller (GTK_WIDGET (monitor),
                             GTK_EVENT_CONTROLLER (click_gesture));

  scroll_controller =
    gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
  g_signal_connect (scroll_controller, "scroll",
                    G_CALLBACK (on_scroll), monitor);
  g_signal_connect (scroll_controller, "scroll-end",
                    G_CALLBACK (on_scroll_end), monitor);
  gtk_widget_add_controller (GTK_WIDGET (monitor),
                             GTK_EVENT_CONTROLLER (scroll_controller));

  key_controller = gtk_event_controller_key_new () ;
  g_signal_connect (key_controller, "key-pressed",
                    G_CALLBACK (on_key_pressed), monitor);
  g_signal_connect (key_controller, "key-released",
                    G_CALLBACK (on_key_released), monitor);
  gtk_widget_add_controller (GTK_WIDGET (monitor), key_controller);

  touch_controller = gtk_event_controller_legacy_new ();
  g_signal_connect (touch_controller, "event",
                    G_CALLBACK (touch_event), monitor);
  gtk_widget_add_controller (GTK_WIDGET (monitor), touch_controller);

  g_signal_connect (monitor, "notify::has-focus",
                    G_CALLBACK (has_focus_changed),
                    NULL);

  none_cursor = gdk_cursor_new_from_name ("none", NULL);
  gtk_widget_set_cursor (GTK_WIDGET (monitor), none_cursor);
}

static void
on_stream_error (MdkStream    *stream,
                 const GError *error,
                 MdkMonitor   *monitor)
{
  GtkWidget *label;

  label = gtk_label_new (_("Failed to create monitor"));
  gtk_widget_set_size_request (label,
                               DEFAULT_MONITOR_WIDTH,
                               DEFAULT_MONITOR_HEIGHT);
  gtk_box_append (GTK_BOX (monitor), label);
  gtk_widget_set_visible (GTK_WIDGET (monitor->picture), FALSE);

  g_warning ("Failed to create monitor: %s", error->message);
}

MdkMonitor *
mdk_monitor_new (MdkContext *context)
{
  MdkSession *session = mdk_context_get_session (context);
  MdkMonitor *monitor;
  GdkPaintable *paintable;

  monitor = g_object_new (MDK_TYPE_MONITOR,
                          "orientation", GTK_ORIENTATION_VERTICAL,
                          "vexpand", TRUE,
                          "hexpand", TRUE,
                          "focusable", TRUE,
                          NULL);
  monitor->context = context;
  monitor->stream = mdk_stream_new (session,
                                    DEFAULT_MONITOR_WIDTH,
                                    DEFAULT_MONITOR_HEIGHT);
  g_signal_connect (monitor->stream,
                    "error",
                    G_CALLBACK (on_stream_error),
                    monitor);

  paintable = GDK_PAINTABLE (monitor->stream);
  monitor->picture = GTK_PICTURE (gtk_picture_new_for_paintable (paintable));
  gtk_widget_add_css_class (GTK_WIDGET (monitor->picture), "monitor");
  gtk_widget_set_sensitive (GTK_WIDGET (monitor->picture), FALSE);
  gtk_box_append (GTK_BOX (monitor), GTK_WIDGET (monitor->picture));

  return monitor;
}

MdkStream *
mdk_monitor_get_stream (MdkMonitor *monitor)
{
  return monitor->stream;
}
