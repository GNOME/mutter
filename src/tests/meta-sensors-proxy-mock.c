/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2020 Canonical, Ltd.
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

#include "meta-sensors-proxy-mock.h"

#define SENSORS_MOCK_TEMPLATE "iio-sensors-proxy"

static MetaSensorsProxyMock *sensors_proxy_mock = NULL;

static const char *
orientation_to_string (MetaOrientation orientation)
{
  const char *orientation_str = "undefined";

  switch (orientation)
    {
    case META_ORIENTATION_UNDEFINED:
      orientation_str = "undefined";
      break;
    case META_ORIENTATION_NORMAL:
      orientation_str = "normal";
      break;
    case META_ORIENTATION_BOTTOM_UP:
      orientation_str = "bottom-up";
      break;
    case META_ORIENTATION_LEFT_UP:
      orientation_str = "left-up";
      break;
    case META_ORIENTATION_RIGHT_UP:
      orientation_str = "right-up";
      break;
    }

  return orientation_str;
}

static void
on_proxy_call_cb (GObject      *object,
                  GAsyncResult *res,
                  gpointer      user_data)
{
  g_autoptr(GError) error = NULL;
  GVariant **ret = user_data;

  *ret = g_dbus_proxy_call_finish (G_DBUS_PROXY (object), res, &error);
  g_assert_no_error (error);
  g_assert_nonnull (ret);
}

static GVariant *
get_internal_property_value (MetaSensorsProxyMock *proxy,
                             const char           *property_name)
{
  g_autoptr (GVariant) ret = NULL;

  g_dbus_proxy_call (proxy, "GetInternalProperty",
                     g_variant_new ("(s)", property_name),
                     G_DBUS_CALL_FLAGS_NO_AUTO_START, -1, NULL,
                     on_proxy_call_cb, &ret);

  while (!ret)
    g_main_context_iteration (NULL, TRUE);

  return g_variant_get_child_value (ret, 0);
}

static void
ensure_property (MetaSensorsProxyMock *proxy,
                 const char           *property_name,
                 GVariant             *expected_value)
{
  g_autoptr (GVariant) value = NULL;
  g_autoptr (GVariant) expected = NULL;
  gboolean equal_properties;

  value = get_internal_property_value (proxy, property_name);

  if (!g_variant_is_of_type (value, G_VARIANT_TYPE_VARIANT))
    {
      g_autoptr (GVariant) tmp = g_variant_ref (value);
      value = g_variant_new ("v", tmp);
    }

  if (g_variant_is_of_type (expected_value, G_VARIANT_TYPE_VARIANT))
    expected = g_variant_ref (expected_value);
  else
    expected = g_variant_new ("v", expected_value);

  equal_properties = g_variant_equal (expected, value);

  if (!equal_properties)
    {
      g_autofree char *actual_str = g_variant_print (value, TRUE);
      g_autofree char *expected_str = g_variant_print (expected, TRUE);

      g_debug ("Property: %s", property_name);
      g_debug ("Expected: %s", expected_str);
      g_debug ("Actual: %s", actual_str);
    }

  g_assert_true (equal_properties);
}

static void
stop_sensors_mock (GDBusConnection *connection)
{
  g_autoptr (GVariant) ret = NULL;
  g_autoptr (GError) error = NULL;

  ret = g_dbus_connection_call_sync (connection,
                                     "org.gnome.Mutter.TestDBusMocksManager",
                                     "/org/gnome/Mutter/TestDBusMocksManager",
                                     "org.gnome.Mutter.TestDBusMocksManager",
                                     "StopLocalTemplate",
                                     g_variant_new ("(s)", SENSORS_MOCK_TEMPLATE),
                                     NULL, G_DBUS_CALL_FLAGS_NO_AUTO_START, -1,
                                     NULL, &error);

  g_assert_no_error (error);
  g_assert_nonnull (ret);
}

static void
start_sensors_mock (void)
{
  g_autoptr (GDBusConnection) connection = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) ret = NULL;

  connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
  g_assert_no_error (error);

  ret = g_dbus_connection_call_sync (connection,
                                     "org.gnome.Mutter.TestDBusMocksManager",
                                     "/org/gnome/Mutter/TestDBusMocksManager",
                                     "org.gnome.Mutter.TestDBusMocksManager",
                                     "StartFromLocalTemplate",
                                     g_variant_new ("(s)", SENSORS_MOCK_TEMPLATE),
                                     NULL, G_DBUS_CALL_FLAGS_NO_AUTO_START, -1,
                                     NULL, &error);

  g_assert_no_error (error);
  g_assert_nonnull (ret);
}

static void
on_proxy_removed (gpointer data)
{
  g_autoptr (GDBusConnection) connection = data;

  stop_sensors_mock (connection);
}

MetaSensorsProxyMock *
meta_sensors_proxy_mock_get (void)
{
  GDBusProxy *proxy = NULL;
  g_autoptr (GError) error = NULL;

  if (sensors_proxy_mock)
    return g_object_ref (sensors_proxy_mock);

  start_sensors_mock ();

  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                         G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START |
                                         G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                         NULL,
                                         "net.hadess.SensorProxy",
                                         "/net/hadess/SensorProxy",
                                         "org.freedesktop.DBus.Mock",
                                         NULL, &error);
  g_assert_true (G_IS_DBUS_PROXY (proxy));
  g_assert_no_error (error);

  sensors_proxy_mock = proxy;
  g_object_add_weak_pointer (G_OBJECT (sensors_proxy_mock),
                             (gpointer *) &sensors_proxy_mock);

  g_object_set_data_full (G_OBJECT (proxy), "proxy-data",
                          g_object_ref (g_dbus_proxy_get_connection (proxy)),
                          on_proxy_removed);

  return proxy;
}

void
meta_sensors_proxy_mock_set_property (MetaSensorsProxyMock *proxy,
                                      const gchar          *property_name,
                                      GVariant             *value)
{
  g_autoptr (GVariant) ret = NULL;
  g_autoptr (GVariant) reffed_value = g_variant_ref (value);

  g_dbus_proxy_call (proxy, "SetInternalProperty",
                     g_variant_new ("(ssv)",
                                    "net.hadess.SensorProxy",
                                    property_name,
                                    reffed_value),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1, NULL, on_proxy_call_cb, &ret);

  while (!ret)
    g_main_context_iteration (NULL, TRUE);

  g_assert_nonnull (ret);

  ensure_property (proxy, property_name, value);
}

void
meta_sensors_proxy_mock_set_orientation (MetaSensorsProxyMock *proxy,
                                         MetaOrientation       orientation)
{
  const char *orientation_str;

  meta_sensors_proxy_mock_set_property (proxy, "HasAccelerometer",
                                        g_variant_new_boolean (TRUE));

  orientation_str = orientation_to_string (orientation);
  meta_sensors_proxy_mock_set_property (proxy, "AccelerometerOrientation",
                                        g_variant_new_string (orientation_str));
}

void
meta_sensors_proxy_mock_wait_accelerometer_claimed (MetaSensorsProxyMock *proxy,
                                                    gboolean              claimed)
{
  while (TRUE)
    {
      g_autoptr (GVariant) ret = NULL;
      size_t n_owners = 0;

      ret = get_internal_property_value (proxy, "AccelerometerOwners");

      if (!g_variant_get_strv (ret, &n_owners))
        g_assert_not_reached ();

      if (n_owners == (claimed ? 1 : 0))
        break;

      g_main_context_iteration (NULL, TRUE);
    }
}
