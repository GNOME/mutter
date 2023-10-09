/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001, 2002 Havoc Pennington
 * Copyright (C) 2002, 2003 Red Hat Inc.
 * Some ICCCM manager selection code derived from fvwm2,
 * Copyright (C) 2001 Dominik Vogt, Matthias Clasen, and fvwm2 team
 * Copyright (C) 2003 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
 * Copyright (C) 2013 Red Hat Inc.
 * Copyright (C) 2020 NVIDIA CORPORATION
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

/**
 * MetaMonitorManager:
 *
 * A manager for multiple monitors
 *
 * #MetaMonitorManager is an abstract class which contains methods to handle
 * multiple monitors (both #MetaMonitor and #MetaLogicalMonitor) and GPU's
 * (#MetaGpu). Its functions include reading and/or changing the current
 * configuration and available capabiliies.
 *
 * The #MetaMonitorManager also provides the "org.gnome.Mutter.DisplayConfig"
 * DBus service, so apps like GNOME Settings can use this functionality.
 */

#include "config.h"

#include "backends/meta-monitor-manager-private.h"

#include <string.h>
#include <math.h>
#include <stdlib.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-crtc.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor.h"
#include "backends/meta-monitor-config-manager.h"
#include "backends/meta-monitor-config-store.h"
#include "backends/meta-orientation-manager.h"
#include "backends/meta-output.h"
#include "backends/meta-virtual-monitor.h"
#include "backends/x11/meta-monitor-manager-xrandr.h"
#include "clutter/clutter.h"
#include "core/util-private.h"
#include "meta/main.h"
#include "meta/meta-enum-types.h"
#include "meta/meta-x11-errors.h"

#include "meta-dbus-display-config.h"

#define DEFAULT_DISPLAY_CONFIGURATION_TIMEOUT 20

enum
{
  PROP_0,

  PROP_BACKEND,
  PROP_PANEL_ORIENTATION_MANAGED,
  PROP_HAS_BUILTIN_PANEL,
  PROP_NIGHT_LIGHT_SUPPORTED,
  PROP_EXPERIMENTAL_HDR,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

enum
{
  MONITORS_CHANGED,
  MONITORS_CHANGED_INTERNAL,
  POWER_SAVE_MODE_CHANGED,
  CONFIRM_DISPLAY_CHANGE,
  MONITOR_PRIVACY_SCREEN_CHANGED,
  SIGNALS_LAST
};

/* Array index matches MetaMonitorTransform */
static gfloat transform_matrices[][6] = {
  {  1,  0,  0,  0,  1,  0 }, /* normal */
  {  0, -1,  1,  1,  0,  0 }, /* 90° */
  { -1,  0,  1,  0, -1,  1 }, /* 180° */
  {  0,  1,  0, -1,  0,  1 }, /* 270° */
  { -1,  0,  1,  0,  1,  0 }, /* normal flipped */
  {  0,  1,  0,  1,  0,  0 }, /* 90° flipped */
  {  1,  0,  0,  0, -1,  1 }, /* 180° flipped */
  {  0, -1,  1, -1,  0,  1 }, /* 270° flipped */
};

static int signals[SIGNALS_LAST];

typedef struct _MetaMonitorManagerPrivate
{
  MetaPowerSave power_save_mode;
  gboolean      initial_orient_change_done;

  GList *virtual_monitors;

  gboolean shutting_down;

  gboolean has_builtin_panel;
  gboolean night_light_supported;
  const char *experimental_hdr;

  guint switch_config_handle_id;
} MetaMonitorManagerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaMonitorManager, meta_monitor_manager,
                            G_TYPE_OBJECT)

static void initialize_dbus_interface (MetaMonitorManager *manager);
static void monitor_manager_setup_dbus_config_handlers (MetaMonitorManager *manager);

static gboolean
meta_monitor_manager_is_config_complete (MetaMonitorManager *manager,
                                         MetaMonitorsConfig *config);

static void
meta_monitor_manager_real_read_current_state (MetaMonitorManager *manager);

static gboolean
is_global_scale_matching_in_config (MetaMonitorsConfig *config,
                                    float               scale);

MetaBackend *
meta_monitor_manager_get_backend (MetaMonitorManager *manager)
{
  return manager->backend;
}

static void
meta_monitor_manager_init (MetaMonitorManager *manager)
{
}

static void
meta_monitor_manager_set_primary_logical_monitor (MetaMonitorManager *manager,
                                                  MetaLogicalMonitor *logical_monitor)
{
  manager->primary_logical_monitor = logical_monitor;
  if (logical_monitor)
    meta_logical_monitor_make_primary (logical_monitor);
}

static gboolean
is_main_tiled_monitor_output (MetaOutput *output)
{
  const MetaOutputInfo *output_info = meta_output_get_info (output);

  return (output_info->tile_info.loc_h_tile == 0 &&
          output_info->tile_info.loc_v_tile == 0);
}

static MetaLogicalMonitor *
logical_monitor_from_layout (MetaMonitorManager *manager,
                             GList              *logical_monitors,
                             MtkRectangle       *layout)
{
  GList *l;

  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;

      if (mtk_rectangle_equal (layout, &logical_monitor->rect))
        return logical_monitor;
    }

  return NULL;
}

static void
meta_monitor_manager_rebuild_logical_monitors (MetaMonitorManager *manager,
                                               MetaMonitorsConfig *config)
{
  GList *logical_monitor_configs;
  GList *logical_monitors = NULL;
  GList *l;
  int monitor_number = 0;
  MetaLogicalMonitor *primary_logical_monitor = NULL;

  logical_monitor_configs = config ? config->logical_monitor_configs : NULL;
  for (l = logical_monitor_configs; l; l = l->next)
    {
      MetaLogicalMonitorConfig *logical_monitor_config = l->data;
      MetaLogicalMonitor *logical_monitor;

      logical_monitor = meta_logical_monitor_new (manager,
                                                  logical_monitor_config,
                                                  monitor_number);
      monitor_number++;

      if (logical_monitor_config->is_primary)
        primary_logical_monitor = logical_monitor;

      logical_monitors = g_list_append (logical_monitors, logical_monitor);
    }

  /*
   * If no monitor was marked as primary, fall back on marking the first
   * logical monitor the primary one.
   */
  if (!primary_logical_monitor && logical_monitors)
    primary_logical_monitor = g_list_first (logical_monitors)->data;

  manager->logical_monitors = logical_monitors;
  meta_monitor_manager_set_primary_logical_monitor (manager,
                                                    primary_logical_monitor);
}

static float
derive_configured_global_scale (MetaMonitorManager *manager,
                                MetaMonitorsConfig *config)
{
  GList *l;

  for (l = config->logical_monitor_configs; l; l = l->next)
    {
      MetaLogicalMonitorConfig *monitor_config = l->data;

      if (is_global_scale_matching_in_config (config, monitor_config->scale))
        return monitor_config->scale;
    }

  return 1.0;
}

static float
calculate_monitor_scale (MetaMonitorManager *manager,
                         MetaMonitor        *monitor)
{
  MetaMonitorMode *monitor_mode;

  monitor_mode = meta_monitor_get_current_mode (monitor);
  return meta_monitor_manager_calculate_monitor_mode_scale (manager,
                                                            manager->layout_mode,
                                                            monitor,
                                                            monitor_mode);
}

static gboolean
meta_monitor_manager_is_scale_supported_by_other_monitors (MetaMonitorManager *manager,
                                                           MetaMonitor        *not_this_one,
                                                           float               scale)
{
  GList *l;

  for (l = manager->monitors; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;
      MetaMonitorMode *mode;

      if (monitor == not_this_one || !meta_monitor_is_active (monitor))
        continue;

      mode = meta_monitor_get_current_mode (monitor);
      if (!meta_monitor_manager_is_scale_supported (manager,
                                                    manager->layout_mode,
                                                    monitor, mode, scale))
        return FALSE;
    }

  return TRUE;
}

static float
derive_calculated_global_scale (MetaMonitorManager *manager)
{
  MetaMonitor *monitor = NULL;
  float scale;
  GList *l;

  scale = 1.0;
  monitor = meta_monitor_manager_get_primary_monitor (manager);

  if (monitor && meta_monitor_is_active (monitor))
    {
      scale = calculate_monitor_scale (manager, monitor);
      if (meta_monitor_manager_is_scale_supported_by_other_monitors (manager,
                                                                     monitor,
                                                                     scale))
        return scale;
    }

  for (l = manager->monitors; l; l = l->next)
    {
      MetaMonitor *other_monitor = l->data;
      float monitor_scale;

      if (other_monitor == monitor || !meta_monitor_is_active (other_monitor))
        continue;

      monitor_scale = calculate_monitor_scale (manager, other_monitor);
      if (meta_monitor_manager_is_scale_supported_by_other_monitors (manager,
                                                                     other_monitor,
                                                                     monitor_scale))
        scale = MAX (scale, monitor_scale);
    }

  return scale;
}

static float
derive_scale_from_config (MetaMonitorManager *manager,
                          MetaMonitorsConfig *config,
                          MtkRectangle       *layout)
{
  GList *l;

  for (l = config->logical_monitor_configs; l; l = l->next)
    {
      MetaLogicalMonitorConfig *logical_monitor_config = l->data;

      if (mtk_rectangle_equal (layout, &logical_monitor_config->layout))
        return logical_monitor_config->scale;
    }

  g_warning ("Missing logical monitor, using scale 1");
  return 1.0;
}

static void
meta_monitor_manager_rebuild_logical_monitors_derived (MetaMonitorManager *manager,
                                                       MetaMonitorsConfig *config)
{
  GList *logical_monitors = NULL;
  GList *l;
  int monitor_number;
  MetaLogicalMonitor *primary_logical_monitor = NULL;
  gboolean use_global_scale;
  float global_scale = 0.0;
  MetaMonitorManagerCapability capabilities;

  monitor_number = 0;

  capabilities = meta_monitor_manager_get_capabilities (manager);
  use_global_scale =
    !!(capabilities & META_MONITOR_MANAGER_CAPABILITY_GLOBAL_SCALE_REQUIRED);

  if (use_global_scale)
    {
      if (config)
        global_scale = derive_configured_global_scale (manager, config);
      else
        global_scale = derive_calculated_global_scale (manager);
    }

  for (l = manager->monitors; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;
      MetaLogicalMonitor *logical_monitor;
      MtkRectangle layout;

      if (!meta_monitor_is_active (monitor))
        continue;

      meta_monitor_derive_layout (monitor, &layout);
      logical_monitor = logical_monitor_from_layout (manager, logical_monitors,
                                                     &layout);
      if (logical_monitor)
        {
          meta_logical_monitor_add_monitor (logical_monitor, monitor);
        }
      else
        {
          float scale;

          if (use_global_scale)
            scale = global_scale;
          else if (config)
            scale = derive_scale_from_config (manager, config, &layout);
          else
            scale = calculate_monitor_scale (manager, monitor);

          g_assert (scale > 0);

          logical_monitor = meta_logical_monitor_new_derived (manager,
                                                              monitor,
                                                              &layout,
                                                              scale,
                                                              monitor_number);
          logical_monitors = g_list_append (logical_monitors, logical_monitor);
          monitor_number++;
        }

      if (meta_monitor_is_primary (monitor))
        primary_logical_monitor = logical_monitor;
    }

  manager->logical_monitors = logical_monitors;

  /*
   * If no monitor was marked as primary, fall back on marking the first
   * logical monitor the primary one.
   */
  if (!primary_logical_monitor && manager->logical_monitors)
    primary_logical_monitor = g_list_first (manager->logical_monitors)->data;

  meta_monitor_manager_set_primary_logical_monitor (manager,
                                                    primary_logical_monitor);
}

void
meta_monitor_manager_power_save_mode_changed (MetaMonitorManager        *manager,
                                              MetaPowerSave              mode,
                                              MetaPowerSaveChangeReason  reason)
{
  MetaMonitorManagerPrivate *priv =
    meta_monitor_manager_get_instance_private (manager);

  if (priv->power_save_mode == mode)
    return;

  priv->power_save_mode = mode;
  g_signal_emit (manager, signals[POWER_SAVE_MODE_CHANGED], 0, reason);
}

static void
power_save_mode_changed (MetaMonitorManager *manager,
                         GParamSpec         *pspec,
                         gpointer            user_data)
{
  MetaMonitorManagerPrivate *priv =
    meta_monitor_manager_get_instance_private (manager);
  MetaMonitorManagerClass *klass;
  int mode = meta_dbus_display_config_get_power_save_mode (manager->display_config);
  MetaPowerSaveChangeReason reason;

  if (mode == META_POWER_SAVE_UNSUPPORTED)
    return;

  /* If DPMS is unsupported, force the property back. */
  if (priv->power_save_mode == META_POWER_SAVE_UNSUPPORTED)
    {
      meta_dbus_display_config_set_power_save_mode (manager->display_config, META_POWER_SAVE_UNSUPPORTED);
      return;
    }

  klass = META_MONITOR_MANAGER_GET_CLASS (manager);
  if (klass->set_power_save_mode)
    klass->set_power_save_mode (manager, mode);

  reason = META_POWER_SAVE_CHANGE_REASON_MODE_CHANGE;
  meta_monitor_manager_power_save_mode_changed (manager, mode, reason);
}

void
meta_monitor_manager_lid_is_closed_changed (MetaMonitorManager *manager)
{
  meta_monitor_manager_ensure_configured (manager);
}

static void
lid_is_closed_changed (MetaBackend *backend,
                       gboolean     lid_is_closed,
                       gpointer     user_data)
{
  MetaMonitorManager *manager = user_data;
  meta_monitor_manager_lid_is_closed_changed (manager);
}

static void
prepare_shutdown (MetaBackend        *backend,
                  MetaMonitorManager *manager)
{
  MetaMonitorManagerPrivate *priv =
    meta_monitor_manager_get_instance_private (manager);

  priv->shutting_down = TRUE;
}

static void
ensure_hdr_settings (MetaMonitorManager *manager)
{
  MetaMonitorManagerPrivate *priv =
    meta_monitor_manager_get_instance_private (manager);
  MetaOutputColorspace color_space;
  MetaOutputHdrMetadata hdr_metadata;
  GList *l;

  if (g_strcmp0 (priv->experimental_hdr, "on") == 0)
    {
      color_space = META_OUTPUT_COLORSPACE_BT2020;
      hdr_metadata = (MetaOutputHdrMetadata) {
        .active = TRUE,
        .eotf = META_OUTPUT_HDR_METADATA_EOTF_PQ,
      };

      meta_topic (META_DEBUG_COLOR,
                  "MonitorManager: Trying to enabling HDR mode "
                  "(Colorimetry: bt.2020, TF: PQ, HDR Metadata: Minimal):");
    }
  else
    {
      color_space = META_OUTPUT_COLORSPACE_DEFAULT;
      hdr_metadata = (MetaOutputHdrMetadata) {
        .active = FALSE,
      };

      meta_topic (META_DEBUG_COLOR,
                  "MonitorManager: Trying to enable default mode "
                  "(Colorimetry: default, TF: default, HDR Metadata: None):");
    }

  for (l = manager->monitors; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;
      g_autoptr (GError) error = NULL;

      if (!meta_monitor_set_color_space (monitor, color_space, &error))
        {
          meta_monitor_set_color_space (monitor,
                                        META_OUTPUT_COLORSPACE_DEFAULT,
                                        NULL);
          meta_monitor_set_hdr_metadata (monitor, &(MetaOutputHdrMetadata) {
                                           .active = FALSE,
                                         }, NULL);

          if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED))
            {
              meta_topic (META_DEBUG_COLOR,
                          "MonitorManager: Colorimetry not supported "
                          "on monitor %s",
                          meta_monitor_get_display_name (monitor));
            }
          else
            {
              g_warning ("Failed to set color space on monitor %s: %s",
                         meta_monitor_get_display_name (monitor), error->message);
            }

          continue;
        }

      if (!meta_monitor_set_hdr_metadata (monitor, &hdr_metadata, &error))
        {
          meta_monitor_set_color_space (monitor,
                                        META_OUTPUT_COLORSPACE_DEFAULT,
                                        NULL);
          meta_monitor_set_hdr_metadata (monitor, &(MetaOutputHdrMetadata) {
                                           .active = FALSE,
                                         }, NULL);

          if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED))
            {
              meta_topic (META_DEBUG_COLOR,
                          "MonitorManager: HDR Metadata not supported "
                          "on monitor %s",
                          meta_monitor_get_display_name (monitor));
            }
          else
            {
              g_warning ("Failed to set HDR metadata on monitor %s: %s",
                         meta_monitor_get_display_name (monitor),
                         error->message);
            }

          continue;
        }

        meta_topic (META_DEBUG_COLOR,
                    "MonitorManager: successfully set on monitor %s",
                    meta_monitor_get_display_name (monitor));
    }
}

/**
 * meta_monitor_manager_is_headless:
 * @manager: A #MetaMonitorManager object
 *
 * Returns whether the monitor manager is headless, i.e. without
 * any `MetaLogicalMonitor`s attached to it.
 *
 * Returns: %TRUE if no monitors are attached, %FALSE otherwise.
 */
gboolean
meta_monitor_manager_is_headless (MetaMonitorManager *manager)
{
  return !manager->logical_monitors;
}

float
meta_monitor_manager_calculate_monitor_mode_scale (MetaMonitorManager           *manager,
                                                   MetaLogicalMonitorLayoutMode  layout_mode,
                                                   MetaMonitor                  *monitor,
                                                   MetaMonitorMode              *monitor_mode)
{
  MetaMonitorManagerClass *manager_class =
    META_MONITOR_MANAGER_GET_CLASS (manager);

  return manager_class->calculate_monitor_mode_scale (manager,
                                                      layout_mode,
                                                      monitor,
                                                      monitor_mode);
}

float *
meta_monitor_manager_calculate_supported_scales (MetaMonitorManager           *manager,
                                                 MetaLogicalMonitorLayoutMode  layout_mode,
                                                 MetaMonitor                  *monitor,
                                                 MetaMonitorMode              *monitor_mode,
                                                 int                          *n_supported_scales)
{
  MetaMonitorManagerClass *manager_class =
    META_MONITOR_MANAGER_GET_CLASS (manager);

  return manager_class->calculate_supported_scales (manager,
                                                    layout_mode,
                                                    monitor,
                                                    monitor_mode,
                                                    n_supported_scales);
}

/**
 * meta_monitor_manager_get_capabilities:
 * @manager: A #MetaMonitorManager object
 *
 * Queries the capabilities of the monitor manager.
 *
 * Returns: #MetaMonitorManagerCapability flags representing the capabilities.
 */
MetaMonitorManagerCapability
meta_monitor_manager_get_capabilities (MetaMonitorManager *manager)
{
  MetaMonitorManagerClass *manager_class =
    META_MONITOR_MANAGER_GET_CLASS (manager);

  return manager_class->get_capabilities (manager);
}

gboolean
meta_monitor_manager_get_max_screen_size (MetaMonitorManager *manager,
                                          int                *max_width,
                                          int                *max_height)
{
  MetaMonitorManagerClass *manager_class =
    META_MONITOR_MANAGER_GET_CLASS (manager);

  return manager_class->get_max_screen_size (manager, max_width, max_height);
}


MetaLogicalMonitorLayoutMode
meta_monitor_manager_get_default_layout_mode (MetaMonitorManager *manager)
{
  MetaMonitorManagerClass *manager_class =
    META_MONITOR_MANAGER_GET_CLASS (manager);

  return manager_class->get_default_layout_mode (manager);
}

static void
on_virtual_monitor_destroyed (MetaVirtualMonitor *virtual_monitor,
                              MetaMonitorManager *manager)
{
  MetaMonitorManagerPrivate *priv =
    meta_monitor_manager_get_instance_private (manager);
  MetaOutput *output;

  output = meta_virtual_monitor_get_output (virtual_monitor);
  g_message ("Removed virtual monitor %s", meta_output_get_name (output));
  priv->virtual_monitors = g_list_remove (priv->virtual_monitors,
                                          virtual_monitor);

  if (!priv->shutting_down)
    meta_monitor_manager_reload (manager);
}

MetaVirtualMonitor *
meta_monitor_manager_create_virtual_monitor (MetaMonitorManager            *manager,
                                             const MetaVirtualMonitorInfo  *info,
                                             GError                       **error)
{
  MetaMonitorManagerPrivate *priv =
    meta_monitor_manager_get_instance_private (manager);
  MetaMonitorManagerClass *manager_class =
    META_MONITOR_MANAGER_GET_CLASS (manager);
  MetaVirtualMonitor *virtual_monitor;
  MetaOutput *output;

  if (!manager_class->create_virtual_monitor)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "Backend doesn't support creating virtual monitors");
      return NULL;
    }

  virtual_monitor = manager_class->create_virtual_monitor (manager, info,
                                                           error);
  if (!virtual_monitor)
    return NULL;

  g_signal_connect (virtual_monitor, "destroy",
                    G_CALLBACK (on_virtual_monitor_destroyed),
                    manager);

  priv->virtual_monitors = g_list_append (priv->virtual_monitors,
                                          virtual_monitor);

  output = meta_virtual_monitor_get_output (virtual_monitor);
  g_message ("Added virtual monitor %s", meta_output_get_name (output));

  return virtual_monitor;
}

static void
meta_monitor_manager_ensure_initial_config (MetaMonitorManager *manager)
{
  META_MONITOR_MANAGER_GET_CLASS (manager)->ensure_initial_config (manager);
}

gboolean
meta_monitor_manager_apply_monitors_config (MetaMonitorManager      *manager,
                                            MetaMonitorsConfig      *config,
                                            MetaMonitorsConfigMethod method,
                                            GError                 **error)
{
  MetaMonitorManagerClass *manager_class =
    META_MONITOR_MANAGER_GET_CLASS (manager);

  g_assert (!config ||
            !(config->flags & META_MONITORS_CONFIG_FLAG_MIGRATED));

  if (!manager_class->apply_monitors_config (manager, config, method, error))
    return FALSE;

  switch (method)
    {
    case META_MONITORS_CONFIG_METHOD_TEMPORARY:
    case META_MONITORS_CONFIG_METHOD_PERSISTENT:
      meta_monitor_config_manager_set_current (manager->config_manager, config);
      break;
    case META_MONITORS_CONFIG_METHOD_VERIFY:
      break;
    }

  return TRUE;
}

gboolean
meta_monitor_manager_has_hotplug_mode_update (MetaMonitorManager *manager)
{
  GList *gpus;
  GList *l;

  gpus = meta_backend_get_gpus (manager->backend);
  for (l = gpus; l; l = l->next)
    {
      MetaGpu *gpu = l->data;

      if (meta_gpu_has_hotplug_mode_update (gpu))
        return TRUE;
    }

  return FALSE;
}

static gboolean
should_use_stored_config (MetaMonitorManager *manager)
{
  return (manager->in_init ||
          !meta_monitor_manager_has_hotplug_mode_update (manager));
}

MetaMonitorsConfig *
meta_monitor_manager_ensure_configured (MetaMonitorManager *manager)
{
  MetaMonitorsConfig *config = NULL;
  GError *error = NULL;
  gboolean use_stored_config;
  MetaMonitorsConfigMethod method;
  MetaMonitorsConfigMethod fallback_method =
    META_MONITORS_CONFIG_METHOD_TEMPORARY;

  use_stored_config = should_use_stored_config (manager);
  if (use_stored_config)
    method = META_MONITORS_CONFIG_METHOD_PERSISTENT;
  else
    method = META_MONITORS_CONFIG_METHOD_TEMPORARY;

  if (use_stored_config)
    {
      config = meta_monitor_config_manager_get_stored (manager->config_manager);
      if (config)
        {
          g_autoptr (MetaMonitorsConfig) oriented_config = NULL;

          if (manager->panel_orientation_managed)
            {
              oriented_config = meta_monitor_config_manager_create_for_builtin_orientation (
                manager->config_manager, config);

              if (oriented_config)
                config = oriented_config;
            }

          if (!meta_monitor_manager_apply_monitors_config (manager,
                                                           config,
                                                           method,
                                                           &error))
            {
              config = NULL;
              g_warning ("Failed to use stored monitor configuration: %s",
                         error->message);
              g_clear_error (&error);
            }
          else
            {
              g_object_ref (config);
              goto done;
            }
        }
    }

  if (manager->panel_orientation_managed)
    {
      MetaMonitorsConfig *current_config =
        meta_monitor_config_manager_get_current (manager->config_manager);

      if (current_config)
        {
          config = meta_monitor_config_manager_create_for_builtin_orientation (
            manager->config_manager, current_config);
        }
    }

  if (config)
    {
      if (meta_monitor_manager_is_config_complete (manager, config))
        {
          if (!meta_monitor_manager_apply_monitors_config (manager,
                                                           config,
                                                           method,
                                                           &error))
            {
              g_clear_object (&config);
              g_warning ("Failed to use current monitor configuration: %s",
                         error->message);
              g_clear_error (&error);
            }
          else
            {
              goto done;
            }
        }
    }

  config = meta_monitor_config_manager_create_suggested (manager->config_manager);
  if (config)
    {
      if (!meta_monitor_manager_apply_monitors_config (manager,
                                                       config,
                                                       method,
                                                       &error))
        {
          g_clear_object (&config);
          g_warning ("Failed to use suggested monitor configuration: %s",
                     error->message);
          g_clear_error (&error);
        }
      else
        {
          goto done;
        }
    }

  config = meta_monitor_config_manager_get_previous (manager->config_manager);
  if (config)
    {
      g_autoptr (MetaMonitorsConfig) oriented_config = NULL;

      if (manager->panel_orientation_managed)
        {
          oriented_config =
            meta_monitor_config_manager_create_for_builtin_orientation (
              manager->config_manager, config);

          if (oriented_config)
            config = oriented_config;
        }

      config = g_object_ref (config);

      if (meta_monitor_manager_is_config_complete (manager, config))
        {
          if (!meta_monitor_manager_apply_monitors_config (manager,
                                                           config,
                                                           method,
                                                           &error))
            {
              g_warning ("Failed to use suggested monitor configuration: %s",
                         error->message);
              g_clear_error (&error);
            }
          else
            {
              goto done;
            }
        }

      g_clear_object (&config);
    }

  config = meta_monitor_config_manager_create_linear (manager->config_manager);
  if (config)
    {
      if (!meta_monitor_manager_apply_monitors_config (manager,
                                                       config,
                                                       method,
                                                       &error))
        {
          g_clear_object (&config);
          g_warning ("Failed to use linear monitor configuration: %s",
                     error->message);
          g_clear_error (&error);
        }
      else
        {
          goto done;
        }
    }

  config = meta_monitor_config_manager_create_fallback (manager->config_manager);
  if (config)
    {
      if (!meta_monitor_manager_apply_monitors_config (manager,
                                                       config,
                                                       fallback_method,
                                                       &error))
        {
          g_clear_object (&config);
          g_warning ("Failed to use fallback monitor configuration: %s",
                     error->message);
          g_clear_error (&error);
        }
      else
        {
          goto done;
        }
    }

done:
  if (!config)
    {
      meta_monitor_manager_apply_monitors_config (manager,
                                                  NULL,
                                                  fallback_method,
                                                  &error);
      return NULL;
    }

  g_object_unref (config);

  return config;
}

static void
handle_orientation_change (MetaOrientationManager *orientation_manager,
                           MetaMonitorManager     *manager)
{
  MetaOrientation orientation;
  MetaMonitorTransform transform;
  MetaMonitorTransform panel_transform;
  GError *error = NULL;
  MetaMonitorsConfig *config;
  MetaMonitor *laptop_panel;
  MetaLogicalMonitor *laptop_logical_monitor;
  MetaMonitorsConfig *current_config;

  laptop_panel = meta_monitor_manager_get_laptop_panel (manager);
  g_return_if_fail (laptop_panel);

  if (!meta_monitor_is_active (laptop_panel))
    return;

  orientation = meta_orientation_manager_get_orientation (orientation_manager);
  transform = meta_monitor_transform_from_orientation (orientation);

  laptop_logical_monitor = meta_monitor_get_logical_monitor (laptop_panel);
  panel_transform =
    meta_monitor_crtc_to_logical_transform (laptop_panel, transform);
  if (meta_logical_monitor_get_transform (laptop_logical_monitor) ==
      panel_transform)
    return;

  current_config =
    meta_monitor_config_manager_get_current (manager->config_manager);
  if (!current_config)
    return;

  config =
    meta_monitor_config_manager_create_for_orientation (manager->config_manager,
                                                        current_config,
                                                        transform);
  if (!config)
    return;

  if (!meta_monitor_manager_apply_monitors_config (manager,
                                                   config,
                                                   META_MONITORS_CONFIG_METHOD_TEMPORARY,
                                                   &error))
    {
      g_warning ("Failed to use orientation monitor configuration: %s",
                 error->message);
      g_error_free (error);
    }
  g_object_unref (config);
}

/*
 * Special case for tablets with a native portrait mode and a keyboard dock,
 * where the device gets docked in landscape mode. For this combo to work
 * properly with mutter starting while the tablet is docked, we need to take
 * the accelerometer reported orientation into account (at mutter startup)
 * even if there is a tablet-mode-switch which indicates that the device is
 * NOT in tablet-mode (because it is docked).
 */
static gboolean
handle_initial_orientation_change (MetaOrientationManager *orientation_manager,
                                   MetaMonitorManager     *manager)
{
  ClutterBackend *clutter_backend;
  ClutterSeat *seat;
  MetaMonitor *monitor;
  MetaMonitorMode *mode;
  int width, height;

  clutter_backend = meta_backend_get_clutter_backend (manager->backend);
  seat = clutter_backend_get_default_seat (clutter_backend);

  /*
   * This is a workaround to ignore the tablet mode switch on the initial config
   * of devices with a native portrait mode panel. The touchscreen and
   * accelerometer requirements for applying the orientation must still be met.
   */
  if (!clutter_seat_has_touchscreen (seat) ||
      !meta_orientation_manager_has_accelerometer (orientation_manager))
    return FALSE;

  /* Check for a portrait mode panel */
  monitor = meta_monitor_manager_get_laptop_panel (manager);
  if (!monitor)
    return FALSE;

  mode = meta_monitor_get_preferred_mode (monitor);
  meta_monitor_mode_get_resolution (mode, &width, &height);
  if (width > height)
    return FALSE;

  handle_orientation_change (orientation_manager, manager);
  return TRUE;
}

static void
orientation_changed (MetaOrientationManager *orientation_manager,
                     MetaMonitorManager     *manager)
{
  MetaMonitorManagerPrivate *priv =
    meta_monitor_manager_get_instance_private (manager);

  if (!priv->initial_orient_change_done)
    {
      priv->initial_orient_change_done = TRUE;
      if (handle_initial_orientation_change (orientation_manager, manager))
        return;
    }

  if (!manager->panel_orientation_managed)
    return;

  handle_orientation_change (orientation_manager, manager);
}

static void
experimental_features_changed (MetaSettings           *settings,
                               MetaExperimentalFeature old_experimental_features,
                               MetaMonitorManager     *manager)
{
  gboolean was_stage_views_scaled;
  gboolean is_stage_views_scaled;
  gboolean should_reconfigure = FALSE;

  was_stage_views_scaled =
    !!(old_experimental_features &
       META_EXPERIMENTAL_FEATURE_SCALE_MONITOR_FRAMEBUFFER);
  is_stage_views_scaled =
    meta_settings_is_experimental_feature_enabled (
      settings,
      META_EXPERIMENTAL_FEATURE_SCALE_MONITOR_FRAMEBUFFER);

  if (is_stage_views_scaled != was_stage_views_scaled)
    should_reconfigure = TRUE;

  if (should_reconfigure)
    meta_monitor_manager_reconfigure (manager);

  meta_settings_update_ui_scaling_factor (settings);
}

static gboolean
ensure_privacy_screen_settings (MetaMonitorManager *manager)
{
  MetaSettings *settings = meta_backend_get_settings (manager->backend);
  gboolean privacy_screen_enabled;
  GList *l;

  privacy_screen_enabled = meta_settings_is_privacy_screen_enabled (settings);
  for (l = manager->monitors; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;
      g_autoptr (GError) error = NULL;

      if (!meta_monitor_set_privacy_screen_enabled (monitor,
                                                    privacy_screen_enabled,
                                                    &error))
        {
          if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED))
            continue;

          g_warning ("Failed to set privacy screen setting on monitor %s: %s",
                     meta_monitor_get_display_name (monitor), error->message);
          return FALSE;
        }
    }

  return TRUE;
}

static MetaPrivacyScreenState
get_global_privacy_screen_state (MetaMonitorManager *manager)
{
  MetaPrivacyScreenState global_state = META_PRIVACY_SCREEN_UNAVAILABLE;
  GList *l;

  for (l = manager->monitors; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;
      MetaPrivacyScreenState monitor_state;

      if (!meta_monitor_is_active (monitor))
        continue;

      monitor_state = meta_monitor_get_privacy_screen_state (monitor);
      if (monitor_state == META_PRIVACY_SCREEN_UNAVAILABLE)
        continue;

      if (monitor_state & META_PRIVACY_SCREEN_DISABLED)
        return META_PRIVACY_SCREEN_DISABLED;

      if (monitor_state & META_PRIVACY_SCREEN_ENABLED)
        global_state = META_PRIVACY_SCREEN_ENABLED;
    }

  return global_state;
}

static gboolean
privacy_screen_needs_update (MetaMonitorManager *manager)
{
  MetaSettings *settings = meta_backend_get_settings (manager->backend);
  MetaPrivacyScreenState privacy_screen_state =
    get_global_privacy_screen_state (manager);

  if (privacy_screen_state == META_PRIVACY_SCREEN_UNAVAILABLE)
    return FALSE;

  return (!!(privacy_screen_state & META_PRIVACY_SCREEN_ENABLED) !=
      meta_settings_is_privacy_screen_enabled (settings));
}

static void
apply_privacy_screen_settings (MetaMonitorManager *manager)
{
  if (privacy_screen_needs_update (manager) &&
      ensure_privacy_screen_settings (manager))
    {
      manager->privacy_screen_change_state =
        META_PRIVACY_SCREEN_CHANGE_STATE_PENDING_SETTING;
    }
}

static void
update_panel_orientation_managed (MetaMonitorManager *manager)
{
  MetaOrientationManager *orientation_manager;
  ClutterBackend *clutter_backend;
  ClutterSeat *seat;
  gboolean panel_orientation_managed;

  clutter_backend = meta_backend_get_clutter_backend (manager->backend);
  seat = clutter_backend_get_default_seat (clutter_backend);

  orientation_manager = meta_backend_get_orientation_manager (manager->backend);

  panel_orientation_managed =
    (clutter_seat_get_touch_mode (seat) &&
     meta_orientation_manager_has_accelerometer (orientation_manager) &&
     meta_monitor_manager_get_laptop_panel (manager));

  if (manager->panel_orientation_managed == panel_orientation_managed)
    return;

  manager->panel_orientation_managed = panel_orientation_managed;
  g_object_notify_by_pspec (G_OBJECT (manager),
                            obj_props[PROP_PANEL_ORIENTATION_MANAGED]);

  meta_dbus_display_config_set_panel_orientation_managed (manager->display_config,
                                                          manager->panel_orientation_managed);

  /* The orientation may have changed while it was unmanaged */
  if (panel_orientation_managed)
    handle_orientation_change (orientation_manager, manager);
}

static void
update_has_builtin_panel (MetaMonitorManager *manager)
{
  MetaMonitorManagerPrivate *priv =
    meta_monitor_manager_get_instance_private (manager);
  GList *l;
  gboolean has_builtin_panel = FALSE;

  for (l = manager->monitors; l; l = l->next)
    {
      MetaMonitor *monitor = META_MONITOR (l->data);

      if (meta_monitor_is_laptop_panel (monitor))
        {
          has_builtin_panel = TRUE;
          break;
        }
    }

  if (priv->has_builtin_panel == has_builtin_panel)
    return;

  priv->has_builtin_panel = has_builtin_panel;
  g_object_notify_by_pspec (G_OBJECT (manager),
                            obj_props[PROP_HAS_BUILTIN_PANEL]);
}

static void
update_night_light_supported (MetaMonitorManager *manager)
{
  MetaMonitorManagerPrivate *priv =
    meta_monitor_manager_get_instance_private (manager);
  GList *l;

  gboolean night_light_supported = FALSE;

  for (l = meta_backend_get_gpus (manager->backend); l; l = l->next)
    {
      MetaGpu *gpu = l->data;
      GList *l_crtc;

      for (l_crtc = meta_gpu_get_crtcs (gpu); l_crtc; l_crtc = l_crtc->next)
        {
          MetaCrtc *crtc = l_crtc->data;

          if (meta_crtc_get_gamma_lut_size (crtc) > 0)
            {
              night_light_supported = TRUE;
              break;
            }
        }
    }

  if (priv->night_light_supported == night_light_supported)
    return;

  priv->night_light_supported = night_light_supported;
  g_object_notify_by_pspec (G_OBJECT (manager),
                            obj_props[PROP_NIGHT_LIGHT_SUPPORTED]);
  meta_dbus_display_config_set_night_light_supported (manager->display_config,
                                                      night_light_supported);
}

void
meta_monitor_manager_setup (MetaMonitorManager *manager)
{
  MetaMonitorConfigStore *config_store;
  const MetaMonitorConfigPolicy *policy;
  MetaMonitorManagerPrivate *priv =
    meta_monitor_manager_get_instance_private (manager);

  manager->in_init = TRUE;

  manager->config_manager = meta_monitor_config_manager_new (manager);
  config_store =
    meta_monitor_config_manager_get_store (manager->config_manager);
  policy = meta_monitor_config_store_get_policy (config_store);
  meta_dbus_display_config_set_apply_monitors_config_allowed (manager->display_config,
                                                              policy->enable_dbus);

  meta_dbus_display_config_set_night_light_supported (manager->display_config,
                                                      priv->night_light_supported);

  meta_monitor_manager_read_current_state (manager);

  meta_monitor_manager_ensure_initial_config (manager);

  if (privacy_screen_needs_update (manager))
    manager->privacy_screen_change_state = META_PRIVACY_SCREEN_CHANGE_STATE_INIT;

  manager->in_init = FALSE;
}

static void
meta_monitor_manager_constructed (GObject *object)
{
  MetaMonitorManager *manager = META_MONITOR_MANAGER (object);
  MetaBackend *backend = manager->backend;
  MetaSettings *settings = meta_backend_get_settings (backend);

  manager->display_config = meta_dbus_display_config_skeleton_new ();

  g_signal_connect_object (settings,
                           "experimental-features-changed",
                           G_CALLBACK (experimental_features_changed),
                           manager, 0);

  g_signal_connect_object (settings,
                           "privacy-screen-changed",
                           G_CALLBACK (apply_privacy_screen_settings),
                           manager, G_CONNECT_SWAPPED);

  monitor_manager_setup_dbus_config_handlers (manager);

  g_signal_connect_object (manager->display_config, "notify::power-save-mode",
                           G_CALLBACK (power_save_mode_changed), manager,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (meta_backend_get_orientation_manager (backend),
                           "orientation-changed",
                           G_CALLBACK (orientation_changed),
                           manager, 0);

  g_signal_connect_object (meta_backend_get_orientation_manager (backend),
                           "notify::has-accelerometer",
                           G_CALLBACK (update_panel_orientation_managed), manager,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (backend,
                           "lid-is-closed-changed",
                           G_CALLBACK (lid_is_closed_changed),
                           manager, 0);

  g_signal_connect (backend, "prepare-shutdown",
                    G_CALLBACK (prepare_shutdown),
                    manager);

  g_signal_connect (manager, "notify::experimental-hdr",
                    G_CALLBACK (ensure_hdr_settings),
                    NULL);

  manager->current_switch_config = META_MONITOR_SWITCH_CONFIG_UNKNOWN;

  initialize_dbus_interface (manager);
}

static void
meta_monitor_manager_finalize (GObject *object)
{
  MetaMonitorManager *manager = META_MONITOR_MANAGER (object);
  MetaMonitorManagerPrivate *priv =
    meta_monitor_manager_get_instance_private (manager);

  g_list_free_full (manager->logical_monitors, g_object_unref);

  g_warn_if_fail (!priv->virtual_monitors);

  G_OBJECT_CLASS (meta_monitor_manager_parent_class)->finalize (object);
}

static void
meta_monitor_manager_dispose (GObject *object)
{
  MetaMonitorManager *manager = META_MONITOR_MANAGER (object);
  MetaMonitorManagerPrivate *priv =
    meta_monitor_manager_get_instance_private (manager);

  g_clear_handle_id (&manager->dbus_name_id, g_bus_unown_name);

  g_clear_object (&manager->display_config);
  g_clear_object (&manager->config_manager);

  g_clear_handle_id (&manager->persistent_timeout_id, g_source_remove);
  g_clear_handle_id (&manager->restore_config_id, g_source_remove);
  g_clear_handle_id (&priv->switch_config_handle_id, g_source_remove);

  G_OBJECT_CLASS (meta_monitor_manager_parent_class)->dispose (object);
}

static GBytes *
meta_monitor_manager_real_read_edid (MetaMonitorManager *manager,
                                     MetaOutput         *output)
{
  return NULL;
}

static void
meta_monitor_manager_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  MetaMonitorManager *manager = META_MONITOR_MANAGER (object);
  MetaMonitorManagerPrivate *priv =
    meta_monitor_manager_get_instance_private (manager);

  switch (prop_id)
    {
    case PROP_BACKEND:
      manager->backend = g_value_get_object (value);
      break;
    case PROP_EXPERIMENTAL_HDR:
      priv->experimental_hdr = g_value_dup_string (value);
      break;
    case PROP_PANEL_ORIENTATION_MANAGED:
    case PROP_HAS_BUILTIN_PANEL:
    case PROP_NIGHT_LIGHT_SUPPORTED:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_monitor_manager_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  MetaMonitorManager *manager = META_MONITOR_MANAGER (object);
  MetaMonitorManagerPrivate *priv =
    meta_monitor_manager_get_instance_private (manager);

  switch (prop_id)
    {
    case PROP_BACKEND:
      g_value_set_object (value, manager->backend);
      break;
    case PROP_PANEL_ORIENTATION_MANAGED:
      g_value_set_boolean (value, manager->panel_orientation_managed);
      break;
    case PROP_HAS_BUILTIN_PANEL:
      g_value_set_boolean (value, priv->has_builtin_panel);
      break;
    case PROP_NIGHT_LIGHT_SUPPORTED:
      g_value_set_boolean (value, priv->night_light_supported);
      break;
    case PROP_EXPERIMENTAL_HDR:
      g_value_set_string (value, priv->experimental_hdr);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_monitor_manager_class_init (MetaMonitorManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_monitor_manager_constructed;
  object_class->dispose = meta_monitor_manager_dispose;
  object_class->finalize = meta_monitor_manager_finalize;
  object_class->get_property = meta_monitor_manager_get_property;
  object_class->set_property = meta_monitor_manager_set_property;

  klass->read_edid = meta_monitor_manager_real_read_edid;
  klass->read_current_state = meta_monitor_manager_real_read_current_state;

  signals[MONITORS_CHANGED] =
    g_signal_new ("monitors-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  signals[MONITORS_CHANGED_INTERNAL] =
    g_signal_new ("monitors-changed-internal",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  signals[POWER_SAVE_MODE_CHANGED] =
    g_signal_new ("power-save-mode-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  META_TYPE_POWER_SAVE_CHANGE_REASON);

  signals[CONFIRM_DISPLAY_CHANGE] =
    g_signal_new ("confirm-display-change",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  0,
                  NULL, NULL, NULL,
		  G_TYPE_NONE, 0);

  /**
   * MetaMonitorManager::monitor-privacy-screen-changed: (skip)
   * @monitor_manager: The #MetaMonitorManager
   * @logical_monitor: The #MetaLogicalMonitor where the privacy screen state
   *                   changed
   * @enabled: %TRUE if the privacy screen was enabled, otherwise %FALSE
   */
  signals[MONITOR_PRIVACY_SCREEN_CHANGED] =
    g_signal_new ("monitor-privacy-screen-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 2, META_TYPE_LOGICAL_MONITOR, G_TYPE_BOOLEAN);

  obj_props[PROP_BACKEND] =
    g_param_spec_object ("backend", NULL, NULL,
                         META_TYPE_BACKEND,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  obj_props[PROP_PANEL_ORIENTATION_MANAGED] =
    g_param_spec_boolean ("panel-orientation-managed", NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS);

  obj_props[PROP_HAS_BUILTIN_PANEL] =
    g_param_spec_boolean ("has-builtin-panel", NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS);

  obj_props[PROP_NIGHT_LIGHT_SUPPORTED] =
    g_param_spec_boolean ("night-light-supported", NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS);

  obj_props[PROP_EXPERIMENTAL_HDR] =
    g_param_spec_string ("experimental-hdr", NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST, obj_props);
}

gboolean
meta_monitor_has_aspect_as_size (MetaMonitor *monitor)
{
  int width_mm;
  int height_mm;

  meta_monitor_get_physical_dimensions (monitor, &width_mm, &height_mm);

  return (width_mm == 1600 && height_mm == 900) ||
     (width_mm == 1600 && height_mm == 1000) ||
     (width_mm == 160 && height_mm == 90) ||
     (width_mm == 160 && height_mm == 100) ||
     (width_mm == 16 && height_mm == 9) ||
     (width_mm == 16 && height_mm == 10);
}

static const char *
get_connector_type_name (MetaConnectorType connector_type)
{
  switch (connector_type)
    {
    case META_CONNECTOR_TYPE_Unknown: return "Unknown";
    case META_CONNECTOR_TYPE_VGA: return "VGA";
    case META_CONNECTOR_TYPE_DVII: return "DVII";
    case META_CONNECTOR_TYPE_DVID: return "DVID";
    case META_CONNECTOR_TYPE_DVIA: return "DVIA";
    case META_CONNECTOR_TYPE_Composite: return "Composite";
    case META_CONNECTOR_TYPE_SVIDEO: return "SVIDEO";
    case META_CONNECTOR_TYPE_LVDS: return "LVDS";
    case META_CONNECTOR_TYPE_Component: return "Component";
    case META_CONNECTOR_TYPE_9PinDIN: return "9PinDIN";
    case META_CONNECTOR_TYPE_DisplayPort: return "DisplayPort";
    case META_CONNECTOR_TYPE_HDMIA: return "HDMIA";
    case META_CONNECTOR_TYPE_HDMIB: return "HDMIB";
    case META_CONNECTOR_TYPE_TV: return "TV";
    case META_CONNECTOR_TYPE_eDP: return "eDP";
    case META_CONNECTOR_TYPE_VIRTUAL: return "VIRTUAL";
    case META_CONNECTOR_TYPE_DSI: return "DSI";
    case META_CONNECTOR_TYPE_DPI: return "DPI";
    case META_CONNECTOR_TYPE_WRITEBACK: return "WRITEBACK";
    case META_CONNECTOR_TYPE_SPI: return "SPI";
    case META_CONNECTOR_TYPE_USB: return "USB";
    default: g_assert_not_reached ();
    }
  return NULL;
}

static GList *
combine_gpu_lists (MetaMonitorManager    *manager,
                   GList              * (*list_getter) (MetaGpu *gpu))
{
  GList *gpus;
  GList *list = NULL;
  GList *l;

  gpus = meta_backend_get_gpus (manager->backend);
  for (l = gpus; l; l = l->next)
    {
      MetaGpu *gpu = l->data;

      list = g_list_concat (list, g_list_copy (list_getter (gpu)));
    }

  return list;
}

static void
emit_privacy_screen_change (MetaMonitorManager *manager)
{
  GList *l;

  for (l = manager->monitors; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;
      MetaPrivacyScreenState privacy_screen_state;
      gboolean enabled;

      if (!meta_monitor_is_active (monitor))
        continue;

      privacy_screen_state = meta_monitor_get_privacy_screen_state (monitor);
      if (privacy_screen_state == META_PRIVACY_SCREEN_UNAVAILABLE)
        continue;

      enabled = !!(privacy_screen_state & META_PRIVACY_SCREEN_ENABLED);

      g_signal_emit (manager, signals[MONITOR_PRIVACY_SCREEN_CHANGED], 0,
                     meta_monitor_get_logical_monitor (monitor), enabled);
    }
}

void
meta_monitor_manager_maybe_emit_privacy_screen_change (MetaMonitorManager *manager)
{
  MetaPrivacyScreenChangeState reason = manager->privacy_screen_change_state;

  if (reason == META_PRIVACY_SCREEN_CHANGE_STATE_NONE ||
      reason == META_PRIVACY_SCREEN_CHANGE_STATE_INIT)
    return;

  if (reason == META_PRIVACY_SCREEN_CHANGE_STATE_PENDING_HOTKEY)
    emit_privacy_screen_change (manager);

  if (reason != META_PRIVACY_SCREEN_CHANGE_STATE_PENDING_SETTING)
    {
      MetaSettings *settings = meta_backend_get_settings (manager->backend);

      meta_settings_set_privacy_screen_enabled (settings,
        get_global_privacy_screen_state (manager) ==
        META_PRIVACY_SCREEN_ENABLED);
    }

  meta_dbus_display_config_emit_monitors_changed (manager->display_config);
  manager->privacy_screen_change_state = META_PRIVACY_SCREEN_CHANGE_STATE_NONE;
}

static gboolean
meta_monitor_manager_handle_get_resources (MetaDBusDisplayConfig *skeleton,
                                           GDBusMethodInvocation *invocation,
                                           MetaMonitorManager    *manager)
{
  MetaMonitorManagerClass *manager_class = META_MONITOR_MANAGER_GET_CLASS (manager);
  GList *combined_modes;
  GList *combined_outputs;
  GList *combined_crtcs;
  GVariantBuilder crtc_builder, output_builder, mode_builder;
  GList *l;
  unsigned int i, j;
  int max_screen_width;
  int max_screen_height;

  combined_modes = combine_gpu_lists (manager, meta_gpu_get_modes);
  combined_outputs = combine_gpu_lists (manager, meta_gpu_get_outputs);
  combined_crtcs = combine_gpu_lists (manager, meta_gpu_get_crtcs);

  g_variant_builder_init (&crtc_builder, G_VARIANT_TYPE ("a(uxiiiiiuaua{sv})"));
  g_variant_builder_init (&output_builder, G_VARIANT_TYPE ("a(uxiausauaua{sv})"));
  g_variant_builder_init (&mode_builder, G_VARIANT_TYPE ("a(uxuudu)"));

  for (l = combined_crtcs, i = 0; l; l = l->next, i++)
    {
      MetaCrtc *crtc = l->data;
      GVariantBuilder transforms;
      const MetaCrtcConfig *crtc_config;

      g_variant_builder_init (&transforms, G_VARIANT_TYPE ("au"));
      for (j = 0; j <= META_MONITOR_TRANSFORM_FLIPPED_270; j++)
        {
          if (meta_crtc_get_all_transforms (crtc) & (1 << j))
            g_variant_builder_add (&transforms, "u", j);
        }

      crtc_config = meta_crtc_get_config (crtc);
      if (crtc_config)
        {
          int current_mode_index;

          current_mode_index = g_list_index (combined_modes, crtc_config->mode);
          g_variant_builder_add (&crtc_builder, "(uxiiiiiuaua{sv})",
                                 i, /* ID */
                                 (int64_t) meta_crtc_get_id (crtc),
                                 (int) roundf (crtc_config->layout.origin.x),
                                 (int) roundf (crtc_config->layout.origin.y),
                                 (int) roundf (crtc_config->layout.size.width),
                                 (int) roundf (crtc_config->layout.size.height),
                                 current_mode_index,
                                 (uint32_t) crtc_config->transform,
                                 &transforms,
                                 NULL /* properties */);
        }
      else
        {
          g_variant_builder_add (&crtc_builder, "(uxiiiiiuaua{sv})",
                                 i, /* ID */
                                 (int64_t) meta_crtc_get_id (crtc),
                                 0,
                                 0,
                                 0,
                                 0,
                                 -1,
                                 (uint32_t) META_MONITOR_TRANSFORM_NORMAL,
                                 &transforms,
                                 NULL /* properties */);
        }
    }

  for (l = combined_outputs, i = 0; l; l = l->next, i++)
    {
      MetaOutput *output = l->data;
      const MetaOutputInfo *output_info = meta_output_get_info (output);
      GVariantBuilder crtcs, modes, clones, properties;
      GBytes *edid;
      MetaCrtc *crtc;
      int crtc_index;
      int backlight;
      int min_backlight_step;
      gboolean is_primary;
      gboolean is_presentation;
      const char * connector_type_name;
      gboolean is_underscanning;
      gboolean supports_underscanning;
      gboolean supports_color_transform;
      const char *vendor;
      const char *product;
      const char *serial;

      g_variant_builder_init (&crtcs, G_VARIANT_TYPE ("au"));
      for (j = 0; j < output_info->n_possible_crtcs; j++)
        {
          MetaCrtc *possible_crtc = output_info->possible_crtcs[j];
          unsigned possible_crtc_index;

          possible_crtc_index = g_list_index (combined_crtcs, possible_crtc);
          g_variant_builder_add (&crtcs, "u", possible_crtc_index);
        }

      g_variant_builder_init (&modes, G_VARIANT_TYPE ("au"));
      for (j = 0; j < output_info->n_modes; j++)
        {
          unsigned mode_index;

          mode_index = g_list_index (combined_modes, output_info->modes[j]);
          g_variant_builder_add (&modes, "u", mode_index);
        }

      g_variant_builder_init (&clones, G_VARIANT_TYPE ("au"));
      for (j = 0; j < output_info->n_possible_clones; j++)
        {
          unsigned int possible_clone_index;

          possible_clone_index = g_list_index (combined_outputs,
                                               output_info->possible_clones[j]);
          g_variant_builder_add (&clones, "u", possible_clone_index);
        }

      backlight = meta_output_get_backlight (output);
      min_backlight_step =
        output_info->backlight_max - output_info->backlight_min
        ? 100 / (output_info->backlight_max - output_info->backlight_min)
        : -1;
      is_primary = meta_output_is_primary (output);
      is_presentation = meta_output_is_presentation (output);
      is_underscanning = meta_output_is_underscanning (output);
      connector_type_name = get_connector_type_name (output_info->connector_type);
      supports_underscanning = output_info->supports_underscanning;
      supports_color_transform = output_info->supports_color_transform;
      vendor = output_info->vendor;
      product = output_info->product;
      serial = output_info->serial;

      g_variant_builder_init (&properties, G_VARIANT_TYPE ("a{sv}"));
      g_variant_builder_add (&properties, "{sv}", "vendor",
                             g_variant_new_string (vendor ? vendor : "unknown"));
      g_variant_builder_add (&properties, "{sv}", "product",
                             g_variant_new_string (product ? product : "unknown"));
      g_variant_builder_add (&properties, "{sv}", "serial",
                             g_variant_new_string (serial ? serial : "unknown"));
      g_variant_builder_add (&properties, "{sv}", "width-mm",
                             g_variant_new_int32 (output_info->width_mm));
      g_variant_builder_add (&properties, "{sv}", "height-mm",
                             g_variant_new_int32 (output_info->height_mm));
      g_variant_builder_add (&properties, "{sv}", "display-name",
                             g_variant_new_string (output_info->name));
      g_variant_builder_add (&properties, "{sv}", "backlight",
                             g_variant_new_int32 (backlight));
      g_variant_builder_add (&properties, "{sv}", "min-backlight-step",
                             g_variant_new_int32 (min_backlight_step));
      g_variant_builder_add (&properties, "{sv}", "primary",
                             g_variant_new_boolean (is_primary));
      g_variant_builder_add (&properties, "{sv}", "presentation",
                             g_variant_new_boolean (is_presentation));
      g_variant_builder_add (&properties, "{sv}", "connector-type",
                             g_variant_new_string (connector_type_name));
      g_variant_builder_add (&properties, "{sv}", "underscanning",
                             g_variant_new_boolean (is_underscanning));
      g_variant_builder_add (&properties, "{sv}", "supports-underscanning",
                             g_variant_new_boolean (supports_underscanning));
      g_variant_builder_add (&properties, "{sv}", "supports-color-transform",
                             g_variant_new_boolean (supports_color_transform));

      edid = manager_class->read_edid (manager, output);
      if (edid)
        {
          g_variant_builder_add (&properties, "{sv}", "edid",
                                 g_variant_new_from_bytes (G_VARIANT_TYPE ("ay"),
                                                           edid, TRUE));
          g_bytes_unref (edid);
        }

      if (output_info->tile_info.group_id)
        {
          GVariant *tile_variant;

          tile_variant = g_variant_new ("(uuuuuuuu)",
                                        output_info->tile_info.group_id,
                                        output_info->tile_info.flags,
                                        output_info->tile_info.max_h_tiles,
                                        output_info->tile_info.max_v_tiles,
                                        output_info->tile_info.loc_h_tile,
                                        output_info->tile_info.loc_v_tile,
                                        output_info->tile_info.tile_w,
                                        output_info->tile_info.tile_h);
          g_variant_builder_add (&properties, "{sv}", "tile", tile_variant);
        }

      crtc = meta_output_get_assigned_crtc (output);
      crtc_index = crtc ? g_list_index (combined_crtcs, crtc) : -1;
      g_variant_builder_add (&output_builder, "(uxiausauaua{sv})",
                             i, /* ID */
                             meta_output_get_id (output),
                             crtc_index,
                             &crtcs,
                             meta_output_get_name (output),
                             &modes,
                             &clones,
                             &properties);
    }

  for (l = combined_modes, i = 0; l; l = l->next, i++)
    {
      MetaCrtcMode *mode = l->data;
      const MetaCrtcModeInfo *crtc_mode_info =
        meta_crtc_mode_get_info (mode);

      g_variant_builder_add (&mode_builder, "(uxuudu)",
                             i, /* ID */
                             (int64_t) meta_crtc_mode_get_id (mode),
                             (uint32_t) crtc_mode_info->width,
                             (uint32_t) crtc_mode_info->height,
                             (double) crtc_mode_info->refresh_rate,
                             (uint32_t) crtc_mode_info->flags);
    }

  if (!meta_monitor_manager_get_max_screen_size (manager,
                                                 &max_screen_width,
                                                 &max_screen_height))
    {
      /* No max screen size, just send something large */
      max_screen_width = 65535;
      max_screen_height = 65535;
    }

  meta_dbus_display_config_complete_get_resources (skeleton,
                                                   invocation,
                                                   manager->serial,
                                                   g_variant_builder_end (&crtc_builder),
                                                   g_variant_builder_end (&output_builder),
                                                   g_variant_builder_end (&mode_builder),
                                                   max_screen_width,
                                                   max_screen_height);

  g_list_free (combined_modes);
  g_list_free (combined_outputs);
  g_list_free (combined_crtcs);

  return TRUE;
}

static void
restore_previous_config (MetaMonitorManager *manager)
{
  MetaMonitorsConfig *previous_config;
  GError *error = NULL;

  previous_config =
    meta_monitor_config_manager_pop_previous (manager->config_manager);

  if (previous_config)
    {
      MetaMonitorsConfigMethod method;

      if (manager->panel_orientation_managed)
        {
          g_autoptr (MetaMonitorsConfig) oriented_config = NULL;

          oriented_config =
            meta_monitor_config_manager_create_for_builtin_orientation (
              manager->config_manager, previous_config);

          if (oriented_config)
            g_set_object (&previous_config, oriented_config);
        }

      method = META_MONITORS_CONFIG_METHOD_TEMPORARY;
      if (meta_monitor_manager_apply_monitors_config (manager,
                                                      previous_config,
                                                      method,
                                                      &error))
        {
          g_object_unref (previous_config);
          return;
        }
      else
        {
          g_object_unref (previous_config);
          g_warning ("Failed to restore previous configuration: %s",
                     error->message);
          g_error_free (error);
        }
    }

  meta_monitor_manager_ensure_configured (manager);
}

int
meta_monitor_manager_get_display_configuration_timeout (MetaMonitorManager *manager)
{
  return DEFAULT_DISPLAY_CONFIGURATION_TIMEOUT;
}

static gboolean
save_config_timeout (gpointer user_data)
{
  MetaMonitorManager *manager = user_data;

  restore_previous_config (manager);
  manager->persistent_timeout_id = 0;

  return G_SOURCE_REMOVE;
}

static void
request_persistent_confirmation (MetaMonitorManager *manager)
{
  int timeout_s;

  timeout_s = meta_monitor_manager_get_display_configuration_timeout (manager);
  manager->persistent_timeout_id = g_timeout_add_seconds (timeout_s,
                                                          save_config_timeout,
                                                          manager);
  g_source_set_name_by_id (manager->persistent_timeout_id,
                           "[mutter] save_config_timeout");

  g_signal_emit (manager, signals[CONFIRM_DISPLAY_CHANGE], 0);
}

#define META_DISPLAY_CONFIG_MODE_FLAGS_PREFERRED (1 << 0)
#define META_DISPLAY_CONFIG_MODE_FLAGS_CURRENT (1 << 1)

#define MODE_FORMAT "(siiddada{sv})"
#define MODES_FORMAT "a" MODE_FORMAT
#define MONITOR_SPEC_FORMAT "(ssss)"
#define MONITOR_FORMAT "(" MONITOR_SPEC_FORMAT MODES_FORMAT "a{sv})"
#define MONITORS_FORMAT "a" MONITOR_FORMAT

#define LOGICAL_MONITOR_MONITORS_FORMAT "a" MONITOR_SPEC_FORMAT
#define LOGICAL_MONITOR_FORMAT "(iidub" LOGICAL_MONITOR_MONITORS_FORMAT "a{sv})"
#define LOGICAL_MONITORS_FORMAT "a" LOGICAL_MONITOR_FORMAT

static gboolean
meta_monitor_manager_handle_get_current_state (MetaDBusDisplayConfig *skeleton,
                                               GDBusMethodInvocation *invocation,
                                               MetaMonitorManager    *manager)
{
  MetaSettings *settings = meta_backend_get_settings (manager->backend);
  GVariantBuilder monitors_builder;
  GVariantBuilder logical_monitors_builder;
  GVariantBuilder properties_builder;
  GList *l;
  int i;
  MetaMonitorManagerCapability capabilities;
  int ui_scaling_factor;
  int max_screen_width, max_screen_height;

  g_variant_builder_init (&monitors_builder,
                          G_VARIANT_TYPE (MONITORS_FORMAT));
  g_variant_builder_init (&logical_monitors_builder,
                          G_VARIANT_TYPE (LOGICAL_MONITORS_FORMAT));

  for (l = manager->monitors; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;
      MetaMonitorSpec *monitor_spec = meta_monitor_get_spec (monitor);
      MetaMonitorMode *current_mode;
      MetaMonitorMode *preferred_mode;
      MetaPrivacyScreenState privacy_screen_state;
      GVariantBuilder modes_builder;
      GVariantBuilder monitor_properties_builder;
      GList *k;
      gboolean is_builtin;
      const char *display_name;

      current_mode = meta_monitor_get_current_mode (monitor);
      preferred_mode = meta_monitor_get_preferred_mode (monitor);

      g_variant_builder_init (&modes_builder, G_VARIANT_TYPE (MODES_FORMAT));
      for (k = meta_monitor_get_modes (monitor); k; k = k->next)
        {
          MetaMonitorMode *monitor_mode = k->data;
          GVariantBuilder supported_scales_builder;
          const char *mode_id;
          int mode_width, mode_height;
          float refresh_rate;
          float preferred_scale;
          float *supported_scales;
          int n_supported_scales;
          GVariantBuilder mode_properties_builder;
          MetaCrtcModeFlag mode_flags;

          if (!meta_monitor_mode_should_be_advertised (monitor_mode))
            continue;

          mode_id = meta_monitor_mode_get_id (monitor_mode);
          meta_monitor_mode_get_resolution (monitor_mode,
                                            &mode_width, &mode_height);

          refresh_rate = meta_monitor_mode_get_refresh_rate (monitor_mode);

          preferred_scale =
            meta_monitor_manager_calculate_monitor_mode_scale (manager,
                                                               manager->layout_mode,
                                                               monitor,
                                                               monitor_mode);

          g_variant_builder_init (&supported_scales_builder,
                                  G_VARIANT_TYPE ("ad"));
          supported_scales =
            meta_monitor_manager_calculate_supported_scales (manager,
                                                             manager->layout_mode,
                                                             monitor,
                                                             monitor_mode,
                                                             &n_supported_scales);
          for (i = 0; i < n_supported_scales; i++)
            g_variant_builder_add (&supported_scales_builder, "d",
                                   (double) supported_scales[i]);
          g_free (supported_scales);

          mode_flags = meta_monitor_mode_get_flags (monitor_mode);

          g_variant_builder_init (&mode_properties_builder,
                                  G_VARIANT_TYPE ("a{sv}"));
          if (monitor_mode == current_mode)
            g_variant_builder_add (&mode_properties_builder, "{sv}",
                                   "is-current",
                                   g_variant_new_boolean (TRUE));
          if (monitor_mode == preferred_mode)
            g_variant_builder_add (&mode_properties_builder, "{sv}",
                                   "is-preferred",
                                   g_variant_new_boolean (TRUE));
          if (mode_flags & META_CRTC_MODE_FLAG_INTERLACE)
            g_variant_builder_add (&mode_properties_builder, "{sv}",
                                   "is-interlaced",
                                   g_variant_new_boolean (TRUE));

          g_variant_builder_add (&modes_builder, MODE_FORMAT,
                                 mode_id,
                                 mode_width,
                                 mode_height,
                                 refresh_rate,
                                 (double) preferred_scale,
                                 &supported_scales_builder,
                                 &mode_properties_builder);
        }

      g_variant_builder_init (&monitor_properties_builder,
                              G_VARIANT_TYPE ("a{sv}"));
      if (meta_monitor_supports_underscanning (monitor))
        {
          gboolean is_underscanning = meta_monitor_is_underscanning (monitor);

          g_variant_builder_add (&monitor_properties_builder, "{sv}",
                                 "is-underscanning",
                                 g_variant_new_boolean (is_underscanning));
        }

      is_builtin = meta_monitor_is_laptop_panel (monitor);
      g_variant_builder_add (&monitor_properties_builder, "{sv}",
                             "is-builtin",
                             g_variant_new_boolean (is_builtin));

      display_name = meta_monitor_get_display_name (monitor);
      g_variant_builder_add (&monitor_properties_builder, "{sv}",
                             "display-name",
                             g_variant_new_string (display_name));

      privacy_screen_state = meta_monitor_get_privacy_screen_state (monitor);
      if (privacy_screen_state != META_PRIVACY_SCREEN_UNAVAILABLE)
        {
          GVariant *state;

          state = g_variant_new ("(bb)",
            !!(privacy_screen_state & META_PRIVACY_SCREEN_ENABLED),
            !!(privacy_screen_state & META_PRIVACY_SCREEN_LOCKED));

          g_variant_builder_add (&monitor_properties_builder, "{sv}",
                                 "privacy-screen-state", state);
        }

      g_variant_builder_add (&monitors_builder, MONITOR_FORMAT,
                             monitor_spec->connector,
                             monitor_spec->vendor,
                             monitor_spec->product,
                             monitor_spec->serial,
                             &modes_builder,
                             &monitor_properties_builder);
    }

  for (l = manager->logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;
      GVariantBuilder logical_monitor_monitors_builder;
      GList *k;

      g_variant_builder_init (&logical_monitor_monitors_builder,
                              G_VARIANT_TYPE (LOGICAL_MONITOR_MONITORS_FORMAT));

      for (k = logical_monitor->monitors; k; k = k->next)
        {
          MetaMonitor *monitor = k->data;
          MetaMonitorSpec *monitor_spec = meta_monitor_get_spec (monitor);

          g_variant_builder_add (&logical_monitor_monitors_builder,
                                 MONITOR_SPEC_FORMAT,
                                 monitor_spec->connector,
                                 monitor_spec->vendor,
                                 monitor_spec->product,
                                 monitor_spec->serial);
        }

      g_variant_builder_add (&logical_monitors_builder,
                             LOGICAL_MONITOR_FORMAT,
                             logical_monitor->rect.x,
                             logical_monitor->rect.y,
                             (double) logical_monitor->scale,
                             logical_monitor->transform,
                             logical_monitor->is_primary,
                             &logical_monitor_monitors_builder,
                             NULL);
    }

  g_variant_builder_init (&properties_builder, G_VARIANT_TYPE ("a{sv}"));
  capabilities = meta_monitor_manager_get_capabilities (manager);

  g_variant_builder_add (&properties_builder, "{sv}",
                         "layout-mode",
                         g_variant_new_uint32 (manager->layout_mode));
  if (capabilities & META_MONITOR_MANAGER_CAPABILITY_LAYOUT_MODE)
    {
      g_variant_builder_add (&properties_builder, "{sv}",
                             "supports-changing-layout-mode",
                             g_variant_new_boolean (TRUE));
    }

  if (capabilities & META_MONITOR_MANAGER_CAPABILITY_GLOBAL_SCALE_REQUIRED)
    {
      g_variant_builder_add (&properties_builder, "{sv}",
                             "global-scale-required",
                             g_variant_new_boolean (TRUE));
    }

  ui_scaling_factor = meta_settings_get_ui_scaling_factor (settings);
  g_variant_builder_add (&properties_builder, "{sv}",
                         "legacy-ui-scaling-factor",
                         g_variant_new_int32 (ui_scaling_factor));

  if (meta_monitor_manager_get_max_screen_size (manager,
                                                &max_screen_width,
                                                &max_screen_height))
    {
      GVariantBuilder max_screen_size_builder;

      g_variant_builder_init (&max_screen_size_builder,
                              G_VARIANT_TYPE ("(ii)"));
      g_variant_builder_add (&max_screen_size_builder, "i",
                             max_screen_width);
      g_variant_builder_add (&max_screen_size_builder, "i",
                             max_screen_height);

      g_variant_builder_add (&properties_builder, "{sv}",
                             "max-screen-size",
                             g_variant_builder_end (&max_screen_size_builder));
    }

  meta_dbus_display_config_complete_get_current_state (
    skeleton,
    invocation,
    manager->serial,
    g_variant_builder_end (&monitors_builder),
    g_variant_builder_end (&logical_monitors_builder),
    g_variant_builder_end (&properties_builder));

  return TRUE;
}

#undef MODE_FORMAT
#undef MODES_FORMAT
#undef MONITOR_SPEC_FORMAT
#undef MONITOR_FORMAT
#undef MONITORS_FORMAT
#undef LOGICAL_MONITOR_MONITORS_FORMAT
#undef LOGICAL_MONITOR_FORMAT
#undef LOGICAL_MONITORS_FORMAT

gboolean
meta_monitor_manager_is_scale_supported (MetaMonitorManager          *manager,
                                         MetaLogicalMonitorLayoutMode layout_mode,
                                         MetaMonitor                 *monitor,
                                         MetaMonitorMode             *monitor_mode,
                                         float                        scale)
{
  g_autofree float *supported_scales = NULL;
  int n_supported_scales;
  int i;

  supported_scales =
    meta_monitor_manager_calculate_supported_scales (manager,
                                                     layout_mode,
                                                     monitor,
                                                     monitor_mode,
                                                     &n_supported_scales);
  for (i = 0; i < n_supported_scales; i++)
    {
      if (supported_scales[i] == scale)
        return TRUE;
    }

  return FALSE;
}

static gboolean
is_global_scale_matching_in_config (MetaMonitorsConfig *config,
                                    float               scale)
{
  GList *l;

  for (l = config->logical_monitor_configs; l; l = l->next)
    {
      MetaLogicalMonitorConfig *logical_monitor_config = l->data;

      if (!G_APPROX_VALUE (logical_monitor_config->scale, scale, FLT_EPSILON))
        return FALSE;
    }

  return TRUE;
}

static gboolean
meta_monitor_manager_is_scale_supported_for_config (MetaMonitorManager *manager,
                                                    MetaMonitorsConfig *config,
                                                    MetaMonitor        *monitor,
                                                    MetaMonitorMode    *monitor_mode,
                                                    float               scale)
{
  if (meta_monitor_manager_is_scale_supported (manager, config->layout_mode,
                                               monitor, monitor_mode, scale))
    {
      if (meta_monitor_manager_get_capabilities (manager) &
          META_MONITOR_MANAGER_CAPABILITY_GLOBAL_SCALE_REQUIRED)
        return is_global_scale_matching_in_config (config, scale);

      return TRUE;
    }

  return FALSE;
}

static gboolean
meta_monitor_manager_is_config_applicable (MetaMonitorManager *manager,
                                           MetaMonitorsConfig *config,
                                           GError            **error)
{
  GList *l;

  for (l = config->logical_monitor_configs; l; l = l->next)
    {
      MetaLogicalMonitorConfig *logical_monitor_config = l->data;
      float scale = logical_monitor_config->scale;
      GList *k;

      for (k = logical_monitor_config->monitor_configs; k; k = k->next)
        {
          MetaMonitorConfig *monitor_config = k->data;
          MetaMonitorSpec *monitor_spec = monitor_config->monitor_spec;
          MetaMonitorModeSpec *mode_spec = monitor_config->mode_spec;
          MetaMonitor *monitor;
          MetaMonitorMode *monitor_mode;

          monitor = meta_monitor_manager_get_monitor_from_spec (manager,
                                                                monitor_spec);
          if (!monitor)
            {
              g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                           "Specified monitor not found");
              return FALSE;
            }

          monitor_mode = meta_monitor_get_mode_from_spec (monitor, mode_spec);
          if (!monitor_mode)
            {
              g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                           "Specified monitor mode not available");
              return FALSE;
            }

          if (!meta_monitor_manager_is_scale_supported_for_config (manager,
                                                                   config,
                                                                   monitor,
                                                                   monitor_mode,
                                                                   scale))
            {
              g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                           "Scale not supported by backend");
              return FALSE;
            }

          if (meta_monitor_is_laptop_panel (monitor) &&
              meta_backend_is_lid_closed (manager->backend))
            {
              g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                           "Refusing to activate a closed laptop panel");
              return FALSE;
            }
        }
    }

  return TRUE;
}

static gboolean
meta_monitor_manager_is_config_complete (MetaMonitorManager *manager,
                                         MetaMonitorsConfig *config)
{
  MetaMonitorsConfigKey *current_state_key;
  gboolean is_config_complete;

  current_state_key =
    meta_create_monitors_config_key_for_current_state (manager);
  if (!current_state_key)
    return FALSE;

  is_config_complete = meta_monitors_config_key_equal (current_state_key,
                                                       config->key);
  meta_monitors_config_key_free (current_state_key);

  if (!is_config_complete)
    return FALSE;

  return meta_monitor_manager_is_config_applicable (manager, config, NULL);
}

static MetaMonitor *
find_monitor_from_connector (MetaMonitorManager *manager,
                             char               *connector)
{
  GList *monitors;
  GList *l;

  if (!connector)
    return NULL;

  monitors = meta_monitor_manager_get_monitors (manager);
  for (l = monitors; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;
      MetaMonitorSpec *monitor_spec = meta_monitor_get_spec (monitor);

      if (g_str_equal (connector, monitor_spec->connector))
        return monitor;
    }

  return NULL;
}

#define MONITOR_CONFIG_FORMAT "(ssa{sv})"
#define MONITOR_CONFIGS_FORMAT "a" MONITOR_CONFIG_FORMAT

#define LOGICAL_MONITOR_CONFIG_FORMAT "(iidub" MONITOR_CONFIGS_FORMAT ")"

static MetaMonitorConfig *
create_monitor_config_from_variant (MetaMonitorManager *manager,
                                    GVariant           *monitor_config_variant,
                                    GError            **error)
{

  MetaMonitorConfig *monitor_config = NULL;
  g_autofree char *connector = NULL;
  g_autofree char *mode_id = NULL;
  MetaMonitorMode *monitor_mode;
  MetaMonitor *monitor;
  MetaMonitorSpec *monitor_spec;
  MetaMonitorModeSpec *monitor_mode_spec;
  g_autoptr (GVariant) properties_variant = NULL;
  gboolean enable_underscanning = FALSE;
  gboolean set_underscanning = FALSE;

  g_variant_get (monitor_config_variant, "(ss@a{sv})",
                 &connector,
                 &mode_id,
                 &properties_variant);

  monitor = find_monitor_from_connector (manager, connector);
  if (!monitor)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Invalid connector '%s' specified", connector);
      return NULL;
    }

  monitor_mode = meta_monitor_get_mode_from_id (monitor, mode_id);
  if (!monitor_mode)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Invalid mode '%s' specified", mode_id);
      return NULL;
    }

  set_underscanning =
    g_variant_lookup (properties_variant, "underscanning", "b",
                      &enable_underscanning);
  if (set_underscanning)
    {
      if (enable_underscanning && !meta_monitor_supports_underscanning (monitor))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Underscanning requested but unsupported");
          return NULL;
        }
    }

  monitor_spec = meta_monitor_spec_clone (meta_monitor_get_spec (monitor));

  monitor_mode_spec = g_new0 (MetaMonitorModeSpec, 1);
  *monitor_mode_spec = *meta_monitor_mode_get_spec (monitor_mode);

  monitor_config = g_new0 (MetaMonitorConfig, 1);
  *monitor_config = (MetaMonitorConfig) {
    .monitor_spec = monitor_spec,
    .mode_spec = monitor_mode_spec,
    .enable_underscanning = enable_underscanning
  };

  return monitor_config;
}

static gboolean
find_monitor_mode_scale (MetaMonitorManager          *manager,
                         MetaLogicalMonitorLayoutMode layout_mode,
                         MetaMonitorConfig           *monitor_config,
                         float                        scale,
                         float                       *out_scale,
                         GError                     **error)
{
  MetaMonitorSpec *monitor_spec;
  MetaMonitor *monitor;
  MetaMonitorModeSpec *monitor_mode_spec;
  MetaMonitorMode *monitor_mode;
  g_autofree float *supported_scales = NULL;
  int n_supported_scales;
  int i;

  monitor_spec = monitor_config->monitor_spec;
  monitor = meta_monitor_manager_get_monitor_from_spec (manager,
                                                        monitor_spec);
  if (!monitor)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Monitor not found");
      return FALSE;
    }

  monitor_mode_spec = monitor_config->mode_spec;
  monitor_mode = meta_monitor_get_mode_from_spec (monitor,
                                                  monitor_mode_spec);
  if (!monitor_mode)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Monitor mode not found");
      return FALSE;
    }

  supported_scales =
    meta_monitor_manager_calculate_supported_scales (manager, layout_mode,
                                                     monitor, monitor_mode,
                                                     &n_supported_scales);

  for (i = 0; i < n_supported_scales; i++)
    {
      float supported_scale = supported_scales[i];

      if (fabsf (supported_scale - scale) < FLT_EPSILON)
        {
          *out_scale = supported_scale;
          return TRUE;
        }
    }

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
               "Scale %g not valid for resolution %dx%d",
               scale,
               monitor_mode_spec->width,
               monitor_mode_spec->height);
  return FALSE;
}

static gboolean
derive_logical_monitor_size (MetaMonitorConfig           *monitor_config,
                             int                         *out_width,
                             int                         *out_height,
                             float                        scale,
                             MetaMonitorTransform         transform,
                             MetaLogicalMonitorLayoutMode layout_mode,
                             GError                     **error)
{
  int width, height;

  if (meta_monitor_transform_is_rotated (transform))
    {
      width = monitor_config->mode_spec->height;
      height = monitor_config->mode_spec->width;
    }
  else
    {
      width = monitor_config->mode_spec->width;
      height = monitor_config->mode_spec->height;
    }

  switch (layout_mode)
    {
    case META_LOGICAL_MONITOR_LAYOUT_MODE_LOGICAL:
      width = roundf (width / scale);
      height = roundf (height / scale);
      break;
    case META_LOGICAL_MONITOR_LAYOUT_MODE_PHYSICAL:
      break;
    }

  *out_width = width;
  *out_height = height;

  return TRUE;
}

static MetaLogicalMonitorConfig *
create_logical_monitor_config_from_variant (MetaMonitorManager          *manager,
                                            GVariant                    *logical_monitor_config_variant,
                                            MetaLogicalMonitorLayoutMode layout_mode,
                                            GError                     **error)
{
  MetaLogicalMonitorConfig *logical_monitor_config;
  int x, y, width, height;
  double scale_d;
  float scale;
  MetaMonitorTransform transform;
  gboolean is_primary;
  GVariantIter *monitor_configs_iter;
  GList *monitor_configs = NULL;
  MetaMonitorConfig *monitor_config;

  g_variant_get (logical_monitor_config_variant, LOGICAL_MONITOR_CONFIG_FORMAT,
                 &x,
                 &y,
                 &scale_d,
                 &transform,
                 &is_primary,
                 &monitor_configs_iter);
  scale = (float) scale_d;

  while (TRUE)
    {
      GVariant *monitor_config_variant =
        g_variant_iter_next_value (monitor_configs_iter);
      MetaMonitorConfig *monitor_config;

      if (!monitor_config_variant)
        break;

      monitor_config =
        create_monitor_config_from_variant (manager,
                                            monitor_config_variant, error);
      g_variant_unref (monitor_config_variant);

      if (!monitor_config)
        goto err;

      if (!meta_verify_monitor_config (monitor_config, error))
        {
          meta_monitor_config_free (monitor_config);
          goto err;
        }

      monitor_configs = g_list_append (monitor_configs, monitor_config);
    }
  g_variant_iter_free (monitor_configs_iter);

  if (!monitor_configs)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Empty logical monitor");
      goto err;
    }

  monitor_config = monitor_configs->data;
  if (!find_monitor_mode_scale (manager,
                                layout_mode,
                                monitor_config,
                                scale,
                                &scale,
                                error))
    goto err;

  if (!derive_logical_monitor_size (monitor_config, &width, &height,
                                    scale, transform, layout_mode, error))
    goto err;

  logical_monitor_config = g_new0 (MetaLogicalMonitorConfig, 1);
  *logical_monitor_config = (MetaLogicalMonitorConfig) {
    .layout = {
      .x = x,
      .y = y,
      .width = width,
      .height = height
    },
    .transform = transform,
    .scale = scale,
    .is_primary = is_primary,
    .monitor_configs = monitor_configs
  };

  if (!meta_verify_logical_monitor_config (logical_monitor_config,
                                           layout_mode,
                                           manager,
                                           error))
    {
      meta_logical_monitor_config_free (logical_monitor_config);
      return NULL;
    }

  return logical_monitor_config;

err:
  g_list_free_full (monitor_configs, (GDestroyNotify) meta_monitor_config_free);
  return NULL;
}

static gboolean
is_valid_layout_mode (MetaLogicalMonitorLayoutMode layout_mode)
{
  switch (layout_mode)
    {
    case META_LOGICAL_MONITOR_LAYOUT_MODE_LOGICAL:
    case META_LOGICAL_MONITOR_LAYOUT_MODE_PHYSICAL:
      return TRUE;
    }

  return FALSE;
}

static gboolean
meta_monitor_manager_handle_apply_monitors_config (MetaDBusDisplayConfig *skeleton,
                                                   GDBusMethodInvocation *invocation,
                                                   guint                  serial,
                                                   guint                  method,
                                                   GVariant              *logical_monitor_configs_variant,
                                                   GVariant              *properties_variant,
                                                   MetaMonitorManager    *manager)
{
  MetaMonitorConfigStore *config_store;
  const MetaMonitorConfigPolicy *policy;
  MetaMonitorManagerCapability capabilities;
  GVariant *layout_mode_variant = NULL;
  MetaLogicalMonitorLayoutMode layout_mode;
  GVariantIter logical_monitor_configs_iter;
  MetaMonitorsConfig *config;
  GList *logical_monitor_configs = NULL;
  GError *error = NULL;

  if (serial != manager->serial)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "The requested configuration is based on stale information");
      return TRUE;
    }

  config_store =
    meta_monitor_config_manager_get_store (manager->config_manager);
  policy = meta_monitor_config_store_get_policy (config_store);

  if (!policy->enable_dbus)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Monitor configuration via D-Bus is disabled");
      return TRUE;
    }

  capabilities = meta_monitor_manager_get_capabilities (manager);

  if (properties_variant)
    layout_mode_variant = g_variant_lookup_value (properties_variant,
                                                  "layout-mode",
                                                  G_VARIANT_TYPE ("u"));

  if (layout_mode_variant &&
      capabilities & META_MONITOR_MANAGER_CAPABILITY_LAYOUT_MODE)
    {
      g_variant_get (layout_mode_variant, "u", &layout_mode);
    }
  else if (!layout_mode_variant)
    {
      layout_mode =
        meta_monitor_manager_get_default_layout_mode (manager);
    }
  else
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "Can't set layout mode");
      return TRUE;
    }

  if (!is_valid_layout_mode (layout_mode))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Invalid layout mode specified");
      return TRUE;
    }

  g_variant_iter_init (&logical_monitor_configs_iter,
                       logical_monitor_configs_variant);
  while (TRUE)
    {
      GVariant *logical_monitor_config_variant =
        g_variant_iter_next_value (&logical_monitor_configs_iter);
      MetaLogicalMonitorConfig *logical_monitor_config;

      if (!logical_monitor_config_variant)
        break;

      logical_monitor_config =
        create_logical_monitor_config_from_variant (manager,
                                                    logical_monitor_config_variant,
                                                    layout_mode,
                                                    &error);
      g_variant_unref (logical_monitor_config_variant);

      if (!logical_monitor_config)
        {
          g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                 G_DBUS_ERROR_INVALID_ARGS,
                                                 "%s", error->message);
          g_error_free (error);
          g_list_free_full (logical_monitor_configs,
                            (GDestroyNotify) meta_logical_monitor_config_free);
          return TRUE;
        }

      logical_monitor_configs = g_list_append (logical_monitor_configs,
                                               logical_monitor_config);
    }

  config = meta_monitors_config_new (manager,
                                     logical_monitor_configs,
                                     layout_mode,
                                     META_MONITORS_CONFIG_FLAG_NONE);
  if (!meta_verify_monitors_config (config, manager, &error))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "%s", error->message);
      g_error_free (error);
      g_object_unref (config);
      return TRUE;
    }

  if (!meta_monitor_manager_is_config_applicable (manager, config, &error))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "%s", error->message);
      g_error_free (error);
      g_object_unref (config);
      return TRUE;
    }

  if (method != META_MONITORS_CONFIG_METHOD_VERIFY)
    {
      g_clear_handle_id (&manager->restore_config_id, g_source_remove);
      g_clear_handle_id (&manager->persistent_timeout_id, g_source_remove);
    }

  if (!meta_monitor_manager_apply_monitors_config (manager,
                                                   config,
                                                   method,
                                                   &error))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "%s", error->message);
      g_error_free (error);
      g_object_unref (config);
      return TRUE;
    }

  if (method == META_MONITORS_CONFIG_METHOD_PERSISTENT)
    request_persistent_confirmation (manager);

  meta_dbus_display_config_complete_apply_monitors_config (skeleton, invocation);

  return TRUE;
}

#undef MONITOR_MODE_SPEC_FORMAT
#undef MONITOR_CONFIG_FORMAT
#undef MONITOR_CONFIGS_FORMAT
#undef LOGICAL_MONITOR_CONFIG_FORMAT

void
meta_monitor_manager_confirm_configuration (MetaMonitorManager *manager,
                                            gboolean            ok)
{
  if (!manager->persistent_timeout_id)
    return;

  g_clear_handle_id (&manager->restore_config_id, g_source_remove);
  g_clear_handle_id (&manager->persistent_timeout_id, g_source_remove);

  if (ok)
    {
      meta_monitor_config_manager_save_current (manager->config_manager);
    }
  else
    {
      manager->restore_config_id =
        g_idle_add_once ((GSourceOnceFunc) restore_previous_config, manager);
    }
}

static gboolean
meta_monitor_manager_handle_change_backlight  (MetaDBusDisplayConfig *skeleton,
                                               GDBusMethodInvocation *invocation,
                                               guint                  serial,
                                               guint                  output_index,
                                               gint                   value,
                                               MetaMonitorManager    *manager)
{
  GList *combined_outputs;
  MetaOutput *output;
  const MetaOutputInfo *output_info;
  int new_backlight;

  if (serial != manager->serial)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "The requested configuration is based on stale information");
      return TRUE;
    }

  combined_outputs = combine_gpu_lists (manager, meta_gpu_get_outputs);

  if (output_index >= g_list_length (combined_outputs))
    {
      g_list_free (combined_outputs);
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "Invalid output id");
      return TRUE;
    }
  output = g_list_nth_data (combined_outputs, output_index);
  g_list_free (combined_outputs);

  if (value < 0 || value > 100)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "Invalid backlight value");
      return TRUE;
    }

  output_info = meta_output_get_info (output);
  if (meta_output_get_backlight (output) == -1 ||
      (output_info->backlight_min == 0 &&
       output_info->backlight_max == 0))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "Output does not support changing backlight");
      return TRUE;
    }

  META_MONITOR_MANAGER_GET_CLASS (manager)->change_backlight (manager, output, value);

  new_backlight = meta_output_get_backlight (output);
  meta_dbus_display_config_complete_change_backlight (skeleton,
                                                      invocation,
                                                      new_backlight);
  return TRUE;
}

static gboolean
meta_monitor_manager_handle_get_crtc_gamma  (MetaDBusDisplayConfig *skeleton,
                                             GDBusMethodInvocation *invocation,
                                             guint                  serial,
                                             guint                  crtc_id,
                                             MetaMonitorManager    *manager)
{
  GList *combined_crtcs;
  MetaCrtc *crtc;
  g_autoptr (MetaGammaLut) gamma_lut = NULL;
  GBytes *red_bytes, *green_bytes, *blue_bytes;
  GVariant *red_v, *green_v, *blue_v;

  if (serial != manager->serial)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "The requested configuration is based on stale information");
      return TRUE;
    }

  combined_crtcs = combine_gpu_lists (manager, meta_gpu_get_crtcs);
  if (crtc_id >= g_list_length (combined_crtcs))
    {
      g_list_free (combined_crtcs);
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "Invalid crtc id");
      return TRUE;
    }

  crtc = g_list_nth_data (combined_crtcs, crtc_id);
  g_list_free (combined_crtcs);

  gamma_lut = meta_crtc_get_gamma_lut (crtc);

  red_bytes = g_bytes_new_take (g_steal_pointer (&gamma_lut->red),
                                gamma_lut->size * sizeof (unsigned short));
  green_bytes = g_bytes_new_take (g_steal_pointer (&gamma_lut->green),
                                  gamma_lut->size * sizeof (unsigned short));
  blue_bytes = g_bytes_new_take (g_steal_pointer (&gamma_lut->blue),
                                 gamma_lut->size * sizeof (unsigned short));

  red_v = g_variant_new_from_bytes (G_VARIANT_TYPE ("aq"), red_bytes, TRUE);
  green_v = g_variant_new_from_bytes (G_VARIANT_TYPE ("aq"), green_bytes, TRUE);
  blue_v = g_variant_new_from_bytes (G_VARIANT_TYPE ("aq"), blue_bytes, TRUE);

  meta_dbus_display_config_complete_get_crtc_gamma (skeleton, invocation,
                                                    red_v, green_v, blue_v);

  g_bytes_unref (red_bytes);
  g_bytes_unref (green_bytes);
  g_bytes_unref (blue_bytes);

  return TRUE;
}

static gboolean
meta_monitor_manager_handle_set_crtc_gamma  (MetaDBusDisplayConfig *skeleton,
                                             GDBusMethodInvocation *invocation,
                                             guint                  serial,
                                             guint                  crtc_id,
                                             GVariant              *red_v,
                                             GVariant              *green_v,
                                             GVariant              *blue_v,
                                             MetaMonitorManager    *manager)
{
  GList *combined_crtcs;
  MetaCrtc *crtc;
  size_t dummy;
  GBytes *red_bytes, *green_bytes, *blue_bytes;
  MetaGammaLut lut;

  if (serial != manager->serial)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "The requested configuration is based on stale information");
      return TRUE;
    }

  combined_crtcs = combine_gpu_lists (manager, meta_gpu_get_crtcs);

  if (crtc_id >= g_list_length (combined_crtcs))
    {
      g_list_free (combined_crtcs);
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "Invalid crtc id");
      return TRUE;
    }

  crtc = g_list_nth_data (combined_crtcs, crtc_id);
  g_list_free (combined_crtcs);

  red_bytes = g_variant_get_data_as_bytes (red_v);
  green_bytes = g_variant_get_data_as_bytes (green_v);
  blue_bytes = g_variant_get_data_as_bytes (blue_v);

  lut.size = g_bytes_get_size (red_bytes) / sizeof (uint16_t);
  lut.red = (uint16_t *) g_bytes_get_data (red_bytes, &dummy);
  lut.green = (uint16_t *) g_bytes_get_data (green_bytes, &dummy);
  lut.blue = (uint16_t *) g_bytes_get_data (blue_bytes, &dummy);

  meta_crtc_set_gamma_lut (crtc, &lut);
  meta_dbus_display_config_complete_set_crtc_gamma (skeleton, invocation);

  g_bytes_unref (red_bytes);
  g_bytes_unref (green_bytes);
  g_bytes_unref (blue_bytes);

  return TRUE;
}

static gboolean
meta_monitor_manager_handle_set_output_ctm  (MetaDBusDisplayConfig *skeleton,
                                             GDBusMethodInvocation *invocation,
                                             guint                  serial,
                                             guint                  output_id,
                                             GVariant              *ctm_var,
                                             MetaMonitorManager    *manager)
{
  MetaMonitorManagerClass *klass;
  GList *combined_outputs;
  MetaOutput *output;
  MetaOutputCtm ctm;
  int i;

  if (serial != manager->serial)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "The requested configuration is based on stale information");
      return TRUE;
    }

  combined_outputs = combine_gpu_lists (manager, meta_gpu_get_outputs);

  if (output_id >= g_list_length (combined_outputs))
    {
      g_list_free (combined_outputs);
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "Invalid output id");
      return TRUE;
    }

  output = g_list_nth_data (combined_outputs, output_id);
  g_list_free (combined_outputs);

  if (g_variant_n_children (ctm_var) != 9)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "Unexpected color transform matrix variant length");
      return TRUE;
    }

  for (i = 0; i < 9; i++)
    {
      GVariant *tmp = g_variant_get_child_value (ctm_var, i);
      ctm.matrix[i] = g_variant_get_uint64 (tmp);
      g_variant_unref (tmp);
    }

  klass = META_MONITOR_MANAGER_GET_CLASS (manager);
  if (klass->set_output_ctm)
    klass->set_output_ctm (output, &ctm);
  meta_dbus_display_config_complete_set_output_ctm (skeleton, invocation);

  return TRUE;
}

static void
monitor_manager_setup_dbus_config_handlers (MetaMonitorManager *manager)
{
  g_signal_connect_object (manager->display_config, "handle-get-resources",
                           G_CALLBACK (meta_monitor_manager_handle_get_resources),
                           manager, 0);
  g_signal_connect_object (manager->display_config, "handle-change-backlight",
                           G_CALLBACK (meta_monitor_manager_handle_change_backlight),
                           manager, 0);
  g_signal_connect_object (manager->display_config, "handle-get-crtc-gamma",
                           G_CALLBACK (meta_monitor_manager_handle_get_crtc_gamma),
                           manager, 0);
  g_signal_connect_object (manager->display_config, "handle-set-crtc-gamma",
                           G_CALLBACK (meta_monitor_manager_handle_set_crtc_gamma),
                           manager, 0);
  g_signal_connect_object (manager->display_config, "handle-get-current-state",
                           G_CALLBACK (meta_monitor_manager_handle_get_current_state),
                           manager, 0);
  g_signal_connect_object (manager->display_config, "handle-apply-monitors-config",
                           G_CALLBACK (meta_monitor_manager_handle_apply_monitors_config),
                           manager, 0);
  g_signal_connect_object (manager->display_config, "handle-set-output-ctm",
                           G_CALLBACK (meta_monitor_manager_handle_set_output_ctm),
                           manager, 0);
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const char      *name,
                 gpointer         user_data)
{
  MetaMonitorManager *manager = user_data;

  g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (manager->display_config),
                                    connection,
                                    "/org/gnome/Mutter/DisplayConfig",
                                    NULL);
}

static void
on_name_acquired (GDBusConnection *connection,
                  const char      *name,
                  gpointer         user_data)
{
  meta_topic (META_DEBUG_DBUS, "Acquired name %s", name);
}

static void
on_name_lost (GDBusConnection *connection,
              const char      *name,
              gpointer         user_data)
{
  meta_topic (META_DEBUG_DBUS, "Lost or failed to acquire name %s", name);
}

static void
initialize_dbus_interface (MetaMonitorManager *manager)
{
  MetaContext *context = meta_backend_get_context (manager->backend);

  manager->dbus_name_id =
    g_bus_own_name (G_BUS_TYPE_SESSION,
                    "org.gnome.Mutter.DisplayConfig",
                    G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                    (meta_context_is_replacing (context) ?
                     G_BUS_NAME_OWNER_FLAGS_REPLACE :
                     G_BUS_NAME_OWNER_FLAGS_NONE),
                    on_bus_acquired,
                    on_name_acquired,
                    on_name_lost,
                    g_object_ref (manager),
                    g_object_unref);
}

/**
 * meta_monitor_manager_get_num_logical_monitors:
 * @manager: A #MetaMonitorManager object
 *
 * Returns the number of `MetaLogicalMonitor`s (can be 0 in case of a
 * headless setup).
 *
 * Returns: the total number of `MetaLogicalMonitor`s.
 */
int
meta_monitor_manager_get_num_logical_monitors (MetaMonitorManager *manager)
{
  return g_list_length (manager->logical_monitors);
}

/**
 * meta_monitor_manager_get_logical_monitors:
 * @manager: A #MetaMonitorManager object
 *
 * Returns the list of `MetaLogicalMonitor`s that is handled. See also
 * meta_monitor_manager_get_num_logical_monitors() if you only need the size of
 * the list.
 *
 * Returns: (transfer none) (nullable): the list of logical monitors.
 */
GList *
meta_monitor_manager_get_logical_monitors (MetaMonitorManager *manager)
{
  return manager->logical_monitors;
}

MetaLogicalMonitor *
meta_monitor_manager_get_logical_monitor_from_number (MetaMonitorManager *manager,
                                                      int                 number)
{
  g_return_val_if_fail ((unsigned int) number < g_list_length (manager->logical_monitors), NULL);

  return g_list_nth (manager->logical_monitors, number)->data;
}

MetaLogicalMonitor *
meta_monitor_manager_get_primary_logical_monitor (MetaMonitorManager *manager)
{
  return manager->primary_logical_monitor;
}

static MetaMonitor *
find_monitor (MetaMonitorManager *monitor_manager,
              gboolean (*match_func) (MetaMonitor *monitor))
{
  GList *monitors;
  GList *l;

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  for (l = monitors; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;

      if (match_func (monitor))
        return monitor;
    }

  return NULL;
}

/**
 * meta_monitor_manager_get_primary_monitor:
 * @manager: A #MetaMonitorManager object
 *
 * Returns the primary monitor. This can be %NULL (e.g. when running headless).
 *
 * Returns: (transfer none) (nullable): The primary #MetaMonitor, or %NULL if
 *          none.
 */
MetaMonitor *
meta_monitor_manager_get_primary_monitor (MetaMonitorManager *manager)
{
  return find_monitor (manager, meta_monitor_is_primary);
}

/**
 * meta_monitor_manager_get_laptop_panel:
 * @manager: A #MetaMonitorManager object
 *
 * Returns the #MetaMonitor that represents the built-in laptop panel (if
 * applicable).
 *
 * Returns: (transfer none) (nullable): The laptop panel, or %NULL if none.
 */
MetaMonitor *
meta_monitor_manager_get_laptop_panel (MetaMonitorManager *manager)
{
  return find_monitor (manager, meta_monitor_is_laptop_panel);
}

MetaMonitor *
meta_monitor_manager_get_monitor_from_connector (MetaMonitorManager *manager,
                                                 const char         *connector)
{
  GList *l;

  for (l = manager->monitors; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;

      if (g_str_equal (meta_monitor_get_connector (monitor),
                       connector))
        return monitor;
    }

  return NULL;
}

MetaMonitor *
meta_monitor_manager_get_monitor_from_spec (MetaMonitorManager *manager,
                                            MetaMonitorSpec    *monitor_spec)
{
  GList *l;

  for (l = manager->monitors; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;

      if (meta_monitor_spec_equals (meta_monitor_get_spec (monitor),
                                    monitor_spec))
        return monitor;
    }

  return NULL;
}

/**
 * meta_monitor_manager_get_logical_monitor_at:
 * @manager: A #MetaMonitorManager object
 * @x: The x-coordinate
 * @y: The y-coordinate
 *
 * Finds the #MetaLogicalMonitor at the given @x and @y coordinates in the
 * total layout.
 *
 * Returns: (transfer none) (nullable): The #MetaLogicalMonitor at the given
 *          point, or %NULL if none.
 */
MetaLogicalMonitor *
meta_monitor_manager_get_logical_monitor_at (MetaMonitorManager *manager,
                                             float               x,
                                             float               y)
{
  GList *l;

  for (l = manager->logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;

      if (META_POINT_IN_RECT (x, y, logical_monitor->rect))
        return logical_monitor;
    }

  return NULL;
}

/**
 * meta_monitor_manager_get_logical_monitor_from_rect:
 * @manager: A #MetaMonitorManager object
 * @rect: The rectangle
 *
 * Finds the #MetaLogicalMonitor which contains the center of the given @rect
 * or which has the largest area in common with the given @rect in the total
 * layout if the center is not on a monitor.
 *
 * Returns: (transfer none) (nullable): The #MetaLogicalMonitor which
 *          corresponds the most to the given @rect, or %NULL if none.
 */
MetaLogicalMonitor *
meta_monitor_manager_get_logical_monitor_from_rect (MetaMonitorManager *manager,
                                                    MtkRectangle       *rect)
{
  MetaLogicalMonitor *best_logical_monitor;
  int best_logical_monitor_area;
  GList *l;
  int center_x = rect->x + (rect->width / 2);
  int center_y = rect->y + (rect->height / 2);

  best_logical_monitor = NULL;
  best_logical_monitor_area = 0;

  for (l = manager->logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;
      MtkRectangle intersection;
      int intersection_area;

      if (META_POINT_IN_RECT (center_x, center_y, logical_monitor->rect))
        return logical_monitor;

      if (!mtk_rectangle_intersect (&logical_monitor->rect,
                                    rect,
                                    &intersection))
        continue;

      intersection_area = mtk_rectangle_area (&intersection);

      if (intersection_area > best_logical_monitor_area)
        {
          best_logical_monitor = logical_monitor;
          best_logical_monitor_area = intersection_area;
        }
    }

  if (!best_logical_monitor)
    best_logical_monitor = manager->primary_logical_monitor;

  return best_logical_monitor;
}

/**
 * meta_monitor_manager_get_highest_scale_from_rect:
 * @manager: A #MetaMonitorManager object
 * @rect: The rectangle
 *
 * Finds the #MetaLogicalMonitor with the highest scale intersecting @rect.
 *
 * Returns: (transfer none) (nullable): the #MetaLogicalMonitor with the
 *          highest scale intersecting with @rect, or %NULL if none.
 */
MetaLogicalMonitor *
meta_monitor_manager_get_highest_scale_monitor_from_rect (MetaMonitorManager *manager,
                                                          MtkRectangle       *rect)
{
  MetaLogicalMonitor *best_logical_monitor = NULL;
  GList *l;
  float best_scale = 0.0;

  for (l = manager->logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;
      MtkRectangle intersection;
      float scale;

      if (!mtk_rectangle_intersect (&logical_monitor->rect,
                                    rect,
                                    &intersection))
        continue;

      scale = meta_logical_monitor_get_scale (logical_monitor);

      if (scale > best_scale)
        {
          best_scale = scale;
          best_logical_monitor = logical_monitor;
        }
    }

  return best_logical_monitor;
}

MetaLogicalMonitor *
meta_monitor_manager_get_logical_monitor_neighbor (MetaMonitorManager  *manager,
                                                   MetaLogicalMonitor  *logical_monitor,
                                                   MetaDisplayDirection direction)
{
  GList *l;

  for (l = manager->logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *other = l->data;

      if (meta_logical_monitor_has_neighbor (logical_monitor, other, direction))
        return other;
    }

  return NULL;
}

/**
 * meta_monitor_manager_get_monitors:
 * @manager: A #MetaMonitorManager object
 *
 * Returns the list of [class@Meta.Monitor]s. See also
 * meta_monitor_manager_get_logical_monitors() for a list of
 * `MetaLogicalMonitor`s.
 *
 * Returns: (transfer none) (nullable): the list of [class@Meta.Monitor]s.
 */
GList *
meta_monitor_manager_get_monitors (MetaMonitorManager *manager)
{
  return manager->monitors;
}

void
meta_monitor_manager_get_screen_size (MetaMonitorManager *manager,
                                      int                *width,
                                      int                *height)
{
  *width = manager->screen_width;
  *height = manager->screen_height;
}

MetaPowerSave
meta_monitor_manager_get_power_save_mode (MetaMonitorManager *manager)
{
  MetaMonitorManagerPrivate *priv =
    meta_monitor_manager_get_instance_private (manager);

  return priv->power_save_mode;
}

static void
destroy_monitor (MetaMonitor *monitor)
{
  g_object_run_dispose (G_OBJECT (monitor));
  g_object_unref (monitor);
}

static void
rebuild_monitors (MetaMonitorManager *manager)
{
  GList *gpus;
  GList *l;

  if (manager->monitors)
    {
      g_list_free_full (manager->monitors, (GDestroyNotify) destroy_monitor);
      manager->monitors = NULL;
    }

  gpus = meta_backend_get_gpus (manager->backend);
  for (l = gpus; l; l = l->next)
    {
      MetaGpu *gpu = l->data;
      GList *k;

      for (k = meta_gpu_get_outputs (gpu); k; k = k->next)
        {
          MetaOutput *output = k->data;
          const MetaOutputInfo *output_info = meta_output_get_info (output);

          if (output_info->tile_info.group_id)
            {
              if (is_main_tiled_monitor_output (output))
                {
                  MetaMonitorTiled *monitor_tiled;

                  monitor_tiled = meta_monitor_tiled_new (manager, output);
                  manager->monitors = g_list_append (manager->monitors,
                                                     monitor_tiled);
                }
            }
          else
            {
              MetaMonitorNormal *monitor_normal;

              monitor_normal = meta_monitor_normal_new (manager, output);
              manager->monitors = g_list_append (manager->monitors,
                                                 monitor_normal);
            }
        }
    }

  for (l = meta_monitor_manager_get_virtual_monitors (manager); l; l = l->next)
    {
      MetaVirtualMonitor *virtual_monitor = l->data;
      MetaOutput *output = meta_virtual_monitor_get_output (virtual_monitor);
      MetaMonitorNormal *monitor_normal;

      monitor_normal = meta_monitor_normal_new (manager, output);
      manager->monitors = g_list_append (manager->monitors,
                                         monitor_normal);

    }

  update_panel_orientation_managed (manager);
  update_has_builtin_panel (manager);
  update_night_light_supported (manager);
}

void
meta_monitor_manager_tiled_monitor_added (MetaMonitorManager *manager,
                                          MetaMonitor        *monitor)
{
  MetaMonitorManagerClass *manager_class =
    META_MONITOR_MANAGER_GET_CLASS (manager);

  if (manager_class->tiled_monitor_added)
    manager_class->tiled_monitor_added (manager, monitor);
}

void
meta_monitor_manager_tiled_monitor_removed (MetaMonitorManager *manager,
                                            MetaMonitor        *monitor)
{
  MetaMonitorManagerClass *manager_class =
    META_MONITOR_MANAGER_GET_CLASS (manager);

  if (manager_class->tiled_monitor_removed)
    manager_class->tiled_monitor_removed (manager, monitor);
}

gboolean
meta_monitor_manager_is_transform_handled (MetaMonitorManager  *manager,
                                           MetaCrtc            *crtc,
                                           MetaMonitorTransform transform)
{
  MetaMonitorManagerClass *manager_class =
    META_MONITOR_MANAGER_GET_CLASS (manager);

  return manager_class->is_transform_handled (manager, crtc, transform);
}

static void
meta_monitor_manager_real_read_current_state (MetaMonitorManager *manager)
{
  GList *l;

  manager->serial++;

  for (l = meta_backend_get_gpus (manager->backend); l; l = l->next)
    {
      MetaGpu *gpu = l->data;
      GError *error = NULL;

      if (!meta_gpu_read_current (gpu, &error))
        {
          g_warning ("Failed to read current monitor state: %s", error->message);
          g_clear_error (&error);
        }
    }

  rebuild_monitors (manager);
}

void
meta_monitor_manager_read_current_state (MetaMonitorManager *manager)
{
  MetaMonitorManagerClass *manager_class =
    META_MONITOR_MANAGER_GET_CLASS (manager);

  manager_class->read_current_state (manager);
}

static void
meta_monitor_manager_notify_monitors_changed (MetaMonitorManager *manager)
{
  meta_backend_monitors_changed (manager->backend);

  g_signal_emit (manager, signals[MONITORS_CHANGED_INTERNAL], 0);
  g_signal_emit (manager, signals[MONITORS_CHANGED], 0);

  meta_dbus_display_config_emit_monitors_changed (manager->display_config);
}

static void
set_logical_monitor_modes (MetaMonitorManager       *manager,
                           MetaLogicalMonitorConfig *logical_monitor_config)
{
  GList *l;

  for (l = logical_monitor_config->monitor_configs; l; l = l->next)
    {
      MetaMonitorConfig *monitor_config = l->data;
      MetaMonitorSpec *monitor_spec;
      MetaMonitor *monitor;
      MetaMonitorModeSpec *monitor_mode_spec;
      MetaMonitorMode *monitor_mode;

      monitor_spec = monitor_config->monitor_spec;
      monitor = meta_monitor_manager_get_monitor_from_spec (manager,
                                                            monitor_spec);
      monitor_mode_spec = monitor_config->mode_spec;
      monitor_mode = meta_monitor_get_mode_from_spec (monitor,
                                                      monitor_mode_spec);

      meta_monitor_set_current_mode (monitor, monitor_mode);
    }
}

static void
meta_monitor_manager_update_monitor_modes (MetaMonitorManager *manager,
                                           MetaMonitorsConfig *config)
{
  GList *logical_monitor_configs;
  GList *l;

  g_list_foreach (manager->monitors,
                  (GFunc) meta_monitor_set_current_mode,
                  NULL);

  logical_monitor_configs = config ? config->logical_monitor_configs : NULL;
  for (l = logical_monitor_configs; l; l = l->next)
    {
      MetaLogicalMonitorConfig *logical_monitor_config = l->data;

      set_logical_monitor_modes (manager, logical_monitor_config);
    }
}

void
meta_monitor_manager_update_logical_state (MetaMonitorManager *manager,
                                           MetaMonitorsConfig *config)
{
  if (config)
    {
      manager->layout_mode = config->layout_mode;
      manager->current_switch_config =
        meta_monitors_config_get_switch_config (config);
    }
  else
    {
      manager->layout_mode =
        meta_monitor_manager_get_default_layout_mode (manager);
      manager->current_switch_config = META_MONITOR_SWITCH_CONFIG_UNKNOWN;
    }

  meta_monitor_manager_rebuild_logical_monitors (manager, config);
}

void
meta_monitor_manager_rebuild (MetaMonitorManager *manager,
                              MetaMonitorsConfig *config)
{
  GList *old_logical_monitors;

  meta_monitor_manager_update_monitor_modes (manager, config);

  if (manager->in_init)
    return;

  old_logical_monitors = manager->logical_monitors;

  meta_monitor_manager_update_logical_state (manager, config);

  meta_monitor_manager_notify_monitors_changed (manager);

  ensure_privacy_screen_settings (manager);

  ensure_hdr_settings (manager);

  g_list_free_full (old_logical_monitors, g_object_unref);
}

static void
meta_monitor_manager_update_monitor_modes_derived (MetaMonitorManager *manager)
{
  GList *l;

  for (l = manager->monitors; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;

      meta_monitor_derive_current_mode (monitor);
    }
}

void
meta_monitor_manager_update_logical_state_derived (MetaMonitorManager *manager,
                                                   MetaMonitorsConfig *config)
{
  if (config)
    manager->current_switch_config =
      meta_monitors_config_get_switch_config (config);
  else
    manager->current_switch_config = META_MONITOR_SWITCH_CONFIG_UNKNOWN;

  manager->layout_mode = META_LOGICAL_MONITOR_LAYOUT_MODE_PHYSICAL;

  meta_monitor_manager_rebuild_logical_monitors_derived (manager, config);
}

void
meta_monitor_manager_rebuild_derived (MetaMonitorManager *manager,
                                      MetaMonitorsConfig *config)
{
  GList *old_logical_monitors;

  meta_monitor_manager_update_monitor_modes_derived (manager);

  if (manager->in_init)
    return;

  old_logical_monitors = manager->logical_monitors;

  meta_monitor_manager_update_logical_state_derived (manager, config);

  meta_monitor_manager_notify_monitors_changed (manager);

  g_list_free_full (old_logical_monitors, g_object_unref);
}

void
meta_monitor_manager_reconfigure (MetaMonitorManager *manager)
{
  meta_monitor_manager_ensure_configured (manager);
}

void
meta_monitor_manager_reload (MetaMonitorManager *manager)
{
  meta_monitor_manager_read_current_state (manager);
  meta_monitor_manager_reconfigure (manager);
}

static gboolean
calculate_viewport_matrix (MetaMonitorManager *manager,
                           MetaLogicalMonitor *logical_monitor,
                           gfloat              viewport[6])
{
  gfloat x, y, width, height;

  x = (float) logical_monitor->rect.x / manager->screen_width;
  y = (float) logical_monitor->rect.y / manager->screen_height;
  width  = (float) logical_monitor->rect.width / manager->screen_width;
  height = (float) logical_monitor->rect.height / manager->screen_height;

  viewport[0] = width;
  viewport[1] = 0.0f;
  viewport[2] = x;
  viewport[3] = 0.0f;
  viewport[4] = height;
  viewport[5] = y;

  return TRUE;
}

static inline void
multiply_matrix (float a[6],
		 float b[6],
		 float res[6])
{
  res[0] = a[0] * b[0] + a[1] * b[3];
  res[1] = a[0] * b[1] + a[1] * b[4];
  res[2] = a[0] * b[2] + a[1] * b[5] + a[2];
  res[3] = a[3] * b[0] + a[4] * b[3];
  res[4] = a[3] * b[1] + a[4] * b[4];
  res[5] = a[3] * b[2] + a[4] * b[5] + a[5];
}

gboolean
meta_monitor_manager_get_monitor_matrix (MetaMonitorManager *manager,
                                         MetaMonitor        *monitor,
                                         MetaLogicalMonitor *logical_monitor,
                                         gfloat              matrix[6])
{
  MetaMonitorTransform transform;
  gfloat viewport[9];

  if (!calculate_viewport_matrix (manager, logical_monitor, viewport))
    return FALSE;

  /* Get transform corrected for LCD panel-orientation. */
  transform = logical_monitor->transform;
  transform = meta_monitor_logical_to_crtc_transform (monitor, transform);
  multiply_matrix (viewport, transform_matrices[transform],
                   matrix);
  return TRUE;
}

/**
 * meta_monitor_manager_get_monitor_for_connector:
 * @manager: A #MetaMonitorManager
 * @connector: A valid connector name
 *
 * Returns: The monitor index or -1 if @id isn't valid or the connector
 * isn't associated with a logical monitor.
 */
gint
meta_monitor_manager_get_monitor_for_connector (MetaMonitorManager *manager,
                                                const char         *connector)
{
  GList *l;

  for (l = manager->monitors; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;

      if (meta_monitor_is_active (monitor) &&
          g_str_equal (connector, meta_monitor_get_connector (monitor)))
        return meta_monitor_get_logical_monitor (monitor)->number;
    }

  return -1;
}

/**
 * meta_monitor_manager_get_is_builtin_display_on:
 * @manager: A #MetaMonitorManager object
 *
 * Returns whether the built-in display (i.e. a laptop panel) is turned on.
 */
gboolean
meta_monitor_manager_get_is_builtin_display_on (MetaMonitorManager *manager)
{
  MetaMonitor *laptop_panel;

  g_return_val_if_fail (META_IS_MONITOR_MANAGER (manager), FALSE);

  laptop_panel = meta_monitor_manager_get_laptop_panel (manager);
  if (!laptop_panel)
    return FALSE;

  return meta_monitor_is_active (laptop_panel);
}

void
meta_monitor_manager_rotate_monitor (MetaMonitorManager *manager)
{
  GError *error = NULL;
  MetaMonitorsConfig *config =
    meta_monitor_config_manager_create_for_rotate_monitor (manager->config_manager);

  if (!config)
    return;

  if (!meta_monitor_manager_apply_monitors_config (manager,
                                                   config,
                                                   META_MONITORS_CONFIG_METHOD_TEMPORARY,
                                                   &error))
    {
      g_warning ("Failed to use rotate monitor configuration: %s",
                 error->message);
      g_error_free (error);
    }
  g_object_unref (config);
}

typedef struct
{
  MetaMonitorManager *monitor_manager;
  MetaMonitorSwitchConfigType config_type;
} SwitchConfigData;

static gboolean
switch_config_idle_cb (gpointer user_data)
{
  SwitchConfigData *data = user_data;
  MetaMonitorManager *monitor_manager = data->monitor_manager;
  MetaMonitorManagerPrivate *priv =
    meta_monitor_manager_get_instance_private (monitor_manager);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaMonitorsConfig *config;
  g_autoptr (GError) error = NULL;

  priv->switch_config_handle_id = 0;

  config =
    meta_monitor_config_manager_create_for_switch_config (config_manager,
                                                          data->config_type);
  if (!config)
    return G_SOURCE_REMOVE;

  if (!meta_monitor_manager_apply_monitors_config (monitor_manager,
                                                   config,
                                                   META_MONITORS_CONFIG_METHOD_TEMPORARY,
                                                   &error))
    {
      g_warning ("Failed to use switch monitor configuration: %s",
                 error->message);
    }
  else
    {
      monitor_manager->current_switch_config = data->config_type;
    }

  return G_SOURCE_REMOVE;
}

void
meta_monitor_manager_switch_config (MetaMonitorManager          *manager,
                                    MetaMonitorSwitchConfigType  config_type)
{
  MetaMonitorManagerPrivate *priv =
    meta_monitor_manager_get_instance_private (manager);
  SwitchConfigData *data;

  g_return_if_fail (config_type != META_MONITOR_SWITCH_CONFIG_UNKNOWN);

  data = g_new0 (SwitchConfigData, 1);
  data->monitor_manager = manager;
  data->config_type = config_type;

  g_clear_handle_id (&priv->switch_config_handle_id, g_source_remove);
  priv->switch_config_handle_id = g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
                                                   switch_config_idle_cb,
                                                   data,
                                                   g_free);
}

gboolean
meta_monitor_manager_can_switch_config (MetaMonitorManager *manager)
{
  return (!meta_backend_is_lid_closed (manager->backend) &&
          g_list_length (manager->monitors) > 1);
}

MetaMonitorSwitchConfigType
meta_monitor_manager_get_switch_config (MetaMonitorManager *manager)
{
  return manager->current_switch_config;
}

MetaMonitorConfigManager *
meta_monitor_manager_get_config_manager (MetaMonitorManager *manager)
{
  return manager->config_manager;
}

gboolean
meta_monitor_manager_get_panel_orientation_managed (MetaMonitorManager *manager)
{
  g_return_val_if_fail (META_IS_MONITOR_MANAGER (manager), FALSE);

  return manager->panel_orientation_managed;
}

void
meta_monitor_manager_post_init (MetaMonitorManager *manager)
{
  ClutterBackend *clutter_backend;
  ClutterSeat *seat;

  if (manager->privacy_screen_change_state ==
      META_PRIVACY_SCREEN_CHANGE_STATE_INIT)
    {
      manager->privacy_screen_change_state =
        META_PRIVACY_SCREEN_CHANGE_STATE_NONE;
    }

  apply_privacy_screen_settings (manager);

  clutter_backend = meta_backend_get_clutter_backend (manager->backend);
  seat = clutter_backend_get_default_seat (clutter_backend);

  g_signal_connect_object (seat, "notify::touch-mode",
                           G_CALLBACK (update_panel_orientation_managed), manager,
                           G_CONNECT_SWAPPED);
}

MetaViewportInfo *
meta_monitor_manager_get_viewports (MetaMonitorManager *manager)
{
  MetaBackend *backend = meta_monitor_manager_get_backend (manager);
  MetaViewportInfo *info;
  GArray *views, *scales;
  GList *logical_monitors, *l;

  views = g_array_new (FALSE, FALSE, sizeof (MtkRectangle));
  scales = g_array_new (FALSE, FALSE, sizeof (float));

  logical_monitors = meta_monitor_manager_get_logical_monitors (manager);

  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;
      MtkRectangle rect;
      float scale;

      rect = logical_monitor->rect;
      g_array_append_val (views, rect);

      scale = logical_monitor->scale;
      g_array_append_val (scales, scale);
    }

  info = meta_viewport_info_new ((MtkRectangle *) views->data,
                                 (float *) scales->data,
                                 views->len,
                                 meta_backend_is_stage_views_scaled (backend));
  g_array_unref (views);
  g_array_unref (scales);

  return info;
}

GList *
meta_monitor_manager_get_virtual_monitors (MetaMonitorManager *manager)
{
  MetaMonitorManagerPrivate *priv =
    meta_monitor_manager_get_instance_private (manager);

  return priv->virtual_monitors;
}
