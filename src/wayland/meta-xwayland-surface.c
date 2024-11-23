/*
 * Copyright (C) 2013 Intel Corporation
 * Copyright (C) 2013-2019 Red Hat Inc.
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

#include "wayland/meta-xwayland-surface.h"

#include "compositor/meta-surface-actor-wayland.h"
#include "compositor/meta-window-actor-private.h"
#include "wayland/meta-wayland-actor-surface.h"
#include "wayland/meta-window-xwayland.h"
#include "wayland/meta-xwayland-private.h"

enum
{
  WINDOW_ASSOCIATED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

struct _MetaXwaylandSurface
{
  MetaWaylandActorSurface parent;

  MetaWindow *window;

  gulong unmanaging_handler_id;
  gulong highest_scale_monitor_handler_id;
};

G_DEFINE_TYPE (MetaXwaylandSurface,
               meta_xwayland_surface,
               META_TYPE_WAYLAND_ACTOR_SURFACE)

static void
clear_window (MetaXwaylandSurface *xwayland_surface)
{
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (xwayland_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaSurfaceActor *surface_actor;
  MetaWindowXwayland *xwayland_window;

  if (!xwayland_surface->window)
    return;

  g_clear_signal_handler (&xwayland_surface->unmanaging_handler_id,
                          xwayland_surface->window);
  g_clear_signal_handler (&xwayland_surface->highest_scale_monitor_handler_id,
                          xwayland_surface->window);
  xwayland_window = META_WINDOW_XWAYLAND (xwayland_surface->window);
  meta_window_xwayland_set_surface (xwayland_window, NULL);
  xwayland_surface->window = NULL;

  surface_actor = meta_wayland_surface_get_actor (surface);
  if (surface_actor)
    clutter_actor_set_reactive (CLUTTER_ACTOR (surface_actor), FALSE);

  meta_wayland_surface_notify_unmapped (surface);
}

static void
window_unmanaging (MetaWindow          *window,
                   MetaXwaylandSurface *xwayland_surface)
{
  clear_window (xwayland_surface);
}

void
meta_xwayland_surface_associate_with_window (MetaXwaylandSurface *xwayland_surface,
                                             MetaWindow          *window)
{
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (xwayland_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWindowXwayland *xwayland_window = META_WINDOW_XWAYLAND (window);
  MetaWaylandSurface *window_surface = meta_window_get_wayland_surface (window);
  MetaSurfaceActor *surface_actor;
  MetaWindowActor *window_actor;

  /*
   * If the window has an existing surface, like if we're undecorating or
   * decorating the window, then we need to detach the window from its old
   * surface.
   */
  if (window_surface)
    {
      MetaXwaylandSurface *other_xwayland_surface;

      other_xwayland_surface = META_XWAYLAND_SURFACE (window_surface->role);
      clear_window (other_xwayland_surface);
    }

  meta_window_xwayland_set_surface (xwayland_window, surface);
  xwayland_surface->window = window;

  surface_actor = meta_wayland_surface_get_actor (surface);
  if (surface_actor)
    clutter_actor_set_reactive (CLUTTER_ACTOR (surface_actor), TRUE);

  xwayland_surface->unmanaging_handler_id =
    g_signal_connect (window,
                      "unmanaging",
                      G_CALLBACK (window_unmanaging),
                      xwayland_surface);

  g_signal_emit (xwayland_surface, signals[WINDOW_ASSOCIATED], 0);

  window_actor = meta_window_actor_from_window (window);
  if (window_actor)
    meta_window_actor_assign_surface_actor (window_actor, surface_actor);

  xwayland_surface->highest_scale_monitor_handler_id =
    g_signal_connect_swapped (window, "highest-scale-monitor-changed",
                              G_CALLBACK (meta_wayland_surface_notify_highest_scale_monitor),
                              surface);
  meta_wayland_surface_notify_highest_scale_monitor (surface);
}

static void
meta_xwayland_surface_assigned (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (meta_xwayland_surface_parent_class);

  surface->dnd.funcs = meta_xwayland_selection_get_drag_dest_funcs ();

  surface_role_class->assigned (surface_role);
}

static void
meta_xwayland_surface_pre_apply_state (MetaWaylandSurfaceRole  *surface_role,
                                       MetaWaylandSurfaceState *pending)
{
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaXwaylandSurface *xwayland_surface = META_XWAYLAND_SURFACE (surface_role);

  if (pending->newly_attached &&
      !surface->buffer &&
      xwayland_surface->window)
    meta_window_queue (xwayland_surface->window, META_QUEUE_CALC_SHOWING);
}

static void
meta_xwayland_surface_get_relative_coordinates (MetaWaylandSurfaceRole *surface_role,
                                                float                   abs_x,
                                                float                   abs_y,
                                                float                  *out_sx,
                                                float                  *out_sy)
{
  MetaXwaylandSurface *xwayland_surface = META_XWAYLAND_SURFACE (surface_role);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandCompositor *compositor =
    meta_wayland_surface_get_compositor (surface);
  MtkRectangle window_rect = { 0 };
  int xwayland_scale;

  if (xwayland_surface->window)
    meta_window_get_buffer_rect (xwayland_surface->window, &window_rect);

  xwayland_scale = meta_xwayland_get_effective_scale (&compositor->xwayland_manager);
  *out_sx = (abs_x - window_rect.x) * xwayland_scale;
  *out_sy = (abs_y - window_rect.y) * xwayland_scale;
}

static MetaWaylandSurface *
meta_xwayland_surface_get_toplevel (MetaWaylandSurfaceRole *surface_role)
{
  return meta_wayland_surface_role_get_surface (surface_role);
}

static MetaWindow *
meta_xwayland_surface_get_window (MetaWaylandSurfaceRole *surface_role)
{
  MetaXwaylandSurface *xwayland_surface = META_XWAYLAND_SURFACE (surface_role);

  return xwayland_surface->window;
}

static int
meta_xwayland_surface_get_geometry_scale (MetaWaylandActorSurface *actor_surface)
{
  return 1;
}

static void
meta_xwayland_surface_sync_actor_state (MetaWaylandActorSurface *actor_surface)
{
  MetaXwaylandSurface *xwayland_surface = META_XWAYLAND_SURFACE (actor_surface);
  MetaWaylandActorSurfaceClass *actor_surface_class =
    META_WAYLAND_ACTOR_SURFACE_CLASS (meta_xwayland_surface_parent_class);

  if (xwayland_surface->window)
    {
      MetaWindowActor *window_actor =
        meta_window_actor_from_window (xwayland_surface->window);

      actor_surface_class->sync_actor_state (actor_surface);
      meta_window_actor_update_regions (window_actor);
    }
}

static void
meta_xwayland_surface_finalize (GObject *object)
{
  MetaXwaylandSurface *xwayland_surface = META_XWAYLAND_SURFACE (object);
  GObjectClass *parent_object_class =
    G_OBJECT_CLASS (meta_xwayland_surface_parent_class);

  clear_window (xwayland_surface);

  parent_object_class->finalize (object);
}

static void
meta_xwayland_surface_init (MetaXwaylandSurface *xwayland_surface)
{
}

static void
meta_xwayland_surface_class_init (MetaXwaylandSurfaceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (klass);
  MetaWaylandActorSurfaceClass *actor_surface_class =
    META_WAYLAND_ACTOR_SURFACE_CLASS (klass);

  object_class->finalize = meta_xwayland_surface_finalize;

  surface_role_class->assigned = meta_xwayland_surface_assigned;
  surface_role_class->pre_apply_state = meta_xwayland_surface_pre_apply_state;
  surface_role_class->get_relative_coordinates =
    meta_xwayland_surface_get_relative_coordinates;
  surface_role_class->get_toplevel = meta_xwayland_surface_get_toplevel;
  surface_role_class->get_window = meta_xwayland_surface_get_window;

  actor_surface_class->get_geometry_scale =
    meta_xwayland_surface_get_geometry_scale;
  actor_surface_class->sync_actor_state =
    meta_xwayland_surface_sync_actor_state;

  signals[WINDOW_ASSOCIATED] =
    g_signal_new ("window-associated",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}
