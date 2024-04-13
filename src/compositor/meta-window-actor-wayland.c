/*
 * Copyright (C) 2018 Endless, Inc.
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
 * Written by:
 *     Georges Basile Stavracas Neto <gbsneto@gnome.org>
 */

#include "config.h"

#include "compositor/clutter-utils.h"
#include "compositor/meta-cullable.h"
#include "compositor/meta-surface-actor-wayland.h"
#include "compositor/meta-window-actor-wayland.h"
#include "meta/meta-window-actor.h"
#include "wayland/meta-wayland-buffer.h"
#include "wayland/meta-wayland-single-pixel-buffer.h"
#include "wayland/meta-wayland-surface-private.h"
#include "wayland/meta-window-wayland.h"

struct _MetaSurfaceContainerActorWayland
{
  ClutterActor parent;

  MetaWindowActor *window_actor;
};

static void surface_container_cullable_iface_init (MetaCullableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaSurfaceContainerActorWayland,
                         meta_surface_container_actor_wayland,
                         CLUTTER_TYPE_ACTOR,
                         G_IMPLEMENT_INTERFACE (META_TYPE_CULLABLE,
                                                surface_container_cullable_iface_init))

struct _MetaWindowActorWayland
{
  MetaWindowActor parent;
  ClutterActor *background;
  MetaSurfaceContainerActorWayland *surface_container;
  gulong highest_scale_monitor_handler_id;
  gboolean needs_sync;
};

static void cullable_iface_init (MetaCullableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaWindowActorWayland, meta_window_actor_wayland,
                         META_TYPE_WINDOW_ACTOR,
                         G_IMPLEMENT_INTERFACE (META_TYPE_CULLABLE,
                                                cullable_iface_init))

typedef struct _SurfaceTreeTraverseData
{
  ClutterActor *surface_container;
  int index;
} SurfaceTreeTraverseData;

static MetaSurfaceContainerActorWayland *
surface_container_new (MetaWindowActor *window_actor)
{
  MetaSurfaceContainerActorWayland *surface_container;

  surface_container = g_object_new (META_TYPE_SURFACE_CONTAINER_ACTOR_WAYLAND,
                                    NULL);
  surface_container->window_actor = window_actor;

  return surface_container;
}

static void
surface_container_cull_unobscured (MetaCullable *cullable,
                                   MtkRegion    *unobscured_region)
{
  meta_cullable_cull_unobscured_children (cullable, unobscured_region);
}

static void
surface_container_cull_redraw_clip (MetaCullable *cullable,
                                    MtkRegion    *clip_region)
{
  meta_cullable_cull_redraw_clip_children (cullable, clip_region);
}

static void
surface_container_cullable_iface_init (MetaCullableInterface *iface)
{
  iface->cull_unobscured = surface_container_cull_unobscured;
  iface->cull_redraw_clip = surface_container_cull_redraw_clip;
}

static void
surface_container_apply_transform (ClutterActor      *actor,
                                   graphene_matrix_t *matrix)
{
  ClutterActor *parent = clutter_actor_get_parent (actor);
  ClutterActorClass *parent_class =
    CLUTTER_ACTOR_CLASS (meta_surface_container_actor_wayland_parent_class);
  MetaWindow *window;
  MetaLogicalMonitor *logical_monitor;
  MtkRectangle monitor_rect;
  float scale;
  float rel_x, rel_y;
  float abs_x, abs_y;
  float adj_rel_x, adj_rel_y;
  float x_off, y_off;

  parent_class->apply_transform (actor, matrix);

  if (!parent)
    return;

  window = meta_window_actor_get_meta_window (META_WINDOW_ACTOR (parent));
  if (!window)
    return;

  logical_monitor = meta_window_get_highest_scale_monitor (window);
  if (!logical_monitor)
    return;

  scale = meta_logical_monitor_get_scale (logical_monitor);
  monitor_rect = meta_logical_monitor_get_layout (logical_monitor);

  abs_x = clutter_actor_get_x (parent) + clutter_actor_get_x (actor);
  abs_y = clutter_actor_get_y (parent) + clutter_actor_get_y (actor);

  rel_x = abs_x - monitor_rect.x;
  rel_y = abs_y - monitor_rect.y;

  adj_rel_x = roundf (rel_x * scale) / scale;
  adj_rel_y = roundf (rel_y * scale) / scale;

  x_off = adj_rel_x - rel_x;
  y_off = adj_rel_y - rel_y;

  if (!G_APPROX_VALUE (x_off, 0.0, FLT_EPSILON) ||
      !G_APPROX_VALUE (y_off, 0.0, FLT_EPSILON))
    {
      graphene_matrix_translate (matrix,
                                 &GRAPHENE_POINT3D_INIT (x_off, y_off, 0));
    }
}

static void
surface_container_dispose (GObject *object)
{
  MetaSurfaceContainerActorWayland *self = META_SURFACE_CONTAINER_ACTOR_WAYLAND (object);

  clutter_actor_remove_all_children (CLUTTER_ACTOR (self));

  G_OBJECT_CLASS (meta_surface_container_actor_wayland_parent_class)->dispose (object);
}

static void
meta_surface_container_actor_wayland_class_init (MetaSurfaceContainerActorWaylandClass *klass)
{
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  actor_class->apply_transform = surface_container_apply_transform;

  object_class->dispose = surface_container_dispose;
}

static void
meta_surface_container_actor_wayland_init (MetaSurfaceContainerActorWayland *self)
{
}

static gboolean
get_surface_actor_list (GNode    *node,
                        gpointer  data)
{
  MetaWaylandSurface *surface = node->data;
  MetaSurfaceActor *surface_actor = meta_wayland_surface_get_actor (surface);
  GList **surface_actors = data;

  *surface_actors = g_list_prepend (*surface_actors, surface_actor);
  return FALSE;
}

static gboolean
set_surface_actor_index (GNode    *node,
                         gpointer  data)
{
  MetaWaylandSurface *surface = node->data;
  SurfaceTreeTraverseData *traverse_data = data;
  ClutterActor *container = traverse_data->surface_container;
  ClutterActor *surface_actor =
    CLUTTER_ACTOR (meta_wayland_surface_get_actor (surface));
  MetaSurfaceContainerActorWayland *surface_container =
    META_SURFACE_CONTAINER_ACTOR_WAYLAND (container);
  MetaWindowActor *window_actor = surface_container->window_actor;

  if (clutter_actor_contains (container, surface_actor))
    {
      if (clutter_actor_get_child_at_index (container, traverse_data->index) !=
          surface_actor)
        {
          clutter_actor_set_child_at_index (container,
                                            surface_actor,
                                            traverse_data->index);
        }
    }
  else
    {
      meta_window_actor_add_surface_actor (window_actor,
                                           META_SURFACE_ACTOR (surface_actor));
      clutter_actor_insert_child_at_index (container,
                                           surface_actor,
                                           traverse_data->index);
    }
  traverse_data->index++;

  return FALSE;
}

void
meta_window_actor_wayland_rebuild_surface_tree (MetaWindowActor *actor)
{
  MetaWindowActorWayland *self = META_WINDOW_ACTOR_WAYLAND (actor);
  MetaSurfaceActor *surface_actor =
    meta_window_actor_get_surface (actor);
  MetaWaylandSurface *surface = meta_surface_actor_wayland_get_surface (
    META_SURFACE_ACTOR_WAYLAND (surface_actor));
  GNode *root_node = surface->applied_state.subsurface_branch_node;
  g_autoptr (GList) surface_actors = NULL;
  g_autoptr (GList) children = NULL;
  GList *l;
  SurfaceTreeTraverseData traverse_data;

  g_node_traverse (root_node,
                   G_IN_ORDER,
                   G_TRAVERSE_LEAVES,
                   -1,
                   get_surface_actor_list,
                   &surface_actors);

  children =
    clutter_actor_get_children (CLUTTER_ACTOR (self->surface_container));
  for (l = children; l; l = l->next)
    {
      ClutterActor *child_actor = l->data;

      if (!g_list_find (surface_actors, child_actor))
        {
          MetaSurfaceActor *surface_actor = META_SURFACE_ACTOR (child_actor);

          meta_window_actor_remove_surface_actor (actor, surface_actor);
          clutter_actor_remove_child (CLUTTER_ACTOR (self->surface_container),
                                      child_actor);
        }
    }

  traverse_data = (SurfaceTreeTraverseData) {
    .surface_container = CLUTTER_ACTOR (self->surface_container),
    .index = 0,
  };
  g_node_traverse (root_node,
                   G_IN_ORDER,
                   G_TRAVERSE_LEAVES,
                   -1,
                   set_surface_actor_index,
                   &traverse_data);
}

static MtkRegion *
calculate_background_cull_region (MetaWindowActorWayland *self)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (self);
  int geometry_scale;
  MtkRectangle rect;

  geometry_scale = meta_window_actor_get_geometry_scale (window_actor);
  rect = (MtkRectangle) {
    .x = 0,
    .y = 0,
    .width = clutter_actor_get_width (self->background) * geometry_scale,
    .height = clutter_actor_get_height (self->background) * geometry_scale,
  };

  return mtk_region_create_rectangle (&rect);
}

static void
subtract_background_opaque_region (MetaWindowActorWayland *self,
                                   MtkRegion              *region)
{
  if (!region)
    return;

  if (self->background &&
      clutter_actor_get_paint_opacity (CLUTTER_ACTOR (self)) == 0xff)
    {
      g_autoptr (MtkRegion) background_cull_region = NULL;

      background_cull_region = calculate_background_cull_region (self);

      mtk_region_subtract (region, background_cull_region);
    }
}

static void
meta_window_actor_wayland_cull_unobscured (MetaCullable *cullable,
                                           MtkRegion    *unobscured_region)
{
  MetaWindowActorWayland *self =
    META_WINDOW_ACTOR_WAYLAND (cullable);

  meta_cullable_cull_unobscured_children (META_CULLABLE (self), unobscured_region);

  subtract_background_opaque_region (self, unobscured_region);
}

static void
meta_window_actor_wayland_cull_redraw_clip (MetaCullable *cullable,
                                            MtkRegion    *clip_region)
{
  MetaWindowActorWayland *self =
    META_WINDOW_ACTOR_WAYLAND (cullable);

  meta_cullable_cull_redraw_clip_children (META_CULLABLE (self), clip_region);

  subtract_background_opaque_region (self, clip_region);
}

static void
cullable_iface_init (MetaCullableInterface *iface)
{
  iface->cull_unobscured = meta_window_actor_wayland_cull_unobscured;
  iface->cull_redraw_clip = meta_window_actor_wayland_cull_redraw_clip;
}

static MetaSurfaceActor *
meta_window_actor_wayland_get_scanout_candidate (MetaWindowActor *actor)
{
  MetaWindowActorWayland *self = META_WINDOW_ACTOR_WAYLAND (actor);
  ClutterActor *surface_container = CLUTTER_ACTOR (self->surface_container);
  ClutterActor *child_actor;
  ClutterActorIter iter;
  MetaSurfaceActor *topmost_surface_actor = NULL;
  int n_visible_surface_actors = 0;
  MetaWindow *window;
  ClutterActorBox window_box;
  ClutterActorBox surface_box;

  if (clutter_actor_get_last_child (CLUTTER_ACTOR (self)) != surface_container)
    {
      meta_topic (META_DEBUG_RENDER,
                  "Top child of window-actor not a surface");
      return NULL;
    }

  clutter_actor_iter_init (&iter, surface_container);
  while (clutter_actor_iter_next (&iter, &child_actor))
    {
      MetaSurfaceActor *surface_actor;

      if (!clutter_actor_is_mapped (child_actor))
        continue;

      surface_actor = META_SURFACE_ACTOR (child_actor);
      if (meta_surface_actor_is_obscured (surface_actor))
        continue;

      topmost_surface_actor = surface_actor;
      n_visible_surface_actors++;
    }

  if (!topmost_surface_actor)
    {
      meta_topic (META_DEBUG_RENDER,
                  "No surface-actor for window-actor");
      return NULL;
    }

  window = meta_window_actor_get_meta_window (actor);
  if (meta_window_is_fullscreen (window) && n_visible_surface_actors == 1)
    return topmost_surface_actor;

  if (meta_window_is_fullscreen (window) && n_visible_surface_actors == 2)
    {
      MetaSurfaceActorWayland *bg_surface_actor = NULL;
      MetaWaylandSurface *bg_surface;
      MetaWaylandBuffer *buffer;
      MetaWaylandSinglePixelBuffer *sp_buffer;

      clutter_actor_iter_init (&iter, surface_container);
      while (clutter_actor_iter_next (&iter, &child_actor))
        {
          MetaSurfaceActor *surface_actor;

          if (!clutter_actor_is_mapped (child_actor))
            continue;

          surface_actor = META_SURFACE_ACTOR (child_actor);
          if (meta_surface_actor_is_obscured (surface_actor))
            continue;

          bg_surface_actor = META_SURFACE_ACTOR_WAYLAND (surface_actor);
          break;
        }
      g_assert (bg_surface_actor);

      bg_surface = meta_surface_actor_wayland_get_surface (bg_surface_actor);
      buffer = meta_wayland_surface_get_buffer (bg_surface);

      sp_buffer = buffer->single_pixel.single_pixel_buffer;
      if (sp_buffer &&
          meta_wayland_single_pixel_buffer_is_opaque_black (sp_buffer))
        return topmost_surface_actor;
    }

  if (meta_surface_actor_is_opaque (topmost_surface_actor) &&
      clutter_actor_get_paint_box (CLUTTER_ACTOR (actor), &window_box) &&
      clutter_actor_get_paint_box (CLUTTER_ACTOR (topmost_surface_actor),
                                   &surface_box) &&
      G_APPROX_VALUE (window_box.x1, surface_box.x1, CLUTTER_COORDINATE_EPSILON) &&
      G_APPROX_VALUE (window_box.y1, surface_box.y1, CLUTTER_COORDINATE_EPSILON) &&
      G_APPROX_VALUE (window_box.x2, surface_box.x2, CLUTTER_COORDINATE_EPSILON) &&
      G_APPROX_VALUE (window_box.y2, surface_box.y2, CLUTTER_COORDINATE_EPSILON))
    return topmost_surface_actor;

  meta_topic (META_DEBUG_RENDER,
              "Could not find suitable scanout candidate for window-actor");
  return NULL;
}

static void
meta_window_actor_wayland_assign_surface_actor (MetaWindowActor  *actor,
                                                MetaSurfaceActor *surface_actor)
{
  MetaWindowActorClass *parent_class =
    META_WINDOW_ACTOR_CLASS (meta_window_actor_wayland_parent_class);

  g_warn_if_fail (!meta_window_actor_get_surface (actor));

  parent_class->assign_surface_actor (actor, surface_actor);

  meta_window_actor_wayland_rebuild_surface_tree (actor);
}

static void
meta_window_actor_wayland_frame_complete (MetaWindowActor  *actor,
                                          ClutterFrameInfo *frame_info,
                                          int64_t           presentation_time)
{
}

static void
meta_window_actor_wayland_queue_frame_drawn (MetaWindowActor *actor,
                                             gboolean         skip_sync_delay)
{
}

static void
meta_window_actor_wayland_before_paint (MetaWindowActor  *actor,
                                        ClutterStageView *stage_view)
{
}

static void
meta_window_actor_wayland_after_paint (MetaWindowActor  *actor,
                                       ClutterStageView *stage_view)
{
}

static void
meta_window_actor_wayland_queue_destroy (MetaWindowActor *actor)
{
}

static void
meta_window_actor_wayland_set_frozen (MetaWindowActor *actor,
                                      gboolean         frozen)
{
  MetaWindowActorWayland *self = META_WINDOW_ACTOR_WAYLAND (actor);
  ClutterActor *child;
  ClutterActorIter iter;

  clutter_actor_iter_init (&iter, CLUTTER_ACTOR (self->surface_container));
  while (clutter_actor_iter_next (&iter, &child))
    meta_surface_actor_set_frozen (META_SURFACE_ACTOR (child), frozen);
}

static void
meta_window_actor_wayland_update_regions (MetaWindowActor *actor)
{
}

static gboolean
meta_window_actor_wayland_can_freeze_commits (MetaWindowActor *actor)
{
  return FALSE;
}

static gboolean
meta_window_actor_wayland_is_single_surface_actor (MetaWindowActor *actor)
{
  MetaWindowActorWayland *self = META_WINDOW_ACTOR_WAYLAND (actor);
  ClutterActor *surface_container = CLUTTER_ACTOR (self->surface_container);

  return clutter_actor_get_n_children (surface_container) == 1 &&
         !self->background;
}

static gboolean
maybe_configure_black_background (MetaWindowActorWayland *self,
                                  float                  *surfaces_width,
                                  float                  *surfaces_height,
                                  float                  *background_width,
                                  float                  *background_height)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (self);
  MetaWindow *window = meta_window_actor_get_meta_window (window_actor);
  MetaLogicalMonitor *logical_monitor;
  int geometry_scale;
  MtkRectangle fullscreen_layout;
  ClutterActor *child;
  ClutterActorIter iter;
  float max_width = 0;
  float max_height = 0;

  if (!meta_window_wayland_is_acked_fullscreen (META_WINDOW_WAYLAND (window)))
    return FALSE;

  geometry_scale = meta_window_actor_get_geometry_scale (window_actor);

  logical_monitor = meta_window_get_main_logical_monitor (window);
  if (!logical_monitor)
    return FALSE;

  fullscreen_layout = meta_logical_monitor_get_layout (logical_monitor);

  clutter_actor_iter_init (&iter, CLUTTER_ACTOR (self->surface_container));
  while (clutter_actor_iter_next (&iter, &child))
    {
      ClutterActorBox actor_box;

      if (!clutter_actor_is_mapped (child))
        continue;

      clutter_actor_get_allocation_box (child, &actor_box);
      if (meta_surface_actor_is_opaque (META_SURFACE_ACTOR (child)) &&
          G_APPROX_VALUE (actor_box.x1, 0, CLUTTER_COORDINATE_EPSILON) &&
          G_APPROX_VALUE (actor_box.y1, 0, CLUTTER_COORDINATE_EPSILON) &&
          G_APPROX_VALUE (actor_box.x2, fullscreen_layout.width,
                          CLUTTER_COORDINATE_EPSILON) &&
          G_APPROX_VALUE (actor_box.y2, fullscreen_layout.height,
                          CLUTTER_COORDINATE_EPSILON))
        return FALSE;

      max_width = MAX (max_width, actor_box.x2 - actor_box.x1);
      max_height = MAX (max_height, actor_box.y2 - actor_box.y1);
    }

  *surfaces_width = max_width;
  *surfaces_height = max_height;
  *background_width = window->rect.width / geometry_scale;
  *background_height = window->rect.height / geometry_scale;
  return TRUE;
}

static gboolean
do_sync_geometry (MetaWindowActorWayland *self)
{
  MetaWindowActor *actor = META_WINDOW_ACTOR (self);
  ClutterActor *surface_container = CLUTTER_ACTOR (self->surface_container);
  MetaWindow *window = meta_window_actor_get_meta_window (actor);
  float surfaces_width, surfaces_height;
  float background_width, background_height;

  if (window->unmanaging)
    return FALSE;

  if (!clutter_actor_is_mapped (CLUTTER_ACTOR (actor)))
    return FALSE;

  if (maybe_configure_black_background (self,
                                        &surfaces_width, &surfaces_height,
                                        &background_width, &background_height))
    {
      MtkRectangle actor_rect;
      int geometry_scale;
      int child_actor_width, child_actor_height;

      if (!self->background)
        {
          self->background = clutter_actor_new ();
          clutter_actor_set_background_color (self->background,
                                               &CLUTTER_COLOR_INIT (0, 0, 0, 255));
          clutter_actor_set_reactive (self->background, TRUE);
          clutter_actor_insert_child_below (CLUTTER_ACTOR (self),
                                            self->background,
                                            NULL);
        }

      meta_window_get_buffer_rect (window, &actor_rect);
      geometry_scale =
        meta_window_actor_get_geometry_scale (actor);
      child_actor_width = actor_rect.width / geometry_scale;
      child_actor_height = actor_rect.height / geometry_scale;

      clutter_actor_set_size (self->background,
                              background_width, background_height);
      clutter_actor_set_position (surface_container,
                                  (child_actor_width - surfaces_width) / 2,
                                  (child_actor_height - surfaces_height) / 2);
    }
  else if (self->background)
    {
      clutter_actor_set_position (surface_container, 0, 0);
      g_clear_pointer (&self->background, clutter_actor_destroy);
    }

  return TRUE;
}

static void
meta_window_actor_wayland_sync_geometry (MetaWindowActor *actor)
{
  MetaWindowActorWayland *self = META_WINDOW_ACTOR_WAYLAND (actor);

  self->needs_sync = !do_sync_geometry (self);
}

static void
meta_window_actor_wayland_map (ClutterActor *actor)
{
  MetaWindowActorWayland *self = META_WINDOW_ACTOR_WAYLAND (actor);
  ClutterActorClass *parent_class =
    CLUTTER_ACTOR_CLASS (meta_window_actor_wayland_parent_class);

  parent_class->map (actor);

  if (self->needs_sync)
    {
      do_sync_geometry (self);
      self->needs_sync = FALSE;
    }
}

static void
meta_window_actor_wayland_dispose (GObject *object)
{
  MetaWindowActorWayland *self = META_WINDOW_ACTOR_WAYLAND (object);
  MetaWindow *window = meta_window_actor_get_meta_window (META_WINDOW_ACTOR (self));
  GObjectClass *parent_class =
    G_OBJECT_CLASS (meta_window_actor_wayland_parent_class);

  g_clear_signal_handler (&self->highest_scale_monitor_handler_id,
                          window);

  parent_class->dispose (object);
}

static void
meta_window_actor_wayland_constructed (GObject *object)
{
  MetaWindowActorWayland *self = META_WINDOW_ACTOR_WAYLAND (object);
  MetaWindow *window = meta_window_actor_get_meta_window (META_WINDOW_ACTOR (self));

  G_OBJECT_CLASS (meta_window_actor_wayland_parent_class)->constructed (object);

  self->highest_scale_monitor_handler_id =
    g_signal_connect_swapped (window, "highest-scale-monitor-changed",
                              G_CALLBACK (clutter_actor_notify_transform_invalid),
                              self->surface_container);
}

static void
meta_window_actor_wayland_class_init (MetaWindowActorWaylandClass *klass)
{
  MetaWindowActorClass *window_actor_class = META_WINDOW_ACTOR_CLASS (klass);
  ClutterActorClass *clutter_actor_class = CLUTTER_ACTOR_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  window_actor_class->get_scanout_candidate = meta_window_actor_wayland_get_scanout_candidate;
  window_actor_class->assign_surface_actor = meta_window_actor_wayland_assign_surface_actor;
  window_actor_class->frame_complete = meta_window_actor_wayland_frame_complete;
  window_actor_class->queue_frame_drawn = meta_window_actor_wayland_queue_frame_drawn;
  window_actor_class->before_paint = meta_window_actor_wayland_before_paint;
  window_actor_class->after_paint = meta_window_actor_wayland_after_paint;
  window_actor_class->queue_destroy = meta_window_actor_wayland_queue_destroy;
  window_actor_class->set_frozen = meta_window_actor_wayland_set_frozen;
  window_actor_class->update_regions = meta_window_actor_wayland_update_regions;
  window_actor_class->can_freeze_commits = meta_window_actor_wayland_can_freeze_commits;
  window_actor_class->sync_geometry = meta_window_actor_wayland_sync_geometry;
  window_actor_class->is_single_surface_actor = meta_window_actor_wayland_is_single_surface_actor;

  clutter_actor_class->map = meta_window_actor_wayland_map;

  object_class->constructed = meta_window_actor_wayland_constructed;
  object_class->dispose = meta_window_actor_wayland_dispose;
}

static void
meta_window_actor_wayland_init (MetaWindowActorWayland *self)
{
  self->surface_container = surface_container_new (META_WINDOW_ACTOR (self));

  clutter_actor_add_child (CLUTTER_ACTOR (self),
                           CLUTTER_ACTOR (self->surface_container));

  g_signal_connect_swapped (self, "notify::allocation",
                            G_CALLBACK (clutter_actor_notify_transform_invalid),
                            self->surface_container);
}
