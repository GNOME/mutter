/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2017 Red Hat, Inc.
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

#include "tests/monitor-test-utils.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-crtc.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor-config-manager.h"
#include "backends/meta-monitor-config-store.h"
#include "backends/meta-output.h"
#include "tests/test-utils.h"
#include "meta-backend-test.h"

MetaGpu *
test_get_gpu (void)
{
  return META_GPU (meta_backend_get_gpus (meta_get_backend ())->data);
}

void
set_custom_monitor_config (const char *filename)
{
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaMonitorConfigStore *config_store;
  GError *error = NULL;
  const char *path;

  g_assert_nonnull (config_manager);

  config_store = meta_monitor_config_manager_get_store (config_manager);

  path = g_test_get_filename (G_TEST_DIST, "tests", "monitor-configs",
                              filename, NULL);
  if (!meta_monitor_config_store_set_custom (config_store, path, NULL,
                                             &error))
    g_error ("Failed to set custom config: %s", error->message);
}

char *
read_file (const char *file_path)
{
  g_autoptr (GFile) file = NULL;
  g_autoptr (GFileInputStream) input_stream = NULL;
  g_autoptr (GFileInfo) file_info = NULL;
  goffset file_size;
  gsize bytes_read;
  g_autofree char *buffer = NULL;
  GError *error = NULL;

  file = g_file_new_for_path (file_path);
  input_stream = g_file_read (file, NULL, &error);
  if (!input_stream)
    g_error ("Failed to read migrated config file: %s", error->message);

  file_info = g_file_input_stream_query_info (input_stream,
                                              G_FILE_ATTRIBUTE_STANDARD_SIZE,
                                              NULL, &error);
  if (!file_info)
    g_error ("Failed to read file info: %s", error->message);

  file_size = g_file_info_get_size (file_info);
  buffer = g_malloc0 (file_size + 1);

  if (!g_input_stream_read_all (G_INPUT_STREAM (input_stream),
                                buffer, file_size, &bytes_read, NULL, &error))
    g_error ("Failed to read file content: %s", error->message);
  g_assert_cmpint ((goffset) bytes_read, ==, file_size);

  return g_steal_pointer (&buffer);
}

static MetaOutput *
output_from_winsys_id (MetaBackend *backend,
                       uint64_t     output_id)
{
  MetaGpu *gpu = meta_backend_test_get_gpu (META_BACKEND_TEST (backend));
  GList *l;

  for (l = meta_gpu_get_outputs (gpu); l; l = l->next)
    {
      MetaOutput *output = l->data;

      if (meta_output_get_id (output) == output_id)
        return output;
    }

  return NULL;
}

typedef struct _CheckMonitorModeData
{
  MetaBackend *backend;
  MetaTestCaseMonitorCrtcMode *expect_crtc_mode_iter;
} CheckMonitorModeData;

static gboolean
check_monitor_mode (MetaMonitor         *monitor,
                    MetaMonitorMode     *mode,
                    MetaMonitorCrtcMode *monitor_crtc_mode,
                    gpointer             user_data,
                    GError             **error)
{
  CheckMonitorModeData *data = user_data;
  MetaBackend *backend = data->backend;
  MetaOutput *output;
  MetaCrtcMode *crtc_mode;
  int expect_crtc_mode_index;

  output = output_from_winsys_id (backend,
                                  data->expect_crtc_mode_iter->output);
  g_assert (monitor_crtc_mode->output == output);

  expect_crtc_mode_index = data->expect_crtc_mode_iter->crtc_mode;
  if (expect_crtc_mode_index == -1)
    {
      crtc_mode = NULL;
    }
  else
    {
      MetaGpu *gpu = meta_output_get_gpu (output);

      crtc_mode = g_list_nth_data (meta_gpu_get_modes (gpu),
                                   expect_crtc_mode_index);
    }
  g_assert (monitor_crtc_mode->crtc_mode == crtc_mode);

  if (crtc_mode)
    {
      const MetaCrtcModeInfo *crtc_mode_info =
        meta_crtc_mode_get_info (crtc_mode);
      float refresh_rate;
      MetaCrtcModeFlag flags;

      refresh_rate = meta_monitor_mode_get_refresh_rate (mode);
      flags = meta_monitor_mode_get_flags (mode);

      g_assert_cmpfloat (refresh_rate, ==, crtc_mode_info->refresh_rate);
      g_assert_cmpint (flags, ==, (crtc_mode_info->flags &
                                   HANDLED_CRTC_MODE_FLAGS));
    }

  data->expect_crtc_mode_iter++;

  return TRUE;
}

static gboolean
check_current_monitor_mode (MetaMonitor         *monitor,
                            MetaMonitorMode     *mode,
                            MetaMonitorCrtcMode *monitor_crtc_mode,
                            gpointer             user_data,
                            GError             **error)
{
  CheckMonitorModeData *data = user_data;
  MetaBackend *backend = data->backend;
  MetaOutput *output;
  MetaCrtc *crtc;

  output = output_from_winsys_id (backend,
                                  data->expect_crtc_mode_iter->output);
  crtc = meta_output_get_assigned_crtc (output);

  if (data->expect_crtc_mode_iter->crtc_mode == -1)
    {
      g_assert_null (crtc);
    }
  else
    {
      const MetaCrtcConfig *crtc_config;
      MetaLogicalMonitor *logical_monitor;

      g_assert_nonnull (crtc);

      crtc_config = meta_crtc_get_config (crtc);
      g_assert_nonnull (crtc_config);

      g_assert (monitor_crtc_mode->crtc_mode == crtc_config->mode);

      logical_monitor = meta_monitor_get_logical_monitor (monitor);
      g_assert_nonnull (logical_monitor);
    }


  data->expect_crtc_mode_iter++;

  return TRUE;
}

static MetaLogicalMonitor *
logical_monitor_from_layout (MetaMonitorManager *monitor_manager,
                             MetaRectangle      *layout)
{
  GList *l;

  for (l = monitor_manager->logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;

      if (meta_rectangle_equal (layout, &logical_monitor->rect))
        return logical_monitor;
    }

  return NULL;
}

static void
check_logical_monitor (MetaMonitorManager            *monitor_manager,
                       MonitorTestCaseLogicalMonitor *test_logical_monitor)
{
  MetaLogicalMonitor *logical_monitor;
  MetaOutput *primary_output;
  GList *monitors;
  GList *l;
  int i;

  logical_monitor = logical_monitor_from_layout (monitor_manager,
                                                 &test_logical_monitor->layout);
  g_assert_nonnull (logical_monitor);

  g_assert_cmpint (logical_monitor->rect.x,
                   ==,
                   test_logical_monitor->layout.x);
  g_assert_cmpint (logical_monitor->rect.y,
                   ==,
                   test_logical_monitor->layout.y);
  g_assert_cmpint (logical_monitor->rect.width,
                   ==,
                   test_logical_monitor->layout.width);
  g_assert_cmpint (logical_monitor->rect.height,
                   ==,
                   test_logical_monitor->layout.height);
  g_assert_cmpfloat (logical_monitor->scale,
                     ==,
                     test_logical_monitor->scale);
  g_assert_cmpuint (logical_monitor->transform,
                    ==,
                    test_logical_monitor->transform);

  if (logical_monitor == monitor_manager->primary_logical_monitor)
    g_assert (meta_logical_monitor_is_primary (logical_monitor));

  primary_output = NULL;
  monitors = meta_logical_monitor_get_monitors (logical_monitor);
  g_assert_cmpint ((int) g_list_length (monitors),
                   ==,
                   test_logical_monitor->n_monitors);

  for (i = 0; i < test_logical_monitor->n_monitors; i++)
    {
      MetaMonitor *monitor =
        g_list_nth (monitor_manager->monitors,
                    test_logical_monitor->monitors[i])->data;

      g_assert_nonnull (g_list_find (monitors, monitor));
    }

  for (l = monitors; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;
      GList *outputs;
      GList *l_output;

      outputs = meta_monitor_get_outputs (monitor);
      for (l_output = outputs; l_output; l_output = l_output->next)
        {
          MetaOutput *output = l_output->data;
          MetaCrtc *crtc;

          if (meta_output_is_primary (output))
            {
              g_assert_null (primary_output);
              primary_output = output;
            }

          crtc = meta_output_get_assigned_crtc (output);
          g_assert (!crtc ||
                    meta_monitor_get_logical_monitor (monitor) == logical_monitor);
          g_assert_cmpint (logical_monitor->is_presentation,
                           ==,
                           meta_output_is_presentation (output));
        }
    }

  if (logical_monitor == monitor_manager->primary_logical_monitor)
    g_assert_nonnull (primary_output);
}

void
check_monitor_configuration (MonitorTestCaseExpect *expect)
{
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  MetaGpu *gpu = meta_backend_test_get_gpu (META_BACKEND_TEST (backend));
  int tiled_monitor_count;
  GList *monitors;
  GList *crtcs;
  int n_logical_monitors;
  GList *l;
  int i;

  g_assert_cmpint (monitor_manager->screen_width,
                   ==,
                   expect->screen_width);
  g_assert_cmpint (monitor_manager->screen_height,
                   ==,
                   expect->screen_height);
  g_assert_cmpint ((int) g_list_length (meta_gpu_get_outputs (gpu)),
                   ==,
                   expect->n_outputs);
  g_assert_cmpint ((int) g_list_length (meta_gpu_get_crtcs (gpu)),
                   ==,
                   expect->n_crtcs);

  tiled_monitor_count =
    meta_monitor_manager_test_get_tiled_monitor_count (monitor_manager_test);
  g_assert_cmpint (tiled_monitor_count,
                   ==,
                   expect->n_tiled_monitors);

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpint ((int) g_list_length (monitors),
                   ==,
                   expect->n_monitors);
  for (l = monitors, i = 0; l; l = l->next, i++)
    {
      MetaMonitor *monitor = l->data;
      GList *outputs;
      GList *l_output;
      int j;
      int width_mm, height_mm;
      GList *modes;
      GList *l_mode;
      MetaMonitorMode *current_mode;
      int expected_current_mode_index;
      MetaMonitorMode *expected_current_mode;

      outputs = meta_monitor_get_outputs (monitor);

      g_assert_cmpint ((int) g_list_length (outputs),
                       ==,
                       expect->monitors[i].n_outputs);

      for (l_output = outputs, j = 0; l_output; l_output = l_output->next, j++)
        {
          MetaOutput *output = l_output->data;
          uint64_t winsys_id = expect->monitors[i].outputs[j];

          g_assert (output == output_from_winsys_id (backend, winsys_id));
          g_assert_cmpint (expect->monitors[i].is_underscanning,
                           ==,
                           meta_output_is_underscanning (output));
        }

      meta_monitor_get_physical_dimensions (monitor, &width_mm, &height_mm);
      g_assert_cmpint (width_mm,
                       ==,
                       expect->monitors[i].width_mm);
      g_assert_cmpint (height_mm,
                       ==,
                       expect->monitors[i].height_mm);

      modes = meta_monitor_get_modes (monitor);
      g_assert_cmpint (g_list_length (modes),
                       ==,
                       expect->monitors[i].n_modes);

      for (l_mode = modes, j = 0; l_mode; l_mode = l_mode->next, j++)
        {
          MetaMonitorMode *mode = l_mode->data;
          int width;
          int height;
          float refresh_rate;
          MetaCrtcModeFlag flags;
          CheckMonitorModeData data;

          meta_monitor_mode_get_resolution (mode, &width, &height);
          refresh_rate = meta_monitor_mode_get_refresh_rate (mode);
          flags = meta_monitor_mode_get_flags (mode);

          g_assert_cmpint (width,
                           ==,
                           expect->monitors[i].modes[j].width);
          g_assert_cmpint (height,
                           ==,
                           expect->monitors[i].modes[j].height);
          g_assert_cmpfloat (refresh_rate,
                             ==,
                             expect->monitors[i].modes[j].refresh_rate);
          g_assert_cmpint (flags,
                           ==,
                           expect->monitors[i].modes[j].flags);

          data = (CheckMonitorModeData) {
            .backend = backend,
            .expect_crtc_mode_iter =
              expect->monitors[i].modes[j].crtc_modes
          };
          meta_monitor_mode_foreach_output (monitor, mode,
                                            check_monitor_mode,
                                            &data,
                                            NULL);
        }

      current_mode = meta_monitor_get_current_mode (monitor);
      expected_current_mode_index = expect->monitors[i].current_mode;
      if (expected_current_mode_index == -1)
        expected_current_mode = NULL;
      else
        expected_current_mode = g_list_nth (modes,
                                            expected_current_mode_index)->data;

      g_assert (current_mode == expected_current_mode);
      if (current_mode)
        g_assert (meta_monitor_is_active (monitor));
      else
        g_assert (!meta_monitor_is_active (monitor));

      if (current_mode)
        {
          CheckMonitorModeData data;

          data = (CheckMonitorModeData) {
            .backend = backend,
            .expect_crtc_mode_iter =
              expect->monitors[i].modes[expected_current_mode_index].crtc_modes
          };
          meta_monitor_mode_foreach_output (monitor, expected_current_mode,
                                            check_current_monitor_mode,
                                            &data,
                                            NULL);
        }

      meta_monitor_derive_current_mode (monitor);
      g_assert (current_mode == meta_monitor_get_current_mode (monitor));
    }

  n_logical_monitors =
    meta_monitor_manager_get_num_logical_monitors (monitor_manager);
  g_assert_cmpint (n_logical_monitors,
                   ==,
                   expect->n_logical_monitors);

  /*
   * Check that we have a primary logical monitor (except for headless),
   * and that the main output of the first monitor is the only output
   * that is marked as primary (further below). Note: outputs being primary or
   * not only matters on X11.
   */
  if (expect->primary_logical_monitor == -1)
    {
      g_assert_null (monitor_manager->primary_logical_monitor);
      g_assert_null (monitor_manager->logical_monitors);
    }
  else
    {
      MonitorTestCaseLogicalMonitor *test_logical_monitor =
        &expect->logical_monitors[expect->primary_logical_monitor];
      MetaLogicalMonitor *logical_monitor;

      logical_monitor =
        logical_monitor_from_layout (monitor_manager,
                                     &test_logical_monitor->layout);
      g_assert (logical_monitor == monitor_manager->primary_logical_monitor);
    }

  for (i = 0; i < expect->n_logical_monitors; i++)
    {
      MonitorTestCaseLogicalMonitor *test_logical_monitor =
        &expect->logical_monitors[i];

      check_logical_monitor (monitor_manager, test_logical_monitor);
    }
  g_assert_cmpint (n_logical_monitors, ==, i);

  crtcs = meta_gpu_get_crtcs (gpu);
  for (l = crtcs, i = 0; l; l = l->next, i++)
    {
      MetaCrtc *crtc = l->data;
      const MetaCrtcConfig *crtc_config = meta_crtc_get_config (crtc);

      if (expect->crtcs[i].current_mode == -1)
        {
          g_assert_null (crtc_config);
        }
      else
        {
          MetaCrtcMode *expected_current_mode;

          g_assert_nonnull (crtc_config);

          expected_current_mode =
            g_list_nth_data (meta_gpu_get_modes (gpu),
                             expect->crtcs[i].current_mode);
          g_assert (crtc_config->mode == expected_current_mode);

          g_assert_cmpuint (crtc_config->transform,
                            ==,
                            expect->crtcs[i].transform);

          g_assert_cmpfloat_with_epsilon (crtc_config->layout.origin.x,
                                          expect->crtcs[i].x,
                                          FLT_EPSILON);
          g_assert_cmpfloat_with_epsilon (crtc_config->layout.origin.y,
                                          expect->crtcs[i].y,
                                          FLT_EPSILON);
        }
    }
}

MetaMonitorTestSetup *
create_monitor_test_setup (MonitorTestCaseSetup *setup,
                           MonitorTestFlag       flags)
{
  MetaMonitorTestSetup *test_setup;
  int i;
  int n_laptop_panels = 0;
  int n_normal_panels = 0;

  test_setup = g_new0 (MetaMonitorTestSetup, 1);

  test_setup->modes = NULL;
  for (i = 0; i < setup->n_modes; i++)
    {
      g_autoptr (MetaCrtcModeInfo) crtc_mode_info = NULL;
      MetaCrtcMode *mode;

      crtc_mode_info = meta_crtc_mode_info_new ();
      crtc_mode_info->width = setup->modes[i].width;
      crtc_mode_info->height = setup->modes[i].height;
      crtc_mode_info->refresh_rate = setup->modes[i].refresh_rate;
      crtc_mode_info->flags = setup->modes[i].flags;

      mode = g_object_new (META_TYPE_CRTC_MODE,
                           "id", (uint64_t) i,
                           "info", crtc_mode_info,
                           NULL);

      test_setup->modes = g_list_append (test_setup->modes, mode);
    }

  test_setup->crtcs = NULL;
  for (i = 0; i < setup->n_crtcs; i++)
    {
      MetaCrtc *crtc;

      crtc = g_object_new (META_TYPE_CRTC_TEST,
                           "id", (uint64_t) i + 1,
                           "gpu", test_get_gpu (),
                           NULL);

      test_setup->crtcs = g_list_append (test_setup->crtcs, crtc);
    }

  test_setup->outputs = NULL;
  for (i = 0; i < setup->n_outputs; i++)
    {
      MetaOutput *output;
      MetaOutputTest *output_test;
      int crtc_index;
      MetaCrtc *crtc;
      int preferred_mode_index;
      MetaCrtcMode *preferred_mode;
      MetaCrtcMode **modes;
      int n_modes;
      int j;
      MetaCrtc **possible_crtcs;
      int n_possible_crtcs;
      int scale;
      gboolean is_laptop_panel;
      const char *serial;
      g_autoptr (MetaOutputInfo) output_info = NULL;

      crtc_index = setup->outputs[i].crtc;
      if (crtc_index == -1)
        crtc = NULL;
      else
        crtc = g_list_nth_data (test_setup->crtcs, crtc_index);

      preferred_mode_index = setup->outputs[i].preferred_mode;
      if (preferred_mode_index == -1)
        preferred_mode = NULL;
      else
        preferred_mode = g_list_nth_data (test_setup->modes,
                                          preferred_mode_index);

      n_modes = setup->outputs[i].n_modes;
      modes = g_new0 (MetaCrtcMode *, n_modes);
      for (j = 0; j < n_modes; j++)
        {
          int mode_index;

          mode_index = setup->outputs[i].modes[j];
          modes[j] = g_list_nth_data (test_setup->modes, mode_index);
        }

      n_possible_crtcs = setup->outputs[i].n_possible_crtcs;
      possible_crtcs = g_new0 (MetaCrtc *, n_possible_crtcs);
      for (j = 0; j < n_possible_crtcs; j++)
        {
          int possible_crtc_index;

          possible_crtc_index = setup->outputs[i].possible_crtcs[j];
          possible_crtcs[j] = g_list_nth_data (test_setup->crtcs,
                                               possible_crtc_index);
        }

      scale = setup->outputs[i].scale;
      if (scale < 1)
        scale = 1;

      is_laptop_panel = setup->outputs[i].is_laptop_panel;

      serial = setup->outputs[i].serial;
      if (!serial)
        serial = "0x123456";

      output_info = meta_output_info_new ();

      output_info->name = (is_laptop_panel
                           ? g_strdup_printf ("eDP-%d", ++n_laptop_panels)
                           : g_strdup_printf ("DP-%d", ++n_normal_panels));
      output_info->vendor = g_strdup ("MetaProduct's Inc.");
      output_info->product = g_strdup ("MetaMonitor");
      output_info->serial = g_strdup (serial);
      if (setup->outputs[i].hotplug_mode)
        {
          output_info->hotplug_mode_update = TRUE;
          output_info->suggested_x = setup->outputs[i].suggested_x;
          output_info->suggested_y = setup->outputs[i].suggested_y;
        }
      else if (flags & MONITOR_TEST_FLAG_NO_STORED)
        {
          output_info->hotplug_mode_update = TRUE;
          output_info->suggested_x = -1;
          output_info->suggested_y = -1;
        }
      output_info->width_mm = setup->outputs[i].width_mm;
      output_info->height_mm = setup->outputs[i].height_mm;
      output_info->subpixel_order = COGL_SUBPIXEL_ORDER_UNKNOWN;
      output_info->preferred_mode = preferred_mode;
      output_info->n_modes = n_modes;
      output_info->modes = modes;
      output_info->n_possible_crtcs = n_possible_crtcs;
      output_info->possible_crtcs = possible_crtcs;
      output_info->n_possible_clones = 0;
      output_info->possible_clones = NULL;
      output_info->connector_type = (is_laptop_panel ? META_CONNECTOR_TYPE_eDP
                                     : META_CONNECTOR_TYPE_DisplayPort);
      output_info->tile_info = setup->outputs[i].tile_info;
      output_info->panel_orientation_transform =
        setup->outputs[i].panel_orientation_transform;

      output = g_object_new (META_TYPE_OUTPUT_TEST,
                             "id", (uint64_t) i,
                             "gpu", test_get_gpu (),
                             "info", output_info,
                             NULL);

      output_test = META_OUTPUT_TEST (output);
      output_test->scale = scale;

      if (crtc)
        {
          MetaOutputAssignment output_assignment;

          output_assignment = (MetaOutputAssignment) {
            .is_underscanning = setup->outputs[i].is_underscanning,
          };
          meta_output_assign_crtc (output, crtc, &output_assignment);
        }

      test_setup->outputs = g_list_append (test_setup->outputs, output);
    }

  return test_setup;
}
