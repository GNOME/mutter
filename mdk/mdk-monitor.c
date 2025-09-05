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
#include "mdk-seat.h"
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

  gboolean emulated_touch_down;
};

G_DEFINE_FINAL_TYPE (MdkMonitor, mdk_monitor, GTK_TYPE_BOX)

static MdkPointer *
get_pointer (MdkMonitor *monitor)
{
  MdkSession *session = mdk_context_get_session (monitor->context);
  MdkSeat *seat;

  g_assert (!mdk_context_get_emulate_touch (monitor->context));

  seat = mdk_session_get_default_seat (session);
  if (!seat)
    return NULL;

  return mdk_seat_get_pointer (seat);
}

static MdkKeyboard *
get_keyboard (MdkMonitor *monitor)
{
  MdkSession *session = mdk_context_get_session (monitor->context);
  MdkSeat *seat;

  seat = mdk_session_get_default_seat (session);
  if (!seat)
    return NULL;

  return mdk_seat_get_keyboard (seat);
}

static MdkTouch *
get_touch (MdkMonitor *monitor)
{
  MdkSession *session = mdk_context_get_session (monitor->context);
  MdkSeat *seat;

  seat = mdk_session_get_default_seat (session);
  if (!seat)
    return NULL;

  return mdk_seat_get_touch (seat);
}

static void
update_cursor (MdkMonitor *monitor)
{
  g_autoptr (GdkCursor) cursor = NULL;

  if (mdk_context_get_emulate_touch (monitor->context))
    cursor = gdk_cursor_new_from_name ("pointer", NULL);
  else
    cursor = gdk_cursor_new_from_name ("none", NULL);

  gtk_widget_set_cursor (GTK_WIDGET (monitor), cursor);
}

static void
on_emulate_touch_changed (MdkContext *context,
                          GParamSpec *pspec,
                          MdkMonitor *monitor)
{
  update_cursor (monitor);
}

static void
on_pointer_motion (GtkEventControllerMotion *controller,
                   double                    x,
                   double                    y,
                   MdkMonitor               *monitor)
{
  MdkContext *context = monitor->context;
  GdkEvent *event;

  event =
    gtk_event_controller_get_current_event (GTK_EVENT_CONTROLLER (controller));
  if (!event)
    return;

  if (mdk_context_get_emulate_touch (context))
    {
      if (monitor->emulated_touch_down)
        {
          MdkTouch *touch;

          touch = get_touch (monitor);
          if (touch)
            mdk_touch_notify_motion (touch, 0, x, y);
        }
    }
  else
    {
      MdkPointer *pointer;

      pointer = get_pointer (monitor);
      if (pointer)
        mdk_pointer_notify_motion (pointer, x, y);
    }
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

static gboolean
on_scroll (GtkEventControllerScroll *controller,
           double                    dx,
           double                    dy,
           MdkMonitor               *monitor)
{
  MdkPointer *pointer;
  GdkEvent *event;
  GdkScrollDirection direction;

  if (mdk_context_get_emulate_touch (monitor->context))
    return TRUE;

  event =
    gtk_event_controller_get_current_event (GTK_EVENT_CONTROLLER (controller));

  pointer = get_pointer (monitor);
  if (!pointer)
    return TRUE;

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

  if (mdk_context_get_emulate_touch (monitor->context))
    return TRUE;

  pointer = get_pointer (monitor);
  if (pointer)
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

  if (mdk_context_get_emulate_touch (monitor->context))
     return TRUE;

  evdev_key_code = gdk_key_code_to_evdev (keycode);
  keyboard = get_keyboard (monitor);
  if (!keyboard)
    return TRUE;

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

  if (mdk_context_get_emulate_touch (monitor->context))
    return TRUE;

  evdev_key_code = gdk_key_code_to_evdev (keycode);
  keyboard = get_keyboard (monitor);
  if (!keyboard)
    return TRUE;

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

static void
handle_touch_event (MdkMonitor *monitor,
                    GdkEvent   *event)
{
  GdkEventType event_type;
  MdkTouch *touch;
  double x, y;
  int slot;

  touch = get_touch (monitor);
  if (!touch)
    return;

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
handle_button_event (MdkMonitor *monitor,
                     GdkEvent   *event)
{
  double x, y;
  unsigned int gtk_button_code;

  if (!gtk_widget_has_focus (GTK_WIDGET (monitor)))
    gtk_widget_grab_focus (GTK_WIDGET (monitor));

  if (!calc_event_widget_coordinates (event, &x, &y, GTK_WIDGET (monitor)))
    return;

  gtk_button_code = gdk_button_event_get_button (event);

  switch (gdk_event_get_event_type (event))
    {
    case GDK_BUTTON_PRESS:
      if (mdk_context_get_emulate_touch (monitor->context))
        {
          if (gtk_button_code == GDK_BUTTON_PRIMARY)
            {
              MdkTouch *touch;

              touch = get_touch (monitor);
              if (touch)
                {
                  mdk_touch_notify_down (touch, 0, x, y);
                  monitor->emulated_touch_down = TRUE;
                }
            }
        }
      else
        {
          MdkPointer *pointer;
          uint32_t evdev_button_code;

          evdev_button_code = gdk_button_code_to_evdev (gtk_button_code);
          pointer = get_pointer (monitor);
          if (pointer)
            {
              mdk_pointer_notify_button (pointer,
                                         evdev_button_code,
                                         1);
            }
        }
      break;
    case GDK_BUTTON_RELEASE:
      if (mdk_context_get_emulate_touch (monitor->context))
        {
          if (gtk_button_code == GDK_BUTTON_PRIMARY)
            {
              MdkTouch *touch;

              touch = get_touch (monitor);
              if (touch)
                {
                  mdk_touch_notify_up (touch, 0);
                  monitor->emulated_touch_down = FALSE;
                }
            }
        }
      else
        {
          MdkPointer *pointer;
          uint32_t evdev_button_code;

          evdev_button_code = gdk_button_code_to_evdev (gtk_button_code);
          pointer = get_pointer (monitor);
          if (pointer)
            {
              mdk_pointer_notify_button (pointer,
                                         evdev_button_code,
                                         0);
            }
        }
      break;
    default:
      g_assert_not_reached ();
    }
}

static gboolean
is_button_event (GdkEvent *event)
{
  switch (gdk_event_get_event_type (event))
    {
    case GDK_BUTTON_PRESS:
    case GDK_BUTTON_RELEASE:
      return TRUE;
    default:
      return FALSE;
    }
}

static gboolean
on_event (GtkEventControllerLegacy *controller,
          GdkEvent                 *event,
          MdkMonitor               *monitor)
{
  if (is_touch_event (event))
    {
      if (monitor->emulated_touch_down)
        return GDK_EVENT_PROPAGATE;
      else
        handle_touch_event (monitor, event);
    }
  else if (is_button_event (event))
    {
      handle_button_event (monitor, event);
    }

  return GDK_EVENT_PROPAGATE;
}

static void
maybe_release_all_keys_and_buttons (MdkMonitor *monitor)
{
  GtkWindow *window;

  window = GTK_WINDOW (gtk_widget_get_root (GTK_WIDGET (monitor)));

  if (!gtk_widget_has_focus (GTK_WIDGET (monitor)) ||
      !gtk_window_is_active (window))
    {
      MdkSession *session = mdk_context_get_session (monitor->context);
      MdkSeat *seat;

      seat = mdk_session_get_default_seat (session);
      if (seat)
        {
          MdkPointer *pointer;
          MdkKeyboard *keyboard;
          MdkTouch *touch;

          pointer = mdk_seat_get_pointer (seat);
          if (pointer)
            mdk_pointer_release_all (pointer);

          keyboard = mdk_seat_get_keyboard (seat);
          if (keyboard)
            mdk_keyboard_release_all (keyboard);

          touch = mdk_seat_get_touch (seat);
          if (touch)
            mdk_touch_release_all (touch);
        }
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
  GtkEventController *scroll_controller;
  GtkEventController *key_controller;
  GtkEventController *event_controller;

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

  event_controller = gtk_event_controller_legacy_new ();
  g_signal_connect (event_controller, "event",
                    G_CALLBACK (on_event), monitor);
  gtk_widget_add_controller (GTK_WIDGET (monitor), event_controller);

  g_signal_connect (monitor, "notify::has-focus",
                    G_CALLBACK (has_focus_changed),
                    NULL);
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

  update_cursor (monitor);
  g_signal_connect_object (context, "notify::emulate-touch",
                           G_CALLBACK (on_emulate_touch_changed),
                           monitor, 0);

  return monitor;
}

MdkStream *
mdk_monitor_get_stream (MdkMonitor *monitor)
{
  return monitor->stream;
}
