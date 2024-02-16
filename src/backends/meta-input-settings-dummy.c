/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright 2021 Canonical, Ltd.
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
 * Author: Marco Trevisan <marco.trevisan@canonical.com>
 */

#include "config.h"

#include "backends/meta-input-settings-dummy.h"

G_DEFINE_TYPE (MetaInputSettingsDummy,
               meta_input_settings_dummy,
               META_TYPE_INPUT_SETTINGS)

static void
meta_input_settings_dummy_set_send_events (MetaInputSettings        *settings,
                                           ClutterInputDevice       *device,
                                           GDesktopDeviceSendEvents  mode)
{
}

static void
meta_input_settings_dummy_set_matrix (MetaInputSettings  *settings,
                                      ClutterInputDevice *device,
                                      const float         matrix[6])
{
}

static void
meta_input_settings_dummy_set_speed (MetaInputSettings  *settings,
                                     ClutterInputDevice *device,
                                     gdouble             speed)
{
}

static void
meta_input_settings_dummy_set_left_handed (MetaInputSettings  *settings,
                                           ClutterInputDevice *device,
                                           gboolean            enabled)
{
}

static void
meta_input_settings_dummy_set_tap_enabled (MetaInputSettings  *settings,
                                           ClutterInputDevice *device,
                                           gboolean            enabled)
{
}

static void
meta_input_settings_dummy_set_tap_button_map (MetaInputSettings           *settings,
                                              ClutterInputDevice          *device,
                                              GDesktopTouchpadTapButtonMap mode)
{
}

static void
meta_input_settings_dummy_set_tap_and_drag_enabled (MetaInputSettings  *settings,
                                                    ClutterInputDevice *device,
                                                    gboolean            enabled)
{
}

static void
meta_input_settings_dummy_set_tap_and_drag_lock_enabled (MetaInputSettings  *settings,
                                                         ClutterInputDevice *device,
                                                         gboolean            enabled)
{
}

static void
meta_input_settings_dummy_set_disable_while_typing (MetaInputSettings  *settings,
                                                    ClutterInputDevice *device,
                                                    gboolean            enabled)
{
}

static void
meta_input_settings_dummy_set_invert_scroll (MetaInputSettings  *settings,
                                             ClutterInputDevice *device,
                                             gboolean            inverted)
{
}

static void
meta_input_settings_dummy_set_edge_scroll (MetaInputSettings  *settings,
                                           ClutterInputDevice *device,
                                           gboolean            enabled)
{
}

static void
meta_input_settings_dummy_set_two_finger_scroll (MetaInputSettings  *settings,
                                                 ClutterInputDevice *device,
                                                 gboolean            enabled)
{
}

static void
meta_input_settings_dummy_set_scroll_button (MetaInputSettings  *settings,
                                             ClutterInputDevice *device,
                                             guint               button,
                                             gboolean            button_lock)
{
}


static void
meta_input_settings_dummy_set_click_method (MetaInputSettings           *settings,
                                            ClutterInputDevice          *device,
                                            GDesktopTouchpadClickMethod  mode)
{
}


static void
meta_input_settings_dummy_set_keyboard_repeat (MetaInputSettings *settings,
                                               gboolean           repeat,
                                               guint              delay,
                                               guint              interval)
{
}


static void
meta_input_settings_dummy_set_tablet_mapping (MetaInputSettings     *settings,
                                              ClutterInputDevice    *device,
                                              GDesktopTabletMapping  mapping)
{
}

static void
meta_input_settings_dummy_set_tablet_aspect_ratio (MetaInputSettings  *settings,
                                                   ClutterInputDevice *device,
                                                   double              ratio)
{
}

static void
meta_input_settings_dummy_set_tablet_area (MetaInputSettings  *settings,
                                           ClutterInputDevice *device,
                                           gdouble             padding_left,
                                           gdouble             padding_right,
                                           gdouble             padding_top,
                                           gdouble             padding_bottom)
{
}


static void
meta_input_settings_dummy_set_mouse_accel_profile (MetaInputSettings           *settings,
                                                   ClutterInputDevice          *device,
                                                   GDesktopPointerAccelProfile  profile)
{
}

static void
meta_input_settings_dummy_set_touchpad_accel_profile (MetaInputSettings           *settings,
                                                      ClutterInputDevice          *device,
                                                      GDesktopPointerAccelProfile  profile)
{
}

static void
meta_input_settings_dummy_set_trackball_accel_profile (MetaInputSettings           *settings,
                                                       ClutterInputDevice          *device,
                                                       GDesktopPointerAccelProfile  profile)
{
}


static void
meta_input_settings_dummy_set_stylus_pressure (MetaInputSettings      *settings,
                                               ClutterInputDevice     *device,
                                               ClutterInputDeviceTool *tool,
                                               const gint32            curve[4])
{
}

static void
meta_input_settings_dummy_set_stylus_button_map (MetaInputSettings          *settings,
                                                 ClutterInputDevice         *device,
                                                 ClutterInputDeviceTool     *tool,
                                                 GDesktopStylusButtonAction  primary,
                                                 GDesktopStylusButtonAction  secondary,
                                                 GDesktopStylusButtonAction  tertiary)
{
}


static void
meta_input_settings_dummy_set_mouse_middle_click_emulation (MetaInputSettings  *settings,
                                                            ClutterInputDevice *device,
                                                            gboolean            enabled)
{
}

static void
meta_input_settings_dummy_set_touchpad_middle_click_emulation (MetaInputSettings  *settings,
                                                               ClutterInputDevice *device,
                                                               gboolean            enabled)
{
}

static void
meta_input_settings_dummy_set_trackball_middle_click_emulation (MetaInputSettings  *settings,
                                                                ClutterInputDevice *device,
                                                                gboolean            enabled)
{
}

static void
meta_input_settings_dummy_set_pointing_stick_scroll_method (MetaInputSettings                 *settings,
                                                            ClutterInputDevice                *device,
                                                            GDesktopPointingStickScrollMethod  method)
{
}

static void
meta_input_settings_dummy_set_pointing_stick_accel_profile (MetaInputSettings           *settings,
                                                            ClutterInputDevice          *device,
                                                            GDesktopPointerAccelProfile  profile)
{
}

static gboolean
meta_input_settings_dummy_has_two_finger_scroll (MetaInputSettings  *settings,
                                                 ClutterInputDevice *device)
{
  return FALSE;
}

static void
meta_input_settings_dummy_init (MetaInputSettingsDummy *input_settings)
{
}

static void
meta_input_settings_dummy_class_init (MetaInputSettingsDummyClass *klass)
{
  MetaInputSettingsClass *input_settings_class = META_INPUT_SETTINGS_CLASS (klass);

  input_settings_class->set_send_events =
    meta_input_settings_dummy_set_send_events;
  input_settings_class->set_matrix =
    meta_input_settings_dummy_set_matrix;
  input_settings_class->set_speed =
    meta_input_settings_dummy_set_speed;
  input_settings_class->set_left_handed =
    meta_input_settings_dummy_set_left_handed;
  input_settings_class->set_tap_enabled =
    meta_input_settings_dummy_set_tap_enabled;
  input_settings_class->set_tap_button_map =
    meta_input_settings_dummy_set_tap_button_map;
  input_settings_class->set_tap_and_drag_enabled =
    meta_input_settings_dummy_set_tap_and_drag_enabled;
  input_settings_class->set_tap_and_drag_lock_enabled =
    meta_input_settings_dummy_set_tap_and_drag_lock_enabled;
  input_settings_class->set_disable_while_typing =
    meta_input_settings_dummy_set_disable_while_typing;
  input_settings_class->set_invert_scroll =
    meta_input_settings_dummy_set_invert_scroll;
  input_settings_class->set_edge_scroll =
    meta_input_settings_dummy_set_edge_scroll;
  input_settings_class->set_two_finger_scroll =
    meta_input_settings_dummy_set_two_finger_scroll;
  input_settings_class->set_scroll_button =
    meta_input_settings_dummy_set_scroll_button;
  input_settings_class->set_click_method =
    meta_input_settings_dummy_set_click_method;
  input_settings_class->set_keyboard_repeat =
    meta_input_settings_dummy_set_keyboard_repeat;
  input_settings_class->set_tablet_mapping =
    meta_input_settings_dummy_set_tablet_mapping;
  input_settings_class->set_tablet_aspect_ratio =
    meta_input_settings_dummy_set_tablet_aspect_ratio;
  input_settings_class->set_tablet_area =
    meta_input_settings_dummy_set_tablet_area;
  input_settings_class->set_mouse_accel_profile =
    meta_input_settings_dummy_set_mouse_accel_profile;
  input_settings_class->set_touchpad_accel_profile =
    meta_input_settings_dummy_set_touchpad_accel_profile;
  input_settings_class->set_trackball_accel_profile =
    meta_input_settings_dummy_set_trackball_accel_profile;
  input_settings_class->set_pointing_stick_scroll_method =
    meta_input_settings_dummy_set_pointing_stick_scroll_method;
  input_settings_class->set_pointing_stick_accel_profile =
    meta_input_settings_dummy_set_pointing_stick_accel_profile;
  input_settings_class->set_stylus_pressure =
    meta_input_settings_dummy_set_stylus_pressure;
  input_settings_class->set_stylus_button_map =
    meta_input_settings_dummy_set_stylus_button_map;
  input_settings_class->set_mouse_middle_click_emulation =
    meta_input_settings_dummy_set_mouse_middle_click_emulation;
  input_settings_class->set_touchpad_middle_click_emulation =
    meta_input_settings_dummy_set_touchpad_middle_click_emulation;
  input_settings_class->set_trackball_middle_click_emulation =
    meta_input_settings_dummy_set_trackball_middle_click_emulation;
  input_settings_class->has_two_finger_scroll =
    meta_input_settings_dummy_has_two_finger_scroll;
}
