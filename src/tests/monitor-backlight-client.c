/*
 * Copyright (C) 2025 Red Hat, Inc.
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
 */

#include "config.h"

#include <glib.h>
#include <gio/gio.h>
#include <stdint.h>
#include <stdio.h>

#include "meta-dbus-display-config.h"

static void
backlight_changed_cb (MetaDBusDisplayConfig *proxy,
                      GParamSpec            *pspec,
                      int                   *new_value)
{
  GVariant *backlight_variant = NULL;
  uint32_t serial;
  g_autoptr (GVariant) backlights = NULL;
  g_autoptr (GVariant) backlight_monitor = NULL;
  int value;

  backlight_variant = meta_dbus_display_config_get_backlight (proxy);
  g_variant_get (backlight_variant, "(u@aa{sv})",
                 &serial, &backlights);
  g_variant_get_child (backlights, 0, "@a{sv}",
                       &backlight_monitor);
  g_variant_lookup (backlight_monitor, "value", "i", &value);

  *new_value = value;
}

static void
test_legacy_backlight (MetaDBusDisplayConfig *proxy)
{
  g_autoptr (GVariant) resources_variant = NULL;
  g_autoptr (GError) error = NULL;
  unsigned int serial;
  g_autoptr (GVariant) crtcs = NULL;
  g_autoptr (GVariant) outputs = NULL;
  g_autoptr (GVariant) modes = NULL;
  int max_screen_width, max_screen_height;
  int output_id;
  g_autoptr (GVariant) output_properties = NULL;
  int normalized_backlight;
  gulong handler_id;
  int new_value = -1;

  g_debug ("Running %s test", __func__);

  meta_dbus_display_config_call_get_resources_sync (proxy,
                                                    &serial,
                                                    &crtcs,
                                                    &outputs,
                                                    &modes,
                                                    &max_screen_width,
                                                    &max_screen_height,
                                                    NULL,
                                                    &error);
  g_assert_no_error (error);

  g_assert_cmpint (g_variant_n_children (outputs), ==, 2);

  g_variant_get_child (outputs, 0,
                       "(uxiausauau@a{sv})",
                       &output_id,
                       NULL,
                       NULL,
                       NULL,
                       NULL,
                       NULL,
                       NULL,
                       &output_properties);
  g_variant_lookup (output_properties,
                    "backlight", "i",
                    &normalized_backlight);
  g_assert_cmpint (normalized_backlight, >=, 0);
  g_assert_cmpint (normalized_backlight, <=, 100);

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  meta_dbus_display_config_call_change_backlight_sync (proxy,
                                                       serial,
                                                       output_id,
                                                       20,
                                                       NULL,
                                                       NULL, &error);
  G_GNUC_END_IGNORE_DEPRECATIONS

  g_debug ("Checking denormalization");

  handler_id = g_signal_connect (proxy, "notify::backlight",
                                 G_CALLBACK (backlight_changed_cb), &new_value);
  while (new_value == -1)
    g_main_context_iteration (NULL, TRUE);
  /* min + (max - min) * 20% = 10 + (150 - 10) * 0,2 = 38 */
  g_assert_cmpint (new_value, ==, 38);

  g_signal_handler_disconnect (proxy, handler_id);
}

static void
test_set_backlight (MetaDBusDisplayConfig *proxy)
{
  g_autoptr (GError) error = NULL;
  GVariant *backlight_variant;
  uint32_t serial;
  g_autoptr (GVariant) backlights = NULL;
  g_autoptr (GVariant) backlight_monitor = NULL;
  g_autofree char *connector = NULL;
  int min;
  int max;
  int value;
  gulong handler_id;
  int new_value = -1;

  g_debug ("Running %s test", __func__);

  backlight_variant = meta_dbus_display_config_get_backlight (proxy);
  g_variant_get (backlight_variant, "(u@aa{sv})",
                 &serial, &backlights);
  g_variant_get_child (backlights, 0, "@a{sv}",
                       &backlight_monitor);
  g_variant_lookup (backlight_monitor, "connector", "s", &connector);
  g_variant_lookup (backlight_monitor, "min", "i", &min);
  g_variant_lookup (backlight_monitor, "max", "i", &max);
  g_variant_lookup (backlight_monitor, "value", "i", &value);

  g_assert_cmpint (min, ==, 10);
  g_assert_cmpint (max, ==, 150);

  handler_id = g_signal_connect (proxy, "notify::backlight",
                                 G_CALLBACK (backlight_changed_cb), &new_value);
  meta_dbus_display_config_call_set_backlight_sync (proxy,
                                                    serial,
                                                    connector,
                                                    value - 10,
                                                    NULL, &error);
  g_assert_no_error (error);
  while (new_value == -1)
    g_main_context_iteration (NULL, TRUE);
  g_assert_cmpint (new_value, ==, value - 10);

  g_signal_handler_disconnect (proxy, handler_id);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaDBusDisplayConfig) proxy = NULL;
  g_autoptr (GError) error = NULL;

  proxy = meta_dbus_display_config_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                           G_DBUS_PROXY_FLAGS_NONE,
                                                           "org.gnome.Mutter.DisplayConfig",
                                                           "/org/gnome/Mutter/DisplayConfig",
                                                           NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (proxy);

  test_set_backlight (proxy);
  test_legacy_backlight (proxy);

  return EXIT_SUCCESS;
}
