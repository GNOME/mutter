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
#ifdef HAVE_X11
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#endif

#ifdef HAVE_X11
#include "backends/x11/meta-clutter-backend-x11.h"
#endif
#include "backends/meta-monitor-manager-private.h"
#include "compositor/compositor-private.h"
#include "compositor/meta-window-actor-private.h"
#include "compositor/meta-plugin-manager.h"
#include "meta/display.h"
#include "meta/util.h"


typedef struct _MetaPluginPrivate
{
  MetaCompositor *compositor;
} MetaPluginPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (MetaPlugin, meta_plugin, G_TYPE_OBJECT);

static void
meta_plugin_class_init (MetaPluginClass *klass)
{
}

static void
meta_plugin_init (MetaPlugin *self)
{
}

const MetaPluginInfo *
meta_plugin_get_info (MetaPlugin *plugin)
{
  MetaPluginClass  *klass = META_PLUGIN_GET_CLASS (plugin);

  if (klass && klass->plugin_info)
    return klass->plugin_info (plugin);

  return NULL;
}

#ifdef HAVE_X11
gboolean
_meta_plugin_xevent_filter (MetaPlugin *plugin,
                            XEvent     *xev)
{
  MetaPluginClass *klass = META_PLUGIN_GET_CLASS (plugin);

  if (klass->xevent_filter)
    return klass->xevent_filter (plugin, xev);
  else
    return FALSE;
}
#endif

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
