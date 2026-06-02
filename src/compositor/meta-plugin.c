/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (c) 2008 Intel Corp.
 *
 * Author: Tomas Frydrych <tf@linux.intel.com>
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
 * MetaPlugin:
 *
 * Entry point for plugins
 */

#include "config.h"

#include "meta/meta-plugin.h"

#include <string.h>

#include "backends/meta-monitor-manager-private.h"
#include "compositor/compositor-private.h"
#include "compositor/meta-window-actor-private.h"
#include "compositor/meta-plugin-manager.h"
#include "core/display-private.h"
#include "core/util-private.h"
#include "core/window-private.h"
#include "meta/display.h"
#include "meta/prefs.h"
#include "meta/util.h"


typedef struct _MetaPluginPrivate
{
  MetaCompositor *compositor;
} MetaPluginPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (MetaPlugin, meta_plugin, G_TYPE_OBJECT);

#define MIN_TIME_BETWEEN_VISUAL_ALERTS_MS 500
#define MIN_TIME_BETWEEN_DOUBLE_VISUAL_ALERT_MS 3000

static void
meta_plugin_real_bell_notify (MetaPlugin  *plugin,
                              MetaDisplay *display,
                              MetaWindow  *window)
{
  int64_t now_us;
  int64_t time_difference_ms;
  int n_flashes;

  if (!meta_prefs_get_visual_bell ())
    return;

  now_us = g_get_monotonic_time ();
  time_difference_ms = us2ms (now_us - display->last_visual_bell_time_us);

  if (time_difference_ms < MIN_TIME_BETWEEN_VISUAL_ALERTS_MS)
    return;

  display->last_visual_bell_time_us = now_us;

  n_flashes = (time_difference_ms < MIN_TIME_BETWEEN_DOUBLE_VISUAL_ALERT_MS) ? 1 : 2;

  switch (meta_prefs_get_visual_bell_type ())
    {
    case G_DESKTOP_VISUAL_BELL_FULLSCREEN_FLASH:
      meta_compositor_flash_display (display->compositor, display, n_flashes);
      break;
    case G_DESKTOP_VISUAL_BELL_FRAME_FLASH:
      if (window)
        meta_compositor_flash_window (display->compositor, window, n_flashes);
      else
        meta_compositor_flash_display (display->compositor, display, n_flashes);
      break;
    }
}

static void
meta_plugin_class_init (MetaPluginClass *klass)
{
  klass->bell_notify = meta_plugin_real_bell_notify;
}

static void
meta_plugin_init (MetaPlugin *self)
{
}

void
meta_plugin_switch_workspace_completed (MetaPlugin *plugin)
{
  MetaPluginPrivate *priv = meta_plugin_get_instance_private (plugin);

  meta_switch_workspace_completed (priv->compositor);
}

static void
meta_plugin_window_effect_completed (MetaPlugin      *plugin,
                                     MetaWindowActor *actor,
                                     unsigned long    event)
{
  meta_window_actor_effect_completed (actor, event);
}

void
meta_plugin_minimize_completed (MetaPlugin      *plugin,
                                MetaWindowActor *actor)
{
  meta_plugin_window_effect_completed (plugin, actor, META_PLUGIN_MINIMIZE);
}

void
meta_plugin_unminimize_completed (MetaPlugin      *plugin,
                                  MetaWindowActor *actor)
{
  meta_plugin_window_effect_completed (plugin, actor, META_PLUGIN_UNMINIMIZE);
}

void
meta_plugin_size_change_completed (MetaPlugin      *plugin,
                                   MetaWindowActor *actor)
{
  meta_plugin_window_effect_completed (plugin, actor, META_PLUGIN_SIZE_CHANGE);
}

void
meta_plugin_map_completed (MetaPlugin      *plugin,
                           MetaWindowActor *actor)
{
  meta_plugin_window_effect_completed (plugin, actor, META_PLUGIN_MAP);
}

void
meta_plugin_destroy_completed (MetaPlugin      *plugin,
                               MetaWindowActor *actor)
{
  meta_plugin_window_effect_completed (plugin, actor, META_PLUGIN_DESTROY);
}

/**
 * meta_plugin_get_display:
 * @plugin: a #MetaPlugin
 *
 * Gets the #MetaDisplay corresponding to a plugin.
 *
 * Return value: (transfer none): the #MetaDisplay for the plugin
 */
MetaDisplay *
meta_plugin_get_display (MetaPlugin *plugin)
{
  MetaPluginPrivate *priv = meta_plugin_get_instance_private (plugin);
  MetaDisplay *display = meta_compositor_get_display (priv->compositor);

  return display;
}

void
_meta_plugin_set_compositor (MetaPlugin *plugin, MetaCompositor *compositor)
{
  MetaPluginPrivate *priv = meta_plugin_get_instance_private (plugin);

  priv->compositor = compositor;
}

void
meta_plugin_complete_display_change (MetaPlugin *plugin,
                                     gboolean    ok)
{
  MetaPluginPrivate *priv = meta_plugin_get_instance_private (plugin);
  MetaBackend *backend = meta_compositor_get_backend (priv->compositor);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  meta_monitor_manager_confirm_configuration (monitor_manager, ok);
}
