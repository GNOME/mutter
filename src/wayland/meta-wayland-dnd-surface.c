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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "wayland/meta-wayland-dnd-surface.h"

#include "backends/meta-logical-monitor.h"
#include "compositor/meta-feedback-actor-private.h"
#include "wayland/meta-wayland.h"

struct _MetaWaylandSurfaceRoleDND
{
  MetaWaylandActorSurface parent;
  ClutterInputDevice *device;
  ClutterEventSequence *sequence;
  int32_t pending_offset_x;
  int32_t pending_offset_y;
};

enum
{
  PROP_0,
  PROP_DEVICE,
  PROP_EVENT_SEQUENCE,
  N_PROPS,
};

static GParamSpec *props[N_PROPS] = { 0, };

G_DEFINE_TYPE (MetaWaylandSurfaceRoleDND,
               meta_wayland_surface_role_dnd,
               META_TYPE_WAYLAND_ACTOR_SURFACE)

static void
dnd_surface_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  MetaWaylandSurfaceRoleDND *surface_role_dnd =
    META_WAYLAND_SURFACE_ROLE_DND (object);

  switch (prop_id)
    {
    case PROP_DEVICE:
      surface_role_dnd->device = g_value_get_object (value);
      break;
    case PROP_EVENT_SEQUENCE:
      surface_role_dnd->sequence = g_value_get_boxed (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
dnd_surface_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  MetaWaylandSurfaceRoleDND *surface_role_dnd =
    META_WAYLAND_SURFACE_ROLE_DND (object);

  switch (prop_id)
    {
    case PROP_DEVICE:
      g_value_set_object (value, surface_role_dnd->device);
      break;
    case PROP_EVENT_SEQUENCE:
      g_value_set_boxed (value, surface_role_dnd->sequence);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
dnd_surface_assigned (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);

  if (wl_list_empty (&surface->unassigned.pending_frame_callback_list))
    return;

  meta_wayland_compositor_add_frame_callback_surface (surface->compositor,
                                                      surface);
}

static void
dnd_surface_apply_state (MetaWaylandSurfaceRole  *surface_role,
                         MetaWaylandSurfaceState *pending)
{
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandSurfaceRoleDND *surface_role_dnd =
    META_WAYLAND_SURFACE_ROLE_DND (surface_role);
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (meta_wayland_surface_role_dnd_parent_class);

  meta_wayland_compositor_add_frame_callback_surface (surface->compositor,
                                                      surface);

  surface_role_dnd->pending_offset_x = pending->dx;
  surface_role_dnd->pending_offset_y = pending->dy;

  surface_role_class->apply_state (surface_role, pending);
}

static MetaLogicalMonitor *
dnd_surface_find_logical_monitor (MetaWaylandActorSurface *actor_surface)
{
  MetaWaylandSurfaceRoleDND *surface_role_dnd =
    META_WAYLAND_SURFACE_ROLE_DND (actor_surface);
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (actor_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaContext *context =
    meta_wayland_compositor_get_context (surface->compositor);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaMonitorManager *monitor_manager =
     meta_backend_get_monitor_manager (backend);
  ClutterSeat *seat =
    clutter_input_device_get_seat (surface_role_dnd->device);
  graphene_point_t point;

  if (!clutter_seat_query_state (seat, surface_role_dnd->device,
                                 surface_role_dnd->sequence, &point, NULL))
    return NULL;

  return meta_monitor_manager_get_logical_monitor_at (monitor_manager,
                                                      point.x, point.y);
}

static int
dnd_subsurface_get_geometry_scale (MetaWaylandActorSurface *actor_surface)
{
  MetaWaylandSurfaceRole *role = META_WAYLAND_SURFACE_ROLE (actor_surface);
  MetaWaylandSurface *surface = meta_wayland_surface_role_get_surface (role);
  MetaContext *context =
    meta_wayland_compositor_get_context (surface->compositor);
  MetaBackend *backend = meta_context_get_backend (context);

  if (!meta_backend_is_stage_views_scaled (backend))
    {
      MetaLogicalMonitor *logical_monitor;

      logical_monitor = dnd_surface_find_logical_monitor (actor_surface);
      if (logical_monitor)
        return (int) roundf (meta_logical_monitor_get_scale (logical_monitor));
    }

  return 1;
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
  meta_feedback_actor_set_geometry_scale (feedback_actor, geometry_scale);

  meta_feedback_actor_get_anchor (feedback_actor, &anchor_x, &anchor_y);
  anchor_x -= surface_role_dnd->pending_offset_x;
  anchor_y -= surface_role_dnd->pending_offset_y;
  meta_feedback_actor_set_anchor (feedback_actor, anchor_x, anchor_y);

  actor_surface_class->sync_actor_state (actor_surface);
}

static void
meta_wayland_surface_role_dnd_init (MetaWaylandSurfaceRoleDND *role)
{
}

static void
meta_wayland_surface_role_dnd_class_init (MetaWaylandSurfaceRoleDNDClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (klass);
  MetaWaylandActorSurfaceClass *actor_surface_class =
    META_WAYLAND_ACTOR_SURFACE_CLASS (klass);

  object_class->set_property = dnd_surface_set_property;
  object_class->get_property = dnd_surface_get_property;

  surface_role_class->assigned = dnd_surface_assigned;
  surface_role_class->apply_state = dnd_surface_apply_state;

  actor_surface_class->get_geometry_scale = dnd_subsurface_get_geometry_scale;
  actor_surface_class->sync_actor_state = dnd_subsurface_sync_actor_state;

  props[PROP_DEVICE] =
    g_param_spec_object ("device", NULL, NULL,
                         CLUTTER_TYPE_INPUT_DEVICE,
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS);
  props[PROP_EVENT_SEQUENCE] =
    g_param_spec_boxed ("event-sequence", NULL, NULL,
                        CLUTTER_TYPE_EVENT_SEQUENCE,
                        G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, props);
}
