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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "backends/x11/meta-input-settings-x11.h"

#include <string.h>
#include <X11/Xatom.h>
#include <X11/extensions/XInput2.h>
#include <X11/XKBlib.h>

#include "backends/x11/meta-backend-x11.h"
#include "backends/x11/meta-input-device-x11.h"
#include "core/display-private.h"
#include "mtk/mtk-x11.h"

typedef struct
{
  MetaInputSettings *settings;
  XDevice *xdev;
} DeviceHandle;

G_DEFINE_TYPE (MetaInputSettingsX11, meta_input_settings_x11,
               META_TYPE_INPUT_SETTINGS)

typedef enum
{
  SCROLL_METHOD_FIELD_2FG,
  SCROLL_METHOD_FIELD_EDGE,
  SCROLL_METHOD_FIELD_BUTTON,
  SCROLL_METHOD_NUM_FIELDS
} ScrollMethod;

static MetaBackend *
get_backend (MetaInputSettings *settings)
{
  return meta_input_settings_get_backend (settings);
}

static void
device_handle_free (gpointer user_data)
{
  DeviceHandle *handle = user_data;
  MetaInputSettings *settings = handle->settings;
  MetaBackend *backend = get_backend (settings);
  Display *xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));

  mtk_x11_error_trap_push (xdisplay);
  XCloseDevice (xdisplay, handle->xdev);
  mtk_x11_error_trap_pop (xdisplay);

  g_free (handle);
}

static XDevice *
device_ensure_xdevice (MetaInputSettings  *settings,
                       ClutterInputDevice *device)
{
  MetaBackend *backend = get_backend (settings);
  Display *xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));
  int device_id = meta_input_device_x11_get_device_id (device);
  DeviceHandle *handle;
  XDevice *xdev;

  handle = g_object_get_data (G_OBJECT (device), "meta-input-settings-xdevice");
  if (handle)
    return handle->xdev;

  mtk_x11_error_trap_push (xdisplay);
  xdev = XOpenDevice (xdisplay, device_id);
  mtk_x11_error_trap_pop (xdisplay);

  if (xdev)
    {
      handle = g_new0 (DeviceHandle, 1);
      handle->settings = settings;
      handle->xdev = xdev;
      g_object_set_data_full (G_OBJECT (device),
                              "meta-input-settings-xdevice",
                              handle, device_handle_free);
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
  MetaInputDevice *input_device = META_INPUT_DEVICE (device);
  MetaBackend *backend = meta_input_device_get_backend (input_device);
  Display *xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));
  gulong nitems_ret, bytes_after_ret;
  int rc, device_id, format_ret;
  Atom property_atom, type_ret;
  guchar *data_ret = NULL;

  property_atom = XInternAtom (xdisplay, property, True);
  if (!property_atom)
    return NULL;

  device_id = meta_input_device_x11_get_device_id (device);

  mtk_x11_error_trap_push (xdisplay);
  rc = XIGetProperty (xdisplay, device_id, property_atom,
                      0, 10, False, type, &type_ret, &format_ret,
                      &nitems_ret, &bytes_after_ret, &data_ret);
  mtk_x11_error_trap_pop (xdisplay);

  if (rc == Success && type_ret == type && format_ret == format && nitems_ret >= nitems)
    return data_ret;

  meta_XFree (data_ret);
  return NULL;
}

static void
change_property (MetaInputSettings  *settings,
                 ClutterInputDevice *device,
                 const gchar        *property,
                 Atom                type,
                 int                 format,
                 void               *data,
                 gulong              nitems)
{
  MetaBackend *backend = get_backend (settings);
  Display *xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));
  int device_id;
  Atom property_atom;
  guchar *data_ret;
  int err;

  property_atom = XInternAtom (xdisplay, property, True);
  if (!property_atom)
    return;

  device_id = meta_input_device_x11_get_device_id (device);

  data_ret = get_property (device, property, type, format, nitems);
  if (!data_ret)
    return;

  mtk_x11_error_trap_push (xdisplay);
  XIChangeProperty (xdisplay, device_id, property_atom, type,
                    format, XIPropModeReplace, data, nitems);
  XSync (xdisplay, False);

  err = mtk_x11_error_trap_pop_with_return (xdisplay);
  if (err)
    {
      g_warning ("XIChangeProperty failed on device %d property \"%s\" with X error %d",
                 device_id,
                 property,
                 err);
    }

  meta_XFree (data_ret);
}

static void
meta_input_settings_x11_set_send_events (MetaInputSettings        *settings,
                                         ClutterInputDevice       *device,
                                         GDesktopDeviceSendEvents  mode)
{
  guchar values[2] = { 0 }; /* disabled, disabled-on-external-mouse */
  guchar *available;

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
    g_warning ("Device '%s' does not support sendevents mode %d",
               clutter_input_device_get_device_name (device), mode);
  else
    change_property (settings, device, "libinput Send Events Mode Enabled",
                     XA_INTEGER, 8, &values, 2);

  meta_XFree (available);
}

static void
meta_input_settings_x11_set_matrix (MetaInputSettings  *settings,
                                    ClutterInputDevice *device,
                                    const float         matrix[6])
{
  MetaBackend *backend = get_backend (settings);
  Display *xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));
  gfloat full_matrix[9] = { matrix[0], matrix[1], matrix[2],
                            matrix[3], matrix[4], matrix[5],
                            0, 0, 1 };

  change_property (settings, device, "Coordinate Transformation Matrix",
                   XInternAtom (xdisplay, "FLOAT", False),
                   32, &full_matrix, 9);
}

static void
meta_input_settings_x11_set_speed (MetaInputSettings  *settings,
                                   ClutterInputDevice *device,
                                   gdouble             speed)
{
  MetaBackend *backend = get_backend (settings);
  Display *xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));
  gfloat value = speed;

  change_property (settings, device, "libinput Accel Speed",
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
      change_property (settings, device, "Wacom Rotation",
                       XA_INTEGER, 8, &value, 1);
    }
  else
    {
      value = enabled ? 1 : 0;
      change_property (settings, device, "libinput Left Handed Enabled",
                       XA_INTEGER, 8, &value, 1);
    }
}

static void
meta_input_settings_x11_set_disable_while_typing (MetaInputSettings  *settings,
                                                  ClutterInputDevice *device,
                                                  gboolean            enabled)
{
  guchar value = (enabled) ? 1 : 0;

  change_property (settings, device, "libinput Disable While Typing Enabled",
                   XA_INTEGER, 8, &value, 1);
}

static void
meta_input_settings_x11_set_tap_enabled (MetaInputSettings  *settings,
                                         ClutterInputDevice *device,
                                         gboolean            enabled)
{
  guchar value = (enabled) ? 1 : 0;

  change_property (settings, device, "libinput Tapping Enabled",
                   XA_INTEGER, 8, &value, 1);
}

static void
meta_input_settings_x11_set_tap_and_drag_enabled (MetaInputSettings  *settings,
                                                  ClutterInputDevice *device,
                                                  gboolean            enabled)
{
  guchar value = (enabled) ? 1 : 0;

  change_property (settings, device, "libinput Tapping Drag Enabled",
                   XA_INTEGER, 8, &value, 1);
}

static void
meta_input_settings_x11_set_tap_and_drag_lock_enabled (MetaInputSettings  *settings,
                                                       ClutterInputDevice *device,
                                                       gboolean            enabled)
{
  guchar value = (enabled) ? 1 : 0;

  change_property (settings, device, "libinput Tapping Drag Lock Enabled",
                   XA_INTEGER, 8, &value, 1);
}

static void
meta_input_settings_x11_set_invert_scroll (MetaInputSettings  *settings,
                                           ClutterInputDevice *device,
                                           gboolean            inverted)
{
  guchar value = (inverted) ? 1 : 0;

  change_property (settings, device, "libinput Natural Scrolling Enabled",
                   XA_INTEGER, 8, &value, 1);
}

static void
change_scroll_method (MetaInputSettings  *settings,
                      ClutterInputDevice *device,
                      ScrollMethod        method,
                      gboolean            enabled)
{
  guchar values[SCROLL_METHOD_NUM_FIELDS] = { 0 }; /* 2fg, edge, button. The last value is unused */
  guchar *current = NULL;
  guchar *available = NULL;

  available = get_property (device, "libinput Scroll Methods Available",
                            XA_INTEGER, 8, SCROLL_METHOD_NUM_FIELDS);
  if (!available || !available[method])
    goto out;

  current = get_property (device, "libinput Scroll Method Enabled",
                          XA_INTEGER, 8, SCROLL_METHOD_NUM_FIELDS);
  if (!current)
    goto out;

  memcpy (values, current, SCROLL_METHOD_NUM_FIELDS);

  values[method] = !!enabled;
  change_property (settings, device, "libinput Scroll Method Enabled",
                   XA_INTEGER, 8, &values, SCROLL_METHOD_NUM_FIELDS);
 out:
  meta_XFree (current);
  meta_XFree (available);
}

static void
meta_input_settings_x11_set_edge_scroll (MetaInputSettings            *settings,
                                         ClutterInputDevice           *device,
                                         gboolean                      edge_scroll_enabled)
{
  change_scroll_method (settings, device,
                        SCROLL_METHOD_FIELD_EDGE, edge_scroll_enabled);
}

static void
meta_input_settings_x11_set_two_finger_scroll (MetaInputSettings            *settings,
                                               ClutterInputDevice           *device,
                                               gboolean                      two_finger_scroll_enabled)
{
  change_scroll_method (settings, device,
                        SCROLL_METHOD_FIELD_2FG, two_finger_scroll_enabled);
}

static gboolean
meta_input_settings_x11_has_two_finger_scroll (MetaInputSettings  *settings,
                                               ClutterInputDevice *device)
{
  guchar *available = NULL;
  gboolean has_two_finger = TRUE;

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
                                           guint               button,
                                           gboolean            button_lock)
{
  gchar lock = button_lock;

  change_scroll_method (settings, device,
                        SCROLL_METHOD_FIELD_BUTTON, button != 0);
  change_property (settings, device, "libinput Button Scrolling Button",
                   XA_CARDINAL, 32, &button, 1);
  change_property (settings, device, "libinput Button Scrolling Button Lock Enabled",
                   XA_INTEGER, 8, &lock, 1);
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
    g_warning ("Device '%s' does not support click method %d",
               clutter_input_device_get_device_name (device), mode);
  else
    change_property (settings, device, "libinput Click Method Enabled",
                     XA_INTEGER, 8, &values, 2);

  meta_XFree(available);
}

static void
meta_input_settings_x11_set_tap_button_map (MetaInputSettings            *settings,
                                            ClutterInputDevice           *device,
                                            GDesktopTouchpadTapButtonMap  mode)
{
  guchar values[2] = { 0 }; /* lrm, lmr */
  guchar *defaults;

  switch (mode)
    {
    case G_DESKTOP_TOUCHPAD_BUTTON_TAP_MAP_DEFAULT:
      defaults = get_property (device, "libinput Tapping Button Mapping Default",
                               XA_INTEGER, 8, 2);
      if (!defaults)
        break;
      memcpy (values, defaults, 2);
      meta_XFree (defaults);
      break;
    case G_DESKTOP_TOUCHPAD_BUTTON_TAP_MAP_LRM:
      values[0] = 1;
      break;
    case G_DESKTOP_TOUCHPAD_BUTTON_TAP_MAP_LMR:
      values[1] = 1;
      break;
    default:
      g_assert_not_reached ();
      return;
  }

  if (values[0] || values[1])
    change_property (settings, device, "libinput Tapping Button Mapping Enabled",
                     XA_INTEGER, 8, &values, 2);
}

static void
meta_input_settings_x11_set_keyboard_repeat (MetaInputSettings *settings,
                                             gboolean           enabled,
                                             guint              delay,
                                             guint              interval)
{
  MetaBackend *backend = get_backend (settings);
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

static void
set_device_accel_profile (MetaInputSettings           *settings,
                          ClutterInputDevice          *device,
                          GDesktopPointerAccelProfile  profile)
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
      G_GNUC_FALLTHROUGH;
    case G_DESKTOP_POINTER_ACCEL_PROFILE_DEFAULT:
      values[0] = defaults[0];
      values[1] = defaults[1];
      break;
    }

  change_property (settings, device, "libinput Accel Profile Enabled",
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
  ClutterInputCapabilities caps = clutter_input_device_get_capabilities (device);

  if ((caps & CLUTTER_INPUT_CAPABILITY_POINTER) == 0)
    return;
  if ((caps &
       (CLUTTER_INPUT_CAPABILITY_TRACKBALL |
        CLUTTER_INPUT_CAPABILITY_TOUCHPAD |
        CLUTTER_INPUT_CAPABILITY_TRACKPOINT)) != 0)
    return;

  set_device_accel_profile (settings, device, profile);
}

static void
meta_input_settings_x11_set_touchpad_accel_profile (MetaInputSettings          *settings,
                                                    ClutterInputDevice         *device,
                                                    GDesktopPointerAccelProfile profile)
{
  ClutterInputCapabilities caps = clutter_input_device_get_capabilities (device);

  if ((caps & CLUTTER_INPUT_CAPABILITY_TOUCHPAD) == 0)
    return;

  set_device_accel_profile (settings, device, profile);
}

static void
meta_input_settings_x11_set_trackball_accel_profile (MetaInputSettings          *settings,
                                                     ClutterInputDevice         *device,
                                                     GDesktopPointerAccelProfile profile)
{
  ClutterInputCapabilities caps = clutter_input_device_get_capabilities (device);

  if ((caps & CLUTTER_INPUT_CAPABILITY_TRACKBALL) == 0)
    return;

  set_device_accel_profile (settings, device, profile);
}

static void
meta_input_settings_x11_set_pointing_stick_accel_profile (MetaInputSettings           *settings,
                                                          ClutterInputDevice          *device,
                                                          GDesktopPointerAccelProfile  profile)
{
  ClutterInputCapabilities caps = clutter_input_device_get_capabilities (device);

  if ((caps & CLUTTER_INPUT_CAPABILITY_TRACKPOINT) == 0)
    return;

  set_device_accel_profile (settings, device, profile);
}

static void
meta_input_settings_x11_set_pointing_stick_scroll_method (MetaInputSettings                 *settings,
                                                          ClutterInputDevice                *device,
                                                          GDesktopPointingStickScrollMethod  method)
{
  ClutterInputCapabilities caps = clutter_input_device_get_capabilities (device);
  guchar *defaults;
  guchar values[3] = { 0 }; /* 2fg, edge, on-button */

  if ((caps & CLUTTER_INPUT_CAPABILITY_TRACKPOINT) == 0)
    return;

  defaults = get_property (device, "libinput Scroll Method Enabled Default",
                           XA_INTEGER, 8, 3);
  if (!defaults)
    return;

  switch (method)
    {
    case G_DESKTOP_POINTING_STICK_SCROLL_METHOD_DEFAULT:
      values[0] = defaults[0];
      values[1] = defaults[1];
      values[2] = defaults[2];
      break;
    case G_DESKTOP_POINTING_STICK_SCROLL_METHOD_NONE:
      values[0] = 0;
      values[1] = 0;
      values[2] = 0;
      break;
    case G_DESKTOP_POINTING_STICK_SCROLL_METHOD_ON_BUTTON_DOWN:
      values[0] = 0;
      values[1] = 0;
      values[2] = 1;
      break;
    default:
      g_assert_not_reached ();
      return;
    }

  change_property (settings, device, "libinput Scroll Method Enabled",
                   XA_INTEGER, 8, &values, 3);

  meta_XFree (defaults);
}

static void
meta_input_settings_x11_set_tablet_mapping (MetaInputSettings     *settings,
                                            ClutterInputDevice    *device,
                                            GDesktopTabletMapping  mapping)
{
  MetaBackend *backend = get_backend (settings);
  Display *xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));
  XDevice *xdev;

  /* Grab the puke bucket! */
  mtk_x11_error_trap_push (xdisplay);
  xdev = device_ensure_xdevice (settings, device);
  if (xdev)
    {
      XSetDeviceMode (xdisplay, xdev,
                      mapping == G_DESKTOP_TABLET_MAPPING_ABSOLUTE ?
                      Absolute : Relative);
    }

  mtk_x11_error_trap_pop (xdisplay);
}

static gboolean
device_query_area (MetaInputSettings  *settings,
                   ClutterInputDevice *device,
                   gint               *x,
                   gint               *y,
                   gint               *width,
                   gint               *height)
{
  MetaBackend *backend = get_backend (settings);
  Display *xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));
  gint device_id, n_devices, i;
  XIDeviceInfo *info;
  Atom abs_x, abs_y;

  *width = *height = 0;
  device_id = meta_input_device_x11_get_device_id (device);
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
  change_property (settings, device, "Wacom Tablet Area",
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

  if (!device_query_area (settings, device, &x, &y, &width, &height))
    return;

  area[0] = (width * padding_left) + x;
  area[1] = (height * padding_top) + y;
  area[2] = width - (width * padding_right) + x;
  area[3] = height - (height * padding_bottom) + y;
  update_tablet_area (settings, device, area);
}

static void
meta_input_settings_x11_set_tablet_aspect_ratio (MetaInputSettings  *settings,
                                                 ClutterInputDevice *device,
                                                 gdouble             aspect_ratio)
{
  int32_t dev_x, dev_y, dev_width, dev_height, area[4] = { 0 };

  if (!device_query_area (settings, device,
                          &dev_x, &dev_y, &dev_width, &dev_height))
    return;

  if (aspect_ratio > 0)
    {
      double dev_aspect;

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
  MetaBackend *backend = get_backend (settings);
  Display *xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));
  XDevice *xdev;

  /* Grab the puke bucket! */
  mtk_x11_error_trap_push (xdisplay);
  xdev = device_ensure_xdevice (settings, device);
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

  mtk_x11_error_trap_pop (xdisplay);
}

static void
meta_input_settings_x11_set_mouse_middle_click_emulation (MetaInputSettings  *settings,
                                                          ClutterInputDevice *device,
                                                          gboolean            enabled)
{
  guchar value = enabled ? 1 : 0;
  ClutterInputCapabilities caps = clutter_input_device_get_capabilities (device);

  if ((caps & CLUTTER_INPUT_CAPABILITY_POINTER) == 0)
    return;
  if ((caps &
       (CLUTTER_INPUT_CAPABILITY_TRACKBALL |
        CLUTTER_INPUT_CAPABILITY_TOUCHPAD |
        CLUTTER_INPUT_CAPABILITY_TRACKPOINT)) != 0)
    return;

  change_property (settings, device, "libinput Middle Emulation Enabled",
                   XA_INTEGER, 8, &value, 1);
}

static void
meta_input_settings_x11_set_touchpad_middle_click_emulation (MetaInputSettings  *settings,
                                                             ClutterInputDevice *device,
                                                             gboolean            enabled)
{
  guchar value = enabled ? 1 : 0;
  ClutterInputCapabilities caps = clutter_input_device_get_capabilities (device);

  if ((caps & CLUTTER_INPUT_CAPABILITY_TOUCHPAD) == 0)
    return;

  change_property (settings, device, "libinput Middle Emulation Enabled",
                   XA_INTEGER, 8, &value, 1);
}

static void
meta_input_settings_x11_set_trackball_middle_click_emulation (MetaInputSettings  *settings,
                                                              ClutterInputDevice *device,
                                                              gboolean            enabled)
{
  guchar value = enabled ? 1 : 0;
  ClutterInputCapabilities caps = clutter_input_device_get_capabilities (device);

  if ((caps & CLUTTER_INPUT_CAPABILITY_TRACKBALL) == 0)
    return;

  change_property (settings, device, "libinput Middle Emulation Enabled",
                   XA_INTEGER, 8, &value, 1);
}

static void
meta_input_settings_x11_set_stylus_pressure (MetaInputSettings      *settings,
                                             ClutterInputDevice     *device,
                                             ClutterInputDeviceTool *tool,
                                             const gint32            pressure[4])
{
  guint32 values[4] = { pressure[0], pressure[1], pressure[2], pressure[3] };

  change_property (settings, device, "Wacom Pressurecurve", XA_INTEGER, 32,
                   &values, G_N_ELEMENTS (values));
}

static void
meta_input_settings_x11_class_init (MetaInputSettingsX11Class *klass)
{
  MetaInputSettingsClass *input_settings_class = META_INPUT_SETTINGS_CLASS (klass);

  input_settings_class->set_send_events = meta_input_settings_x11_set_send_events;
  input_settings_class->set_matrix = meta_input_settings_x11_set_matrix;
  input_settings_class->set_speed = meta_input_settings_x11_set_speed;
  input_settings_class->set_left_handed = meta_input_settings_x11_set_left_handed;
  input_settings_class->set_tap_enabled = meta_input_settings_x11_set_tap_enabled;
  input_settings_class->set_tap_button_map = meta_input_settings_x11_set_tap_button_map;
  input_settings_class->set_tap_and_drag_enabled = meta_input_settings_x11_set_tap_and_drag_enabled;
  input_settings_class->set_tap_and_drag_lock_enabled =
    meta_input_settings_x11_set_tap_and_drag_lock_enabled;
  input_settings_class->set_disable_while_typing = meta_input_settings_x11_set_disable_while_typing;
  input_settings_class->set_invert_scroll = meta_input_settings_x11_set_invert_scroll;
  input_settings_class->set_edge_scroll = meta_input_settings_x11_set_edge_scroll;
  input_settings_class->set_two_finger_scroll = meta_input_settings_x11_set_two_finger_scroll;
  input_settings_class->set_scroll_button = meta_input_settings_x11_set_scroll_button;
  input_settings_class->set_click_method = meta_input_settings_x11_set_click_method;
  input_settings_class->set_keyboard_repeat = meta_input_settings_x11_set_keyboard_repeat;

  input_settings_class->set_tablet_mapping = meta_input_settings_x11_set_tablet_mapping;
  input_settings_class->set_tablet_aspect_ratio = meta_input_settings_x11_set_tablet_aspect_ratio;
  input_settings_class->set_tablet_area = meta_input_settings_x11_set_tablet_area;

  input_settings_class->set_mouse_accel_profile = meta_input_settings_x11_set_mouse_accel_profile;
  input_settings_class->set_touchpad_accel_profile = meta_input_settings_x11_set_touchpad_accel_profile;
  input_settings_class->set_trackball_accel_profile = meta_input_settings_x11_set_trackball_accel_profile;
  input_settings_class->set_pointing_stick_accel_profile = meta_input_settings_x11_set_pointing_stick_accel_profile;
  input_settings_class->set_pointing_stick_scroll_method = meta_input_settings_x11_set_pointing_stick_scroll_method;

  input_settings_class->set_stylus_pressure = meta_input_settings_x11_set_stylus_pressure;
  input_settings_class->set_stylus_button_map = meta_input_settings_x11_set_stylus_button_map;

  input_settings_class->set_mouse_middle_click_emulation = meta_input_settings_x11_set_mouse_middle_click_emulation;
  input_settings_class->set_touchpad_middle_click_emulation = meta_input_settings_x11_set_touchpad_middle_click_emulation;
  input_settings_class->set_trackball_middle_click_emulation = meta_input_settings_x11_set_trackball_middle_click_emulation;

  input_settings_class->has_two_finger_scroll = meta_input_settings_x11_has_two_finger_scroll;
}

static void
meta_input_settings_x11_init (MetaInputSettingsX11 *settings)
{
}
