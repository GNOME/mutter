/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "meta-backend-x11.h"
#include "meta-input-settings-x11.h"

#include <stdlib.h>
#include <string.h>
#include <gdk/gdkx.h>
#include <X11/Xatom.h>
#include <X11/extensions/XInput2.h>
#include <X11/XKBlib.h>
#ifdef HAVE_LIBGUDEV
#include <gudev/gudev.h>
#endif
#ifdef __linux
#include <sys/prctl.h>
#endif

#include <meta/errors.h>
#include "backends/meta-logical-monitor.h"

typedef struct _MetaInputSettingsX11Private
{
#ifdef HAVE_LIBGUDEV
  GUdevClient *udev_client;
#endif
  gboolean syndaemon_spawned;
  GPid syndaemon_pid;
} MetaInputSettingsX11Private;

G_DEFINE_TYPE_WITH_PRIVATE (MetaInputSettingsX11, meta_input_settings_x11,
                            META_TYPE_INPUT_SETTINGS)

enum {
  SCROLL_METHOD_FIELD_2FG,
  SCROLL_METHOD_FIELD_EDGE,
  SCROLL_METHOD_FIELD_BUTTON,
  SCROLL_METHOD_NUM_FIELDS
};

static void
device_free_xdevice (gpointer user_data)
{
  MetaDisplay *display = meta_get_display ();
  MetaBackend *backend = meta_get_backend ();
  Display *xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));
  XDevice *xdev = user_data;

  meta_error_trap_push (display);
  XCloseDevice (xdisplay, xdev);
  meta_error_trap_pop (display);
}

static XDevice *
device_ensure_xdevice (ClutterInputDevice *device)
{
  MetaDisplay *display = meta_get_display ();
  MetaBackend *backend = meta_get_backend ();
  Display *xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));
  int device_id = clutter_input_device_get_device_id (device);
  XDevice *xdev = NULL;

  xdev = g_object_get_data (G_OBJECT (device), "meta-input-settings-xdevice");
  if (xdev)
    return xdev;

  meta_error_trap_push (display);
  xdev = XOpenDevice (xdisplay, device_id);
  meta_error_trap_pop (display);

  if (xdev)
    {
      g_object_set_data_full (G_OBJECT (device),
                              "meta-input-settings-xdevice",
                              xdev, device_free_xdevice);
    }

  return xdev;
}

static void *
get_property (ClutterInputDevice *device,
              const gchar        *property,
              Atom                type,
              int                 format,
              gulong              nitems)
{
  MetaBackend *backend = meta_get_backend ();
  Display *xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));
  gulong nitems_ret, bytes_after_ret;
  int rc, device_id, format_ret;
  Atom property_atom, type_ret;
  guchar *data_ret = NULL;

  property_atom = XInternAtom (xdisplay, property, True);
  if (!property_atom)
    return NULL;

  device_id = clutter_input_device_get_device_id (device);

  rc = XIGetProperty (xdisplay, device_id, property_atom,
                      0, 10, False, type, &type_ret, &format_ret,
                      &nitems_ret, &bytes_after_ret, &data_ret);
  if (rc == Success && type_ret == type && format_ret == format && nitems_ret >= nitems)
    {
      if (nitems_ret > nitems)
        g_warning ("Property '%s' for device '%s' returned %lu items, expected %lu",
                   property, clutter_input_device_get_device_name (device), nitems_ret, nitems);
      return data_ret;
    }

  meta_XFree (data_ret);
  return NULL;
}

static void
change_property (ClutterInputDevice *device,
                 const gchar        *property,
                 Atom                type,
                 int                 format,
                 void               *data,
                 gulong              nitems)
{
  MetaBackend *backend = meta_get_backend ();
  Display *xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));
  int device_id;
  Atom property_atom;
  guchar *data_ret;

  property_atom = XInternAtom (xdisplay, property, True);
  if (!property_atom)
    return;

  device_id = clutter_input_device_get_device_id (device);

  data_ret = get_property (device, property, type, format, nitems);
  if (!data_ret)
    return;

  XIChangeProperty (xdisplay, device_id, property_atom, type,
                    format, XIPropModeReplace, data, nitems);
  meta_XFree (data_ret);
}

static gboolean
is_device_synaptics (ClutterInputDevice *device)
{
  guchar *has_setting;

  /* We just need looking for a synaptics-specific property */
  has_setting = get_property (device, "Synaptics Off", XA_INTEGER, 8, 1);
  if (!has_setting)
    return FALSE;

  meta_XFree (has_setting);
  return TRUE;
}

static gboolean
is_device_libinput (ClutterInputDevice *device)
{
  guchar *has_setting;

  /* We just need looking for a synaptics-specific property */
  has_setting = get_property (device, "libinput Send Events Modes Available", XA_INTEGER, 8, 2);
  if (!has_setting)
    return FALSE;

  meta_XFree (has_setting);
  return TRUE;
}

static void
change_x_device_left_handed (ClutterInputDevice *device,
                             gboolean            left_handed)
{
  MetaDisplay *display = meta_get_display ();
  MetaBackend *backend = meta_get_backend ();
  Display *xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));
  XDevice *xdevice;
  guchar *buttons;
  guint buttons_capacity = 16, n_buttons;

  xdevice = XOpenDevice(xdisplay, clutter_input_device_get_device_id (device));
  if (!xdevice)
    return;

  if (display)
    meta_error_trap_push (display);
  buttons = g_new (guchar, buttons_capacity);
  n_buttons = XGetDeviceButtonMapping (xdisplay, xdevice,
                                       buttons, buttons_capacity);

  while (n_buttons > buttons_capacity)
    {
      buttons_capacity = n_buttons;
      buttons = (guchar *) g_realloc (buttons,
                                      buttons_capacity * sizeof (guchar));

      n_buttons = XGetDeviceButtonMapping (xdisplay, xdevice,
                                           buttons, buttons_capacity);
    }

  buttons[0] = left_handed ? 3 : 1;
  buttons[2] = left_handed ? 1 : 3;
  XSetDeviceButtonMapping (xdisplay, xdevice, buttons, n_buttons);
  g_free (buttons);

  if (display && meta_error_trap_pop_with_return (display))
    {
      g_warning ("Could not set left-handed for %s",
                 clutter_input_device_get_device_name (device));
    }

  XCloseDevice (xdisplay, xdevice);
}

static void
change_synaptics_tap_left_handed (ClutterInputDevice *device,
                                  gboolean            tap_enabled,
                                  gboolean            left_handed)
{
  guchar *tap_action;

  tap_action = get_property (device, "Synaptics Tap Action",
                             XA_INTEGER, 8, 7);
  if (!tap_action)
    return;

  tap_action[4] = tap_enabled ? (left_handed ? 3 : 1) : 0;
  tap_action[5] = tap_enabled ? (left_handed ? 1 : 3) : 0;
  tap_action[6] = tap_enabled ? 2 : 0;

  change_property (device, "Synaptics Tap Action",
                   XA_INTEGER, 8, tap_action, 7);
  meta_XFree (tap_action);

  change_x_device_left_handed (device, left_handed);
}

static void
change_x_device_speed (ClutterInputDevice *device,
                       gdouble             speed)
{
  MetaDisplay *display = meta_get_display ();
  MetaBackend *backend = meta_get_backend ();
  Display *xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));
  XDevice *xdevice;
  XPtrFeedbackControl feedback;
  XFeedbackState *states, *state;
  int i, num_feedbacks, motion_threshold, numerator, denominator;
  gfloat motion_acceleration;

  xdevice = XOpenDevice(xdisplay, clutter_input_device_get_device_id (device));
  if (!xdevice)
    return;
  /* Get the list of feedbacks for the device */
  states = XGetFeedbackControl (xdisplay, xdevice, &num_feedbacks);
  if (!states)
    return;

  /* Calculate acceleration and threshold */
  motion_acceleration = (speed + 1) * 5; /* speed is [-1..1], map to [0..10] */
  motion_threshold = CLAMP (10 - floor (motion_acceleration), 1, 10);

  if (motion_acceleration >= 1.0)
    {
      /* we want to get the acceleration, with a resolution of 0.5
       */
      if ((motion_acceleration - floor (motion_acceleration)) < 0.25)
        {
          numerator = floor (motion_acceleration);
          denominator = 1;
        }
      else if ((motion_acceleration - floor (motion_acceleration)) < 0.5)
        {
          numerator = ceil (2.0 * motion_acceleration);
          denominator = 2;
        }
      else if ((motion_acceleration - floor (motion_acceleration)) < 0.75)
        {
          numerator = floor (2.0 *motion_acceleration);
          denominator = 2;
        }
      else
        {
          numerator = ceil (motion_acceleration);
          denominator = 1;
        }
    }
  else if (motion_acceleration < 1.0 && motion_acceleration > 0)
    {
      /* This we do to 1/10ths */
      numerator = floor (motion_acceleration * 10) + 1;
      denominator= 10;
    }
  else
    {
      numerator = -1;
      denominator = -1;
    }

  if (display)
    meta_error_trap_push (display);

  state = (XFeedbackState *) states;

  for (i = 0; i < num_feedbacks; i++)
    {
      if (state->class == PtrFeedbackClass)
        {
          /* And tell the device */
          feedback.class      = PtrFeedbackClass;
          feedback.length     = sizeof (XPtrFeedbackControl);
          feedback.id         = state->id;
          feedback.threshold  = motion_threshold;
          feedback.accelNum   = numerator;
          feedback.accelDenom = denominator;

          XChangeFeedbackControl (xdisplay, xdevice,
                                  DvAccelNum | DvAccelDenom | DvThreshold,
                                  (XFeedbackControl *) &feedback);
          break;
        }

      state = (XFeedbackState *) ((char *) state + state->length);
    }

  if (display && meta_error_trap_pop_with_return (display))
    {
      g_warning ("Could not set synaptics touchpad acceleration for %s",
                 clutter_input_device_get_device_name (device));
    }

  XFreeFeedbackList (states);
  XCloseDevice (xdisplay, xdevice);
}

static void
change_x_device_scroll_button (ClutterInputDevice *device,
                               guint               button)
{
  guchar value;

  value = button > 0 ? 1 : 0;
  change_property (device, "Evdev Wheel Emulation",
                   XA_INTEGER, 8, &value, 1);
  if (button > 0)
    {
      value = button;
      change_property (device, "Evdev Wheel Emulation Button",
                       XA_INTEGER, 8, &value, 1);
    }
}

/* Ensure that syndaemon dies together with us, to avoid running several of
 * them */
static void
setup_syndaemon (gpointer user_data)
{
#ifdef __linux
  prctl (PR_SET_PDEATHSIG, SIGHUP);
#endif
}

static gboolean
have_program_in_path (const char *name)
{
  gchar *path;
  gboolean result;

  path = g_find_program_in_path (name);
  result = (path != NULL);
  g_free (path);
  return result;
}

static void
syndaemon_died (GPid     pid,
                gint     status,
                gpointer user_data)
{
  MetaInputSettingsX11 *settings_x11 = META_INPUT_SETTINGS_X11 (user_data);
  MetaInputSettingsX11Private *priv =
    meta_input_settings_x11_get_instance_private (settings_x11);
  GError *error = NULL;

  if (!g_spawn_check_exit_status (status, &error))
    {
      if ((WIFSIGNALED (status) && WTERMSIG (status) != SIGHUP) ||
          error->domain == G_SPAWN_EXIT_ERROR)
        g_warning ("Syndaemon exited unexpectedly: %s", error->message);
      g_error_free (error);
    }

  g_spawn_close_pid (pid);
  priv->syndaemon_spawned = FALSE;
}

static void
set_synaptics_disable_w_typing (MetaInputSettings *settings,
                                gboolean           state)
{
  MetaInputSettingsX11 *settings_x11 = META_INPUT_SETTINGS_X11 (settings);
  MetaInputSettingsX11Private *priv =
    meta_input_settings_x11_get_instance_private (settings_x11);

  if (state)
    {
      GError *error = NULL;
      GPtrArray *args;

      if (priv->syndaemon_spawned)
        return;

      if (!have_program_in_path ("syndaemon"))
        return;

      args = g_ptr_array_new ();

      g_ptr_array_add (args, "syndaemon");
      g_ptr_array_add (args, "-i");
      g_ptr_array_add (args, "1.0");
      g_ptr_array_add (args, "-t");
      g_ptr_array_add (args, "-K");
      g_ptr_array_add (args, "-R");
      g_ptr_array_add (args, NULL);

      /* we must use G_SPAWN_DO_NOT_REAP_CHILD to avoid
       * double-forking, otherwise syndaemon will immediately get
       * killed again through (PR_SET_PDEATHSIG when the intermediate
       * process dies */
      g_spawn_async (g_get_home_dir (), (char **) args->pdata, NULL,
                     G_SPAWN_SEARCH_PATH|G_SPAWN_DO_NOT_REAP_CHILD, setup_syndaemon, NULL,
                     &priv->syndaemon_pid, &error);

      priv->syndaemon_spawned = (error == NULL);
      g_ptr_array_free (args, TRUE);

      if (error)
        {
          g_warning ("Failed to launch syndaemon: %s", error->message);
          g_error_free (error);
        }
      else
        {
          g_child_watch_add (priv->syndaemon_pid, syndaemon_died, settings);
        }
    }
  else if (priv->syndaemon_spawned)
    {
      kill (priv->syndaemon_pid, SIGHUP);
      priv->syndaemon_spawned = FALSE;
    }
}

static void
meta_input_settings_x11_set_send_events (MetaInputSettings        *settings,
                                         ClutterInputDevice       *device,
                                         GDesktopDeviceSendEvents  mode)
{
  guchar values[2] = { 0 }; /* disabled, disabled-on-external-mouse */
  guchar *available;

  if (is_device_synaptics (device))
    {
      values[0] = mode != G_DESKTOP_DEVICE_SEND_EVENTS_ENABLED;
      change_property (device, "Synaptics Off", XA_INTEGER, 8, &values, 1);
      return;
    }

  available = get_property (device, "libinput Send Events Modes Available",
                            XA_INTEGER, 8, 2);
  if (!available)
    return;

  switch (mode)
    {
    case G_DESKTOP_DEVICE_SEND_EVENTS_DISABLED:
      values[0] = 1;
      break;
    case G_DESKTOP_DEVICE_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE:
      values[1] = 1;
      break;
    default:
      break;
    }

  if ((values[0] && !available[0]) || (values[1] && !available[1]))
    g_warning ("Device '%s' does not support sendevents mode %d\n",
               clutter_input_device_get_device_name (device), mode);
  else
    change_property (device, "libinput Send Events Mode Enabled",
                     XA_INTEGER, 8, &values, 2);

  meta_XFree (available);
}

static void
meta_input_settings_x11_set_matrix (MetaInputSettings  *settings,
                                    ClutterInputDevice *device,
                                    gfloat              matrix[6])
{
  MetaBackend *backend = meta_get_backend ();
  Display *xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));
  gfloat full_matrix[9] = { matrix[0], matrix[1], matrix[2],
                            matrix[3], matrix[4], matrix[5],
                            0, 0, 1 };

  change_property (device, "Coordinate Transformation Matrix",
                   XInternAtom (xdisplay, "FLOAT", False),
                   32, &full_matrix, 9);
}

static void
meta_input_settings_x11_set_speed (MetaInputSettings  *settings,
                                   ClutterInputDevice *device,
                                   gdouble             speed)
{
  MetaBackend *backend = meta_get_backend ();
  Display *xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));
  gfloat value = speed;

  if (is_device_synaptics (device) ||
      !is_device_libinput (device))
    {
      change_x_device_speed (device, speed);
      return;
    }

  change_property (device, "libinput Accel Speed",
                   XInternAtom (xdisplay, "FLOAT", False),
                   32, &value, 1);
}

static void
meta_input_settings_x11_set_left_handed (MetaInputSettings  *settings,
                                         ClutterInputDevice *device,
                                         gboolean            enabled)
{
  ClutterInputDeviceType device_type;
  guchar value;

  device_type = clutter_input_device_get_device_type (device);

  if (device_type == CLUTTER_TABLET_DEVICE ||
      device_type == CLUTTER_PEN_DEVICE ||
      device_type == CLUTTER_ERASER_DEVICE)
    {
      value = enabled ? 3 : 0;
      change_property (device, "Wacom Rotation",
                       XA_INTEGER, 8, &value, 1);
    }
  else
    {
      value = enabled ? 1 : 0;

      if (is_device_synaptics (device))
        {
          GSettings *settings;

          settings = g_settings_new ("org.gnome.desktop.peripherals.touchpad");
          change_synaptics_tap_left_handed (device,
                                            g_settings_get_boolean (settings, "tap-to-click"),
                                            enabled);
          g_object_unref (settings);
          return;
        }
      else if (!is_device_libinput (device))
        {
          change_x_device_left_handed (device, enabled);
          return;
        }

      change_property (device, "libinput Left Handed Enabled",
                       XA_INTEGER, 8, &value, 1);
    }
}

static void
meta_input_settings_x11_set_disable_while_typing (MetaInputSettings  *settings,
                                                  ClutterInputDevice *device,
                                                  gboolean            enabled)
{
  guchar value = (enabled) ? 1 : 0;

  if (is_device_synaptics (device))
    {
      set_synaptics_disable_w_typing (settings, enabled);
      return;
    }

  change_property (device, "libinput Disable While Typing Enabled",
                   XA_INTEGER, 8, &value, 1);
}

static void
meta_input_settings_x11_set_tap_enabled (MetaInputSettings  *settings,
                                         ClutterInputDevice *device,
                                         gboolean            enabled)
{
  guchar value = (enabled) ? 1 : 0;

  if (is_device_synaptics (device))
    {
      GDesktopTouchpadHandedness handedness;
      GSettings *settings;

      settings = g_settings_new ("org.gnome.desktop.peripherals.touchpad");
      handedness = g_settings_get_enum (settings, "left-handed");
      g_object_unref (settings);

      change_synaptics_tap_left_handed (device, enabled,
                                        handedness == G_DESKTOP_TOUCHPAD_HANDEDNESS_LEFT);
      return;
    }

  change_property (device, "libinput Tapping Enabled",
                   XA_INTEGER, 8, &value, 1);
}

static void
meta_input_settings_x11_set_tap_and_drag_enabled (MetaInputSettings  *settings,
                                                  ClutterInputDevice *device,
                                                  gboolean            enabled)
{
  guchar value = (enabled) ? 1 : 0;

  change_property (device, "libinput Tapping Drag Enabled",
                   XA_INTEGER, 8, &value, 1);
}

static void
meta_input_settings_x11_set_invert_scroll (MetaInputSettings  *settings,
                                           ClutterInputDevice *device,
                                           gboolean            inverted)
{
  guchar value = (inverted) ? 1 : 0;

  if (is_device_synaptics (device))
    {
      gint32 *scrolling_distance;

      scrolling_distance = get_property (device, "Synaptics Scrolling Distance",
                                         XA_INTEGER, 32, 2);
      if (scrolling_distance)
        {
          scrolling_distance[0] = inverted ?
            -abs (scrolling_distance[0]) : abs (scrolling_distance[0]);
          scrolling_distance[1] = inverted ?
            -abs (scrolling_distance[1]) : abs (scrolling_distance[1]);

          change_property (device, "Synaptics Scrolling Distance",
                           XA_INTEGER, 32, scrolling_distance, 2);
          meta_XFree (scrolling_distance);
        }

      return;
    }

  change_property (device, "libinput Natural Scrolling Enabled",
                   XA_INTEGER, 8, &value, 1);
}

static void
meta_input_settings_x11_set_edge_scroll (MetaInputSettings            *settings,
                                         ClutterInputDevice           *device,
                                         gboolean                      edge_scroll_enabled)
{
  guchar values[SCROLL_METHOD_NUM_FIELDS] = { 0 }; /* 2fg, edge, button. The last value is unused */
  guchar *current = NULL;
  guchar *available = NULL;

  if (is_device_synaptics (device))
    {
      current = get_property (device, "Synaptics Edge Scrolling",
                              XA_INTEGER, 8, 3);
      if (current)
        {
          current[0] = !!edge_scroll_enabled;
          current[1] = !!edge_scroll_enabled;
          change_property (device, "Synaptics Edge Scrolling",
                           XA_INTEGER, 8, current, 3);
          meta_XFree (current);
        }

      return;
    }

  available = get_property (device, "libinput Scroll Methods Available",
                            XA_INTEGER, 8, SCROLL_METHOD_NUM_FIELDS);
  if (!available || !available[SCROLL_METHOD_FIELD_EDGE])
    goto out;

  current = get_property (device, "libinput Scroll Method Enabled",
                          XA_INTEGER, 8, SCROLL_METHOD_NUM_FIELDS);
  if (!current)
    goto out;

  memcpy (values, current, SCROLL_METHOD_NUM_FIELDS);

  values[SCROLL_METHOD_FIELD_EDGE] = !!edge_scroll_enabled;
  change_property (device, "libinput Scroll Method Enabled",
                   XA_INTEGER, 8, &values, SCROLL_METHOD_NUM_FIELDS);
 out:
  meta_XFree (current);
  meta_XFree (available);
}

static void
meta_input_settings_x11_set_two_finger_scroll (MetaInputSettings            *settings,
                                               ClutterInputDevice           *device,
                                               gboolean                      two_finger_scroll_enabled)
{
  guchar values[SCROLL_METHOD_NUM_FIELDS] = { 0 }; /* 2fg, edge, button. The last value is unused */
  guchar *current = NULL;
  guchar *available = NULL;

  if (is_device_synaptics (device))
    {
      current = get_property (device, "Synaptics Two-Finger Scrolling",
                              XA_INTEGER, 8, 2);
      if (current)
        {
          current[0] = !!two_finger_scroll_enabled;
          current[1] = !!two_finger_scroll_enabled;
          change_property (device, "Synaptics Two-Finger Scrolling",
                           XA_INTEGER, 8, current, 2);
          meta_XFree (current);
        }

      return;
    }

  available = get_property (device, "libinput Scroll Methods Available",
                            XA_INTEGER, 8, SCROLL_METHOD_NUM_FIELDS);
  if (!available || !available[SCROLL_METHOD_FIELD_2FG])
    goto out;

  current = get_property (device, "libinput Scroll Method Enabled",
                          XA_INTEGER, 8, SCROLL_METHOD_NUM_FIELDS);
  if (!current)
    goto out;

  memcpy (values, current, SCROLL_METHOD_NUM_FIELDS);

  values[SCROLL_METHOD_FIELD_2FG] = !!two_finger_scroll_enabled;
  change_property (device, "libinput Scroll Method Enabled",
                   XA_INTEGER, 8, &values, SCROLL_METHOD_NUM_FIELDS);
 out:
  meta_XFree (current);
  meta_XFree (available);
}

static gboolean
meta_input_settings_x11_has_two_finger_scroll (MetaInputSettings  *settings,
                                               ClutterInputDevice *device)
{
  guchar *available = NULL;
  gboolean has_two_finger = TRUE;

  if (is_device_synaptics (device))
    {
      available = get_property (device, "Synaptics Capabilities",
                                XA_INTEGER, 8, 4);
      if (!available || !available[3])
          has_two_finger = FALSE;

      meta_XFree (available);
      return has_two_finger;
    }

  available = get_property (device, "libinput Scroll Methods Available",
                            XA_INTEGER, 8, SCROLL_METHOD_NUM_FIELDS);
  if (!available || !available[SCROLL_METHOD_FIELD_2FG])
    has_two_finger = FALSE;

  meta_XFree (available);
  return has_two_finger;
}

static void
meta_input_settings_x11_set_scroll_button (MetaInputSettings  *settings,
                                           ClutterInputDevice *device,
                                           guint               button)
{
  if (!is_device_libinput (device))
    {
      change_x_device_scroll_button (device, button);
      return;
    }

  change_property (device, "libinput Button Scrolling Button",
                   XA_INTEGER, 32, &button, 1);
}

static void
meta_input_settings_x11_set_click_method (MetaInputSettings           *settings,
                                          ClutterInputDevice          *device,
                                          GDesktopTouchpadClickMethod  mode)
{
  guchar values[2] = { 0 }; /* buttonareas, clickfinger */
  guchar *defaults, *available;

  available = get_property (device, "libinput Click Methods Available",
                            XA_INTEGER, 8, 2);
  if (!available)
    return;

  switch (mode)
    {
    case G_DESKTOP_TOUCHPAD_CLICK_METHOD_DEFAULT:
      defaults = get_property (device, "libinput Click Method Enabled Default",
                               XA_INTEGER, 8, 2);
      if (!defaults)
        break;
      memcpy (values, defaults, 2);
      meta_XFree (defaults);
      break;
    case G_DESKTOP_TOUCHPAD_CLICK_METHOD_NONE:
      break;
    case G_DESKTOP_TOUCHPAD_CLICK_METHOD_AREAS:
      values[0] = 1;
      break;
    case G_DESKTOP_TOUCHPAD_CLICK_METHOD_FINGERS:
      values[1] = 1;
      break;
    default:
      g_assert_not_reached ();
      return;
  }

  if ((values[0] && !available[0]) || (values[1] && !available[1]))
    g_warning ("Device '%s' does not support click method %d\n",
               clutter_input_device_get_device_name (device), mode);
  else
    change_property (device, "libinput Click Method Enabled",
                     XA_INTEGER, 8, &values, 2);

  meta_XFree(available);
}

static void
meta_input_settings_x11_set_keyboard_repeat (MetaInputSettings *settings,
                                             gboolean           enabled,
                                             guint              delay,
                                             guint              interval)
{
  MetaBackend *backend = meta_get_backend ();
  Display *xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));

  if (enabled)
    {
      XAutoRepeatOn (xdisplay);
      XkbSetAutoRepeatRate (xdisplay, XkbUseCoreKbd, delay, interval);
    }
  else
    {
      XAutoRepeatOff (xdisplay);
    }
}

static gboolean
has_udev_property (MetaInputSettings  *settings,
                   ClutterInputDevice *device,
                   const char         *property_name)
{
#ifdef HAVE_LIBGUDEV
  MetaInputSettingsX11 *settings_x11 = META_INPUT_SETTINGS_X11 (settings);
  MetaInputSettingsX11Private *priv =
    meta_input_settings_x11_get_instance_private (settings_x11);
  const char *device_node;
  GUdevDevice *udev_device = NULL;
  GUdevDevice *parent_udev_device = NULL;

  device_node = clutter_input_device_get_device_node (device);
  if (!device_node)
    return FALSE;

  udev_device = g_udev_client_query_by_device_file (priv->udev_client,
                                                    device_node);
  if (!udev_device)
    return FALSE;

  if (NULL != g_udev_device_get_property (udev_device, property_name))
    {
      g_object_unref (udev_device);
      return TRUE;
    }

  parent_udev_device = g_udev_device_get_parent (udev_device);
  g_object_unref (udev_device);

  if (!parent_udev_device)
    return FALSE;

  if (NULL != g_udev_device_get_property (parent_udev_device, property_name))
    {
      g_object_unref (parent_udev_device);
      return TRUE;
    }

  g_object_unref (parent_udev_device);
  return FALSE;
#else
  g_warning ("Failed to set acceleration profile: no udev support");
  return FALSE;
#endif
}

static gboolean
is_mouse (MetaInputSettings  *settings,
          ClutterInputDevice *device)
{
  return (has_udev_property (settings, device, "ID_INPUT_MOUSE") &&
          !has_udev_property (settings, device, "ID_INPUT_POINTINGSTICK"));
}

static gboolean
is_trackball (MetaInputSettings  *settings,
              ClutterInputDevice *device)
{
  return meta_input_device_is_trackball (device);
}

static void
set_device_accel_profile (ClutterInputDevice         *device,
                          GDesktopPointerAccelProfile profile)
{
  guchar *defaults, *available;
  guchar values[2] = { 0 }; /* adaptive, flat */

  defaults = get_property (device, "libinput Accel Profile Enabled Default",
                           XA_INTEGER, 8, 2);
  if (!defaults)
    return;

  available = get_property (device, "libinput Accel Profiles Available",
                           XA_INTEGER, 8, 2);
  if (!available)
    goto err_available;

  switch (profile)
    {
    case G_DESKTOP_POINTER_ACCEL_PROFILE_FLAT:
      values[0] = 0;
      values[1] = 1;
      break;
    case G_DESKTOP_POINTER_ACCEL_PROFILE_ADAPTIVE:
      values[0] = 1;
      values[1] = 0;
      break;
    default:
      g_warn_if_reached ();
    case G_DESKTOP_POINTER_ACCEL_PROFILE_DEFAULT:
      values[0] = defaults[0];
      values[1] = defaults[1];
      break;
    }

  change_property (device, "libinput Accel Profile Enabled",
                   XA_INTEGER, 8, &values, 2);

  meta_XFree (available);

err_available:
  meta_XFree (defaults);
}

static void
meta_input_settings_x11_set_mouse_accel_profile (MetaInputSettings          *settings,
                                                 ClutterInputDevice         *device,
                                                 GDesktopPointerAccelProfile profile)
{
  if (!is_mouse (settings, device))
    return;

  set_device_accel_profile (device, profile);
}

static void
meta_input_settings_x11_set_trackball_accel_profile (MetaInputSettings          *settings,
                                                     ClutterInputDevice         *device,
                                                     GDesktopPointerAccelProfile profile)
{
  if (!is_trackball (settings, device))
    return;

  set_device_accel_profile (device, profile);
}

static void
meta_input_settings_x11_set_tablet_mapping (MetaInputSettings     *settings,
                                            ClutterInputDevice    *device,
                                            GDesktopTabletMapping  mapping)
{
  MetaDisplay *display = meta_get_display ();
  MetaBackend *backend = meta_get_backend ();
  Display *xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));
  XDevice *xdev;

  if (!display)
    return;

  /* Grab the puke bucket! */
  meta_error_trap_push (display);
  xdev = device_ensure_xdevice (device);
  if (xdev)
    {
      XSetDeviceMode (xdisplay, xdev,
                      mapping == G_DESKTOP_TABLET_MAPPING_ABSOLUTE ?
                      Absolute : Relative);
    }

  if (meta_error_trap_pop_with_return (display))
    {
      g_warning ("Could not set tablet mapping for %s",
                 clutter_input_device_get_device_name (device));
    }
  else
    {
      ClutterInputDeviceMapping dev_mapping;

      dev_mapping = (mapping == G_DESKTOP_TABLET_MAPPING_ABSOLUTE) ?
        CLUTTER_INPUT_DEVICE_MAPPING_ABSOLUTE :
        CLUTTER_INPUT_DEVICE_MAPPING_RELATIVE;
      clutter_input_device_set_mapping_mode (device, dev_mapping);
    }
}

static gboolean
device_query_area (ClutterInputDevice *device,
                   gint               *x,
                   gint               *y,
                   gint               *width,
                   gint               *height)
{
  MetaBackend *backend = meta_get_backend ();
  Display *xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));
  gint device_id, n_devices, i;
  XIDeviceInfo *info;
  Atom abs_x, abs_y;

  *width = *height = 0;
  device_id = clutter_input_device_get_device_id (device);
  info = XIQueryDevice (xdisplay, device_id, &n_devices);
  if (n_devices <= 0 || !info)
    return FALSE;

  abs_x = XInternAtom (xdisplay, "Abs X", True);
  abs_y = XInternAtom (xdisplay, "Abs Y", True);

  for (i = 0; i < info->num_classes; i++)
    {
      XIValuatorClassInfo *valuator = (XIValuatorClassInfo *) info->classes[i];

      if (valuator->type != XIValuatorClass)
        continue;
      if (valuator->label == abs_x)
        {
          *x = valuator->min;
          *width = valuator->max - valuator->min;
        }
      else if (valuator->label == abs_y)
        {
          *y = valuator->min;
          *height = valuator->max - valuator->min;
        }
    }

  XIFreeDeviceInfo (info);
  return TRUE;
}

static void
update_tablet_area (MetaInputSettings  *settings,
                    ClutterInputDevice *device,
                    gint32             *area)
{
  change_property (device, "Wacom Tablet Area",
                   XA_INTEGER, 32, area, 4);
}

static void
meta_input_settings_x11_set_tablet_area (MetaInputSettings  *settings,
                                         ClutterInputDevice *device,
                                         gdouble             padding_left,
                                         gdouble             padding_right,
                                         gdouble             padding_top,
                                         gdouble             padding_bottom)
{
  gint32 x, y, width, height, area[4] = { 0 };

  if (!device_query_area (device, &x, &y, &width, &height))
    return;

  area[0] = (width * padding_left) + x;
  area[1] = (height * padding_top) + y;
  area[2] = width - (width * padding_right) + x;
  area[3] = height - (height * padding_bottom) + y;
  update_tablet_area (settings, device, area);
}

static void
meta_input_settings_x11_set_tablet_keep_aspect (MetaInputSettings  *settings,
                                                ClutterInputDevice *device,
                                                MetaLogicalMonitor *logical_monitor,
                                                gboolean            keep_aspect)
{
  gint32 width, height, dev_x, dev_y, dev_width, dev_height, area[4] = { 0 };

  if (!device_query_area (device, &dev_x, &dev_y, &dev_width, &dev_height))
    return;

  if (keep_aspect)
    {
      double aspect_ratio, dev_aspect;

      if (logical_monitor)
        {
          width = logical_monitor->rect.width;
          height = logical_monitor->rect.height;
        }
      else
        {
          MetaMonitorManager *monitor_manager;
          MetaBackend *backend;

          backend = meta_get_backend ();
          monitor_manager = meta_backend_get_monitor_manager (backend);
          meta_monitor_manager_get_screen_size (monitor_manager,
                                                &width, &height);
        }

      aspect_ratio = (double) width / height;
      dev_aspect = (double) dev_width / dev_height;

      if (dev_aspect > aspect_ratio)
        dev_width = dev_height * aspect_ratio;
      else if (dev_aspect < aspect_ratio)
        dev_height = dev_width / aspect_ratio;
    }

  area[0] = dev_x;
  area[1] = dev_y;
  area[2] = dev_width + dev_x;
  area[3] = dev_height + dev_y;
  update_tablet_area (settings, device, area);
}

static void
meta_input_settings_x11_dispose (GObject *object)
{
#ifdef HAVE_LIBGUDEV
  MetaInputSettingsX11 *settings_x11 = META_INPUT_SETTINGS_X11 (object);
  MetaInputSettingsX11Private *priv =
    meta_input_settings_x11_get_instance_private (settings_x11);

  g_clear_object (&priv->udev_client);
#endif

  G_OBJECT_CLASS (meta_input_settings_x11_parent_class)->dispose (object);
}

static guint
action_to_button (GDesktopStylusButtonAction action,
                  guint                      button)
{
  switch (action)
    {
    case G_DESKTOP_STYLUS_BUTTON_ACTION_MIDDLE:
      return CLUTTER_BUTTON_MIDDLE;
    case G_DESKTOP_STYLUS_BUTTON_ACTION_RIGHT:
      return CLUTTER_BUTTON_SECONDARY;
    case G_DESKTOP_STYLUS_BUTTON_ACTION_BACK:
      return 8;
    case G_DESKTOP_STYLUS_BUTTON_ACTION_FORWARD:
      return 9;
    case G_DESKTOP_STYLUS_BUTTON_ACTION_DEFAULT:
    default:
      return button;
    }
}

static void
meta_input_settings_x11_set_stylus_button_map (MetaInputSettings          *settings,
                                               ClutterInputDevice         *device,
                                               ClutterInputDeviceTool     *tool,
                                               GDesktopStylusButtonAction  primary,
                                               GDesktopStylusButtonAction  secondary,
                                               GDesktopStylusButtonAction  tertiary)
{
  MetaDisplay *display = meta_get_display ();
  MetaBackend *backend = meta_get_backend ();
  Display *xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));
  XDevice *xdev;

  if (!display)
    return;

  /* Grab the puke bucket! */
  meta_error_trap_push (display);
  xdev = device_ensure_xdevice (device);
  if (xdev)
    {
      guchar map[8] = {
        CLUTTER_BUTTON_PRIMARY,
        action_to_button (primary, CLUTTER_BUTTON_MIDDLE),
        action_to_button (secondary, CLUTTER_BUTTON_SECONDARY),
        4,
        5,
        6,
        7,
        action_to_button (tertiary, 8), /* "Back" */
      };

      XSetDeviceButtonMapping (xdisplay, xdev, map, G_N_ELEMENTS (map));
    }

  if (meta_error_trap_pop_with_return (display))
    {
      g_warning ("Could not set stylus button map for %s",
                 clutter_input_device_get_device_name (device));
    }
}

static void
meta_input_settings_x11_set_stylus_pressure (MetaInputSettings      *settings,
                                             ClutterInputDevice     *device,
                                             ClutterInputDeviceTool *tool,
                                             const gint32            pressure[4])
{
  guint32 values[4] = { pressure[0], pressure[1], pressure[2], pressure[3] };

  change_property (device, "Wacom Pressurecurve", XA_INTEGER, 32,
                   &values, G_N_ELEMENTS (values));
}

static void
meta_input_settings_x11_class_init (MetaInputSettingsX11Class *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaInputSettingsClass *input_settings_class = META_INPUT_SETTINGS_CLASS (klass);

  object_class->dispose = meta_input_settings_x11_dispose;

  input_settings_class->set_send_events = meta_input_settings_x11_set_send_events;
  input_settings_class->set_matrix = meta_input_settings_x11_set_matrix;
  input_settings_class->set_speed = meta_input_settings_x11_set_speed;
  input_settings_class->set_left_handed = meta_input_settings_x11_set_left_handed;
  input_settings_class->set_tap_enabled = meta_input_settings_x11_set_tap_enabled;
  input_settings_class->set_tap_and_drag_enabled = meta_input_settings_x11_set_tap_and_drag_enabled;
  input_settings_class->set_disable_while_typing = meta_input_settings_x11_set_disable_while_typing;
  input_settings_class->set_invert_scroll = meta_input_settings_x11_set_invert_scroll;
  input_settings_class->set_edge_scroll = meta_input_settings_x11_set_edge_scroll;
  input_settings_class->set_two_finger_scroll = meta_input_settings_x11_set_two_finger_scroll;
  input_settings_class->set_scroll_button = meta_input_settings_x11_set_scroll_button;
  input_settings_class->set_click_method = meta_input_settings_x11_set_click_method;
  input_settings_class->set_keyboard_repeat = meta_input_settings_x11_set_keyboard_repeat;

  input_settings_class->set_tablet_mapping = meta_input_settings_x11_set_tablet_mapping;
  input_settings_class->set_tablet_keep_aspect = meta_input_settings_x11_set_tablet_keep_aspect;
  input_settings_class->set_tablet_area = meta_input_settings_x11_set_tablet_area;

  input_settings_class->set_mouse_accel_profile = meta_input_settings_x11_set_mouse_accel_profile;
  input_settings_class->set_trackball_accel_profile = meta_input_settings_x11_set_trackball_accel_profile;

  input_settings_class->set_stylus_pressure = meta_input_settings_x11_set_stylus_pressure;
  input_settings_class->set_stylus_button_map = meta_input_settings_x11_set_stylus_button_map;

  input_settings_class->has_two_finger_scroll = meta_input_settings_x11_has_two_finger_scroll;
}

static void
meta_input_settings_x11_init (MetaInputSettingsX11 *settings)
{
#ifdef HAVE_LIBGUDEV
  MetaInputSettingsX11Private *priv =
    meta_input_settings_x11_get_instance_private (settings);
  const char *subsystems[] = { NULL };

  priv->udev_client = g_udev_client_new (subsystems);
#endif
}
