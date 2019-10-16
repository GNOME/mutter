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

static void
meta_xwayland_surface_assigned (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (meta_xwayland_surface_parent_class);

  /* See comment in xwayland_surface_commit for why we reply even though the
   * surface may not be drawn the next frame.
   */
  wl_list_insert_list (&surface->compositor->frame_callbacks,
                       &surface->pending_frame_callback_list);
  wl_list_init (&surface->pending_frame_callback_list);

  surface_role_class->assigned (surface_role);
}

static void
meta_xwayland_surface_commit (MetaWaylandSurfaceRole  *surface_role,
                              MetaWaylandPendingState *pending)
{
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (meta_xwayland_surface_parent_class);

  /* For Xwayland windows, throttling frames when the window isn't actually
   * drawn is less useful, because Xwayland still has to do the drawing sent
   * from the application - the throttling would only be of sending us damage
   * messages, so we simplify and send frame callbacks after the next paint of
   * the screen, whether the window was drawn or not.
   *
   * Currently it may take a few frames before we draw the window, for not
   * completely understood reasons, and in that case, not thottling frame
   * callbacks to drawing has the happy side effect that we avoid showing the
   * user the initial black frame from when the window is mapped empty.
   */
  meta_wayland_surface_queue_pending_state_frame_callbacks (surface, pending);

  surface_role_class->commit (surface_role, pending);
}

static void
meta_xwayland_surface_get_relative_coordinates (MetaWaylandSurfaceRole *surface_role,
                                                float                   abs_x,
                                                float                   abs_y,
                                                float                  *out_sx,
                                                float                  *out_sy)
{
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaRectangle window_rect;

  meta_window_get_buffer_rect (meta_wayland_surface_get_window (surface),
                               &window_rect);
  *out_sx = abs_x - window_rect.x;
  *out_sy = abs_y - window_rect.y;
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

  surface_role_class->assigned = meta_xwayland_surface_assigned;
  surface_role_class->commit = meta_xwayland_surface_commit;
  surface_role_class->get_relative_coordinates =
    meta_xwayland_surface_get_relative_coordinates;
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
