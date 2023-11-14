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
 */

#include "config.h"

#include "wayland/meta-wayland-subsurface.h"

#include "compositor/meta-surface-actor-wayland.h"
#include "compositor/meta-window-actor-wayland.h"
#include "wayland/meta-wayland.h"
#include "wayland/meta-wayland-actor-surface.h"
#include "wayland/meta-wayland-buffer.h"
#include "wayland/meta-wayland-surface-private.h"
#include "wayland/meta-wayland-transaction.h"
#include "wayland/meta-window-wayland.h"

struct _MetaWaylandSubsurface
{
  MetaWaylandActorSurface parent;
};

G_DEFINE_TYPE (MetaWaylandSubsurface,
               meta_wayland_subsurface,
               META_TYPE_WAYLAND_ACTOR_SURFACE)

static void
transform_subsurface_position (MetaWaylandSurface *surface,
                               int                *x,
                               int                *y)
{
  do
    {
      *x += surface->sub.x;
      *y += surface->sub.y;

      surface = surface->applied_state.parent;
    }
  while (surface);
}

static gboolean
should_show (MetaWaylandSurface *surface)
{
  if (!surface->buffer)
    return FALSE;
  else if (surface->applied_state.parent)
    return should_show (surface->applied_state.parent);
  else
    return TRUE;
}

static void
sync_actor_subsurface_state (MetaWaylandSurface *surface)
{
  ClutterActor *actor = CLUTTER_ACTOR (meta_wayland_surface_get_actor (surface));
  MetaWindow *toplevel_window;
  int x, y;

  toplevel_window = meta_wayland_surface_get_toplevel_window (surface);
  if (!toplevel_window || !should_show (surface))
    {
      clutter_actor_hide (actor);
      return;
    }

  if (toplevel_window->client_type == META_WINDOW_CLIENT_TYPE_X11)
    return;

  x = y = 0;
  transform_subsurface_position (surface, &x, &y);

  clutter_actor_set_position (actor, x, y);
  clutter_actor_set_reactive (actor, TRUE);

  clutter_actor_show (actor);

  clutter_actor_notify_transform_invalid (actor);
}

static gboolean
is_child (MetaWaylandSurface *surface,
          MetaWaylandSurface *sibling)
{
  return surface->committed_state.parent == sibling;
}

static gboolean
is_sibling (MetaWaylandSurface *surface,
            MetaWaylandSurface *sibling)
{
  return surface != sibling &&
         surface->committed_state.parent == sibling->committed_state.parent;
}

void
meta_wayland_subsurface_union_geometry (MetaWaylandSubsurface *subsurface,
                                        int                    parent_x,
                                        int                    parent_y,
                                        MtkRectangle          *out_geometry)
{
  MetaWaylandSurfaceRole *surface_role = META_WAYLAND_SURFACE_ROLE (subsurface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MtkRectangle geometry;
  MetaWaylandSurface *subsurface_surface;

  geometry = (MtkRectangle) {
    .x = surface->offset_x + surface->sub.x,
    .y = surface->offset_y + surface->sub.y,
    .width = meta_wayland_surface_get_width (surface),
    .height = meta_wayland_surface_get_height (surface),
  };

  if (surface->buffer)
    mtk_rectangle_union (out_geometry, &geometry, out_geometry);

  META_WAYLAND_SURFACE_FOREACH_SUBSURFACE (&surface->applied_state,
                                           subsurface_surface)
    {
      MetaWaylandSubsurface *subsurface;

      subsurface = META_WAYLAND_SUBSURFACE (subsurface_surface->role);
      meta_wayland_subsurface_union_geometry (subsurface,
                                              parent_x + geometry.x,
                                              parent_y + geometry.y,
                                              out_geometry);
    }
}

static void
meta_wayland_subsurface_assigned (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (meta_wayland_subsurface_parent_class);

  surface->dnd.funcs = meta_wayland_data_device_get_drag_dest_funcs ();

  surface_role_class->assigned (surface_role);
}

static MetaWaylandSurface *
meta_wayland_subsurface_get_toplevel (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandSurface *parent = surface->applied_state.parent;

  if (parent)
    return meta_wayland_surface_get_toplevel (parent);
  else
    return NULL;
}

static MetaWindow *
meta_wayland_subsurface_get_window (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandSurface *parent;

  parent = surface->committed_state.parent;
  if (parent)
    return meta_wayland_surface_get_window (parent);
  else
    return NULL;
}

static gboolean
meta_wayland_subsurface_is_synchronized (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandSurface *parent;

  if (surface->sub.synchronous)
    return TRUE;

  parent = surface->committed_state.parent;
  if (parent)
    return meta_wayland_surface_is_synchronized (parent);

  return TRUE;
}

static void
meta_wayland_subsurface_notify_subsurface_state_changed (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandSurface *parent = surface->applied_state.parent;

  if (parent)
    return meta_wayland_surface_notify_subsurface_state_changed (parent);
}

static int
meta_wayland_subsurface_get_geometry_scale (MetaWaylandActorSurface *actor_surface)
{
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (actor_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandSurface *parent = surface->applied_state.parent;

  if (parent)
    {
      MetaWaylandActorSurface *parent_actor;

      parent_actor =
        META_WAYLAND_ACTOR_SURFACE (surface->applied_state.parent->role);
      return meta_wayland_actor_surface_get_geometry_scale (parent_actor);
    }
  else
    {
      return 1;
    }
}

static void
meta_wayland_subsurface_sync_actor_state (MetaWaylandActorSurface *actor_surface)
{
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (actor_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandActorSurfaceClass *actor_surface_class =
    META_WAYLAND_ACTOR_SURFACE_CLASS (meta_wayland_subsurface_parent_class);

  if (meta_wayland_surface_get_window (surface))
    actor_surface_class->sync_actor_state (actor_surface);

  sync_actor_subsurface_state (surface);
}

static void
meta_wayland_subsurface_init (MetaWaylandSubsurface *role)
{
}

static void
meta_wayland_subsurface_class_init (MetaWaylandSubsurfaceClass *klass)
{
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (klass);
  MetaWaylandActorSurfaceClass *actor_surface_class =
    META_WAYLAND_ACTOR_SURFACE_CLASS (klass);

  surface_role_class->assigned = meta_wayland_subsurface_assigned;
  surface_role_class->get_toplevel = meta_wayland_subsurface_get_toplevel;
  surface_role_class->get_window = meta_wayland_subsurface_get_window;
  surface_role_class->is_synchronized = meta_wayland_subsurface_is_synchronized;
  surface_role_class->notify_subsurface_state_changed =
    meta_wayland_subsurface_notify_subsurface_state_changed;

  actor_surface_class->get_geometry_scale =
    meta_wayland_subsurface_get_geometry_scale;
  actor_surface_class->sync_actor_state =
    meta_wayland_subsurface_sync_actor_state;
}

static void
wl_subsurface_destroy (struct wl_client   *client,
                       struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
wl_subsurface_set_position (struct wl_client   *client,
                            struct wl_resource *resource,
                            int32_t             x,
                            int32_t             y)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (resource);
  MetaWaylandTransaction *transaction;

  transaction = meta_wayland_surface_ensure_transaction (surface);
  meta_wayland_transaction_add_subsurface_position (transaction, surface, x, y);
}

static gboolean
is_valid_sibling (MetaWaylandSurface *surface,
                  MetaWaylandSurface *sibling)
{
  if (is_child (surface, sibling))
    return TRUE;
  if (is_sibling (surface, sibling))
    return TRUE;
  return FALSE;
}

static MetaWaylandSubsurfacePlacementOp *
get_subsurface_placement_op (MetaWaylandSurface             *surface,
                             MetaWaylandSurface             *sibling,
                             MetaWaylandSubsurfacePlacement  placement)
{
  MetaWaylandSurface *parent = surface->committed_state.parent;
  MetaWaylandSubsurfacePlacementOp *op =
    g_new0 (MetaWaylandSubsurfacePlacementOp, 1);
  GNode *sibling_node;

  op->placement = placement;
  op->sibling = sibling;
  op->surface = surface;

  g_node_unlink (surface->committed_state.subsurface_branch_node);

  if (!sibling)
    return op;

  if (sibling == parent)
    sibling_node = parent->committed_state.subsurface_leaf_node;
  else
    sibling_node = sibling->committed_state.subsurface_branch_node;

  switch (placement)
    {
    case META_WAYLAND_SUBSURFACE_PLACEMENT_ABOVE:
      g_node_insert_after (parent->committed_state.subsurface_branch_node,
                           sibling_node,
                           surface->committed_state.subsurface_branch_node);
      break;
    case META_WAYLAND_SUBSURFACE_PLACEMENT_BELOW:
      g_node_insert_before (parent->committed_state.subsurface_branch_node,
                            sibling_node,
                            surface->committed_state.subsurface_branch_node);
      break;
    }

  return op;
}

static void
subsurface_place (struct wl_client               *client,
                  struct wl_resource             *resource,
                  struct wl_resource             *sibling_resource,
                  MetaWaylandSubsurfacePlacement  placement)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (resource);
  MetaWaylandSurface *sibling = wl_resource_get_user_data (sibling_resource);
  MetaWaylandSurfaceState *pending_state;
  MetaWaylandSubsurfacePlacementOp *op;

  if (!is_valid_sibling (surface, sibling))
    {
      wl_resource_post_error (resource, WL_SUBSURFACE_ERROR_BAD_SURFACE,
                              "wl_subsurface::place_%s: wl_surface@%d is "
                              "not a valid parent or sibling",
                              placement == META_WAYLAND_SUBSURFACE_PLACEMENT_ABOVE ?
                              "above" : "below",
                              wl_resource_get_id (sibling->resource));
      return;
    }

  op = get_subsurface_placement_op (surface,
                                    sibling,
                                    placement);

  pending_state =
    meta_wayland_surface_get_pending_state (surface->committed_state.parent);
  pending_state->subsurface_placement_ops =
    g_slist_append (pending_state->subsurface_placement_ops, op);
}

static void
wl_subsurface_place_above (struct wl_client   *client,
                           struct wl_resource *resource,
                           struct wl_resource *sibling_resource)
{
  subsurface_place (client, resource, sibling_resource,
                    META_WAYLAND_SUBSURFACE_PLACEMENT_ABOVE);
}

static void
wl_subsurface_place_below (struct wl_client   *client,
                           struct wl_resource *resource,
                           struct wl_resource *sibling_resource)
{
  subsurface_place (client, resource, sibling_resource,
                    META_WAYLAND_SUBSURFACE_PLACEMENT_BELOW);
}

void
meta_wayland_subsurface_drop_placement_ops (MetaWaylandSurfaceState *state,
                                            MetaWaylandSurface      *surface)
{
  GSList **list = &state->subsurface_placement_ops;
  GSList *link = *list;

  while (link)
    {
      MetaWaylandSubsurfacePlacementOp *op = link->data;

      if (op->surface == surface)
        {
          g_free (link->data);
          *list = g_slist_delete_link (*list, link);
        }
      else
        {
          list = &link->next;
        }

      link = *list;
    }
}

static void
permanently_unmap_subsurface (MetaWaylandSurface *surface)
{
  MetaWaylandSubsurfacePlacementOp *op;
  MetaWaylandTransaction *transaction;
  MetaWaylandSurface *parent;
  MetaWaylandSurfaceState *pending_state;

  op = get_subsurface_placement_op (surface, NULL,
                                    META_WAYLAND_SUBSURFACE_PLACEMENT_BELOW);

  transaction = meta_wayland_transaction_new (surface->compositor);
  meta_wayland_transaction_add_placement_op (transaction,
                                             surface->committed_state.parent, op);
  meta_wayland_transaction_add_subsurface_position (transaction, surface, 0, 0);
  meta_wayland_transaction_commit (transaction);

  if (surface->sub.transaction)
    meta_wayland_transaction_drop_subsurface_state (surface->sub.transaction,
                                                    surface);

  parent = surface->committed_state.parent;
  pending_state = meta_wayland_surface_get_pending_state (parent);
  if (pending_state && pending_state->subsurface_placement_ops)
    meta_wayland_subsurface_drop_placement_ops (pending_state, surface);

  while (parent)
    {
      if (parent->sub.transaction)
        meta_wayland_transaction_drop_subsurface_state (parent->sub.transaction,
                                                        surface);

      parent = parent->committed_state.parent;
    }

  surface->committed_state.parent = NULL;
}

static void
wl_subsurface_destructor (struct wl_resource *resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (resource);

  if (surface->committed_state.parent)
    permanently_unmap_subsurface (surface);

  surface->wl_subsurface = NULL;
}

static void
wl_subsurface_set_sync (struct wl_client   *client,
                        struct wl_resource *resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (resource);

  surface->sub.synchronous = TRUE;
}

static void
meta_wayland_subsurface_parent_desynced (MetaWaylandSurface *surface)
{
  MetaWaylandSurface *subsurface_surface;

  if (surface->sub.synchronous)
    return;

  if (surface->sub.transaction)
    meta_wayland_transaction_commit (g_steal_pointer (&surface->sub.transaction));

  META_WAYLAND_SURFACE_FOREACH_SUBSURFACE (&surface->committed_state,
                                           subsurface_surface)
    meta_wayland_subsurface_parent_desynced (subsurface_surface);
}

static void
wl_subsurface_set_desync (struct wl_client   *client,
                          struct wl_resource *resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (resource);

  if (!surface->sub.synchronous)
    return;

  surface->sub.synchronous = FALSE;

  if (!meta_wayland_surface_is_synchronized (surface))
    meta_wayland_subsurface_parent_desynced (surface);
}

static const struct wl_subsurface_interface meta_wayland_wl_subsurface_interface = {
  wl_subsurface_destroy,
  wl_subsurface_set_position,
  wl_subsurface_place_above,
  wl_subsurface_place_below,
  wl_subsurface_set_sync,
  wl_subsurface_set_desync,
};

static void
wl_subcompositor_destroy (struct wl_client   *client,
                          struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

void
meta_wayland_subsurface_parent_destroyed (MetaWaylandSurface *surface)
{
  permanently_unmap_subsurface (surface);
}

static gboolean
is_same_or_ancestor (MetaWaylandSurface *surface,
                     MetaWaylandSurface *other_surface)
{
  if (surface == other_surface)
    return TRUE;
  if (other_surface->committed_state.parent)
    return is_same_or_ancestor (surface, other_surface->committed_state.parent);
  return FALSE;
}

static void
wl_subcompositor_get_subsurface (struct wl_client   *client,
                                 struct wl_resource *resource,
                                 uint32_t            id,
                                 struct wl_resource *surface_resource,
                                 struct wl_resource *parent_resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandSurface *parent = wl_resource_get_user_data (parent_resource);
  MetaWaylandSurfaceState *pending_state;
  MetaWaylandSubsurfacePlacementOp *op;
  MetaWaylandSurface *reference;
  MetaWindow *toplevel_window;

  if (surface->wl_subsurface)
    {
      wl_resource_post_error (surface_resource,
                              WL_DISPLAY_ERROR_INVALID_OBJECT,
                              "wl_subcompositor::get_subsurface already requested");
      return;
    }

  if (is_same_or_ancestor (surface, parent))
    {
      wl_resource_post_error (resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                              "Circular relationship between wl_surface@%d "
                              "and parent surface wl_surface@%d",
                              wl_resource_get_id (surface->resource),
                              wl_resource_get_id (parent->resource));
      return;
    }

  if (!meta_wayland_surface_assign_role (surface,
                                         META_TYPE_WAYLAND_SUBSURFACE,
                                         NULL))
    {
      wl_resource_post_error (resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                              "wl_surface@%d already has a different role",
                              wl_resource_get_id (surface->resource));
      return;
    }

  toplevel_window = meta_wayland_surface_get_toplevel_window (parent);
  if (toplevel_window &&
      toplevel_window->client_type == META_WINDOW_CLIENT_TYPE_X11)
    g_warning ("XWayland subsurfaces not currently supported");

  surface->wl_subsurface =
    wl_resource_create (client,
                        &wl_subsurface_interface,
                        wl_resource_get_version (resource),
                        id);
  wl_resource_set_implementation (surface->wl_subsurface,
                                  &meta_wayland_wl_subsurface_interface,
                                  surface,
                                  wl_subsurface_destructor);

  surface->sub.synchronous = TRUE;
  surface->committed_state.parent = parent;

  meta_wayland_surface_notify_highest_scale_monitor (surface);

  reference =
    g_node_last_child (parent->committed_state.subsurface_branch_node)->data;
  op = get_subsurface_placement_op (surface, reference,
                                    META_WAYLAND_SUBSURFACE_PLACEMENT_ABOVE);

  pending_state = meta_wayland_surface_get_pending_state (parent);
  pending_state->subsurface_placement_ops =
    g_slist_append (pending_state->subsurface_placement_ops, op);
}

static const struct wl_subcompositor_interface meta_wayland_subcompositor_interface = {
  wl_subcompositor_destroy,
  wl_subcompositor_get_subsurface,
};

static void
bind_subcompositor (struct wl_client *client,
                    void             *data,
                    uint32_t          version,
                    uint32_t          id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client, &wl_subcompositor_interface,
                                 version, id);
  wl_resource_set_implementation (resource, &meta_wayland_subcompositor_interface,
                                  data, NULL);
}

void
meta_wayland_subsurfaces_init (MetaWaylandCompositor *compositor)
{
  if (wl_global_create (compositor->wayland_display,
                        &wl_subcompositor_interface,
                        META_WL_SUBCOMPOSITOR_VERSION,
                        compositor, bind_subcompositor) == NULL)
    g_error ("Failed to register a global wl-subcompositor object");
}
