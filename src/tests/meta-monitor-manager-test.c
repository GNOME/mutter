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

G_DEFINE_TYPE (MetaCrtcTest, meta_crtc_test, META_TYPE_CRTC)
G_DEFINE_TYPE (MetaOutputTest, meta_output_test, META_TYPE_OUTPUT)

struct _MetaMonitorManagerTest
{
  MetaMonitorManager parent;

  gboolean handles_transforms;

  int tiled_monitor_count;

  MetaMonitorTestSetup *test_setup;
};

G_DEFINE_TYPE (MetaMonitorManagerTest, meta_monitor_manager_test,
               META_TYPE_MONITOR_MANAGER)

static CreateTestSetupFunc initial_setup_func;

void
meta_monitor_manager_test_init_test_setup (CreateTestSetupFunc func)
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
  g_assert (handles_transforms || meta_is_stage_views_enabled());

  manager_test->handles_transforms = handles_transforms;
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

  g_assert (manager_test->test_setup);

  meta_gpu_take_modes (gpu, manager_test->test_setup->modes);
  meta_gpu_take_crtcs (gpu, manager_test->test_setup->crtcs);
  meta_gpu_take_outputs (gpu, manager_test->test_setup->outputs);
}

static void
meta_monitor_manager_test_ensure_initial_config (MetaMonitorManager *manager)
{
  MetaMonitorsConfig *config;

  config = meta_monitor_manager_ensure_configured (manager);

  if (meta_is_stage_views_enabled ())
    {
      meta_monitor_manager_update_logical_state (manager, config);
    }
  else
    {
      meta_monitor_manager_update_logical_state_derived (manager, NULL);
    }
}

static void
apply_crtc_assignments (MetaMonitorManager    *manager,
                        MetaCrtcAssignment   **crtcs,
                        unsigned int           n_crtcs,
                        MetaOutputAssignment **outputs,
                        unsigned int           n_outputs)
{
  MetaBackend *backend = meta_monitor_manager_get_backend (manager);
  MetaBackendTest *backend_test = META_BACKEND_TEST (backend);
  MetaGpu *gpu = meta_backend_test_get_gpu (backend_test);
  g_autoptr (GList) to_configure_outputs = NULL;
  g_autoptr (GList) to_configure_crtcs = NULL;
  unsigned int i;

  to_configure_outputs = g_list_copy (meta_gpu_get_outputs (gpu));
  to_configure_crtcs = g_list_copy (meta_gpu_get_crtcs (gpu));

  for (i = 0; i < n_crtcs; i++)
    {
      MetaCrtcAssignment *crtc_assignment = crtcs[i];
      MetaCrtc *crtc = crtc_assignment->crtc;

      to_configure_crtcs = g_list_remove (to_configure_crtcs, crtc);

      if (crtc_assignment->mode == NULL)
        {
          meta_crtc_unset_config (crtc);
        }
      else
        {
          unsigned int j;

          meta_crtc_set_config (crtc,
                                &crtc_assignment->layout,
                                crtc_assignment->mode,
                                crtc_assignment->transform);

          for (j = 0; j < crtc_assignment->outputs->len; j++)
            {
              MetaOutput *output;
              MetaOutputAssignment *output_assignment;

              output = ((MetaOutput**) crtc_assignment->outputs->pdata)[j];

              to_configure_outputs = g_list_remove (to_configure_outputs,
                                                    output);

              output_assignment = meta_find_output_assignment (outputs,
                                                               n_outputs,
                                                               output);
              meta_output_assign_crtc (output, crtc, output_assignment);
            }
        }
    }

  g_list_foreach (to_configure_crtcs,
                  (GFunc) meta_crtc_unset_config,
                  NULL);
  g_list_foreach (to_configure_outputs,
                  (GFunc) meta_output_unassign_crtc,
                  NULL);
}

static void
update_screen_size (MetaMonitorManager *manager,
                    MetaMonitorsConfig *config)
{
  GList *l;
  int screen_width = 0;
  int screen_height = 0;

  for (l = config->logical_monitor_configs; l; l = l->next)
    {
      MetaLogicalMonitorConfig *logical_monitor_config = l->data;
      int right_edge;
      int bottom_edge;

      right_edge = (logical_monitor_config->layout.width +
                    logical_monitor_config->layout.x);
      if (right_edge > screen_width)
        screen_width = right_edge;

      bottom_edge = (logical_monitor_config->layout.height +
                     logical_monitor_config->layout.y);
      if (bottom_edge > screen_height)
        screen_height = bottom_edge;
    }

  manager->screen_width = screen_width;
  manager->screen_height = screen_height;
}

static gboolean
meta_monitor_manager_test_apply_monitors_config (MetaMonitorManager      *manager,
                                                 MetaMonitorsConfig      *config,
                                                 MetaMonitorsConfigMethod method,
                                                 GError                 **error)
{
  GPtrArray *crtc_assignments;
  GPtrArray *output_assignments;

  if (!config)
    {
      manager->screen_width = META_MONITOR_MANAGER_MIN_SCREEN_WIDTH;
      manager->screen_height = META_MONITOR_MANAGER_MIN_SCREEN_HEIGHT;

      if (meta_is_stage_views_enabled ())
        meta_monitor_manager_rebuild (manager, NULL);
      else
        meta_monitor_manager_rebuild_derived (manager, config);

      return TRUE;
    }

  if (!meta_monitor_config_manager_assign (manager, config,
                                           &crtc_assignments,
                                           &output_assignments,
                                           error))
    return FALSE;

  if (method == META_MONITORS_CONFIG_METHOD_VERIFY)
    {
      g_ptr_array_free (crtc_assignments, TRUE);
      g_ptr_array_free (output_assignments, TRUE);
      return TRUE;
    }

  apply_crtc_assignments (manager,
                          (MetaCrtcAssignment **) crtc_assignments->pdata,
                          crtc_assignments->len,
                          (MetaOutputAssignment **) output_assignments->pdata,
                          output_assignments->len);

  g_ptr_array_free (crtc_assignments, TRUE);
  g_ptr_array_free (output_assignments, TRUE);

  update_screen_size (manager, config);

  if (meta_is_stage_views_enabled ())
    meta_monitor_manager_rebuild (manager, config);
  else
    meta_monitor_manager_rebuild_derived (manager, config);

  return TRUE;
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

static gboolean
meta_monitor_manager_test_is_transform_handled (MetaMonitorManager  *manager,
                                                MetaCrtc            *crtc,
                                                MetaMonitorTransform transform)
{
  MetaMonitorManagerTest *manager_test = META_MONITOR_MANAGER_TEST (manager);

  return manager_test->handles_transforms;
}

static float
meta_monitor_manager_test_calculate_monitor_mode_scale (MetaMonitorManager *manager,
                                                        MetaMonitor        *monitor,
                                                        MetaMonitorMode    *monitor_mode)
{
  MetaOutput *output;
  MetaOutputTest *output_test;

  output = meta_monitor_get_main_output (monitor);
  output_test = META_OUTPUT_TEST (output);

  if (output_test)
    return output_test->scale;
  else
    return 1;
}

static float *
meta_monitor_manager_test_calculate_supported_scales (MetaMonitorManager           *manager,
                                                      MetaLogicalMonitorLayoutMode  layout_mode,
                                                      MetaMonitor                  *monitor,
                                                      MetaMonitorMode              *monitor_mode,
                                                      int                          *n_supported_scales)
{
  MetaMonitorScalesConstraint constraints =
    META_MONITOR_SCALES_CONSTRAINT_NONE;

  switch (layout_mode)
    {
    case META_LOGICAL_MONITOR_LAYOUT_MODE_LOGICAL:
      break;
    case META_LOGICAL_MONITOR_LAYOUT_MODE_PHYSICAL:
      constraints |= META_MONITOR_SCALES_CONSTRAINT_NO_FRAC;
      break;
    }

  return meta_monitor_calculate_supported_scales (monitor, monitor_mode,
                                                  constraints,
                                                  n_supported_scales);
}

static gboolean
is_monitor_framebuffer_scaled (void)
{
  MetaBackend *backend = meta_get_backend ();
  MetaSettings *settings = meta_backend_get_settings (backend);

  return meta_settings_is_experimental_feature_enabled (
    settings,
    META_EXPERIMENTAL_FEATURE_SCALE_MONITOR_FRAMEBUFFER);
}

static MetaMonitorManagerCapability
meta_monitor_manager_test_get_capabilities (MetaMonitorManager *manager)
{
  MetaMonitorManagerCapability capabilities =
    META_MONITOR_MANAGER_CAPABILITY_NONE;

  if (is_monitor_framebuffer_scaled ())
    capabilities |= META_MONITOR_MANAGER_CAPABILITY_LAYOUT_MODE;

  return capabilities;
}

static gboolean
meta_monitor_manager_test_get_max_screen_size (MetaMonitorManager *manager,
                                               int                *max_width,
                                               int                *max_height)
{
  if (meta_is_stage_views_enabled ())
    return FALSE;

  *max_width = 65535;
  *max_height = 65535;

  return TRUE;
}

static MetaLogicalMonitorLayoutMode
meta_monitor_manager_test_get_default_layout_mode (MetaMonitorManager *manager)
{
  if (!meta_is_stage_views_enabled ())
    return META_LOGICAL_MONITOR_LAYOUT_MODE_PHYSICAL;

  if (is_monitor_framebuffer_scaled ())
    return META_LOGICAL_MONITOR_LAYOUT_MODE_LOGICAL;
  else
    return META_LOGICAL_MONITOR_LAYOUT_MODE_PHYSICAL;
}

static void
meta_monitor_manager_test_dispose (GObject *object)
{
  MetaMonitorManagerTest *manager_test = META_MONITOR_MANAGER_TEST (object);

  g_clear_pointer (&manager_test->test_setup, g_free);

  G_OBJECT_CLASS (meta_monitor_manager_test_parent_class)->dispose (object);
}

static void
meta_monitor_manager_test_constructed (GObject *object)
{
  MetaMonitorManagerTest *manager_test = META_MONITOR_MANAGER_TEST (object);

  manager_test->test_setup = initial_setup_func ();

  G_OBJECT_CLASS (meta_monitor_manager_test_parent_class)->constructed (object);
}

static void
meta_monitor_manager_test_init (MetaMonitorManagerTest *manager_test)
{
  manager_test->handles_transforms = TRUE;
}

static void
meta_monitor_manager_test_class_init (MetaMonitorManagerTestClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaMonitorManagerClass *manager_class = META_MONITOR_MANAGER_CLASS (klass);

  object_class->dispose = meta_monitor_manager_test_dispose;
  object_class->constructed = meta_monitor_manager_test_constructed;

  manager_class->ensure_initial_config = meta_monitor_manager_test_ensure_initial_config;
  manager_class->apply_monitors_config = meta_monitor_manager_test_apply_monitors_config;
  manager_class->tiled_monitor_added = meta_monitor_manager_test_tiled_monitor_added;
  manager_class->tiled_monitor_removed = meta_monitor_manager_test_tiled_monitor_removed;
  manager_class->is_transform_handled = meta_monitor_manager_test_is_transform_handled;
  manager_class->calculate_monitor_mode_scale = meta_monitor_manager_test_calculate_monitor_mode_scale;
  manager_class->calculate_supported_scales = meta_monitor_manager_test_calculate_supported_scales;
  manager_class->get_capabilities = meta_monitor_manager_test_get_capabilities;
  manager_class->get_max_screen_size = meta_monitor_manager_test_get_max_screen_size;
  manager_class->get_default_layout_mode = meta_monitor_manager_test_get_default_layout_mode;
}

static void
meta_output_test_init (MetaOutputTest *output_test)
{
  output_test->scale = 1;
}

static void
meta_output_test_class_init (MetaOutputTestClass *klass)
{
}

static void
meta_crtc_test_init (MetaCrtcTest *crtc_test)
{
}

static void
meta_crtc_test_class_init (MetaCrtcTestClass *klass)
{
}
