/*
 * Copyright (C) 2015-2019 Red Hat, Inc.
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
 */

#include "config.h"

#include "wayland/meta-wayland-dnd-surface.h"

#include "backends/meta-logical-monitor.h"
#include "compositor/meta-feedback-actor-private.h"

struct _MetaWaylandSurfaceRoleDND
{
  MetaWaylandActorSurface parent;
  int32_t pending_offset_x;
  int32_t pending_offset_y;
  int geometry_scale;
};

G_DEFINE_TYPE (MetaWaylandSurfaceRoleDND,
               meta_wayland_surface_role_dnd,
               META_TYPE_WAYLAND_ACTOR_SURFACE)

static void
dnd_surface_assigned (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);

  meta_wayland_surface_queue_pending_frame_callbacks (surface);
}

static void
dnd_surface_commit (MetaWaylandSurfaceRole  *surface_role,
                    MetaWaylandPendingState *pending)
{
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandSurfaceRoleDND *surface_role_dnd =
    META_WAYLAND_SURFACE_ROLE_DND (surface_role);
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (meta_wayland_surface_role_dnd_parent_class);

  meta_wayland_surface_queue_pending_state_frame_callbacks (surface, pending);

  surface_role_dnd->pending_offset_x = pending->dx;
  surface_role_dnd->pending_offset_y = pending->dy;

  surface_role_class->commit (surface_role, pending);
}

static MetaLogicalMonitor *
dnd_surface_find_logical_monitor (MetaWaylandActorSurface *actor_surface)
{
  MetaBackend *backend = meta_get_backend ();
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);
  MetaMonitorManager *monitor_manager =
     meta_backend_get_monitor_manager (backend);
  ClutterPoint pointer_pos;

  pointer_pos = meta_cursor_renderer_get_position (cursor_renderer);
  return meta_monitor_manager_get_logical_monitor_at (monitor_manager,
                                                      pointer_pos.x,
                                                      pointer_pos.y);
}

static double
dnd_subsurface_get_geometry_scale (MetaWaylandActorSurface *actor_surface)
{
  if (meta_is_stage_views_scaled ())
    {
      return 1;
    }
  else
    {
      MetaLogicalMonitor *logical_monitor;

      logical_monitor = dnd_surface_find_logical_monitor (actor_surface);
      return meta_logical_monitor_get_scale (logical_monitor);
    }
}

static void
dnd_subsurface_sync_actor_state (MetaWaylandActorSurface *actor_surface)
{
  MetaSurfaceActor *surface_actor =
    meta_wayland_actor_surface_get_actor (actor_surface);
  MetaFeedbackActor *feedback_actor =
    META_FEEDBACK_ACTOR (clutter_actor_get_parent (CLUTTER_ACTOR (surface_actor)));
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (actor_surface);
  MetaWaylandSurfaceRoleDND *surface_role_dnd =
    META_WAYLAND_SURFACE_ROLE_DND (surface_role);
  MetaWaylandActorSurfaceClass *actor_surface_class =
    META_WAYLAND_ACTOR_SURFACE_CLASS (meta_wayland_surface_role_dnd_parent_class);
  int geometry_scale;
  float anchor_x;
  float anchor_y;

  g_return_if_fail (META_IS_FEEDBACK_ACTOR (feedback_actor));

  geometry_scale =
    meta_wayland_actor_surface_get_geometry_scale (actor_surface);

  meta_feedback_actor_get_anchor (feedback_actor, &anchor_x, &anchor_y);
  anchor_x -= surface_role_dnd->pending_offset_x * geometry_scale;
  anchor_y -= surface_role_dnd->pending_offset_y * geometry_scale;
  meta_feedback_actor_set_anchor (feedback_actor, anchor_x, anchor_y);

  if (surface_role_dnd->geometry_scale != geometry_scale)
    {
      surface_role_dnd->geometry_scale = geometry_scale;
      clutter_actor_set_scale (CLUTTER_ACTOR (surface_actor),
                               geometry_scale,
                               geometry_scale);
    }

  actor_surface_class->sync_actor_state (actor_surface);
}

static void
meta_wayland_surface_role_dnd_init (MetaWaylandSurfaceRoleDND *role)
{
  role->geometry_scale = 1;
}

static void
meta_wayland_surface_role_dnd_class_init (MetaWaylandSurfaceRoleDNDClass *klass)
{
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (klass);
  MetaWaylandActorSurfaceClass *actor_surface_class =
    META_WAYLAND_ACTOR_SURFACE_CLASS (klass);

  surface_role_class->assigned = dnd_surface_assigned;
  surface_role_class->commit = dnd_surface_commit;

  actor_surface_class->get_geometry_scale = dnd_subsurface_get_geometry_scale;
  actor_surface_class->sync_actor_state = dnd_subsurface_sync_actor_state;
}
