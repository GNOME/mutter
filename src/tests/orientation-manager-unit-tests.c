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

#include "orientation-manager-unit-tests.h"

#include "tests/meta-monitor-test-utils.h"
#include "tests/meta-sensors-proxy-mock.h"

static void
meta_test_orientation_manager_no_daemon (void)
{
  g_autoptr (MetaOrientationManager) manager = NULL;

  manager = g_object_new (META_TYPE_ORIENTATION_MANAGER, NULL);
  g_assert_false (meta_orientation_manager_has_accelerometer (manager));
  g_assert_cmpuint (meta_orientation_manager_get_orientation (manager),
                    ==,
                    META_ORIENTATION_UNDEFINED);
}

static void
meta_test_orientation_manager_no_device (void)
{
  g_autoptr (MetaOrientationManager) manager = NULL;
  MetaSensorsProxyMock* orientation_mock = NULL;

  orientation_mock = meta_sensors_proxy_mock_get ();
  manager = g_object_new (META_TYPE_ORIENTATION_MANAGER, NULL);
  g_assert_false (meta_orientation_manager_has_accelerometer (manager));
  g_assert_cmpuint (meta_orientation_manager_get_orientation (manager),
                    ==,
                    META_ORIENTATION_UNDEFINED);

  g_object_unref (orientation_mock);
}

static gboolean
on_wait_for_accel_timeout (gpointer data)
{
  guint *timeout_p = data;

  *timeout_p = 0;
  return G_SOURCE_REMOVE;
}

static void
meta_test_orientation_manager_has_accelerometer (void)
{
  g_autoptr (MetaOrientationManager) manager = NULL;
  g_autoptr (MetaSensorsProxyMock) orientation_mock = NULL;
  guint timeout_id;

  manager = g_object_new (META_TYPE_ORIENTATION_MANAGER, NULL);
  orientation_mock = meta_sensors_proxy_mock_get ();

  timeout_id = g_timeout_add_seconds (10, on_wait_for_accel_timeout, &timeout_id);
  meta_sensors_proxy_mock_set_property (orientation_mock,
                                        "HasAccelerometer",
                                        g_variant_new_boolean (TRUE));

  while (!meta_orientation_manager_has_accelerometer (manager) &&
         timeout_id != 0)
    g_main_context_iteration (NULL, TRUE);

  g_debug ("Checking whether accelerometer is present");
  g_assert_true (meta_orientation_manager_has_accelerometer (manager));
  g_assert_cmpuint (meta_orientation_manager_get_orientation (manager),
                    ==,
                    META_ORIENTATION_UNDEFINED);
  g_clear_handle_id (&timeout_id, g_source_remove);
}

static void
orientation_changed_cb (MetaOrientationManager *manager,
                        gpointer                user_data)
{
  gboolean *changed_called = user_data;

  *changed_called = TRUE;
}

static void
meta_test_orientation_manager_accelerometer_orientations (void)
{
  g_autoptr (MetaOrientationManager) manager = NULL;
  g_autoptr (MetaSensorsProxyMock) orientation_mock = NULL;

  manager = g_object_new (META_TYPE_ORIENTATION_MANAGER, NULL);
  orientation_mock = meta_sensors_proxy_mock_get ();

  MetaOrientation initial;
  gboolean changed_called;
  unsigned i;

  g_signal_connect (manager, "orientation-changed",
                    G_CALLBACK (orientation_changed_cb),
                    &changed_called);

  initial = meta_orientation_manager_get_orientation (manager);

  for (i = initial + 1; i != initial; i = (i + 1) % META_N_ORIENTATIONS)
    {
      changed_called = FALSE;
      g_debug ("Checking orientation %d", i);
      meta_sensors_proxy_mock_set_orientation (orientation_mock, i);
      while (meta_orientation_manager_get_orientation (manager) != i)
        g_main_context_iteration (NULL, TRUE);

      if (i != META_ORIENTATION_UNDEFINED)
        g_assert_true (changed_called);
      else
        g_assert_false (changed_called);
    }
}

void
init_orientation_manager_tests (void)
{
  g_test_add_func ("/backends/orientation-manager/no-daemon",
                   meta_test_orientation_manager_no_daemon);
  g_test_add_func ("/backends/orientation-manager/no-device",
                   meta_test_orientation_manager_no_device);
  g_test_add_func ("/backends/orientation-manager/has-accelerometer",
                   meta_test_orientation_manager_has_accelerometer);
  g_test_add_func ("/backends/orientation-manager/accelerometer-orientations",
                   meta_test_orientation_manager_accelerometer_orientations);
}
