/*
 * Copyright (C) 2012,2013 Intel Corporation
 * Copyright (C) 2013-2017 Red Hat, Inc.
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
 *
 */

#include "config.h"

#include "wayland/meta-wayland-shell-surface.h"

#include "compositor/meta-surface-actor-wayland.h"
#include "compositor/meta-window-actor-private.h"
#include "compositor/meta-window-actor-wayland.h"
#include "wayland/meta-wayland-actor-surface.h"
#include "wayland/meta-wayland-buffer.h"
#include "wayland/meta-wayland-subsurface.h"
#include "wayland/meta-wayland-surface-private.h"
#include "wayland/meta-window-wayland.h"

typedef struct _MetaWaylandShellSurfacePrivate
{
  MetaWindow *window;

  gulong unmanaging_handler_id;
  gulong highest_scale_monitor_handler_id;
  GBinding *main_monitor_binding;
} MetaWaylandShellSurfacePrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (MetaWaylandShellSurface,
                                     meta_wayland_shell_surface,
                                     META_TYPE_WAYLAND_ACTOR_SURFACE)

void
meta_wayland_shell_surface_calculate_geometry (MetaWaylandShellSurface *shell_surface,
                                               MtkRectangle            *out_geometry)
{
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (shell_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MtkRectangle geometry;
  MetaWaylandSurface *subsurface_surface;

  geometry = (MtkRectangle) {
    .width = meta_wayland_surface_get_width (surface),
    .height = meta_wayland_surface_get_height (surface),
  };

  META_WAYLAND_SURFACE_FOREACH_SUBSURFACE (&surface->applied_state,
                                           subsurface_surface)
    {
      MetaWaylandSubsurface *subsurface;

      subsurface = META_WAYLAND_SUBSURFACE (subsurface_surface->role);
      meta_wayland_subsurface_union_geometry (subsurface,
                                              0, 0,
                                              &geometry);
    }

  *out_geometry = geometry;
}

void
meta_wayland_shell_surface_determine_geometry (MetaWaylandShellSurface *shell_surface,
                                               MtkRectangle            *set_geometry,
                                               MtkRectangle            *out_geometry)
{
  MtkRectangle bounding_geometry = { 0 };
  MtkRectangle intersected_geometry = { 0 };

  meta_wayland_shell_surface_calculate_geometry (shell_surface,
                                                 &bounding_geometry);

  mtk_rectangle_intersect (set_geometry, &bounding_geometry,
                           &intersected_geometry);

  *out_geometry = intersected_geometry;
}

static void
clear_window (MetaWaylandShellSurface *shell_surface)
{
  MetaWaylandShellSurfacePrivate *priv =
    meta_wayland_shell_surface_get_instance_private (shell_surface);
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (shell_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaSurfaceActor *surface_actor;

  if (!priv->window)
    return;

  g_clear_signal_handler (&priv->unmanaging_handler_id,
                          priv->window);
  g_clear_signal_handler (&priv->highest_scale_monitor_handler_id,
                          priv->window);
  priv->window = NULL;

  surface_actor = meta_wayland_surface_get_actor (surface);
  if (surface_actor)
    clutter_actor_set_reactive (CLUTTER_ACTOR (surface_actor), FALSE);

  meta_wayland_surface_notify_unmapped (surface);

  meta_wayland_surface_set_main_monitor (surface, NULL);
  g_clear_object (&priv->main_monitor_binding);
}

static void
window_unmanaging (MetaWindow              *window,
                   MetaWaylandShellSurface *shell_surface)
{
  clear_window (shell_surface);
}

void
meta_wayland_shell_surface_set_window (MetaWaylandShellSurface *shell_surface,
                                       MetaWindow              *window)
{
  MetaWaylandShellSurfacePrivate *priv =
    meta_wayland_shell_surface_get_instance_private (shell_surface);
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (shell_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaSurfaceActor *surface_actor;

  g_assert (!priv->window);

  priv->window = window;

  surface_actor = meta_wayland_surface_get_actor (surface);
  if (surface_actor)
    clutter_actor_set_reactive (CLUTTER_ACTOR (surface_actor), TRUE);

  priv->unmanaging_handler_id =
    g_signal_connect (window,
                      "unmanaging",
                      G_CALLBACK (window_unmanaging),
                      shell_surface);

  meta_window_update_monitor (window, META_WINDOW_UPDATE_MONITOR_FLAGS_NONE);

  priv->main_monitor_binding =
    g_object_bind_property (G_OBJECT (window), "main-monitor",
                            G_OBJECT (surface), "main-monitor",
                            G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  priv->highest_scale_monitor_handler_id =
    g_signal_connect_swapped (window, "highest-scale-monitor-changed",
                              G_CALLBACK (meta_wayland_surface_notify_preferred_scale_monitor),
                              surface);
  meta_wayland_surface_notify_preferred_scale_monitor (surface);
}

void
meta_wayland_shell_surface_configure (MetaWaylandShellSurface        *shell_surface,
                                      MetaWaylandWindowConfiguration *configuration)
{
  MetaWaylandShellSurfaceClass *shell_surface_class =
    META_WAYLAND_SHELL_SURFACE_GET_CLASS (shell_surface);

  shell_surface_class->configure (shell_surface, configuration);
}

void
meta_wayland_shell_surface_ping (MetaWaylandShellSurface *shell_surface,
                                 uint32_t                 serial)
{
  MetaWaylandShellSurfaceClass *shell_surface_class =
    META_WAYLAND_SHELL_SURFACE_GET_CLASS (shell_surface);

  shell_surface_class->ping (shell_surface, serial);
}

void
meta_wayland_shell_surface_close (MetaWaylandShellSurface *shell_surface)
{
  MetaWaylandShellSurfaceClass *shell_surface_class =
    META_WAYLAND_SHELL_SURFACE_GET_CLASS (shell_surface);

  shell_surface_class->close (shell_surface);
}

void
meta_wayland_shell_surface_managed (MetaWaylandShellSurface *shell_surface,
                                    MetaWindow              *window)
{
  MetaWaylandShellSurfaceClass *shell_surface_class =
    META_WAYLAND_SHELL_SURFACE_GET_CLASS (shell_surface);

  shell_surface_class->managed (shell_surface, window);
}

static void
meta_wayland_shell_surface_assigned (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (meta_wayland_shell_surface_parent_class);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);

  surface->dnd.funcs = meta_wayland_data_device_get_drag_dest_funcs ();

  surface_role_class->assigned (surface_role);
}

static void
meta_wayland_shell_surface_surface_pre_apply_state (MetaWaylandSurfaceRole  *surface_role,
                                                    MetaWaylandSurfaceState *pending)
{
  MetaWaylandShellSurface *shell_surface =
    META_WAYLAND_SHELL_SURFACE (surface_role);
  MetaWaylandShellSurfacePrivate *priv =
    meta_wayland_shell_surface_get_instance_private (shell_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);

  if (pending->newly_attached &&
      !surface->buffer &&
      priv->window)
    meta_window_queue (priv->window, META_QUEUE_CALC_SHOWING);
}

static MetaWindow *
meta_wayland_shell_surface_get_window (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandShellSurface *shell_surface =
    META_WAYLAND_SHELL_SURFACE (surface_role);
  MetaWaylandShellSurfacePrivate *priv =
    meta_wayland_shell_surface_get_instance_private (shell_surface);

  return priv->window;
}

static MetaLogicalMonitor *
meta_wayland_shell_surface_get_preferred_scale_monitor (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWindow *window;

  window = meta_wayland_surface_get_window (surface);
  if (!window)
    return NULL;

  return meta_window_get_highest_scale_monitor (window);
}

static void
meta_wayland_shell_surface_notify_subsurface_state_changed (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandShellSurface *shell_surface =
    META_WAYLAND_SHELL_SURFACE (surface_role);
  MetaWaylandShellSurfacePrivate *priv =
    meta_wayland_shell_surface_get_instance_private (shell_surface);
  MetaWindow *window;
  MetaWindowActor *window_actor;

  window = priv->window;
  if (!window)
    return;

  window_actor = meta_window_actor_from_window (window);
  meta_window_actor_wayland_rebuild_surface_tree (window_actor);
}

static int
meta_wayland_shell_surface_get_geometry_scale (MetaWaylandActorSurface *actor_surface)
{
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (actor_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaContext *context =
    meta_wayland_compositor_get_context (surface->compositor);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaWindow *toplevel_window;

  toplevel_window = meta_wayland_surface_get_toplevel_window (surface);
  if (meta_backend_is_stage_views_scaled (backend) ||
      !toplevel_window)
    return 1;
  else
    return meta_window_wayland_get_geometry_scale (toplevel_window);
}

static void
meta_wayland_shell_surface_sync_actor_state (MetaWaylandActorSurface *actor_surface)
{
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (actor_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandActorSurfaceClass *actor_surface_class =
    META_WAYLAND_ACTOR_SURFACE_CLASS (meta_wayland_shell_surface_parent_class);
  MetaWindow *toplevel_window;

  toplevel_window = meta_wayland_surface_get_toplevel_window (surface);
  if (!toplevel_window)
    return;

  actor_surface_class->sync_actor_state (actor_surface);
}

void
meta_wayland_shell_surface_destroy_window (MetaWaylandShellSurface *shell_surface)
{
  MetaWaylandShellSurfacePrivate *priv =
    meta_wayland_shell_surface_get_instance_private (shell_surface);
  MetaWindow *window;
  MetaDisplay *display;
  uint32_t timestamp;

  window = priv->window;
  if (!window)
    return;

  display = meta_window_get_display (window);
  timestamp = meta_display_get_current_time_roundtrip (display);
  meta_window_unmanage (window, timestamp);
  g_assert (!priv->window);
}

static void
meta_wayland_shell_surface_dispose (GObject *object)
{
  MetaWaylandShellSurface *shell_surface = META_WAYLAND_SHELL_SURFACE (object);

  meta_wayland_shell_surface_destroy_window (shell_surface);

  G_OBJECT_CLASS (meta_wayland_shell_surface_parent_class)->dispose (object);
}

static void
meta_wayland_shell_surface_init (MetaWaylandShellSurface *role)
{
}

static void
meta_wayland_shell_surface_class_init (MetaWaylandShellSurfaceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (klass);
  MetaWaylandActorSurfaceClass *actor_surface_class =
    META_WAYLAND_ACTOR_SURFACE_CLASS (klass);

  object_class->dispose = meta_wayland_shell_surface_dispose;

  surface_role_class->assigned = meta_wayland_shell_surface_assigned;
  surface_role_class->pre_apply_state =
    meta_wayland_shell_surface_surface_pre_apply_state;
  surface_role_class->notify_subsurface_state_changed =
    meta_wayland_shell_surface_notify_subsurface_state_changed;
  surface_role_class->get_window = meta_wayland_shell_surface_get_window;
  surface_role_class->get_preferred_scale_monitor =
    meta_wayland_shell_surface_get_preferred_scale_monitor;

  actor_surface_class->get_geometry_scale =
    meta_wayland_shell_surface_get_geometry_scale;
  actor_surface_class->sync_actor_state =
    meta_wayland_shell_surface_sync_actor_state;
}
