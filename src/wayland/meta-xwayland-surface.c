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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#include "config.h"

#include "wayland/meta-xwayland-surface.h"

#include "compositor/meta-surface-actor-wayland.h"
#include "compositor/meta-window-actor-private.h"
#include "wayland/meta-wayland-actor-surface.h"

enum
{
  WINDOW_ASSOCIATED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

struct _MetaXwaylandSurface
{
  MetaWaylandActorSurface parent;
};

G_DEFINE_TYPE (MetaXwaylandSurface,
               meta_xwayland_surface,
               META_TYPE_WAYLAND_ACTOR_SURFACE)

void
meta_xwayland_surface_associate_with_window (MetaXwaylandSurface *xwayland_surface,
                                             MetaWindow          *window)
{
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (xwayland_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWindowActor *window_actor;

  /*
   * If the window has an existing surface, like if we're undecorating or
   * decorating the window, then we need to detach the window from its old
   * surface.
   */
  if (window->surface)
    {
      meta_wayland_surface_set_window (window->surface, NULL);
      window->surface = NULL;
    }

  window->surface = surface;
  meta_wayland_surface_set_window (surface, window);
  g_signal_emit (xwayland_surface, signals[WINDOW_ASSOCIATED], 0);

  window_actor = meta_window_actor_from_window (window);
  if (window_actor)
    {
      MetaSurfaceActor *surface_actor;

      surface_actor = meta_wayland_surface_get_actor (surface);
      meta_window_actor_assign_surface_actor (window_actor, surface_actor);
    }
}

static MetaWaylandSurface *
meta_xwayland_surface_get_toplevel (MetaWaylandSurfaceRole *surface_role)
{
  return meta_wayland_surface_role_get_surface (surface_role);
}

static double
meta_xwayland_surface_get_geometry_scale (MetaWaylandActorSurface *actor_surface)
{
  return 1;
}

static void
meta_xwayland_surface_sync_actor_state (MetaWaylandActorSurface *actor_surface)
{
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (actor_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandActorSurfaceClass *actor_surface_class =
    META_WAYLAND_ACTOR_SURFACE_CLASS (meta_xwayland_surface_parent_class);

  if (meta_wayland_surface_get_window (surface))
    actor_surface_class->sync_actor_state (actor_surface);
}

static void
meta_xwayland_surface_finalize (GObject *object)
{
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (object);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  GObjectClass *parent_object_class =
    G_OBJECT_CLASS (meta_xwayland_surface_parent_class);
  MetaWindow *window;

  window = meta_wayland_surface_get_window (surface);
  if (window)
    {
      meta_wayland_surface_set_window (surface, NULL);
      window->surface = NULL;
    }

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

  surface_role_class->get_toplevel = meta_xwayland_surface_get_toplevel;

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
