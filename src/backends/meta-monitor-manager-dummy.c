/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001, 2002 Havoc Pennington
 * Copyright (C) 2002, 2003 Red Hat Inc.
 * Some ICCCM manager selection code derived from fvwm2,
 * Copyright (C) 2001 Dominik Vogt, Matthias Clasen, and fvwm2 team
 * Copyright (C) 2003 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
 * Copyright (C) 2013 Red Hat Inc.
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

#include "backends/meta-monitor-manager-dummy.h"

#include <stdlib.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-crtc.h"
#include "backends/meta-monitor.h"
#include "backends/meta-monitor-config-manager.h"
#include "backends/meta-output.h"
#include "meta/main.h"
#include "meta/util.h"

#define MAX_MONITORS 5
#define MAX_OUTPUTS (MAX_MONITORS * 2)
#define MAX_CRTCS (MAX_MONITORS * 2)
#define MAX_MODES (MAX_MONITORS * 4)

struct _MetaMonitorManagerDummy
{
  MetaMonitorManager parent_instance;

  gboolean is_transform_handled;
};

struct _MetaMonitorManagerDummyClass
{
  MetaMonitorManagerClass parent_class;
};

struct _MetaOutputDummy
{
  MetaOutput parent;

  float scale;
};

struct _MetaCrtcDummy
{
  MetaCrtc parent;
};

G_DEFINE_TYPE (MetaOutputDummy, meta_output_dummy, META_TYPE_OUTPUT)
G_DEFINE_TYPE (MetaCrtcDummy, meta_crtc_dummy, META_TYPE_CRTC)
G_DEFINE_TYPE (MetaMonitorManagerDummy, meta_monitor_manager_dummy, META_TYPE_MONITOR_MANAGER);

struct _MetaGpuDummy
{
  MetaGpu parent;
};

G_DEFINE_TYPE (MetaGpuDummy, meta_gpu_dummy, META_TYPE_GPU)

typedef struct _CrtcModeSpec
{
  int width;
  int height;
  float refresh_rate;
} CrtcModeSpec;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(CrtcModeSpec, g_free);

static MetaCrtcMode *
create_mode (CrtcModeSpec *spec,
             long          mode_id)
{
  g_autoptr (MetaCrtcModeInfo) crtc_mode_info = NULL;

  crtc_mode_info = meta_crtc_mode_info_new ();
  crtc_mode_info->width = spec->width;
  crtc_mode_info->height = spec->height;
  crtc_mode_info->refresh_rate = spec->refresh_rate;

  return g_object_new (META_TYPE_CRTC_MODE,
                       "id", (uint64_t) mode_id,
                       "info", crtc_mode_info,
                       NULL);
}

static MetaGpu *
get_gpu (MetaMonitorManager *manager)
{
  MetaBackend *backend = meta_monitor_manager_get_backend (manager);

  return META_GPU (meta_backend_get_gpus (backend)->data);
}

static void
append_monitor (MetaMonitorManager *manager,
                GList             **modes,
                GList             **crtcs,
                GList             **outputs,
                float               scale)
{
  MetaGpu *gpu = get_gpu (manager);
  CrtcModeSpec default_specs[] = {
    {
      .width = 800,
      .height = 600,
      .refresh_rate = 60.0
    },
    {
      .width = 1024,
      .height = 768,
      .refresh_rate = 60.0
    },
    {
      .width = 1440,
      .height = 900,
      .refresh_rate = 60.0
    },
    {
      .width = 1600,
      .height = 920,
      .refresh_rate = 60.0
    },
  };
  g_autolist (CrtcModeSpec) mode_specs = NULL;
  unsigned int n_mode_specs = 0;
  GList *new_modes = NULL;
  MetaCrtc *crtc;
  MetaOutputDummy *output_dummy;
  MetaOutput *output;
  unsigned int i;
  unsigned int number;
  g_autoptr (MetaOutputInfo) output_info = NULL;
  const char *mode_specs_str;
  GList *l;

  mode_specs_str = getenv ("MUTTER_DEBUG_DUMMY_MODE_SPECS");
  if (mode_specs_str && *mode_specs_str != '\0')
    {
      g_auto (GStrv) specs = g_strsplit (mode_specs_str, ":", -1);
      for (i = 0; specs[i]; ++i)
        {
          int width, height;
          float refresh_rate;

          if (meta_parse_monitor_mode (specs[i], &width, &height, &refresh_rate,
                                       60.0))
            {
              CrtcModeSpec *spec;

              spec = g_new0 (CrtcModeSpec, 1);
              spec->width = width;
              spec->height = height;
              spec->refresh_rate = refresh_rate;
              mode_specs = g_list_prepend (mode_specs, spec);
            }
        }
    }
  else
    {
      for (i = 0; i < G_N_ELEMENTS (default_specs); i++)
        {
          CrtcModeSpec *spec;

          spec = g_memdup2 (&default_specs[i], sizeof (CrtcModeSpec));
          mode_specs = g_list_prepend (mode_specs, spec);
        }
    }

  if (!mode_specs)
    {
      g_warning ("Cannot create dummy output: No valid mode specs.");
      meta_exit (META_EXIT_ERROR);
    }

  for (l = mode_specs; l; l = l->next)
    {
      CrtcModeSpec *spec = l->data;
      long mode_id;
      MetaCrtcMode *mode;

      mode_id = g_list_length (*modes) + n_mode_specs + 1;
      mode = create_mode (spec, mode_id);

      new_modes = g_list_append (new_modes, mode);
      n_mode_specs++;
    }
  *modes = g_list_concat (*modes, new_modes);

  crtc = g_object_new (META_TYPE_CRTC_DUMMY,
                       "id", (uint64_t) g_list_length (*crtcs) + 1,
                       "backend", meta_gpu_get_backend (gpu),
                       "gpu", gpu,
                       NULL);
  *crtcs = g_list_append (*crtcs, crtc);

  number = g_list_length (*outputs) + 1;

  output_info = meta_output_info_new ();
  output_info->name = g_strdup_printf ("LVDS%d", number);
  output_info->vendor = g_strdup ("MetaProducts Inc.");
  output_info->product = g_strdup ("MetaMonitor");
  output_info->serial = g_strdup_printf ("0xC0FFEE-%d", number);
  output_info->width_mm = 222;
  output_info->height_mm = 125;
  output_info->subpixel_order = COGL_SUBPIXEL_ORDER_UNKNOWN;
  output_info->preferred_mode = g_list_last (*modes)->data;
  output_info->n_possible_clones = 0;
  output_info->connector_type = META_CONNECTOR_TYPE_LVDS;

  output_info->modes = g_new0 (MetaCrtcMode *, n_mode_specs);
  for (l = new_modes, i = 0; l; l = l->next, i++)
    {
      MetaCrtcMode *mode = l->data;

      output_info->modes[i] = mode;
    }
  output_info->n_modes = n_mode_specs;
  output_info->possible_crtcs = g_new0 (MetaCrtc *, 1);
  output_info->possible_crtcs[0] = g_list_last (*crtcs)->data;
  output_info->n_possible_crtcs = 1;

  output = g_object_new (META_TYPE_OUTPUT_DUMMY,
                         "id", (uint64_t) number,
                         "gpu", gpu,
                         "info", output_info,
                         NULL);
  output_dummy = META_OUTPUT_DUMMY (output);
  output_dummy->scale = scale;

  *outputs = g_list_append (*outputs, output);
}

static void
append_tiled_monitor (MetaMonitorManager *manager,
                      GList             **modes,
                      GList             **crtcs,
                      GList             **outputs,
                      int                 scale)
{
  MetaGpu *gpu = get_gpu (manager);
  CrtcModeSpec mode_specs[] = {
    {
      .width = 800,
      .height = 600,
      .refresh_rate = 60.0
    },
    {
      .width = 512,
      .height = 768,
      .refresh_rate = 60.0
    }
  };
  unsigned int n_tiles = 2;
  GList *new_modes = NULL;
  GList *new_crtcs = NULL;
  MetaOutput *output;
  unsigned int i;
  uint32_t tile_group_id;

  for (i = 0; i < G_N_ELEMENTS (mode_specs); i++)
    {
      long mode_id;
      MetaCrtcMode *mode;

      mode_id = g_list_length (*modes) + i + 1;
      mode = create_mode (&mode_specs[i], mode_id);

      new_modes = g_list_append (new_modes, mode);
    }
  *modes = g_list_concat (*modes, new_modes);

  for (i = 0; i < n_tiles; i++)
    {
      MetaCrtc *crtc;

      crtc = g_object_new (META_TYPE_CRTC_DUMMY,
                           "id", (uint64_t) g_list_length (*crtcs) + i + 1,
                           "backend", meta_gpu_get_backend (gpu),
                           "gpu", gpu,
                           NULL);
      new_crtcs = g_list_append (new_crtcs, crtc);
    }
  *crtcs = g_list_concat (*crtcs, new_crtcs);

  tile_group_id = g_list_length (*outputs) + 1;
  for (i = 0; i < n_tiles; i++)
    {
      MetaOutputDummy *output_dummy;
      MetaCrtcMode *preferred_mode;
      const MetaCrtcModeInfo *preferred_mode_info;
      unsigned int j;
      unsigned int number;
      g_autoptr (MetaOutputInfo) output_info = NULL;
      GList *l;

      /* Arbitrary ID unique for this output */
      number = g_list_length (*outputs) + 1;

      preferred_mode = g_list_last (*modes)->data;
      preferred_mode_info = meta_crtc_mode_get_info (preferred_mode);

      output_info = meta_output_info_new ();

      output_info->name = g_strdup_printf ("LVDS%d", number);
      output_info->vendor = g_strdup ("MetaProducts Inc.");
      output_info->product = g_strdup ("MetaMonitor");
      output_info->serial = g_strdup_printf ("0xC0FFEE-%d", number);
      output_info->suggested_x = -1;
      output_info->suggested_y = -1;
      output_info->width_mm = 222;
      output_info->height_mm = 125;
      output_info->subpixel_order = COGL_SUBPIXEL_ORDER_UNKNOWN;
      output_info->preferred_mode = preferred_mode;
      output_info->n_possible_clones = 0;
      output_info->connector_type = META_CONNECTOR_TYPE_LVDS;
      output_info->tile_info = (MetaTileInfo) {
        .group_id = tile_group_id,
        .max_h_tiles = n_tiles,
        .max_v_tiles = 1,
        .loc_h_tile = i,
        .loc_v_tile = 0,
        .tile_w = preferred_mode_info->width,
        .tile_h = preferred_mode_info->height
      },

      output_info->modes = g_new0 (MetaCrtcMode *, G_N_ELEMENTS (mode_specs));
      for (l = new_modes, j = 0; l; l = l->next, j++)
        {
          MetaCrtcMode *mode = l->data;

          output_info->modes[j] = mode;
        }
      output_info->n_modes = G_N_ELEMENTS (mode_specs);

      output_info->possible_crtcs = g_new0 (MetaCrtc *, n_tiles);
      for (l = new_crtcs, j = 0; l; l = l->next, j++)
        {
          MetaCrtc *crtc = l->data;

          output_info->possible_crtcs[j] = crtc;
        }
      output_info->n_possible_crtcs = n_tiles;

      output = g_object_new (META_TYPE_OUTPUT_DUMMY,
                             "id", (uint64_t) number,
                             "gpu", gpu,
                             "info", output_info,
                             NULL);
      output_dummy = META_OUTPUT_DUMMY (output);
      output_dummy->scale = scale;

      *outputs = g_list_append (*outputs, output);
    }
}

static void
meta_monitor_manager_dummy_read_current (MetaMonitorManager *manager)
{
  MetaGpu *gpu = get_gpu (manager);
  unsigned int num_monitors = 1;
  float *monitor_scales = NULL;
  const char *num_monitors_str;
  const char *monitor_scales_str;
  const char *tiled_monitors_str;
  gboolean tiled_monitors;
  unsigned int i;
  GList *outputs;
  GList *crtcs;
  GList *modes;

  /* To control what monitor configuration is generated, there are two available
   * environmental variables that can be used:
   *
   * MUTTER_DEBUG_NUM_DUMMY_MONITORS
   *
   * Specifies the number of dummy monitors to include in the stage. Every
   * monitor is 1024x786 pixels and they are placed on a horizontal row.
   *
   * MUTTER_DEBUG_DUMMY_MODE_SPECS
   *
   * A colon separated list of mode specifications that can be used to
   * configure the monitor via dbus API. Setting this environment variable
   * overrides the default set of modes available.
   * Format should be WWxHH:WWxHH@RR
   *
   * MUTTER_DEBUG_DUMMY_MONITOR_SCALES
   *
   * A comma separated list that specifies the scales of the dummy monitors.
   *
   * MUTTER_DEBUG_TILED_DUMMY_MONITORS
   *
   * If set to "1" the dummy monitors will emulate being tiled, i.e. each have a
   * unique tile group id, made up of multiple outputs and CRTCs.
   *
   * For example the following configuration results in two monitors, where the
   * first one has the monitor scale 1, and the other the monitor scale 2.
   *
   * MUTTER_DEBUG_NUM_DUMMY_MONITORS=2
   * MUTTER_DEBUG_DUMMY_MONITOR_SCALES=1,2
   * MUTTER_DEBUG_TILED_DUMMY_MONITORS=1
   */
  num_monitors_str = getenv ("MUTTER_DEBUG_NUM_DUMMY_MONITORS");
  if (num_monitors_str)
    {
      num_monitors = g_ascii_strtoll (num_monitors_str, NULL, 10);
      if (num_monitors <= 0)
        {
          meta_warning ("Invalid number of dummy monitors");
          num_monitors = 1;
        }

      if (num_monitors > MAX_MONITORS)
        {
          meta_warning ("Clamping monitor count to max (%d)",
                        MAX_MONITORS);
          num_monitors = MAX_MONITORS;
        }
    }

  monitor_scales = g_newa (typeof (*monitor_scales), num_monitors);
  for (i = 0; i < num_monitors; i++)
    monitor_scales[i] = 1.0;

  monitor_scales_str = getenv ("MUTTER_DEBUG_DUMMY_MONITOR_SCALES");
  if (monitor_scales_str)
    {
      gchar **scales_str_list;

      scales_str_list = g_strsplit (monitor_scales_str, ",", -1);
      if (g_strv_length (scales_str_list) != num_monitors)
        meta_warning ("Number of specified monitor scales differ from number "
                      "of monitors (defaults to 1).");
      for (i = 0; i < num_monitors && scales_str_list[i]; i++)
        {
          float scale = g_ascii_strtod (scales_str_list[i], NULL);

          monitor_scales[i] = scale;
        }
      g_strfreev (scales_str_list);
    }

  tiled_monitors_str = g_getenv ("MUTTER_DEBUG_TILED_DUMMY_MONITORS");
  tiled_monitors = g_strcmp0 (tiled_monitors_str, "1") == 0;

  modes = NULL;
  crtcs = NULL;
  outputs = NULL;

  for (i = 0; i < num_monitors; i++)
    {
      if (tiled_monitors)
        append_tiled_monitor (manager,
                              &modes, &crtcs, &outputs, monitor_scales[i]);
      else
        append_monitor (manager, &modes, &crtcs, &outputs, monitor_scales[i]);
    }

  meta_gpu_take_modes (gpu, modes);
  meta_gpu_take_crtcs (gpu, crtcs);
  meta_gpu_take_outputs (gpu, outputs);
}

static void
meta_monitor_manager_dummy_ensure_initial_config (MetaMonitorManager *manager)
{
  MetaMonitorsConfig *config;

  config = meta_monitor_manager_ensure_configured (manager);

  meta_monitor_manager_update_logical_state (manager, config);
}

static void
apply_crtc_assignments (MetaMonitorManager    *manager,
                        MetaCrtcAssignment   **crtcs,
                        unsigned int           n_crtcs,
                        MetaOutputAssignment **outputs,
                        unsigned int           n_outputs)
{
  g_autoptr (GList) to_configure_outputs = NULL;
  g_autoptr (GList) to_configure_crtcs = NULL;
  unsigned i;

  to_configure_outputs = g_list_copy (meta_gpu_get_outputs (get_gpu (manager)));
  to_configure_crtcs = g_list_copy (meta_gpu_get_crtcs (get_gpu (manager)));

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
          MetaCrtcConfig *crtc_config;
          unsigned int j;

          crtc_config = meta_crtc_config_new (&crtc_assignment->layout,
                                              crtc_assignment->mode,
                                              crtc_assignment->transform);
          meta_crtc_set_config (crtc, crtc_config,
                                crtc_assignment->backend_private);

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
meta_monitor_manager_dummy_apply_monitors_config (MetaMonitorManager      *manager,
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

      meta_monitor_manager_rebuild (manager, NULL);
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
  meta_monitor_manager_rebuild (manager, config);

  return TRUE;
}

static gboolean
meta_monitor_manager_dummy_is_transform_handled (MetaMonitorManager  *manager,
                                                 MetaCrtc            *crtc,
                                                 MetaMonitorTransform transform)
{
  MetaMonitorManagerDummy *manager_dummy = META_MONITOR_MANAGER_DUMMY (manager);

  return manager_dummy->is_transform_handled;
}

static float
meta_monitor_manager_dummy_calculate_monitor_mode_scale (MetaMonitorManager           *manager,
                                                         MetaLogicalMonitorLayoutMode  layout_mode,
                                                         MetaMonitor                  *monitor,
                                                         MetaMonitorMode              *monitor_mode)
{
  MetaOutput *output;
  MetaOutputDummy *output_dummy;

  output = meta_monitor_get_main_output (monitor);
  output_dummy = META_OUTPUT_DUMMY (output);

  return output_dummy->scale;
}

static float *
meta_monitor_manager_dummy_calculate_supported_scales (MetaMonitorManager           *manager,
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
is_monitor_framebuffers_scaled (MetaMonitorManager *manager)
{
  MetaBackend *backend = meta_monitor_manager_get_backend (manager);
  MetaSettings *settings = meta_backend_get_settings (backend);

  return meta_settings_is_experimental_feature_enabled (
    settings,
    META_EXPERIMENTAL_FEATURE_SCALE_MONITOR_FRAMEBUFFER);
}

static MetaMonitorManagerCapability
meta_monitor_manager_dummy_get_capabilities (MetaMonitorManager *manager)
{
  MetaBackend *backend = meta_monitor_manager_get_backend (manager);
  MetaSettings *settings = meta_backend_get_settings (backend);
  MetaMonitorManagerCapability capabilities =
    META_MONITOR_MANAGER_CAPABILITY_NONE;

  if (meta_settings_is_experimental_feature_enabled (
        settings,
        META_EXPERIMENTAL_FEATURE_SCALE_MONITOR_FRAMEBUFFER))
    capabilities |= META_MONITOR_MANAGER_CAPABILITY_LAYOUT_MODE;

  return capabilities;
}

static gboolean
meta_monitor_manager_dummy_get_max_screen_size (MetaMonitorManager *manager,
                                                int                *max_width,
                                                int                *max_height)
{
  return FALSE;
}

static MetaLogicalMonitorLayoutMode
meta_monitor_manager_dummy_get_default_layout_mode (MetaMonitorManager *manager)
{
  if (is_monitor_framebuffers_scaled (manager))
    return META_LOGICAL_MONITOR_LAYOUT_MODE_LOGICAL;
  else
    return META_LOGICAL_MONITOR_LAYOUT_MODE_PHYSICAL;
}

static void
meta_monitor_manager_dummy_constructed (GObject *object)
{
  MetaMonitorManagerDummy *manager_dummy = META_MONITOR_MANAGER_DUMMY (object);
  const char *nested_offscreen_transform;
  GObjectClass *parent_object_class =
    G_OBJECT_CLASS (meta_monitor_manager_dummy_parent_class);

  parent_object_class->constructed (object);

  nested_offscreen_transform =
    g_getenv ("MUTTER_DEBUG_NESTED_OFFSCREEN_TRANSFORM");
  if (g_strcmp0 (nested_offscreen_transform, "1") == 0)
    manager_dummy->is_transform_handled = FALSE;
  else
    manager_dummy->is_transform_handled = TRUE;
}

static void
meta_monitor_manager_dummy_class_init (MetaMonitorManagerDummyClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaMonitorManagerClass *manager_class = META_MONITOR_MANAGER_CLASS (klass);

  object_class->constructed = meta_monitor_manager_dummy_constructed;

  manager_class->ensure_initial_config = meta_monitor_manager_dummy_ensure_initial_config;
  manager_class->apply_monitors_config = meta_monitor_manager_dummy_apply_monitors_config;
  manager_class->is_transform_handled = meta_monitor_manager_dummy_is_transform_handled;
  manager_class->calculate_monitor_mode_scale = meta_monitor_manager_dummy_calculate_monitor_mode_scale;
  manager_class->calculate_supported_scales = meta_monitor_manager_dummy_calculate_supported_scales;
  manager_class->get_capabilities = meta_monitor_manager_dummy_get_capabilities;
  manager_class->get_max_screen_size = meta_monitor_manager_dummy_get_max_screen_size;
  manager_class->get_default_layout_mode = meta_monitor_manager_dummy_get_default_layout_mode;
}

static void
meta_monitor_manager_dummy_init (MetaMonitorManagerDummy *manager_dummy)
{
}

static gboolean
meta_gpu_dummy_read_current (MetaGpu  *gpu,
                             GError  **error)
{
  MetaBackend *backend = meta_gpu_get_backend (gpu);
  MetaMonitorManager *manager = meta_backend_get_monitor_manager (backend);

  meta_monitor_manager_dummy_read_current (manager);

  return TRUE;
}

static void
meta_gpu_dummy_init (MetaGpuDummy *gpu_dummy)
{
}

static void
meta_gpu_dummy_class_init (MetaGpuDummyClass *klass)
{
  MetaGpuClass *gpu_class = META_GPU_CLASS (klass);

  gpu_class->read_current = meta_gpu_dummy_read_current;
}

static void
meta_output_dummy_init (MetaOutputDummy *output_dummy)
{
  output_dummy->scale = 1;
}

static void
meta_output_dummy_class_init (MetaOutputDummyClass *klass)
{
}

static size_t
meta_crtc_dummy_get_gamma_lut_size (MetaCrtc *crtc)
{
  return 0;
}

static MetaGammaLut *
meta_crtc_dummy_get_gamma_lut (MetaCrtc *crtc)
{
  return NULL;
}

static void
meta_crtc_dummy_set_gamma_lut (MetaCrtc           *crtc,
                               const MetaGammaLut *lut)
{
  g_warn_if_reached ();
}

static void
meta_crtc_dummy_init (MetaCrtcDummy *crtc_dummy)
{
}

static void
meta_crtc_dummy_class_init (MetaCrtcDummyClass *klass)
{
  MetaCrtcClass *crtc_class = META_CRTC_CLASS (klass);

  crtc_class->get_gamma_lut_size = meta_crtc_dummy_get_gamma_lut_size;
  crtc_class->get_gamma_lut = meta_crtc_dummy_get_gamma_lut;
  crtc_class->set_gamma_lut = meta_crtc_dummy_set_gamma_lut;
}
