/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat
 * Copyright (c) 2018 DisplayLink (UK) Ltd.
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

#include "backends/meta-monitor-config-manager.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-monitor-config-store.h"
#include "backends/meta-monitor-config-utils.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-output.h"
#include "core/boxes-private.h"
#include "meta/meta-monitor-manager.h"

#define CONFIG_HISTORY_MAX_SIZE 3

struct _MetaMonitorConfigManager
{
  GObject parent;

  MetaMonitorManager *monitor_manager;

  MetaMonitorConfigStore *config_store;

  MetaMonitorsConfig *current_config;
  GQueue config_history;
};

G_DEFINE_TYPE (MetaMonitorConfigManager, meta_monitor_config_manager,
               G_TYPE_OBJECT)

G_DEFINE_TYPE (MetaMonitorsConfig, meta_monitors_config,
               G_TYPE_OBJECT)

static void
meta_crtc_assignment_free (MetaCrtcAssignment *assignment);

static void
meta_output_assignment_free (MetaOutputAssignment *assignment);

MetaMonitorConfigManager *
meta_monitor_config_manager_new (MetaMonitorManager *monitor_manager)
{
  MetaMonitorConfigManager *config_manager;

  config_manager = g_object_new (META_TYPE_MONITOR_CONFIG_MANAGER, NULL);
  config_manager->monitor_manager = monitor_manager;
  config_manager->config_store =
    meta_monitor_config_store_new (monitor_manager);

  return config_manager;
}

MetaMonitorConfigStore *
meta_monitor_config_manager_get_store (MetaMonitorConfigManager *config_manager)
{
  return config_manager->config_store;
}

static gboolean
is_crtc_reserved (MetaCrtc *crtc,
                  GArray   *reserved_crtcs)
{
  unsigned int i;

  for (i = 0; i < reserved_crtcs->len; i++)
    {
       uint64_t id;

       id = g_array_index (reserved_crtcs, uint64_t, i);
       if (id == meta_crtc_get_id (crtc))
         return TRUE;
    }

  return FALSE;
}

static gboolean
is_crtc_assigned (MetaCrtc  *crtc,
                  GPtrArray *crtc_assignments)
{
  unsigned int i;

  if (meta_crtc_is_leased (crtc))
    return TRUE;

  for (i = 0; i < crtc_assignments->len; i++)
    {
      MetaCrtcAssignment *assigned_crtc_assignment =
        g_ptr_array_index (crtc_assignments, i);

      if (assigned_crtc_assignment->crtc == crtc)
        return TRUE;
    }

  return FALSE;
}

static MetaCrtc *
find_unassigned_crtc (MetaOutput *output,
                      GPtrArray  *crtc_assignments,
                      GArray     *reserved_crtcs)
{
  MetaCrtc *crtc;
  const MetaOutputInfo *output_info;
  unsigned int i;

  crtc = meta_output_get_assigned_crtc (output);
  if (crtc && !is_crtc_assigned (crtc, crtc_assignments))
    return crtc;

  output_info = meta_output_get_info (output);

  /* then try to assign a CRTC that wasn't used */
  for (i = 0; i < output_info->n_possible_crtcs; i++)
    {
      crtc = output_info->possible_crtcs[i];

      if (is_crtc_assigned (crtc, crtc_assignments))
        continue;

      if (is_crtc_reserved (crtc, reserved_crtcs))
        continue;

      return crtc;
    }

  /* finally just give a CRTC that we haven't assigned */
  for (i = 0; i < output_info->n_possible_crtcs; i++)
    {
      crtc = output_info->possible_crtcs[i];

      if (is_crtc_assigned (crtc, crtc_assignments))
        continue;

      return crtc;
    }

  return NULL;
}

typedef struct
{
  MetaMonitorManager *monitor_manager;
  MetaMonitorsConfig *config;
  MetaLogicalMonitorConfig *logical_monitor_config;
  MetaMonitorConfig *monitor_config;
  GPtrArray *crtc_assignments;
  GPtrArray *output_assignments;
  GArray *reserved_crtcs;
} MonitorAssignmentData;

static gboolean
assign_monitor_crtc (MetaMonitor         *monitor,
                     MetaMonitorMode     *mode,
                     MetaMonitorCrtcMode *monitor_crtc_mode,
                     gpointer             user_data,
                     GError             **error)
{
  MonitorAssignmentData *data = user_data;
  MetaOutput *output;
  MetaCrtc *crtc;
  MtkMonitorTransform transform;
  MtkMonitorTransform crtc_transform;
  int crtc_x, crtc_y;
  float x_offset, y_offset;
  float scale = 0.0;
  float width, height;
  MetaCrtcMode *crtc_mode;
  const MetaCrtcModeInfo *crtc_mode_info;
  graphene_rect_t crtc_layout;
  MetaCrtcAssignment *crtc_assignment;
  MetaOutputAssignment *output_assignment;
  MetaMonitorConfig *first_monitor_config;
  gboolean assign_output_as_primary;
  gboolean assign_output_as_presentation;

  output = monitor_crtc_mode->output;

  crtc = find_unassigned_crtc (output,
                               data->crtc_assignments,
                               data->reserved_crtcs);

  if (!crtc)
    {
      MetaMonitorSpec *monitor_spec = meta_monitor_get_spec (monitor);

      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No available CRTC for monitor '%s %s' not found",
                   monitor_spec->vendor, monitor_spec->product);
      return FALSE;
    }

  transform = data->logical_monitor_config->transform;
  crtc_transform = meta_monitor_logical_to_crtc_transform (monitor, transform);

  meta_monitor_calculate_crtc_pos (monitor, mode, output, crtc_transform,
                                   &crtc_x, &crtc_y);

  x_offset = data->logical_monitor_config->layout.x;
  y_offset = data->logical_monitor_config->layout.y;

  switch (data->config->layout_mode)
    {
    case META_LOGICAL_MONITOR_LAYOUT_MODE_LOGICAL:
      scale = data->logical_monitor_config->scale;
      break;
    case META_LOGICAL_MONITOR_LAYOUT_MODE_PHYSICAL:
      scale = 1.0;
      break;
    }

  crtc_mode = monitor_crtc_mode->crtc_mode;
  crtc_mode_info = meta_crtc_mode_get_info (monitor_crtc_mode->crtc_mode);

  if (mtk_monitor_transform_is_rotated (crtc_transform))
    {
      width = crtc_mode_info->height / scale;
      height = crtc_mode_info->width / scale;
    }
  else
    {
      width = crtc_mode_info->width / scale;
      height = crtc_mode_info->height / scale;
    }

  crtc_layout = GRAPHENE_RECT_INIT (x_offset + (crtc_x / scale),
                                    y_offset + (crtc_y / scale),
                                    width,
                                    height);

  crtc_assignment = g_new0 (MetaCrtcAssignment, 1);
  *crtc_assignment = (MetaCrtcAssignment) {
    .crtc = crtc,
    .mode = crtc_mode,
    .layout = crtc_layout,
    .transform = crtc_transform,
    .outputs = g_ptr_array_new ()
  };
  g_ptr_array_add (crtc_assignment->outputs, output);

  if (!meta_crtc_assign_extra (crtc,
                               crtc_assignment,
                               data->crtc_assignments,
                               error))
    return FALSE;

  /*
   * Only one output can be marked as primary (due to Xrandr limitation),
   * so only mark the main output of the first monitor in the logical monitor
   * as such.
   */
  first_monitor_config = data->logical_monitor_config->monitor_configs->data;
  if (data->logical_monitor_config->is_primary &&
      data->monitor_config == first_monitor_config &&
      meta_monitor_get_main_output (monitor) == output)
    assign_output_as_primary = TRUE;
  else
    assign_output_as_primary = FALSE;

  if (data->logical_monitor_config->is_presentation)
    assign_output_as_presentation = TRUE;
  else
    assign_output_as_presentation = FALSE;

  output_assignment = g_new0 (MetaOutputAssignment, 1);
  *output_assignment = (MetaOutputAssignment) {
    .output = output,
    .is_primary = assign_output_as_primary,
    .is_presentation = assign_output_as_presentation,
    .is_underscanning = data->monitor_config->enable_underscanning,
    .has_max_bpc = data->monitor_config->has_max_bpc,
    .max_bpc = data->monitor_config->max_bpc,
    .rgb_range = data->monitor_config->rgb_range,
    .color_mode = data->monitor_config->color_mode,
  };

  g_ptr_array_add (data->crtc_assignments, crtc_assignment);
  g_ptr_array_add (data->output_assignments, output_assignment);

  return TRUE;
}

static gboolean
assign_monitor_crtcs (MetaMonitorManager       *manager,
                      MetaMonitorsConfig       *config,
                      MetaLogicalMonitorConfig *logical_monitor_config,
                      MetaMonitorConfig        *monitor_config,
                      GPtrArray                *crtc_assignments,
                      GPtrArray                *output_assignments,
                      GArray                   *reserved_crtcs,
                      GError                  **error)
{
  MetaMonitorSpec *monitor_spec = monitor_config->monitor_spec;
  MetaMonitorModeSpec *monitor_mode_spec = monitor_config->mode_spec;
  MetaMonitor *monitor;
  MetaMonitorMode *monitor_mode;
  MonitorAssignmentData data;

  monitor = meta_monitor_manager_get_monitor_from_spec (manager, monitor_spec);
  if (!monitor)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Configured monitor '%s %s' not found",
                   monitor_spec->vendor, monitor_spec->product);
      return FALSE;
    }

  monitor_mode = meta_monitor_get_mode_from_spec (monitor, monitor_mode_spec);
  if (!monitor_mode)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Invalid mode %dx%d (%.3f) for monitor '%s %s'",
                   monitor_mode_spec->width, monitor_mode_spec->height,
                   monitor_mode_spec->refresh_rate,
                   monitor_spec->vendor, monitor_spec->product);
      return FALSE;
    }

  data = (MonitorAssignmentData) {
    .monitor_manager = manager,
    .config = config,
    .logical_monitor_config = logical_monitor_config,
    .monitor_config = monitor_config,
    .crtc_assignments = crtc_assignments,
    .output_assignments = output_assignments,
    .reserved_crtcs = reserved_crtcs
  };
  if (!meta_monitor_mode_foreach_crtc (monitor, monitor_mode,
                                       assign_monitor_crtc,
                                       &data,
                                       error))
    return FALSE;

  return TRUE;
}

static gboolean
assign_logical_monitor_crtcs (MetaMonitorManager       *manager,
                              MetaMonitorsConfig       *config,
                              MetaLogicalMonitorConfig *logical_monitor_config,
                              GPtrArray                *crtc_assignments,
                              GPtrArray                *output_assignments,
                              GArray                   *reserved_crtcs,
                              GError                  **error)
{
  GList *l;

  for (l = logical_monitor_config->monitor_configs; l; l = l->next)
    {
      MetaMonitorConfig *monitor_config = l->data;

      if (!assign_monitor_crtcs (manager,
                                 config,
                                 logical_monitor_config,
                                 monitor_config,
                                 crtc_assignments, output_assignments,
                                 reserved_crtcs, error))
        return FALSE;
    }

  return TRUE;
}

gboolean
meta_monitor_config_manager_assign (MetaMonitorManager *manager,
                                    MetaMonitorsConfig *config,
                                    GPtrArray         **out_crtc_assignments,
                                    GPtrArray         **out_output_assignments,
                                    GError            **error)
{
  g_autoptr (GPtrArray) crtc_assignments = NULL;
  g_autoptr (GPtrArray) output_assignments = NULL;
  g_autoptr (GArray) reserved_crtcs = NULL;
  GList *l;

  crtc_assignments =
    g_ptr_array_new_with_free_func ((GDestroyNotify) meta_crtc_assignment_free);
  output_assignments =
    g_ptr_array_new_with_free_func ((GDestroyNotify) meta_output_assignment_free);
  reserved_crtcs = g_array_new (FALSE, FALSE, sizeof (uint64_t));

  for (l = config->logical_monitor_configs; l; l = l->next)
    {
      MetaLogicalMonitorConfig *logical_monitor_config = l->data;
      GList *k;

      for (k = logical_monitor_config->monitor_configs; k; k = k->next)
        {
          MetaMonitorConfig *monitor_config = k->data;
          MetaMonitorSpec *monitor_spec = monitor_config->monitor_spec;
          MetaMonitor *monitor;
          GList *o;

          monitor = meta_monitor_manager_get_monitor_from_spec (manager, monitor_spec);

          for (o = meta_monitor_get_outputs (monitor); o; o = o->next)
            {
              MetaOutput *output = o->data;
              MetaCrtc *crtc;

              crtc = meta_output_get_assigned_crtc (output);
              if (crtc)
                {
                  uint64_t crtc_id = meta_crtc_get_id (crtc);

                  g_array_append_val (reserved_crtcs, crtc_id);
                }
            }
        }
    }

  for (l = config->logical_monitor_configs; l; l = l->next)
    {
      MetaLogicalMonitorConfig *logical_monitor_config = l->data;

      if (!assign_logical_monitor_crtcs (manager,
                                         config, logical_monitor_config,
                                         crtc_assignments, output_assignments,
                                         reserved_crtcs, error))
        return FALSE;
    }

  *out_crtc_assignments = g_steal_pointer (&crtc_assignments);
  *out_output_assignments = g_steal_pointer (&output_assignments);

  return TRUE;
}

static gboolean
is_lid_closed (MetaMonitorManager *monitor_manager)
{
    MetaBackend *backend;

    backend = meta_monitor_manager_get_backend (monitor_manager);
    return meta_backend_is_lid_closed (backend);
}

MetaMonitorsConfigKey *
meta_create_monitors_config_key_for_current_state (MetaMonitorManager *monitor_manager)
{
  MetaMonitorsConfigKey *config_key;
  MetaMonitorSpec *laptop_monitor_spec;
  GList *l;
  GList *monitor_specs;

  laptop_monitor_spec = NULL;
  monitor_specs = NULL;
  for (l = monitor_manager->monitors; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;
      MetaMonitorSpec *monitor_spec;

      if (meta_monitor_is_builtin (monitor))
        {
          laptop_monitor_spec = meta_monitor_get_spec (monitor);

          if (is_lid_closed (monitor_manager))
            continue;
        }

      monitor_spec = meta_monitor_spec_clone (meta_monitor_get_spec (monitor));
      monitor_specs = g_list_prepend (monitor_specs, monitor_spec);
    }

  if (!monitor_specs && laptop_monitor_spec)
    {
      monitor_specs =
        g_list_prepend (NULL, meta_monitor_spec_clone (laptop_monitor_spec));
    }

  if (!monitor_specs)
    return NULL;

  monitor_specs = g_list_sort (monitor_specs,
                               (GCompareFunc) meta_monitor_spec_compare);

  config_key = g_new0 (MetaMonitorsConfigKey, 1);
  *config_key = (MetaMonitorsConfigKey) {
    .monitor_specs = monitor_specs,
    .layout_mode = meta_monitor_manager_get_default_layout_mode (monitor_manager),
  };

  return config_key;
}

MetaMonitorsConfig *
meta_monitor_config_manager_get_stored (MetaMonitorConfigManager *config_manager)
{
  MetaMonitorManager *monitor_manager = config_manager->monitor_manager;
  MetaMonitorsConfigKey *config_key;
  MetaMonitorsConfig *config;

  config_key =
    meta_create_monitors_config_key_for_current_state (monitor_manager);
  if (!config_key)
    return NULL;

  config = meta_monitor_config_store_lookup (config_manager->config_store,
                                             config_key);
  meta_monitors_config_key_free (config_key);

  return config;
}

typedef enum _MonitorMatchRule
{
  MONITOR_MATCH_ALL = 0,
  MONITOR_MATCH_EXTERNAL = (1 << 0),
  MONITOR_MATCH_BUILTIN = (1 << 1),
  MONITOR_MATCH_VISIBLE = (1 << 2),
  MONITOR_MATCH_WITH_SUGGESTED_POSITION = (1 << 3),
  MONITOR_MATCH_PRIMARY = (1 << 4),
  MONITOR_MATCH_ALLOW_FALLBACK = (1 << 5),
} MonitorMatchRule;

static gboolean
monitor_matches_rule (MetaMonitor        *monitor,
                      MetaMonitorManager *monitor_manager,
                      MonitorMatchRule    match_rule)
{
  if (!monitor)
    return FALSE;

  if (match_rule & MONITOR_MATCH_BUILTIN)
    {
      if (!meta_monitor_is_builtin (monitor))
        return FALSE;
    }
  else if (match_rule & MONITOR_MATCH_EXTERNAL)
    {
      if (meta_monitor_is_builtin (monitor))
        return FALSE;
    }

  if (match_rule & MONITOR_MATCH_VISIBLE)
    {
      if (meta_monitor_is_builtin (monitor) &&
          is_lid_closed (monitor_manager))
        return FALSE;
    }

  if (match_rule & MONITOR_MATCH_WITH_SUGGESTED_POSITION)
    {
      if (!meta_monitor_get_suggested_position (monitor, NULL, NULL))
        return FALSE;
    }

  return TRUE;
}

static GList *
find_monitors (MetaMonitorManager *monitor_manager,
               MonitorMatchRule    match_rule,
               MetaMonitor        *not_this_one)
{
  GList *result = NULL;
  GList *monitors, *l;

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  for (l = g_list_last (monitors); l; l = l->prev)
    {
      MetaMonitor *monitor = l->data;

      if (not_this_one && monitor == not_this_one)
        continue;

      if (monitor_matches_rule (monitor, monitor_manager, match_rule))
        result = g_list_prepend (result, monitor);
    }

  return result;
}

static MetaMonitor *
find_monitor_with_highest_preferred_resolution (MetaMonitorManager *monitor_manager,
                                                MonitorMatchRule    match_rule)
{
  g_autoptr (GList) monitors = NULL;
  GList *l;
  int largest_area = 0;
  MetaMonitor *largest_monitor = NULL;

  monitors = find_monitors (monitor_manager, match_rule, NULL);
  for (l = monitors; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;
      MetaMonitorMode *mode;
      int width, height;
      int area;

      mode = meta_monitor_get_preferred_mode (monitor);
      meta_monitor_mode_get_resolution (mode, &width, &height);
      area = width * height;

      if (area > largest_area)
        {
          largest_area = area;
          largest_monitor = monitor;
        }
    }

  return largest_monitor;
}

/*
 * Try to find the primary monitor. The priority of classification is:
 *
 * 1. Find the primary monitor as reported by the underlying system,
 * 2. Find the laptop panel
 * 3. Find the external monitor with highest resolution
 *
 * If the laptop lid is closed, exclude the laptop panel from possible
 * alternatives, except if no other alternatives exist.
 */
static MetaMonitor *
find_primary_monitor (MetaMonitorManager *monitor_manager,
                      MonitorMatchRule    match_rule)
{
  MetaMonitor *monitor;

  monitor = meta_monitor_manager_get_primary_monitor (monitor_manager);
  if (monitor_matches_rule (monitor, monitor_manager, match_rule))
    return monitor;

  monitor = meta_monitor_manager_get_builtin_monitor (monitor_manager);
  if (monitor_matches_rule (monitor, monitor_manager, match_rule))
    return monitor;

  monitor = find_monitor_with_highest_preferred_resolution (monitor_manager,
                                                            match_rule);
  if (monitor)
    return monitor;

  if (match_rule & MONITOR_MATCH_ALLOW_FALLBACK)
    return find_monitor_with_highest_preferred_resolution (monitor_manager,
                                                           MONITOR_MATCH_ALL);

  return NULL;
}

static MetaMonitorConfig *
create_monitor_config (MetaMonitor     *monitor,
                       MetaMonitorMode *mode,
                       MetaColorMode    color_mode)
{
  MetaMonitorSpec *monitor_spec;
  MetaMonitorModeSpec *mode_spec;
  MetaMonitorConfig *monitor_config;

  monitor_spec = meta_monitor_get_spec (monitor);
  mode_spec = meta_monitor_mode_get_spec (mode);

  monitor_config = g_new0 (MetaMonitorConfig, 1);
  *monitor_config = (MetaMonitorConfig) {
    .monitor_spec = meta_monitor_spec_clone (monitor_spec),
    .mode_spec = g_memdup2 (mode_spec, sizeof (MetaMonitorModeSpec)),
    .enable_underscanning = meta_monitor_is_underscanning (monitor),
    .rgb_range = meta_monitor_get_rgb_range (monitor),
    .color_mode = color_mode,
  };

  monitor_config->has_max_bpc =
    meta_monitor_get_max_bpc (monitor, &monitor_config->max_bpc);

  return monitor_config;
}

static MtkMonitorTransform
get_monitor_transform (MetaMonitorManager *monitor_manager,
                       MetaMonitor        *monitor)
{
  MetaOrientationManager *orientation_manager;
  MetaOrientation orientation;
  MetaBackend *backend;

  if (!meta_monitor_is_builtin (monitor) ||
      !meta_monitor_manager_get_panel_orientation_managed (monitor_manager))
    return MTK_MONITOR_TRANSFORM_NORMAL;

  backend = meta_monitor_manager_get_backend (monitor_manager);
  orientation_manager = meta_backend_get_orientation_manager (backend);
  orientation = meta_orientation_manager_get_orientation (orientation_manager);

  return meta_orientation_to_transform (orientation);
}

static void
scale_logical_monitor_width (MetaLogicalMonitorLayoutMode  layout_mode,
                             float                         scale,
                             int                           mode_width,
                             int                           mode_height,
                             int                          *width,
                             int                          *height)
{
  switch (layout_mode)
    {
    case META_LOGICAL_MONITOR_LAYOUT_MODE_LOGICAL:
      *width = (int) roundf (mode_width / scale);
      *height = (int) roundf (mode_height / scale);
      return;
    case META_LOGICAL_MONITOR_LAYOUT_MODE_PHYSICAL:
      *width = mode_width;
      *height = mode_height;
      return;
    }

  g_assert_not_reached ();
}

static MetaLogicalMonitorConfig *
create_preferred_logical_monitor_config (MetaMonitorManager           *monitor_manager,
                                         MetaMonitor                  *monitor,
                                         int                           x,
                                         int                           y,
                                         float                         scale,
                                         MetaColorMode                 color_mode,
                                         MetaLogicalMonitorLayoutMode  layout_mode)
{
  MetaMonitorMode *mode;
  int width, height;
  MtkMonitorTransform transform;
  MetaMonitorConfig *monitor_config;
  MetaLogicalMonitorConfig *logical_monitor_config;

  mode = meta_monitor_get_preferred_mode (monitor);
  meta_monitor_mode_get_resolution (mode, &width, &height);
  scale_logical_monitor_width (layout_mode, scale,
                               width, height, &width, &height);

  monitor_config = create_monitor_config (monitor, mode, color_mode);

  transform = get_monitor_transform (monitor_manager, monitor);
  if (mtk_monitor_transform_is_rotated (transform))
    {
      int temp = width;
      width = height;
      height = temp;
    }

  logical_monitor_config = g_new0 (MetaLogicalMonitorConfig, 1);
  *logical_monitor_config = (MetaLogicalMonitorConfig) {
    .layout = (MtkRectangle) {
      .x = x,
      .y = y,
      .width = width,
      .height = height
    },
    .transform = transform,
    .scale = scale,
    .monitor_configs = g_list_append (NULL, monitor_config)
  };

  return logical_monitor_config;
}

static MetaLogicalMonitorConfig *
find_logical_monitor_config (MetaMonitorsConfig *config,
                             MetaMonitor        *monitor,
                             MetaMonitorMode    *monitor_mode)
{
  int mode_width, mode_height;
  GList *l;

  meta_monitor_mode_get_resolution (monitor_mode, &mode_width, &mode_height);

  for (l = config->logical_monitor_configs; l; l = l->next)
    {
      MetaLogicalMonitorConfig *logical_monitor_config = l->data;
      GList *l_monitor;

      for (l_monitor = logical_monitor_config->monitor_configs;
           l_monitor;
           l_monitor = l_monitor->next)
        {
          MetaMonitorConfig *monitor_config = l_monitor->data;
          MetaMonitorModeSpec *mode_spec =
            meta_monitor_mode_get_spec (monitor_mode);

          if (meta_monitor_spec_equals (meta_monitor_get_spec (monitor),
                                        monitor_config->monitor_spec) &&
              meta_monitor_mode_spec_has_similar_size (mode_spec,
                                                       monitor_config->mode_spec))
            return logical_monitor_config;
        }
    }

  return NULL;
}

static MetaMonitorConfig *
find_monitor_config (MetaMonitorsConfig *config,
                     MetaMonitor        *monitor)
{
  GList *l;

  for (l = config->logical_monitor_configs; l; l = l->next)
    {
      MetaLogicalMonitorConfig *logical_monitor_config = l->data;
      GList *l_monitor;

      for (l_monitor = logical_monitor_config->monitor_configs;
           l_monitor;
           l_monitor = l_monitor->next)
        {
          MetaMonitorConfig *monitor_config = l_monitor->data;

          if (meta_monitor_spec_equals (meta_monitor_get_spec (monitor),
                                        monitor_config->monitor_spec))
            return monitor_config;
        }
    }

  return NULL;
}

static GList *
get_relevant_configs (MetaMonitorConfigManager *config_manager)
{
  GList *configs = NULL;

  if (config_manager->current_config)
    configs = g_list_append (configs, config_manager->current_config);

  configs = g_list_concat (configs,
                           g_list_copy (config_manager->config_history.head));

  return configs;
}

static gboolean
get_last_scale_for_monitor (MetaMonitorConfigManager *config_manager,
                            MetaMonitor              *monitor,
                            MetaMonitorMode          *monitor_mode,
                            float                    *out_scale)
{
  g_autoptr (GList) configs = NULL;
  GList *l;

  configs = get_relevant_configs (config_manager);

  for (l = configs; l; l = l->next)
    {
      MetaMonitorsConfig *config = l->data;
      MetaLogicalMonitorConfig *logical_monitor_config;

      logical_monitor_config = find_logical_monitor_config (config,
                                                            monitor,
                                                            monitor_mode);
      if (logical_monitor_config)
        {
          *out_scale = logical_monitor_config->scale;
          return TRUE;
        }
    }

  return FALSE;
}

static float
compute_scale_for_monitor (MetaMonitorConfigManager *config_manager,
                           MetaMonitor              *monitor,
                           MetaMonitor              *primary_monitor)
{
  MetaMonitorManager *monitor_manager = config_manager->monitor_manager;
  MetaMonitor *target_monitor = monitor;
  MetaLogicalMonitorLayoutMode layout_mode;
  MetaMonitorMode *monitor_mode;
  float scale;

  if ((meta_monitor_manager_get_capabilities (monitor_manager) &
       META_MONITOR_MANAGER_CAPABILITY_GLOBAL_SCALE_REQUIRED) &&
      primary_monitor)
    target_monitor = primary_monitor;

  layout_mode = meta_monitor_manager_get_default_layout_mode (monitor_manager);
  monitor_mode = meta_monitor_get_preferred_mode (target_monitor);

  if (get_last_scale_for_monitor (config_manager,
                                  target_monitor, monitor_mode,
                                  &scale))
    return scale;

  return meta_monitor_manager_calculate_monitor_mode_scale (monitor_manager,
                                                            layout_mode,
                                                            target_monitor,
                                                            monitor_mode);
}

static gboolean
get_last_color_mode_for_monitor (MetaMonitorConfigManager *config_manager,
                                 MetaMonitor              *monitor,
                                 MetaColorMode            *out_color_mode)
{
  g_autoptr (GList) configs = NULL;
  GList *l;

  configs = get_relevant_configs (config_manager);

  for (l = configs; l; l = l->next)
    {
      MetaMonitorsConfig *config = l->data;
      MetaMonitorConfig *monitor_config;

      monitor_config = find_monitor_config (config, monitor);
      if (monitor_config)
        {
          MetaColorMode color_mode = monitor_config->color_mode;

          if (!meta_monitor_is_color_mode_supported (monitor, color_mode))
            continue;

          *out_color_mode = color_mode;
          return TRUE;
        }
    }

  return FALSE;
}

typedef enum _MonitorPositioningMode
{
  MONITOR_POSITIONING_LINEAR,
  MONITOR_POSITIONING_SUGGESTED,
} MonitorPositioningMode;

static gboolean
verify_suggested_monitors_config (GList *logical_monitor_configs)
{
  g_autoptr (GList) region = NULL;
  GList *l;

  for (l = logical_monitor_configs; l; l = l->next)
    {
      MetaLogicalMonitorConfig *logical_monitor_config = l->data;
      MtkRectangle *rect = &logical_monitor_config->layout;

      if (meta_rectangle_overlaps_with_region (region, rect))
        {
          g_warning ("Suggested monitor config has overlapping region, "
                      "rejecting");
          return FALSE;
        }

      region = g_list_prepend (region, rect);
    }

  for (l = region; region->next && l; l = l->next)
    {
      MtkRectangle *rect = l->data;

      if (!meta_rectangle_is_adjacent_to_any_in_region (region, rect))
        {
          g_warning ("Suggested monitor config has monitors with no "
                      "neighbors, rejecting");
          return FALSE;
        }
    }

  return TRUE;
}

static MetaMonitorsConfig *
create_monitors_config (MetaMonitorConfigManager *config_manager,
                        MonitorMatchRule          match_rule,
                        MonitorPositioningMode    positioning,
                        MetaMonitorsConfigFlag    config_flags)
{
  MetaMonitorManager *monitor_manager = config_manager->monitor_manager;
  g_autoptr (GList) monitors = NULL;
  g_autolist (MetaLogicalMonitorConfig) logical_monitor_configs = NULL;
  MetaMonitor *primary_monitor;
  MetaLogicalMonitorLayoutMode layout_mode;
  float scale;
  GList *l;
  int x, y;

  primary_monitor = find_primary_monitor (monitor_manager,
                                          match_rule | MONITOR_MATCH_VISIBLE);
  if (!primary_monitor)
    return NULL;

  x = y = 0;
  layout_mode = meta_monitor_manager_get_default_layout_mode (monitor_manager);

  if (!(match_rule & MONITOR_MATCH_PRIMARY))
    monitors = find_monitors (monitor_manager, match_rule, primary_monitor);

  /* The primary monitor needs to be at the head of the list for the
   * linear positioning to be correct.
   */
  monitors = g_list_prepend (monitors, primary_monitor);

  for (l = monitors; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;
      MetaLogicalMonitorConfig *logical_monitor_config;
      gboolean has_suggested_position;
      MetaColorMode color_mode;

      switch (positioning)
        {
        case MONITOR_POSITIONING_LINEAR:
          break;
        case MONITOR_POSITIONING_SUGGESTED:
          has_suggested_position =
            meta_monitor_get_suggested_position (monitor, &x, &y);
          g_assert (has_suggested_position);
          break;
        }

      scale = compute_scale_for_monitor (config_manager, monitor,
                                         primary_monitor);
      if (!get_last_color_mode_for_monitor (config_manager, monitor,
                                            &color_mode))
        color_mode = META_COLOR_MODE_DEFAULT;

      logical_monitor_config =
        create_preferred_logical_monitor_config (monitor_manager,
                                                 monitor,
                                                 x, y, scale,
                                                 color_mode,
                                                 layout_mode);
      logical_monitor_config->is_primary = (monitor == primary_monitor);
      logical_monitor_configs = g_list_append (logical_monitor_configs,
                                               logical_monitor_config);

      x += logical_monitor_config->layout.width;
    }

  if (positioning == MONITOR_POSITIONING_SUGGESTED)
    {
      if (!verify_suggested_monitors_config (logical_monitor_configs))
        return NULL;
    }

  return meta_monitors_config_new (monitor_manager,
                                   g_steal_pointer (&logical_monitor_configs),
                                   layout_mode,
                                   config_flags);
}

MetaMonitorsConfig *
meta_monitor_config_manager_create_linear (MetaMonitorConfigManager *config_manager)
{
  return create_monitors_config (config_manager,
                                 MONITOR_MATCH_VISIBLE |
                                 MONITOR_MATCH_ALLOW_FALLBACK,
                                 MONITOR_POSITIONING_LINEAR,
                                 META_MONITORS_CONFIG_FLAG_NONE);
}

MetaMonitorsConfig *
meta_monitor_config_manager_create_fallback (MetaMonitorConfigManager *config_manager)
{
  return create_monitors_config (config_manager,
                                 MONITOR_MATCH_PRIMARY |
                                 MONITOR_MATCH_ALLOW_FALLBACK,
                                 MONITOR_POSITIONING_LINEAR,
                                 META_MONITORS_CONFIG_FLAG_NONE);
}

MetaMonitorsConfig *
meta_monitor_config_manager_create_suggested (MetaMonitorConfigManager *config_manager)
{
  return create_monitors_config (config_manager,
                                 MONITOR_MATCH_WITH_SUGGESTED_POSITION,
                                 MONITOR_POSITIONING_SUGGESTED,
                                 META_MONITORS_CONFIG_FLAG_NONE);
}

static MetaLogicalMonitorConfig *
find_logical_config_for_builtin_monitor (MetaMonitorConfigManager *config_manager,
                                         GList                    *logical_monitor_configs)
{
  MetaLogicalMonitorConfig *logical_monitor_config;
  MetaMonitorConfig *monitor_config;
  MetaMonitor *monitor;
  GList *l;

  monitor =
    meta_monitor_manager_get_builtin_monitor (config_manager->monitor_manager);

  if (!monitor)
    return NULL;

  for (l = logical_monitor_configs; l; l = l->next)
    {
      logical_monitor_config = l->data;
      /*
       * We only want to return the config for the monitor if it is
       * configured on its own, so we skip configs which contain clones.
       */
      if (g_list_length (logical_monitor_config->monitor_configs) != 1)
        continue;

      monitor_config = logical_monitor_config->monitor_configs->data;
      if (meta_monitor_spec_equals (meta_monitor_get_spec (monitor),
                                    monitor_config->monitor_spec))
        {
          MetaMonitorMode *mode;

          mode = meta_monitor_get_mode_from_spec (monitor,
                                                  monitor_config->mode_spec);
          if (mode)
            return logical_monitor_config;
        }
    }

  return NULL;
}

static MetaMonitorsConfig *
create_for_builtin_display_rotation (MetaMonitorConfigManager *config_manager,
                                     MetaMonitorsConfig       *base_config,
                                     gboolean                  rotate,
                                     MtkMonitorTransform       transform)
{
  MetaMonitorManager *monitor_manager = config_manager->monitor_manager;
  MetaLogicalMonitorConfig *logical_monitor_config;
  MetaLogicalMonitorConfig *current_logical_monitor_config;
  MetaMonitorsConfig *config;
  GList *logical_monitor_configs, *current_configs;
  MetaLogicalMonitorLayoutMode layout_mode;

  g_return_val_if_fail (base_config, NULL);

  current_configs = base_config->logical_monitor_configs;
  current_logical_monitor_config =
    find_logical_config_for_builtin_monitor (config_manager, current_configs);
  if (!current_logical_monitor_config)
    return NULL;

  if (rotate)
    transform = (current_logical_monitor_config->transform + 1) % MTK_MONITOR_TRANSFORM_FLIPPED;
  else
    {
      /*
       * The transform coming from the accelerometer should be applied to
       * the crtc as is, without taking panel-orientation into account, this
       * is done so that non panel-orientation aware desktop environments do the
       * right thing. Mutter corrects for panel-orientation when applying the
       * transform from a logical-monitor-config, so we must convert here.
       */
      MetaMonitor *monitor =
        meta_monitor_manager_get_builtin_monitor (config_manager->monitor_manager);

      transform = meta_monitor_crtc_to_logical_transform (monitor, transform);
    }

  if (current_logical_monitor_config->transform == transform)
    return NULL;

  logical_monitor_configs =
    meta_clone_logical_monitor_config_list (base_config->logical_monitor_configs);
  logical_monitor_config =
    find_logical_config_for_builtin_monitor (config_manager,
                                             logical_monitor_configs);
  logical_monitor_config->transform = transform;

  if (mtk_monitor_transform_is_rotated (current_logical_monitor_config->transform) !=
      mtk_monitor_transform_is_rotated (logical_monitor_config->transform))
    {
      int temp = logical_monitor_config->layout.width;
      logical_monitor_config->layout.width = logical_monitor_config->layout.height;
      logical_monitor_config->layout.height = temp;
    }

  layout_mode = base_config->layout_mode;
  config = meta_monitors_config_new (monitor_manager,
                                     logical_monitor_configs,
                                     layout_mode,
                                     META_MONITORS_CONFIG_FLAG_NONE);
  meta_monitors_config_set_parent_config (config, base_config);

  return config;
}

MetaMonitorsConfig *
meta_monitor_config_manager_create_for_orientation (MetaMonitorConfigManager *config_manager,
                                                    MetaMonitorsConfig       *base_config,
                                                    MtkMonitorTransform       transform)
{
  return create_for_builtin_display_rotation (config_manager, base_config,
                                              FALSE, transform);
}

MetaMonitorsConfig *
meta_monitor_config_manager_create_for_builtin_orientation (MetaMonitorConfigManager *config_manager,
                                                            MetaMonitorsConfig       *base_config)
{
  MetaMonitorManager *monitor_manager = config_manager->monitor_manager;
  MtkMonitorTransform current_transform;
  MetaMonitor *monitor;

  g_return_val_if_fail (
    meta_monitor_manager_get_panel_orientation_managed (monitor_manager), NULL);

  monitor = meta_monitor_manager_get_builtin_monitor (monitor_manager);
  current_transform = get_monitor_transform (monitor_manager, monitor);

  return create_for_builtin_display_rotation (config_manager, base_config,
                                              FALSE, current_transform);
}

MetaMonitorsConfig *
meta_monitor_config_manager_create_for_rotate_monitor (MetaMonitorConfigManager *config_manager)
{
  if (!config_manager->current_config)
    return NULL;

  return create_for_builtin_display_rotation (config_manager,
                                              config_manager->current_config,
                                              TRUE,
                                              MTK_MONITOR_TRANSFORM_NORMAL);
}

static MetaMonitorsConfig *
create_monitors_switch_config (MetaMonitorConfigManager    *config_manager,
                               MonitorMatchRule             match_rule,
                               MonitorPositioningMode       positioning,
                               MetaMonitorsConfigFlag       config_flags,
                               MetaMonitorSwitchConfigType  switch_config)
{
  MetaMonitorsConfig *monitors_config;

  monitors_config = create_monitors_config (config_manager, match_rule,
                                            positioning, config_flags);

  if (!monitors_config)
    return NULL;

  meta_monitors_config_set_switch_config (monitors_config, switch_config);
  return monitors_config;
}

static MetaMonitorsConfig *
create_for_switch_config_all_mirror (MetaMonitorConfigManager *config_manager)
{
  MetaMonitorManager *monitor_manager = config_manager->monitor_manager;
  MetaMonitor *primary_monitor;
  MetaLogicalMonitorLayoutMode layout_mode;
  MetaLogicalMonitorConfig *logical_monitor_config = NULL;
  GList *logical_monitor_configs;
  GList *monitor_configs = NULL;
  gint common_mode_w = 0, common_mode_h = 0;
  float best_scale = 1.0;
  MetaMonitor *monitor;
  GList *modes;
  GList *monitors;
  GList *l;
  MetaMonitorsConfig *monitors_config;
  int width, height;

  primary_monitor = find_primary_monitor (monitor_manager,
                                          MONITOR_MATCH_ALLOW_FALLBACK);
  if (!primary_monitor)
    return NULL;

  layout_mode = meta_monitor_manager_get_default_layout_mode (monitor_manager);
  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  monitor = monitors->data;
  modes = meta_monitor_get_modes (monitor);
  for (l = modes; l; l = l->next)
    {
      MetaMonitorMode *mode = l->data;
      gboolean common_mode_size = TRUE;
      gint mode_w, mode_h;
      GList *ll;

      meta_monitor_mode_get_resolution (mode, &mode_w, &mode_h);

      for (ll = monitors->next; ll; ll = ll->next)
        {
          MetaMonitor *monitor_b = ll->data;
          gboolean have_same_mode_size = FALSE;
          GList *mm;

          for (mm = meta_monitor_get_modes (monitor_b); mm; mm = mm->next)
            {
              MetaMonitorMode *mode_b = mm->data;
              gint mode_b_w, mode_b_h;

              meta_monitor_mode_get_resolution (mode_b, &mode_b_w, &mode_b_h);

              if (mode_w == mode_b_w &&
                  mode_h == mode_b_h)
                {
                  have_same_mode_size = TRUE;
                  break;
                }
            }

          if (!have_same_mode_size)
            {
              common_mode_size = FALSE;
              break;
            }
        }

      if (common_mode_size &&
          common_mode_w * common_mode_h < mode_w * mode_h)
        {
          common_mode_w = mode_w;
          common_mode_h = mode_h;
        }
    }

  if (common_mode_w == 0 || common_mode_h == 0)
    return NULL;

  for (l = monitors; l; l = l->next)
    {
      MetaMonitor *other_monitor = l->data;
      MetaMonitorMode *mode = NULL;
      MetaColorMode color_mode;
      GList *ll;
      float scale;

      for (ll = meta_monitor_get_modes (other_monitor); ll; ll = ll->next)
        {
          gint mode_w, mode_h;

          mode = ll->data;
          meta_monitor_mode_get_resolution (mode, &mode_w, &mode_h);

          if (mode_w == common_mode_w && mode_h == common_mode_h)
            break;
        }

      if (!mode)
        continue;

      scale = compute_scale_for_monitor (config_manager, other_monitor,
                                         primary_monitor);
      best_scale = MAX (best_scale, scale);

      if (!get_last_color_mode_for_monitor (config_manager, monitor,
                                            &color_mode))
        color_mode = META_COLOR_MODE_DEFAULT;

      monitor_configs =
        g_list_prepend (monitor_configs,
                        create_monitor_config (other_monitor,
                                               mode,
                                               color_mode));
    }

  scale_logical_monitor_width (layout_mode, best_scale,
                               common_mode_w, common_mode_h,
                               &width, &height);

  logical_monitor_config = g_new0 (MetaLogicalMonitorConfig, 1);
  *logical_monitor_config = (MetaLogicalMonitorConfig) {
    .layout = MTK_RECTANGLE_INIT (0, 0, width, height),
    .scale = best_scale,
    .monitor_configs = monitor_configs,
    .is_primary = TRUE,
  };

  logical_monitor_configs = g_list_append (NULL, logical_monitor_config);
  monitors_config = meta_monitors_config_new (monitor_manager,
                                              logical_monitor_configs,
                                              layout_mode,
                                              META_MONITORS_CONFIG_FLAG_NONE);

  if (monitors_config)
    meta_monitors_config_set_switch_config (monitors_config, META_MONITOR_SWITCH_CONFIG_ALL_MIRROR);

  return monitors_config;
}

static MetaMonitorsConfig *
create_for_switch_config_external (MetaMonitorConfigManager *config_manager)
{
  return create_monitors_switch_config (config_manager,
                                        MONITOR_MATCH_EXTERNAL,
                                        MONITOR_POSITIONING_LINEAR,
                                        META_MONITORS_CONFIG_FLAG_NONE,
                                        META_MONITOR_SWITCH_CONFIG_EXTERNAL);
}

static MetaMonitorsConfig *
create_for_switch_config_builtin (MetaMonitorConfigManager *config_manager)
{
  return create_monitors_switch_config (config_manager,
                                        MONITOR_MATCH_BUILTIN,
                                        MONITOR_POSITIONING_LINEAR,
                                        META_MONITORS_CONFIG_FLAG_NONE,
                                        META_MONITOR_SWITCH_CONFIG_BUILTIN);
}

MetaMonitorsConfig *
meta_monitor_config_manager_create_for_switch_config (MetaMonitorConfigManager    *config_manager,
                                                      MetaMonitorSwitchConfigType  config_type)
{
  MetaMonitorManager *monitor_manager = config_manager->monitor_manager;
  MetaMonitorsConfig *config;

  if (!meta_monitor_manager_can_switch_config (monitor_manager))
    return NULL;

  switch (config_type)
    {
    case META_MONITOR_SWITCH_CONFIG_ALL_MIRROR:
      config = create_for_switch_config_all_mirror (config_manager);
      break;
    case META_MONITOR_SWITCH_CONFIG_ALL_LINEAR:
      config = meta_monitor_config_manager_create_linear (config_manager);
      break;
    case META_MONITOR_SWITCH_CONFIG_EXTERNAL:
      config = create_for_switch_config_external (config_manager);
      break;
    case META_MONITOR_SWITCH_CONFIG_BUILTIN:
      config = create_for_switch_config_builtin (config_manager);
      break;
    case META_MONITOR_SWITCH_CONFIG_UNKNOWN:
    default:
      g_warn_if_reached ();
      return NULL;
    }

  return config;
}

static MetaMonitorsConfig *
get_root_config (MetaMonitorsConfig *config)
{
  if (!config->parent_config)
    return config;

  return get_root_config (config->parent_config);
}

static gboolean
has_same_root_config (MetaMonitorsConfig *config_a,
                      MetaMonitorsConfig *config_b)
{
  return get_root_config (config_a) == get_root_config (config_b);
}

void
meta_monitor_config_manager_set_current (MetaMonitorConfigManager *config_manager,
                                         MetaMonitorsConfig       *config)
{
  MetaMonitorsConfig *current_config = config_manager->current_config;
  gboolean overrides_current = FALSE;

  if (config && current_config &&
      has_same_root_config (config, current_config))
    {
      overrides_current = meta_monitors_config_key_equal (config->key,
                                                          current_config->key);
    }

  if (current_config && !overrides_current)
    {
      g_queue_push_head (&config_manager->config_history,
                         g_object_ref (config_manager->current_config));
      if (g_queue_get_length (&config_manager->config_history) >
          CONFIG_HISTORY_MAX_SIZE)
        g_object_unref (g_queue_pop_tail (&config_manager->config_history));
    }

  g_set_object (&config_manager->current_config, config);
}

void
meta_monitor_config_manager_save_current (MetaMonitorConfigManager *config_manager)
{
  g_return_if_fail (config_manager->current_config);

  meta_monitor_config_store_add (config_manager->config_store,
                                 config_manager->current_config);
}

MetaMonitorsConfig *
meta_monitor_config_manager_get_current (MetaMonitorConfigManager *config_manager)
{
  return config_manager->current_config;
}

MetaMonitorsConfig *
meta_monitor_config_manager_pop_previous (MetaMonitorConfigManager *config_manager)
{
  return g_queue_pop_head (&config_manager->config_history);
}

MetaMonitorsConfig *
meta_monitor_config_manager_get_previous (MetaMonitorConfigManager *config_manager)
{
  return g_queue_peek_head (&config_manager->config_history);
}

void
meta_monitor_config_manager_clear_history (MetaMonitorConfigManager *config_manager)
{
  g_queue_foreach (&config_manager->config_history, (GFunc) g_object_unref, NULL);
  g_queue_clear (&config_manager->config_history);
}

static void
meta_monitor_config_manager_dispose (GObject *object)
{
  MetaMonitorConfigManager *config_manager =
    META_MONITOR_CONFIG_MANAGER (object);

  g_clear_object (&config_manager->current_config);
  meta_monitor_config_manager_clear_history (config_manager);

  G_OBJECT_CLASS (meta_monitor_config_manager_parent_class)->dispose (object);
}

static void
meta_monitor_config_manager_init (MetaMonitorConfigManager *config_manager)
{
  g_queue_init (&config_manager->config_history);
}

static void
meta_monitor_config_manager_class_init (MetaMonitorConfigManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_monitor_config_manager_dispose;
}

void
meta_monitor_config_free (MetaMonitorConfig *monitor_config)
{
  if (monitor_config->monitor_spec)
    meta_monitor_spec_free (monitor_config->monitor_spec);
  g_free (monitor_config->mode_spec);
  g_free (monitor_config);
}

void
meta_logical_monitor_config_free (MetaLogicalMonitorConfig *logical_monitor_config)
{
  g_list_free_full (logical_monitor_config->monitor_configs,
                    (GDestroyNotify) meta_monitor_config_free);
  g_free (logical_monitor_config);
}

static MetaMonitorsConfigKey *
meta_monitors_config_key_new (GList                        *logical_monitor_configs,
                              GList                        *disabled_monitor_specs,
                              MetaLogicalMonitorLayoutMode  layout_mode)
{
  MetaMonitorsConfigKey *config_key;
  GList *monitor_specs;
  GList *l;

  monitor_specs = NULL;
  for (l = logical_monitor_configs; l; l = l->next)
    {
      MetaLogicalMonitorConfig *logical_monitor_config = l->data;
      GList *k;

      for (k = logical_monitor_config->monitor_configs; k; k = k->next)
        {
          MetaMonitorConfig *monitor_config = k->data;
          MetaMonitorSpec *monitor_spec;

          monitor_spec = meta_monitor_spec_clone (monitor_config->monitor_spec);
          monitor_specs = g_list_prepend (monitor_specs, monitor_spec);
        }
    }

  /* Monitors for lease must be disabled (see meta_verify_monitors_config ()).
     Therefore, there is no need to include them here. */
  for (l = disabled_monitor_specs; l; l = l->next)
    {
      MetaMonitorSpec *monitor_spec = l->data;

      monitor_spec = meta_monitor_spec_clone (monitor_spec);
      monitor_specs = g_list_prepend (monitor_specs, monitor_spec);
    }

  monitor_specs = g_list_sort (monitor_specs,
                               (GCompareFunc) meta_monitor_spec_compare);

  config_key = g_new0 (MetaMonitorsConfigKey, 1);
  *config_key = (MetaMonitorsConfigKey) {
    .monitor_specs = monitor_specs,
    .layout_mode = layout_mode
  };

  return config_key;
}

void
meta_monitors_config_key_free (MetaMonitorsConfigKey *config_key)
{
  g_list_free_full (config_key->monitor_specs,
                    (GDestroyNotify) meta_monitor_spec_free);
  g_free (config_key);
}

unsigned int
meta_monitors_config_key_hash (gconstpointer data)
{
  const MetaMonitorsConfigKey *config_key = data;
  GList *l;
  unsigned long hash;

  hash = config_key->layout_mode;
  for (l = config_key->monitor_specs; l; l = l->next)
    {
      MetaMonitorSpec *monitor_spec = l->data;

      hash ^= (g_str_hash (monitor_spec->connector) ^
               g_str_hash (monitor_spec->vendor) ^
               g_str_hash (monitor_spec->product) ^
               g_str_hash (monitor_spec->serial));
    }

  return hash;
}

gboolean
meta_monitors_config_key_equal (gconstpointer data_a,
                                gconstpointer data_b)
{
  const MetaMonitorsConfigKey *config_key_a = data_a;
  const MetaMonitorsConfigKey *config_key_b = data_b;
  GList *l_a, *l_b;

  if (config_key_a->layout_mode != config_key_b->layout_mode)
    return FALSE;

  for (l_a = config_key_a->monitor_specs, l_b = config_key_b->monitor_specs;
       l_a && l_b;
       l_a = l_a->next, l_b = l_b->next)
    {
      MetaMonitorSpec *monitor_spec_a = l_a->data;
      MetaMonitorSpec *monitor_spec_b = l_b->data;

      if (!meta_monitor_spec_equals (monitor_spec_a, monitor_spec_b))
        return FALSE;
    }

  if (l_a || l_b)
    return FALSE;

  return TRUE;
}

MetaMonitorSwitchConfigType
meta_monitors_config_get_switch_config (MetaMonitorsConfig *config)
{
  return config->switch_config;
}

void
meta_monitors_config_set_switch_config (MetaMonitorsConfig          *config,
                                        MetaMonitorSwitchConfigType  switch_config)
{
  config->switch_config = switch_config;
}

void
meta_monitors_config_set_parent_config (MetaMonitorsConfig *config,
                                        MetaMonitorsConfig *parent_config)
{
  g_assert (config != parent_config);
  g_assert (!parent_config || parent_config->parent_config != config);

  g_set_object (&config->parent_config, parent_config);
}

MetaMonitorsConfig *
meta_monitors_config_new_full (GList                        *logical_monitor_configs,
                               GList                        *disabled_monitor_specs,
                               GList                        *for_lease_monitor_specs,
                               MetaLogicalMonitorLayoutMode  layout_mode,
                               MetaMonitorsConfigFlag        flags)
{
  MetaMonitorsConfig *config;

  config = g_object_new (META_TYPE_MONITORS_CONFIG, NULL);
  config->logical_monitor_configs = logical_monitor_configs;
  config->disabled_monitor_specs = disabled_monitor_specs;
  config->for_lease_monitor_specs = for_lease_monitor_specs;
  config->layout_mode = layout_mode;
  config->key = meta_monitors_config_key_new (logical_monitor_configs,
                                              disabled_monitor_specs,
                                              layout_mode);

  config->flags = flags;
  config->switch_config = META_MONITOR_SWITCH_CONFIG_UNKNOWN;

  return config;
}

MetaMonitorsConfig *
meta_monitors_config_new (MetaMonitorManager           *monitor_manager,
                          GList                        *logical_monitor_configs,
                          MetaLogicalMonitorLayoutMode  layout_mode,
                          MetaMonitorsConfigFlag        flags)
{
  GList *disabled_monitor_specs = NULL;
  GList *for_lease_monitor_specs = NULL;
  GList *monitors;
  GList *l;

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  for (l = monitors; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;
      MetaMonitorSpec *monitor_spec;

      if (meta_logical_monitor_configs_have_visible_monitor (monitor_manager,
                                                             logical_monitor_configs,
                                                             monitor))
        continue;

      monitor_spec = meta_monitor_get_spec (monitor);

      disabled_monitor_specs =
        g_list_prepend (disabled_monitor_specs,
                        meta_monitor_spec_clone (monitor_spec));

      if (meta_monitor_is_for_lease (monitor))
        {
          for_lease_monitor_specs =
            g_list_prepend (for_lease_monitor_specs,
                            meta_monitor_spec_clone (monitor_spec));
        }
    }

  return meta_monitors_config_new_full (logical_monitor_configs,
                                        disabled_monitor_specs,
                                        for_lease_monitor_specs,
                                        layout_mode,
                                        flags);
}

static void
meta_monitors_config_finalize (GObject *object)
{
  MetaMonitorsConfig *config = META_MONITORS_CONFIG (object);

  g_clear_object (&config->parent_config);
  meta_monitors_config_key_free (config->key);
  g_list_free_full (config->logical_monitor_configs,
                    (GDestroyNotify) meta_logical_monitor_config_free);
  g_list_free_full (config->disabled_monitor_specs,
                    (GDestroyNotify) meta_monitor_spec_free);
  g_list_free_full (config->for_lease_monitor_specs,
                    (GDestroyNotify) meta_monitor_spec_free);

  G_OBJECT_CLASS (meta_monitors_config_parent_class)->finalize (object);
}

static void
meta_monitors_config_init (MetaMonitorsConfig *config)
{
}

static void
meta_monitors_config_class_init (MetaMonitorsConfigClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_monitors_config_finalize;
}

static void
meta_crtc_assignment_free (MetaCrtcAssignment *assignment)
{
  g_clear_pointer (&assignment->backend_private,
                   assignment->backend_private_destroy);
  g_ptr_array_free (assignment->outputs, TRUE);
  g_free (assignment);
}

static void
meta_output_assignment_free (MetaOutputAssignment *assignment)
{
  g_free (assignment);
}

gboolean
meta_verify_monitor_mode_spec (MetaMonitorModeSpec *monitor_mode_spec,
                               GError             **error)
{
  if (monitor_mode_spec->width > 0 &&
      monitor_mode_spec->height > 0 &&
      monitor_mode_spec->refresh_rate > 0.0f)
    {
      return TRUE;
    }
  else
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Monitor mode invalid");
      return FALSE;
    }
}

gboolean
meta_verify_monitor_spec (MetaMonitorSpec *monitor_spec,
                          GError         **error)
{
  if (monitor_spec->connector &&
      monitor_spec->vendor &&
      monitor_spec->product &&
      monitor_spec->serial)
    {
      return TRUE;
    }
  else
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Monitor spec incomplete");
      return FALSE;
    }
}

gboolean
meta_verify_monitor_config (MetaMonitorConfig *monitor_config,
                            GError           **error)
{
  if (monitor_config->monitor_spec && monitor_config->mode_spec)
    {
      return TRUE;
    }
  else
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Monitor config incomplete");
      return FALSE;
    }
}

gboolean
meta_verify_logical_monitor_config (MetaLogicalMonitorConfig    *logical_monitor_config,
                                    MetaLogicalMonitorLayoutMode layout_mode,
                                    MetaMonitorManager          *monitor_manager,
                                    GError                     **error)
{
  GList *l;
  int layout_width, layout_height;
  MetaMonitorConfig *first_monitor_config;
  int mode_width, mode_height;
  int expected_mode_width = 0, expected_mode_height = 0;
  float scale = logical_monitor_config->scale;

  if (logical_monitor_config->layout.x < 0 ||
      logical_monitor_config->layout.y < 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Invalid logical monitor position (%d, %d)",
                   logical_monitor_config->layout.x,
                   logical_monitor_config->layout.y);
      return FALSE;
    }

  if (!logical_monitor_config->monitor_configs)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Logical monitor is empty");
      return FALSE;
    }

  first_monitor_config = logical_monitor_config->monitor_configs->data;
  mode_width = first_monitor_config->mode_spec->width;
  mode_height = first_monitor_config->mode_spec->height;

  for (l = logical_monitor_config->monitor_configs; l; l = l->next)
    {
      MetaMonitorConfig *monitor_config = l->data;

      if (monitor_config->mode_spec->width != mode_width ||
          monitor_config->mode_spec->height != mode_height)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Monitors modes in logical monitor not equal");
          return FALSE;
        }
    }

  if (mtk_monitor_transform_is_rotated (logical_monitor_config->transform))
    {
      layout_width = logical_monitor_config->layout.height;
      layout_height = logical_monitor_config->layout.width;
    }
  else
    {
      layout_width = logical_monitor_config->layout.width;
      layout_height = logical_monitor_config->layout.height;
    }

  switch (layout_mode)
    {
    case META_LOGICAL_MONITOR_LAYOUT_MODE_LOGICAL:
      {
        float scaled_width = mode_width / scale;
        float scaled_height = mode_height / scale;

        if (floorf (scaled_width) != scaled_width ||
            floorf (scaled_height) != scaled_height)
          {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                         "Scaled logical monitor size is fractional");
            return FALSE;
          }

        expected_mode_width = (int) roundf (layout_width * scale);
        expected_mode_height = (int) roundf (layout_height * scale);
        break;
      }

    case META_LOGICAL_MONITOR_LAYOUT_MODE_PHYSICAL:
      if (!G_APPROX_VALUE (scale, roundf (scale), FLT_EPSILON))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "A fractional scale with physical layout mode not allowed");
          return FALSE;
        }

      expected_mode_width = layout_width;
      expected_mode_height = layout_height;
      break;
    }

  if (mode_width != expected_mode_width || mode_height != expected_mode_height)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Monitor mode size doesn't match scaled monitor layout");
      return FALSE;
    }

  return TRUE;
}

gboolean
meta_logical_monitor_configs_have_monitor (GList           *logical_monitor_configs,
                                           MetaMonitorSpec *monitor_spec)
{
  GList *l;

  for (l = logical_monitor_configs; l; l = l->next)
    {
      MetaLogicalMonitorConfig *logical_monitor_config = l->data;
      GList *k;

      for (k = logical_monitor_config->monitor_configs; k; k = k->next)
        {
          MetaMonitorConfig *monitor_config = k->data;

          if (meta_monitor_spec_equals (monitor_spec,
                                        monitor_config->monitor_spec))
            return TRUE;
        }
    }

  return FALSE;
}

gboolean
meta_logical_monitor_configs_have_visible_monitor (MetaMonitorManager *monitor_manager,
                                                   GList              *logical_monitor_configs,
                                                   MetaMonitor        *monitor)
{
  MetaMonitorSpec *monitor_spec;

  if (!monitor_matches_rule (monitor, monitor_manager, MONITOR_MATCH_VISIBLE))
    return TRUE;

  monitor_spec = meta_monitor_get_spec (monitor);

  return meta_logical_monitor_configs_have_monitor (logical_monitor_configs,
                                                    monitor_spec);
}

static gboolean
meta_monitors_config_is_monitor_enabled (MetaMonitorsConfig *config,
                                         MetaMonitorSpec    *monitor_spec)
{
  return meta_logical_monitor_configs_have_monitor (config->logical_monitor_configs,
                                                    monitor_spec);
}

gboolean
meta_verify_monitors_config (MetaMonitorsConfig *config,
                             MetaMonitorManager *monitor_manager,
                             GError            **error)
{
  GList *l;

  if (!meta_verify_logical_monitor_config_list (config->logical_monitor_configs,
                                                config->layout_mode,
                                                monitor_manager,
                                                error))
    return FALSE;

  for (l = config->disabled_monitor_specs; l; l = l->next)
    {
      MetaMonitorSpec *monitor_spec = l->data;

      if (meta_monitors_config_is_monitor_enabled (config, monitor_spec))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Assigned monitor explicitly disabled");
          return FALSE;
        }
    }

  for (l = config->for_lease_monitor_specs; l; l = l->next)
    {
      MetaMonitorSpec *monitor_spec = l->data;
      gpointer disabled = NULL;

      disabled = g_list_find_custom (config->disabled_monitor_specs,
                                     monitor_spec,
                                     (GCompareFunc) meta_monitor_spec_compare);
      if (!disabled)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "For lease monitor must be explicitly disabled");
          return FALSE;
        }
    }

  return TRUE;
}
