/*
 * Copyright 2023 Red Hat
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

#include "meta-test/meta-test-monitor.h"

#include "backends/meta-virtual-monitor.h"
#include "backends/meta-monitor-manager-private.h"
#include "meta/meta-backend.h"

struct _MetaTestMonitor
{
  GObject parent;

  MetaVirtualMonitor *virtual_monitor;
};

G_DEFINE_TYPE (MetaTestMonitor, meta_test_monitor, G_TYPE_OBJECT)

static void
meta_test_monitor_dispose (GObject *object)
{
  MetaTestMonitor *test_monitor = META_TEST_MONITOR (object);

  g_clear_object (&test_monitor->virtual_monitor);

  G_OBJECT_CLASS (meta_test_monitor_parent_class)->dispose (object);
}

static void
meta_test_monitor_class_init (MetaTestMonitorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_test_monitor_dispose;
}

static void
meta_test_monitor_init (MetaTestMonitor *test_monitor)
{
}

MetaTestMonitor *
meta_test_monitor_new (MetaContext  *context,
                       int           width,
                       int           height,
                       float         refresh_rate,
                       GError      **error)
{
  MetaBackend *backend = meta_context_get_backend (context);
  MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);
  g_autoptr (MetaVirtualMonitorInfo) monitor_info = NULL;
  static int serial_count = 0x10000;
  g_autofree char *serial = NULL;
  MetaVirtualMonitor *virtual_monitor;
  MetaTestMonitor *test_monitor;

  serial = g_strdup_printf ("0x%x", serial_count++);
  monitor_info = meta_virtual_monitor_info_new (width, height, refresh_rate,
                                                "MetaTestVendor",
                                                "MetaTestMonitor",
                                                serial);
  virtual_monitor = meta_monitor_manager_create_virtual_monitor (monitor_manager,
                                                                 monitor_info,
                                                                 error);
  if (!virtual_monitor)
    return NULL;

  g_idle_add_once ((GSourceOnceFunc) meta_monitor_manager_reload,
                   monitor_manager);

  test_monitor = g_object_new (META_TYPE_TEST_MONITOR, NULL);
  test_monitor->virtual_monitor = virtual_monitor;

  return test_monitor;
}

/**
 * meta_test_monitor_destroy:
 * @test_monitor: (transfer full): A #MetaTestMonitor
 */
void
meta_test_monitor_destroy (MetaTestMonitor *test_monitor)
{
  g_object_run_dispose (G_OBJECT (test_monitor));
  g_object_unref (test_monitor);
}
