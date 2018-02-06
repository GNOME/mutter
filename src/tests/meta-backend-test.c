/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat, Inc.
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

#include "tests/meta-backend-test.h"

#include "tests/meta-monitor-manager-test.h"

struct _MetaBackendTest
{
  MetaBackendX11Nested parent;

  gboolean is_headless;
};

G_DEFINE_TYPE (MetaBackendTest, meta_backend_test, META_TYPE_BACKEND_X11_NESTED)

static void
meta_backend_test_init (MetaBackendTest *backend_test)
{
}

static void
on_monitors_changed_internal (MetaMonitorManager *monitor_manager,
                              MetaBackendTest    *backend_test)
{
  gboolean is_headless;
  gboolean was_headless;

  is_headless = meta_monitor_manager_is_headless (monitor_manager);
  was_headless = backend_test->is_headless;

  if (is_headless != was_headless)
    {
      ClutterMasterClock *master_clock;

      master_clock = _clutter_master_clock_get_default ();

      if (is_headless)
        {
          _clutter_master_clock_set_paused (master_clock, TRUE);
        }
      else
        {
          _clutter_master_clock_set_paused (master_clock, FALSE);
          _clutter_master_clock_start_running (master_clock);
        }
    }

  backend_test->is_headless = is_headless;
}

static MetaMonitorManager *
meta_backend_test_create_monitor_manager (MetaBackend *backend,
                                          GError     **error)
{
  MetaMonitorManager *monitor_manager;

  monitor_manager = g_object_new (META_TYPE_MONITOR_MANAGER_TEST,
                                  "backend", backend,
                                  NULL);

  g_signal_connect (monitor_manager, "monitors-changed-internal",
                    G_CALLBACK (on_monitors_changed_internal),
                    backend);

  return monitor_manager;
}

static void
meta_backend_test_class_init (MetaBackendTestClass *klass)
{
  MetaBackendClass *backend_class = META_BACKEND_CLASS (klass);

  backend_class->create_monitor_manager = meta_backend_test_create_monitor_manager;
}
