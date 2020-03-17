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

#include "tests/meta-monitor-test-utils.h"

#include <float.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-crtc.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor-config-manager.h"
#include "backends/meta-monitor-config-store.h"
#include "backends/meta-output.h"
#include "tests/meta-test-utils.h"
#include "meta-backend-test.h"

MetaGpu *
meta_test_get_gpu (MetaBackend *backend)
{
  return META_GPU (meta_backend_get_gpus (backend)->data);
}

void
meta_set_custom_monitor_config (MetaContext *context,
                                const char  *filename)
{
  meta_set_custom_monitor_config_full (meta_context_get_backend (context),
                                       filename,
                                       META_MONITORS_CONFIG_FLAG_NONE);
}

void
meta_set_custom_monitor_system_config (MetaContext *context,
                                       const char  *filename)
{
  meta_set_custom_monitor_config_full (meta_context_get_backend (context),
                                       filename,
                                       META_MONITORS_CONFIG_FLAG_SYSTEM_CONFIG);
}

char *
meta_read_file (const char *file_path)
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
      MetaCrtcRefreshRateMode refresh_rate_mode;
      MetaCrtcModeFlag flags;

      refresh_rate = meta_monitor_mode_get_refresh_rate (mode);
      refresh_rate_mode = meta_monitor_mode_get_refresh_rate_mode (mode);
      flags = meta_monitor_mode_get_flags (mode);

      g_assert_cmpfloat (refresh_rate, ==, crtc_mode_info->refresh_rate);
      g_assert_cmpint (refresh_rate_mode, ==, crtc_mode_info->refresh_rate_mode);
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
                             MtkRectangle       *layout)
{
  GList *l;

  for (l = monitor_manager->logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;

      if (mtk_rectangle_equal (layout, &logical_monitor->rect))
        return logical_monitor;
    }

  return NULL;
}

static void
check_logical_monitor (MetaMonitorManager             *monitor_manager,
                       MonitorTestCaseLogicalMonitor  *test_logical_monitor,
                       GList                         **all_crtcs)
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

          g_assert (meta_output_get_monitor (output) == monitor);

          if (meta_output_is_primary (output))
            {
              g_assert_null (primary_output);
              primary_output = output;
            }

          crtc = meta_output_get_assigned_crtc (output);
          if (crtc)
            {
              g_assert (meta_monitor_get_logical_monitor (monitor) ==
                        logical_monitor);
              g_assert (g_list_find ((GList *) meta_crtc_get_outputs (crtc),
                                     output));
              *all_crtcs = g_list_remove (*all_crtcs, crtc);
            }
          else
            {
              g_assert_null (crtc);
            }

          g_assert_cmpint (logical_monitor->is_presentation,
                           ==,
                           meta_output_is_presentation (output));
        }
    }

  if (logical_monitor == monitor_manager->primary_logical_monitor)
    g_assert_nonnull (primary_output);
}

void
meta_check_monitor_configuration (MetaContext           *context,
                                  MonitorTestCaseExpect *expect)
{
  MetaBackend *backend = meta_context_get_backend (context);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  MetaGpu *gpu = meta_backend_test_get_gpu (META_BACKEND_TEST (backend));
  int tiled_monitor_count;
  GList *monitors;
  GList *crtcs;
  int n_logical_monitors;
  GList *all_crtcs;
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
      MetaOutput *main_output;
      const MetaOutputInfo *main_output_info;
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
      g_debug ("Checking monitor %d", i);

      g_assert_cmpint ((int) g_list_length (outputs),
                       ==,
                       expect->monitors[i].n_outputs);

      for (l_output = outputs, j = 0; l_output; l_output = l_output->next, j++)
        {
          MetaOutput *output = l_output->data;
          uint64_t winsys_id = expect->monitors[i].outputs[j];
          unsigned int output_max_bpc;
          MetaOutputRGBRange rgb_range = META_OUTPUT_RGB_RANGE_AUTO;

          g_assert (output == output_from_winsys_id (backend, winsys_id));
          g_assert_cmpint (expect->monitors[i].is_underscanning,
                           ==,
                           meta_output_is_underscanning (output));

          if (!meta_output_get_max_bpc (output, &output_max_bpc))
            output_max_bpc = 0;

          g_assert_cmpint (expect->monitors[i].max_bpc, ==, output_max_bpc);

          if (expect->monitors[i].rgb_range)
            rgb_range = expect->monitors[i].rgb_range;
          g_assert_cmpint (rgb_range, ==, meta_output_peek_rgb_range (output));
        }

      meta_monitor_get_physical_dimensions (monitor, &width_mm, &height_mm);
      g_assert_cmpint (width_mm,
                       ==,
                       expect->monitors[i].width_mm);
      g_assert_cmpint (height_mm,
                       ==,
                       expect->monitors[i].height_mm);

      main_output = meta_monitor_get_main_output (monitor);
      main_output_info = meta_output_get_info (main_output);
      g_assert_cmpstr (meta_monitor_get_connector (monitor), ==,
                       main_output_info->name);
      g_assert_cmpstr (meta_monitor_get_vendor (monitor), ==,
                       main_output_info->vendor);
      g_assert_cmpstr (meta_monitor_get_product (monitor), ==,
                       main_output_info->product);
      g_assert_cmpstr (meta_monitor_get_serial (monitor), ==,
                       main_output_info->serial);
      g_assert_cmpint (meta_monitor_get_connector_type (monitor), ==,
                       main_output_info->connector_type);

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
          MetaCrtcRefreshRateMode refresh_rate_mode;
          MetaCrtcModeFlag flags;
          CheckMonitorModeData data;

          meta_monitor_mode_get_resolution (mode, &width, &height);
          refresh_rate = meta_monitor_mode_get_refresh_rate (mode);
          refresh_rate_mode = meta_monitor_mode_get_refresh_rate_mode (mode);
          flags = meta_monitor_mode_get_flags (mode);

          g_debug ("Checking mode %dx%d @ %f", width, height, refresh_rate);

          g_assert_cmpint (width,
                           ==,
                           expect->monitors[i].modes[j].width);
          g_assert_cmpint (height,
                           ==,
                           expect->monitors[i].modes[j].height);
          g_assert_cmpfloat (refresh_rate,
                             ==,
                             expect->monitors[i].modes[j].refresh_rate);
          g_assert_cmpint (refresh_rate_mode,
                           ==,
                           expect->monitors[i].modes[j].refresh_rate_mode);
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

  all_crtcs = NULL;
  for (l = meta_backend_get_gpus (backend); l; l = l->next)
    {
      MetaGpu *gpu = l->data;

      all_crtcs = g_list_concat (all_crtcs,
                                 g_list_copy (meta_gpu_get_crtcs (gpu)));
    }

  for (i = 0; i < expect->n_logical_monitors; i++)
    {
      MonitorTestCaseLogicalMonitor *test_logical_monitor =
        &expect->logical_monitors[i];

      check_logical_monitor (monitor_manager, test_logical_monitor, &all_crtcs);
    }
  g_assert_cmpint (n_logical_monitors, ==, i);

  for (l = all_crtcs; l; l = l->next)
    {
      MetaCrtc *crtc = l->data;

      g_assert_null (meta_crtc_get_outputs (crtc));
    }
  g_list_free (all_crtcs);

  crtcs = meta_gpu_get_crtcs (gpu);
  for (l = crtcs, i = 0; l; l = l->next, i++)
    {
      MetaCrtc *crtc = l->data;
      const MetaCrtcConfig *crtc_config = meta_crtc_get_config (crtc);

      g_debug ("Checking CRTC %d", i);

      if (expect->crtcs[i].current_mode == -1)
        {
          g_assert_null (meta_crtc_get_outputs (crtc));
          g_assert_null (crtc_config);
        }
      else
        {
          MetaCrtcMode *expected_current_mode;
          const GList *outputs = meta_crtc_get_outputs (crtc);
          const GList *l_output;
          MetaRendererView *view;
          MtkRectangle view_layout;

          for (l_output = outputs;
               l_output;
               l_output = l_output->next)
            {
              MetaOutput *output = l_output->data;

              g_debug ("Checking CRTC Output %d",
                       g_list_index ((GList *) outputs, output));

              g_assert (meta_output_get_assigned_crtc (output) == crtc);
              g_assert_null (g_list_find (l_output->next, output));
            }

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

          view = meta_renderer_get_view_for_crtc (renderer, crtc);
          g_assert_nonnull (view);
          clutter_stage_view_get_layout (CLUTTER_STAGE_VIEW (view),
                                         &view_layout);
          g_assert_cmpfloat_with_epsilon (crtc_config->layout.origin.x,
                                          view_layout.x,
                                          FLT_EPSILON);
          g_assert_cmpfloat_with_epsilon (crtc_config->layout.origin.y,
                                          view_layout.y,
                                          FLT_EPSILON);
          g_assert_cmpfloat_with_epsilon (crtc_config->layout.size.width,
                                          view_layout.width,
                                          FLT_EPSILON);
          g_assert_cmpfloat_with_epsilon (crtc_config->layout.size.height,
                                          view_layout.height,
                                          FLT_EPSILON);
        }
    }
}

MetaMonitorTestSetup *
meta_create_monitor_test_setup (MetaBackend          *backend,
                                MonitorTestCaseSetup *setup,
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
      crtc_mode_info->refresh_rate_mode = setup->modes[i].refresh_rate_mode;
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
                           "backend", backend,
                           "gpu", meta_test_get_gpu (backend),
                           NULL);
      if (setup->crtcs[i].disable_gamma_lut)
        meta_crtc_test_disable_gamma_lut (META_CRTC_TEST (crtc));

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
      char *serial;
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
      if (scale < 1 && scale != -1)
        scale = 1;

      is_laptop_panel = setup->outputs[i].is_laptop_panel;

      serial = g_strdup (setup->outputs[i].serial);
      if (!serial)
        serial = g_strdup_printf ("0x123456%d", i);

      output_info = meta_output_info_new ();

      output_info->name = (is_laptop_panel
                           ? g_strdup_printf ("eDP-%d", ++n_laptop_panels)
                           : g_strdup_printf ("DP-%d", ++n_normal_panels));
      output_info->vendor = g_strdup ("MetaProduct's Inc.");
      output_info->product = g_strdup ("MetaMonitor");
      output_info->serial = serial;
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
      if (setup->outputs[i].has_edid_info)
        {
          output_info->edid_info = g_memdup2 (&setup->outputs[i].edid_info,
                                              sizeof (setup->outputs[i].edid_info));
          output_info->edid_checksum_md5 =
            g_compute_checksum_for_data (G_CHECKSUM_MD5,
                                         (uint8_t *) &setup->outputs[i].edid_info,
                                         sizeof (setup->outputs[i].edid_info));
        }

      output = g_object_new (META_TYPE_OUTPUT_TEST,
                             "id", (uint64_t) i,
                             "gpu", meta_test_get_gpu (backend),
                             "info", output_info,
                             NULL);

      output_test = META_OUTPUT_TEST (output);
      output_test->scale = scale;

      if (crtc)
        {
          MetaOutputAssignment output_assignment;

          output_assignment = (MetaOutputAssignment) {
            .is_underscanning = setup->outputs[i].is_underscanning,
            .has_max_bpc = !!setup->outputs[i].max_bpc,
            .max_bpc = setup->outputs[i].max_bpc,
            .rgb_range = setup->outputs[i].rgb_range,
          };
          meta_output_assign_crtc (output, crtc, &output_assignment);
        }

      test_setup->outputs = g_list_append (test_setup->outputs, output);
    }

  return test_setup;
}

static void
check_expected_scales (MetaMonitor                 *monitor,
                       MetaMonitorMode             *monitor_mode,
                       MetaMonitorScalesConstraint  constraints,
                       int                          n_expected_scales,
                       float                       *expected_scales)
{
  g_autofree float *scales = NULL;
  int n_supported_scales;
  int width, height;
  int i;

  scales = meta_monitor_calculate_supported_scales (monitor, monitor_mode,
                                                    constraints,
                                                    &n_supported_scales);
  g_assert_cmpint (n_expected_scales, ==, n_supported_scales);

  meta_monitor_mode_get_resolution (monitor_mode, &width, &height);

  for (i = 0; i < n_supported_scales; i++)
    {
      g_assert_cmpfloat (scales[i], >, 0.0);
      g_assert_cmpfloat_with_epsilon (scales[i], expected_scales[i], 0.000001);

      if (!(constraints & META_MONITOR_SCALES_CONSTRAINT_NO_FRAC))
        {
          /* Also ensure that the scale will generate an integral resolution */
          g_assert_cmpfloat (fmodf (width / scales[i], 1.0), ==, 0.0);
          g_assert_cmpfloat (fmodf (height / scales[i], 1.0), ==, 0.0);
        }

      if (i > 0)
        {
          /* And that scales are sorted and unique */
          g_assert_cmpfloat (scales[i], >, scales[i-1]);
          g_assert_false (G_APPROX_VALUE (scales[i], scales[i-1], 0.000001));
        }
    }
}

void
meta_check_monitor_scales (MetaContext                 *context,
                           MonitorTestCaseExpect       *expect,
                           MetaMonitorScalesConstraint  scales_constraints)
{
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (meta_context_get_backend (context));

  GList *monitors;
  GList *l;
  int i;

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, expect->n_monitors);

  for (l = monitors, i = 0; l; l = l->next, i++)
    {
      MetaMonitor *monitor = l->data;
      MonitorTestCaseMonitor *expected_monitor = &expect->monitors[i];
      GList *modes = meta_monitor_get_modes (monitor);
      GList *k;
      int j;

      g_debug ("Checking monitor %d", i);
      g_assert_cmpuint (g_list_length (modes), ==, expected_monitor->n_modes);

      for (j = 0, k = modes; k; k = k->next, j++)
        {
          MetaMonitorMode *monitor_mode = k->data;
          MetaMonitorTestCaseMonitorMode *expected_mode =
            &expected_monitor->modes[j];
          int width, height;

          meta_monitor_mode_get_resolution (monitor_mode, &width, &height);
          g_debug ("Checking %s scaling values for mode %dx%d",
            (scales_constraints & META_MONITOR_SCALES_CONSTRAINT_NO_FRAC) ?
            "integer" : "fractional", width, height);

          g_assert_cmpint (width, ==, expected_mode->width);
          g_assert_cmpint (height, ==, expected_mode->height);

          check_expected_scales (monitor, monitor_mode, scales_constraints,
                                 expected_mode->n_scales,
                                 expected_mode->scales);
        }
    }
}

const char *
meta_orientation_to_string (MetaOrientation orientation)
{
  switch (orientation)
    {
    case META_ORIENTATION_UNDEFINED:
      return "(undefined)";
    case META_ORIENTATION_NORMAL:
      return "normal";
    case META_ORIENTATION_BOTTOM_UP:
      return "bottom-up";
    case META_ORIENTATION_LEFT_UP:
      return "left-up";
    case META_ORIENTATION_RIGHT_UP:
      return "right-up";
    default:
      return "(invalid)";
    }
}

typedef struct
{
  MetaOrientation expected;
  MetaOrientation orientation;
  gulong connection_id;
  guint timeout_id;
  unsigned int times_signalled;
} WaitForOrientation;

static void
on_orientation_changed (WaitForOrientation     *wfo,
                        MetaOrientationManager *orientation_manager)
{
  wfo->orientation = meta_orientation_manager_get_orientation (orientation_manager);
  wfo->times_signalled++;

  g_test_message ("wait_for_orientation_changes: Orientation changed to %d: %s",
                  wfo->orientation, meta_orientation_to_string (wfo->orientation));
}

static gboolean
on_max_wait_timeout (gpointer data)
{
  WaitForOrientation *wfo = data;

  wfo->timeout_id = 0;
  return G_SOURCE_REMOVE;
}

/*
 * Assert that the orientation eventually changes to @orientation.
 */
void
meta_wait_for_orientation (MetaOrientationManager *orientation_manager,
                           MetaOrientation         orientation,
                           unsigned int           *times_signalled_out)
{
  WaitForOrientation wfo = {
    .expected = orientation,
  };

  wfo.orientation = meta_orientation_manager_get_orientation (orientation_manager);
  g_test_message ("%s: Waiting for orientation to change from "
                  "%d: %s to %d: %s...",
                  G_STRFUNC, wfo.orientation,
                  meta_orientation_to_string (wfo.orientation),
                  orientation, meta_orientation_to_string (orientation));

  /* This timeout can be relatively generous because we don't expect to
   * reach it: if we do, that's a test failure. */
  wfo.timeout_id = g_timeout_add_seconds (10, on_max_wait_timeout, &wfo);
  wfo.connection_id = g_signal_connect_swapped (orientation_manager,
                                                "orientation-changed",
                                                G_CALLBACK (on_orientation_changed),
                                                &wfo);

  while (wfo.orientation != orientation && wfo.timeout_id != 0)
    g_main_context_iteration (NULL, TRUE);

  if (wfo.orientation != orientation)
    {
      g_error ("Timed out waiting for orientation to change from %s to %s "
               "(received %u orientation-changed signal(s) while waiting)",
               meta_orientation_to_string (wfo.orientation),
               meta_orientation_to_string (orientation),
               wfo.times_signalled);
    }

  g_test_message ("%s: Orientation is now %d: %s",
                  G_STRFUNC, orientation,
                  meta_orientation_to_string (orientation));

  g_clear_handle_id (&wfo.timeout_id, g_source_remove);
  g_signal_handler_disconnect (orientation_manager, wfo.connection_id);

  if (times_signalled_out != NULL)
    *times_signalled_out = wfo.times_signalled;
}

/*
 * Wait for a possible orientation change, but don't assert that one occurs.
 */
void
meta_wait_for_possible_orientation_change (MetaOrientationManager *orientation_manager,
                                           unsigned int           *times_signalled_out)
{
  WaitForOrientation wfo = {
    .expected = META_ORIENTATION_UNDEFINED,
  };

  wfo.orientation = meta_orientation_manager_get_orientation (orientation_manager);
  g_test_message ("%s: Waiting for orientation to maybe change from %d: %s...",
                  G_STRFUNC, wfo.orientation,
                  meta_orientation_to_string (wfo.orientation));

  /* This can't be as long as the timeout for meta_wait_for_orientation(),
   * because in the usual case we expect to reach this timeout: we're
   * only waiting so that if the orientation (incorrectly?) changed here,
   * we'd have a chance to detect that. */
  wfo.timeout_id = g_timeout_add (1000, on_max_wait_timeout, &wfo);
  wfo.connection_id = g_signal_connect_swapped (orientation_manager,
                                                "orientation-changed",
                                                G_CALLBACK (on_orientation_changed),
                                                &wfo);

  while (wfo.times_signalled == 0 && wfo.timeout_id != 0)
    g_main_context_iteration (NULL, TRUE);

  if (wfo.timeout_id == 0)
    {
      g_test_message ("%s: Orientation didn't change", G_STRFUNC);
    }
  else
    {
      g_test_message ("%s: Orientation is now %d: %s",
                      G_STRFUNC, wfo.orientation,
                      meta_orientation_to_string (wfo.orientation));
    }

  g_clear_handle_id (&wfo.timeout_id, g_source_remove);
  g_signal_handler_disconnect (orientation_manager, wfo.connection_id);

  if (times_signalled_out != NULL)
    *times_signalled_out = wfo.times_signalled;
}
