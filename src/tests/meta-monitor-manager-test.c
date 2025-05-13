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

#include "tests/meta-monitor-manager-test.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-crtc.h"
#include "backends/meta-gpu.h"
#include "backends/meta-monitor-config-manager.h"
#include "backends/meta-output.h"
#include "tests/meta-backend-test.h"
#include "tests/meta-crtc-test.h"
#include "tests/meta-monitor-test-utils.h"
#include "tests/meta-output-test.h"

struct _MetaMonitorManagerTest
{
  MetaMonitorManagerNative parent;

  int tiled_monitor_count;

  MetaLogicalMonitorLayoutMode layout_mode;

  MetaMonitorTestSetup *test_setup;
};

G_DEFINE_TYPE (MetaMonitorManagerTest, meta_monitor_manager_test,
               META_TYPE_MONITOR_MANAGER_NATIVE)

static MetaCreateTestSetupFunc initial_setup_func;

static MonitorTestCaseSetup default_test_case_setup = {
  .modes = {
    {
      .width = 800,
      .height = 600,
      .refresh_rate = 60.0
    }
  },
  .n_modes = 1,
  .outputs = {
     {
      .crtc = 0,
      .modes = { 0 },
      .n_modes = 1,
      .preferred_mode = 0,
      .possible_crtcs = { 0 },
      .n_possible_crtcs = 1,
      .width_mm = 222,
      .height_mm = 125
    },

  },
  .n_outputs = 1,
  .crtcs = {
    {
      .current_mode = 0
    },
  },
  .n_crtcs = 1,
};

static MetaMonitorTestSetup *
create_default_test_setup (MetaBackend *backend)
{
  return meta_create_monitor_test_setup (backend,
                                         &default_test_case_setup,
                                         MONITOR_TEST_FLAG_NO_STORED);
}

void
meta_init_monitor_test_setup (MetaCreateTestSetupFunc func)
{
  initial_setup_func = func;
}

void
meta_monitor_manager_test_emulate_hotplug (MetaMonitorManagerTest *manager_test,
                                           MetaMonitorTestSetup   *test_setup)
{
  MetaMonitorManager *manager = META_MONITOR_MANAGER (manager_test);
  MetaMonitorTestSetup *old_test_setup;

  old_test_setup = manager_test->test_setup;
  manager_test->test_setup = test_setup;

  meta_monitor_manager_reload (manager);

  g_free (old_test_setup);
}

void
meta_monitor_manager_test_set_handles_transforms (MetaMonitorManagerTest *manager_test,
                                                  gboolean                handles_transforms)
{
  MetaMonitorManager *manager = META_MONITOR_MANAGER (manager_test);
  MetaBackend *backend = meta_monitor_manager_get_backend (manager);
  MetaBackendTest *backend_test = META_BACKEND_TEST (backend);
  MetaGpu *gpu = meta_backend_test_get_gpu (backend_test);
  GList *l;

  for (l = meta_gpu_get_crtcs (gpu); l; l = l->next)
    {
      MetaCrtcTest *crtc_test = META_CRTC_TEST (l->data);

      meta_crtc_test_set_is_transform_handled (crtc_test, handles_transforms);
    }
}

int
meta_monitor_manager_test_get_tiled_monitor_count (MetaMonitorManagerTest *manager_test)
{
  return manager_test->tiled_monitor_count;
}

void
meta_monitor_manager_test_read_current (MetaMonitorManager *manager)
{
  MetaMonitorManagerTest *manager_test = META_MONITOR_MANAGER_TEST (manager);
  MetaBackend *backend = meta_monitor_manager_get_backend (manager);
  MetaBackendTest *backend_test = META_BACKEND_TEST (backend);
  MetaGpu *gpu = meta_backend_test_get_gpu (backend_test);
  MetaMonitorTestSetup *test_setup;

  test_setup = manager_test->test_setup;
  g_assert_nonnull (test_setup);

  meta_gpu_take_modes (gpu, g_steal_pointer (&test_setup->modes));
  meta_gpu_take_crtcs (gpu, g_steal_pointer (&test_setup->crtcs));
  meta_gpu_take_outputs (gpu, g_steal_pointer (&test_setup->outputs));
}

static void
meta_monitor_manager_test_tiled_monitor_added (MetaMonitorManager *manager,
                                               MetaMonitor        *monitor)
{
  MetaMonitorManagerTest *manager_test = META_MONITOR_MANAGER_TEST (manager);

  manager_test->tiled_monitor_count++;
}

static void
meta_monitor_manager_test_tiled_monitor_removed (MetaMonitorManager *manager,
                                                 MetaMonitor        *monitor)
{
  MetaMonitorManagerTest *manager_test = META_MONITOR_MANAGER_TEST (manager);

  manager_test->tiled_monitor_count--;
}

static float
meta_monitor_manager_test_calculate_monitor_mode_scale (MetaMonitorManager           *manager,
                                                        MetaLogicalMonitorLayoutMode  layout_mode,
                                                        MetaMonitor                  *monitor,
                                                        MetaMonitorMode              *monitor_mode)
{
  MetaMonitorManagerClass *parent_class =
    META_MONITOR_MANAGER_CLASS (meta_monitor_manager_test_parent_class);
  MetaOutput *output;
  MetaOutputTest *output_test;

  output = meta_monitor_get_main_output (monitor);
  output_test = META_OUTPUT_TEST (output);

  if (output_test->override_scale)
    return output_test->scale;

  return parent_class->calculate_monitor_mode_scale (manager,
                                                     layout_mode,
                                                     monitor,
                                                     monitor_mode);
}

void
meta_monitor_manager_test_set_layout_mode (MetaMonitorManagerTest       *manager_test,
                                           MetaLogicalMonitorLayoutMode  layout_mode)
{
  manager_test->layout_mode = layout_mode;
}


static MetaLogicalMonitorLayoutMode
meta_monitor_manager_test_get_default_layout_mode (MetaMonitorManager *manager)
{
  MetaMonitorManagerTest *manager_test = META_MONITOR_MANAGER_TEST (manager);
  return manager_test->layout_mode;
}

static void
meta_monitor_manager_test_constructed (GObject *object)
{
  MetaMonitorManagerTest *manager_test = META_MONITOR_MANAGER_TEST (object);
  MetaMonitorManager *manager = META_MONITOR_MANAGER (manager_test);
  MetaBackend *backend = meta_monitor_manager_get_backend (manager);

  if (initial_setup_func)
    manager_test->test_setup = initial_setup_func (backend);
  else
    manager_test->test_setup = create_default_test_setup (backend);

  G_OBJECT_CLASS (meta_monitor_manager_test_parent_class)->constructed (object);
}

static void
meta_monitor_manager_test_dispose (GObject *object)
{
  MetaMonitorManagerTest *manager_test = META_MONITOR_MANAGER_TEST (object);

  g_clear_pointer (&manager_test->test_setup, g_free);

  G_OBJECT_CLASS (meta_monitor_manager_test_parent_class)->dispose (object);
}

static void
meta_monitor_manager_test_init (MetaMonitorManagerTest *manager_test)
{
  manager_test->layout_mode = META_LOGICAL_MONITOR_LAYOUT_MODE_LOGICAL;
}

static void
meta_monitor_manager_test_class_init (MetaMonitorManagerTestClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaMonitorManagerClass *manager_class = META_MONITOR_MANAGER_CLASS (klass);

  object_class->constructed = meta_monitor_manager_test_constructed;
  object_class->dispose = meta_monitor_manager_test_dispose;

  manager_class->tiled_monitor_added = meta_monitor_manager_test_tiled_monitor_added;
  manager_class->tiled_monitor_removed = meta_monitor_manager_test_tiled_monitor_removed;
  manager_class->calculate_monitor_mode_scale = meta_monitor_manager_test_calculate_monitor_mode_scale;
  manager_class->get_default_layout_mode = meta_monitor_manager_test_get_default_layout_mode;
}
