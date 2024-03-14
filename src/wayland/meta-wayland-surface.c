/*
 * Wayland Support
 *
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

#include "wayland/meta-wayland-surface-private.h"

#include <gobject/gvaluecollector.h>
#include <wayland-server.h>

#include "backends/meta-cursor-tracker-private.h"
#include "clutter/clutter.h"
#include "cogl/cogl.h"
#include "compositor/meta-surface-actor-wayland.h"
#include "compositor/meta-surface-actor.h"
#include "compositor/meta-window-actor-private.h"
#include "core/boxes-private.h"
#include "core/display-private.h"
#include "core/window-private.h"
#include "wayland/meta-wayland-actor-surface.h"
#include "wayland/meta-wayland-buffer.h"
#include "wayland/meta-wayland-fractional-scale.h"
#include "wayland/meta-wayland-gtk-shell.h"
#include "wayland/meta-wayland-outputs.h"
#include "wayland/meta-wayland-presentation-time-private.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-region.h"
#include "wayland/meta-wayland-seat.h"
#include "wayland/meta-wayland-subsurface.h"
#include "wayland/meta-wayland-transaction.h"
#include "wayland/meta-wayland-viewporter.h"
#include "wayland/meta-wayland-xdg-shell.h"
#include "wayland/meta-window-wayland.h"

#ifdef HAVE_XWAYLAND
#include "wayland/meta-xwayland-private.h"
#endif

enum
{
  SURFACE_STATE_SIGNAL_APPLIED,

  SURFACE_STATE_SIGNAL_N_SIGNALS
};

enum
{
  SURFACE_ROLE_PROP_0,

  SURFACE_ROLE_PROP_SURFACE,
};

static guint surface_state_signals[SURFACE_STATE_SIGNAL_N_SIGNALS];

typedef struct _MetaWaylandSurfaceRolePrivate
{
  MetaWaylandSurface *surface;
} MetaWaylandSurfaceRolePrivate;

enum
{
  PROP_0,

  PROP_SCANOUT_CANDIDATE,
  PROP_WINDOW,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

G_DEFINE_TYPE (MetaWaylandSurface, meta_wayland_surface, G_TYPE_OBJECT);

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (MetaWaylandSurfaceRole,
                                     meta_wayland_surface_role,
                                     G_TYPE_OBJECT)

G_DEFINE_TYPE (MetaWaylandSurfaceState,
               meta_wayland_surface_state,
               G_TYPE_OBJECT)

enum
{
  SURFACE_DESTROY,
  SURFACE_UNMAPPED,
  SURFACE_CONFIGURE,
  SURFACE_SHORTCUTS_INHIBITED,
  SURFACE_SHORTCUTS_RESTORED,
  SURFACE_GEOMETRY_CHANGED,
  SURFACE_PRE_STATE_APPLIED,
  SURFACE_ACTOR_CHANGED,
  N_SURFACE_SIGNALS
};

guint surface_signals[N_SURFACE_SIGNALS] = { 0 };

static void
meta_wayland_surface_role_assigned (MetaWaylandSurfaceRole *surface_role);

static void
meta_wayland_surface_role_commit_state (MetaWaylandSurfaceRole  *surface_role,
                                        MetaWaylandTransaction  *transaction,
                                        MetaWaylandSurfaceState *pending);

static void
meta_wayland_surface_role_pre_apply_state (MetaWaylandSurfaceRole  *surface_role,
                                           MetaWaylandSurfaceState *pending);

static void
meta_wayland_surface_role_apply_state (MetaWaylandSurfaceRole  *surface_role,
                                       MetaWaylandSurfaceState *pending);

static void
meta_wayland_surface_role_post_apply_state (MetaWaylandSurfaceRole  *surface_role,
                                            MetaWaylandSurfaceState *pending);

static gboolean
meta_wayland_surface_role_is_on_logical_monitor (MetaWaylandSurfaceRole *surface_role,
                                                 MetaLogicalMonitor     *logical_monitor);

static MetaWaylandSurface *
meta_wayland_surface_role_get_toplevel (MetaWaylandSurfaceRole *surface_role);

static void
set_surface_is_on_output (MetaWaylandSurface *surface,
                          MetaWaylandOutput  *wayland_output,
                          gboolean            is_on_output);

static void
role_assignment_valist_to_properties (GType       role_type,
                                      const char *first_property_name,
                                      va_list     var_args,
                                      GArray     *names,
                                      GArray     *values)
{
  GObjectClass *object_class;
  const char *property_name = first_property_name;

  object_class = g_type_class_ref (role_type);

  while (property_name)
    {
      GValue value = G_VALUE_INIT;
      GParamSpec *pspec;
      GType ptype;
      gchar *error = NULL;

      pspec = g_object_class_find_property (object_class,
                                            property_name);
      g_assert (pspec);

      ptype = G_PARAM_SPEC_VALUE_TYPE (pspec);
      G_VALUE_COLLECT_INIT (&value, ptype, var_args, 0, &error);
      g_assert (!error);

      g_array_append_val (names, property_name);
      g_array_append_val (values, value);

      property_name = va_arg (var_args, const char *);
    }

  g_type_class_unref (object_class);
}

gboolean
meta_wayland_surface_assign_role (MetaWaylandSurface *surface,
                                  GType               role_type,
                                  const char         *first_property_name,
                                  ...)
{
  va_list var_args;

  if (!surface->role)
    {
      if (first_property_name)
        {
          GArray *names;
          GArray *values;
          const char *surface_prop_name;
          GValue surface_value = G_VALUE_INIT;
          GObject *role_object;

          names = g_array_new (FALSE, FALSE, sizeof (const char *));
          values = g_array_new (FALSE, FALSE, sizeof (GValue));
          g_array_set_clear_func (values, (GDestroyNotify) g_value_unset);

          va_start (var_args, first_property_name);
          role_assignment_valist_to_properties (role_type,
                                                first_property_name,
                                                var_args,
                                                names,
                                                values);
          va_end (var_args);

          surface_prop_name = "surface";
          g_value_init (&surface_value, META_TYPE_WAYLAND_SURFACE);
          g_value_set_object (&surface_value, surface);
          g_array_append_val (names, surface_prop_name);
          g_array_append_val (values, surface_value);

          role_object =
            g_object_new_with_properties (role_type,
                                          values->len,
                                          (const char **) names->data,
                                          (const GValue *) values->data);
          surface->role = META_WAYLAND_SURFACE_ROLE (role_object);

          g_array_free (names, TRUE);
          g_array_free (values, TRUE);
        }
      else
        {
          surface->role = g_object_new (role_type, "surface", surface, NULL);
        }

      meta_wayland_surface_role_assigned (surface->role);

      /* Release the use count held on behalf of the just assigned role. */
      if (surface->unassigned.buffer)
        {
          meta_wayland_buffer_dec_use_count (surface->unassigned.buffer);
          g_clear_object (&surface->unassigned.buffer);
        }

      return TRUE;
    }
  else if (G_OBJECT_TYPE (surface->role) != role_type)
    {
      return FALSE;
    }
  else
    {
      va_start (var_args, first_property_name);
      g_object_set_valist (G_OBJECT (surface->role),
                           first_property_name, var_args);
      va_end (var_args);

      meta_wayland_surface_role_assigned (surface->role);

      return TRUE;
    }
}

static MtkRegion *
region_transform (const MtkRegion      *region,
                  MetaMonitorTransform  transform,
                  int                   width,
                  int                   height)
{
  MtkRegion *transformed_region;
  MtkRectangle *rects;
  int n_rects, i;

  if (transform == META_MONITOR_TRANSFORM_NORMAL)
    return mtk_region_copy (region);

  n_rects = mtk_region_num_rectangles (region);
  MTK_RECTANGLE_CREATE_ARRAY_SCOPED (n_rects, rects);
  for (i = 0; i < n_rects; i++)
    {
      rects[i] = mtk_region_get_rectangle (region, i);

      meta_rectangle_transform (&rects[i],
                                transform,
                                width,
                                height,
                                &rects[i]);
    }

  transformed_region = mtk_region_create_rectangles (rects, n_rects);

  return transformed_region;
}

static void
surface_process_damage (MetaWaylandSurface *surface,
                        MtkRegion          *surface_region,
                        MtkRegion          *buffer_region)
{
  MetaWaylandBuffer *buffer = meta_wayland_surface_get_buffer (surface);
  MtkRectangle buffer_rect;
  MetaSurfaceActor *actor;

  /* If the client destroyed the buffer it attached before committing, but
   * still posted damage, or posted damage without any buffer, don't try to
   * process it on the non-existing buffer.
   */
  if (!buffer)
    return;

  buffer_rect = (MtkRectangle) {
    .width = meta_wayland_surface_get_buffer_width (surface),
    .height = meta_wayland_surface_get_buffer_height (surface),
  };

  if (!mtk_region_is_empty (surface_region))
    {
      int surface_scale = surface->applied_state.scale;
      MtkRectangle surface_rect;
      g_autoptr (MtkRegion) scaled_region = NULL;
      g_autoptr (MtkRegion) transformed_region = NULL;
      g_autoptr (MtkRegion) viewport_region = NULL;
      graphene_rect_t src_rect;

      /* Intersect the damage region with the surface region before scaling in
       * order to avoid integer overflow when scaling a damage region is too
       * large (for example INT32_MAX which mesa passes). */
      surface_rect = (MtkRectangle) {
        .width = meta_wayland_surface_get_width (surface),
        .height = meta_wayland_surface_get_height (surface),
      };
      mtk_region_intersect_rectangle (surface_region, &surface_rect);

      /* The damage region must be in the same coordinate space as the buffer,
       * i.e. scaled with surface->applied_state.scale. */
      if (surface->viewport.has_src_rect)
        {
          src_rect = (graphene_rect_t) {
            .origin.x = surface->viewport.src_rect.origin.x,
            .origin.y = surface->viewport.src_rect.origin.y,
            .size.width = surface->viewport.src_rect.size.width,
            .size.height = surface->viewport.src_rect.size.height
          };
        }
      else
        {
          int width, height;

          if (meta_monitor_transform_is_rotated (surface->buffer_transform))
            {
              width = meta_wayland_surface_get_buffer_height (surface);
              height = meta_wayland_surface_get_buffer_width (surface);
            }
          else
            {
              width = meta_wayland_surface_get_buffer_width (surface);
              height = meta_wayland_surface_get_buffer_height (surface);
            }

          src_rect = (graphene_rect_t) {
            .size.width = width / surface_scale,
            .size.height = height / surface_scale
          };
        }
      viewport_region = mtk_region_crop_and_scale (surface_region,
                                                   &src_rect,
                                                   surface_rect.width,
                                                   surface_rect.height);
      scaled_region = mtk_region_scale (viewport_region, surface_scale);
      transformed_region = region_transform (scaled_region,
                                             surface->buffer_transform,
                                             buffer_rect.width,
                                             buffer_rect.height);

      /* Now add the scaled, cropped and transformed damage region to the
       * buffer damage. Buffer damage is already in the correct coordinate
       * space. */
      mtk_region_union (buffer_region, transformed_region);
    }

  mtk_region_intersect_rectangle (buffer_region, &buffer_rect);

  meta_wayland_buffer_process_damage (buffer, surface->applied_state.texture,
                                      buffer_region);

  actor = meta_wayland_surface_get_actor (surface);
  if (actor)
    {
      int i, n_rectangles;

      n_rectangles = mtk_region_num_rectangles (buffer_region);
      for (i = 0; i < n_rectangles; i++)
        {
          MtkRectangle rect;
          rect = mtk_region_get_rectangle (buffer_region, i);

          meta_surface_actor_process_damage (actor,
                                             rect.x, rect.y,
                                             rect.width, rect.height);
        }
    }
}

MetaWaylandBuffer *
meta_wayland_surface_get_buffer (MetaWaylandSurface *surface)
{
  return surface->buffer;
}

static void
pending_buffer_resource_destroyed (MetaWaylandBuffer       *buffer,
                                   MetaWaylandSurfaceState *pending)
{
  g_clear_signal_handler (&pending->buffer_destroy_handler_id, buffer);
  pending->buffer = NULL;
}

static void
meta_wayland_surface_state_set_default (MetaWaylandSurfaceState *state)
{
  state->newly_attached = FALSE;
  state->buffer = NULL;
  state->texture = NULL;
  state->buffer_destroy_handler_id = 0;
  state->dx = 0;
  state->dy = 0;
  state->scale = 0;

  state->input_region = NULL;
  state->input_region_set = FALSE;
  state->opaque_region = NULL;
  state->opaque_region_set = FALSE;

  state->surface_damage = mtk_region_create ();
  state->buffer_damage = mtk_region_create ();
  wl_list_init (&state->frame_callback_list);

  state->has_new_geometry = FALSE;
  state->has_acked_configure_serial = FALSE;
  state->has_new_min_size = FALSE;
  state->has_new_max_size = FALSE;

  state->has_new_buffer_transform = FALSE;
  state->has_new_viewport_src_rect = FALSE;
  state->has_new_viewport_dst_size = FALSE;

  state->subsurface_placement_ops = NULL;

  wl_list_init (&state->presentation_feedback_list);

  state->xdg_popup_reposition_token = 0;
}

static void
meta_wayland_surface_state_discard_presentation_feedback (MetaWaylandSurfaceState *state)
{
  while (!wl_list_empty (&state->presentation_feedback_list))
    {
      MetaWaylandPresentationFeedback *feedback =
        wl_container_of (state->presentation_feedback_list.next, feedback, link);

      meta_wayland_presentation_feedback_discard (feedback);
    }
}

static void
meta_wayland_surface_state_clear (MetaWaylandSurfaceState *state)
{
  MetaWaylandFrameCallback *cb, *next;

  g_clear_object (&state->texture);

  g_clear_pointer (&state->surface_damage, mtk_region_unref);
  g_clear_pointer (&state->buffer_damage, mtk_region_unref);
  g_clear_pointer (&state->input_region, mtk_region_unref);
  g_clear_pointer (&state->opaque_region, mtk_region_unref);
  g_clear_pointer (&state->xdg_positioner, g_free);

  if (state->buffer_destroy_handler_id)
    {
      g_clear_signal_handler (&state->buffer_destroy_handler_id, state->buffer);
      state->buffer = NULL;
    }
  else
    {
      g_clear_object (&state->buffer);
    }

  wl_list_for_each_safe (cb, next, &state->frame_callback_list, link)
    wl_resource_destroy (cb->resource);

  if (state->subsurface_placement_ops)
    g_slist_free_full (state->subsurface_placement_ops, g_free);

  meta_wayland_surface_state_discard_presentation_feedback (state);
}

void
meta_wayland_surface_state_reset (MetaWaylandSurfaceState *state)
{
  meta_wayland_surface_state_clear (state);
  meta_wayland_surface_state_set_default (state);
}

void
meta_wayland_surface_state_merge_into (MetaWaylandSurfaceState *from,
                                       MetaWaylandSurfaceState *to)
{
  if (from->newly_attached)
    {
      if (to->buffer)
        {
          g_warn_if_fail (to->buffer_destroy_handler_id == 0);
          meta_wayland_buffer_dec_use_count (to->buffer);
          g_object_unref (to->buffer);
        }

      to->newly_attached = TRUE;
      to->buffer = g_steal_pointer (&from->buffer);

      g_clear_object (&to->texture);
      to->texture = g_steal_pointer (&from->texture);
    }

  to->dx += from->dx;
  to->dy += from->dy;

  wl_list_insert_list (&to->frame_callback_list, &from->frame_callback_list);
  wl_list_init (&from->frame_callback_list);

  mtk_region_union (to->surface_damage, from->surface_damage);
  mtk_region_union (to->buffer_damage, from->buffer_damage);

  if (from->input_region_set)
    {
      if (to->input_region)
        mtk_region_union (to->input_region, from->input_region);
      else
        to->input_region = mtk_region_ref (from->input_region);

      to->input_region_set = TRUE;
    }

  if (from->opaque_region_set)
    {
      if (to->opaque_region)
        mtk_region_union (to->opaque_region, from->opaque_region);
      else
        to->opaque_region = mtk_region_ref (from->opaque_region);

      to->opaque_region_set = TRUE;
    }

  if (from->has_new_geometry)
    {
      to->new_geometry = from->new_geometry;
      to->has_new_geometry = TRUE;
    }

  if (from->has_acked_configure_serial)
    {
      to->acked_configure_serial = from->acked_configure_serial;
      to->has_acked_configure_serial = TRUE;
    }

  if (from->has_new_min_size)
    {
      to->new_min_width = from->new_min_width;
      to->new_min_height = from->new_min_height;
      to->has_new_min_size = TRUE;
    }

  if (from->has_new_max_size)
    {
      to->new_max_width = from->new_max_width;
      to->new_max_height = from->new_max_height;
      to->has_new_max_size = TRUE;
    }

  if (from->scale > 0)
    to->scale = from->scale;

  if (from->has_new_buffer_transform)
    {
      to->buffer_transform = from->buffer_transform;
      to->has_new_buffer_transform = TRUE;
    }

  if (from->has_new_viewport_src_rect)
    {
      to->viewport_src_rect.origin.x = from->viewport_src_rect.origin.x;
      to->viewport_src_rect.origin.y = from->viewport_src_rect.origin.y;
      to->viewport_src_rect.size.width = from->viewport_src_rect.size.width;
      to->viewport_src_rect.size.height = from->viewport_src_rect.size.height;
      to->has_new_viewport_src_rect = TRUE;
    }

  if (from->has_new_viewport_dst_size)
    {
      to->viewport_dst_width = from->viewport_dst_width;
      to->viewport_dst_height = from->viewport_dst_height;
      to->has_new_viewport_dst_size = TRUE;
    }

  if (from->subsurface_placement_ops != NULL)
    {
      if (to->subsurface_placement_ops != NULL)
        {
          to->subsurface_placement_ops =
            g_slist_concat (to->subsurface_placement_ops,
                            from->subsurface_placement_ops);
        }
      else
        {
          to->subsurface_placement_ops = from->subsurface_placement_ops;
        }

      from->subsurface_placement_ops = NULL;
    }

  /*
   * A new commit indicates a new content update, so any previous
   * content update did not go on screen and needs to be discarded.
   */
  meta_wayland_surface_state_discard_presentation_feedback (to);
  wl_list_insert_list (&to->presentation_feedback_list,
                       &from->presentation_feedback_list);
  wl_list_init (&from->presentation_feedback_list);

  if (from->xdg_positioner)
    {
      g_clear_pointer (&to->xdg_positioner, g_free);
      to->xdg_positioner = g_steal_pointer (&from->xdg_positioner);
      to->xdg_popup_reposition_token = from->xdg_popup_reposition_token;
    }
}

static void
meta_wayland_surface_state_finalize (GObject *object)
{
  MetaWaylandSurfaceState *state = META_WAYLAND_SURFACE_STATE (object);

  meta_wayland_surface_state_clear (state);

  G_OBJECT_CLASS (meta_wayland_surface_state_parent_class)->finalize (object);
}

static void
meta_wayland_surface_state_init (MetaWaylandSurfaceState *state)
{
  meta_wayland_surface_state_set_default (state);
}

static void
meta_wayland_surface_state_class_init (MetaWaylandSurfaceStateClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_wayland_surface_state_finalize;

  surface_state_signals[SURFACE_STATE_SIGNAL_APPLIED] =
    g_signal_new ("applied",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
meta_wayland_surface_discard_presentation_feedback (MetaWaylandSurface *surface)
{
  while (!wl_list_empty (&surface->presentation_time.feedback_list))
    {
      MetaWaylandPresentationFeedback *feedback =
        wl_container_of (surface->presentation_time.feedback_list.next,
                         feedback, link);

      meta_wayland_presentation_feedback_discard (feedback);
    }
}

void
meta_wayland_surface_apply_placement_ops (MetaWaylandSurface      *parent,
                                          MetaWaylandSurfaceState *state)
{
  GSList *l;

  for (l = state->subsurface_placement_ops; l; l = l->next)
    {
      MetaWaylandSubsurfacePlacementOp *op = l->data;
      MetaWaylandSurface *surface = op->surface;
      GNode *sibling_node;

      g_node_unlink (surface->applied_state.subsurface_branch_node);

      if (!op->sibling)
        {
          surface->applied_state.parent = NULL;
          continue;
        }

      surface->applied_state.parent = parent;

      if (op->sibling == parent)
        sibling_node = parent->applied_state.subsurface_leaf_node;
      else
        sibling_node = op->sibling->applied_state.subsurface_branch_node;

      switch (op->placement)
        {
        case META_WAYLAND_SUBSURFACE_PLACEMENT_ABOVE:
          g_node_insert_after (parent->applied_state.subsurface_branch_node,
                               sibling_node,
                               surface->applied_state.subsurface_branch_node);
          break;
        case META_WAYLAND_SUBSURFACE_PLACEMENT_BELOW:
          g_node_insert_before (parent->applied_state.subsurface_branch_node,
                                sibling_node,
                                surface->applied_state.subsurface_branch_node);
          break;
        }
    }
}

void
meta_wayland_surface_apply_state (MetaWaylandSurface      *surface,
                                  MetaWaylandSurfaceState *state)
{
  gboolean had_damage = FALSE;
  int old_width, old_height;

  old_width = meta_wayland_surface_get_width (surface);
  old_height = meta_wayland_surface_get_height (surface);

  g_signal_emit (surface, surface_signals[SURFACE_PRE_STATE_APPLIED], 0);

  if (surface->role)
    {
      meta_wayland_surface_role_pre_apply_state (surface->role, state);
    }
  else
    {
      if (state->newly_attached && surface->unassigned.buffer)
        {
          meta_wayland_buffer_dec_use_count (surface->unassigned.buffer);
          g_clear_object (&surface->unassigned.buffer);
        }
    }

  if (state->newly_attached)
    {
      /* Always release any previously held buffer. If the buffer held is same
       * as the newly attached buffer, we still need to release it here, because
       * wl_surface.attach+commit and wl_buffer.release on the attached buffer
       * is symmetric.
       */
      if (surface->buffer_held)
        meta_wayland_buffer_dec_use_count (surface->buffer);

      g_set_object (&surface->buffer, state->buffer);
      g_clear_object (&surface->applied_state.texture);
      surface->applied_state.texture = g_steal_pointer (&state->texture);

      /* If the newly attached buffer is going to be accessed directly without
       * making a copy, such as an EGL buffer, mark it as in-use don't release
       * it until is replaced by a subsequent wl_surface.commit or when the
       * wl_surface is destroyed.
       */
      surface->buffer_held =
        (state->buffer &&
         (state->buffer->type != META_WAYLAND_BUFFER_TYPE_SHM &&
          state->buffer->type != META_WAYLAND_BUFFER_TYPE_SINGLE_PIXEL));
    }

  if (state->scale > 0)
    surface->applied_state.scale = state->scale;

  if (state->has_new_buffer_transform)
    surface->buffer_transform = state->buffer_transform;

  if (state->has_new_viewport_src_rect)
    {
      surface->viewport.src_rect.origin.x = state->viewport_src_rect.origin.x;
      surface->viewport.src_rect.origin.y = state->viewport_src_rect.origin.y;
      surface->viewport.src_rect.size.width = state->viewport_src_rect.size.width;
      surface->viewport.src_rect.size.height = state->viewport_src_rect.size.height;
      surface->viewport.has_src_rect = surface->viewport.src_rect.size.width > 0;
    }

  if (state->has_new_viewport_dst_size)
    {
      surface->viewport.dst_width = state->viewport_dst_width;
      surface->viewport.dst_height = state->viewport_dst_height;
      surface->viewport.has_dst_size = surface->viewport.dst_width > 0;
    }

  state->derived.surface_size_changed =
    meta_wayland_surface_get_width (surface) != old_width ||
    meta_wayland_surface_get_height (surface) != old_height;

  if (!mtk_region_is_empty (state->surface_damage) ||
      !mtk_region_is_empty (state->buffer_damage))
    {
      surface_process_damage (surface,
                              state->surface_damage,
                              state->buffer_damage);
      had_damage = TRUE;
    }

  surface->offset_x += state->dx;
  surface->offset_y += state->dy;

  if (state->opaque_region_set)
    {
      g_clear_pointer (&surface->opaque_region, mtk_region_unref);
      if (state->opaque_region)
        surface->opaque_region = mtk_region_ref (state->opaque_region);
    }

  if (state->input_region_set)
    {
      g_clear_pointer (&surface->input_region, mtk_region_unref);
      if (state->input_region)
        surface->input_region = mtk_region_ref (state->input_region);
    }

  /*
   * A new commit indicates a new content update, so any previous
   * content update did not go on screen and needs to be discarded.
   */
  meta_wayland_surface_discard_presentation_feedback (surface);

  wl_list_insert_list (&surface->presentation_time.feedback_list,
                       &state->presentation_feedback_list);
  wl_list_init (&state->presentation_feedback_list);

  if (!wl_list_empty (&surface->presentation_time.feedback_list))
    meta_wayland_compositor_add_presentation_feedback_surface (surface->compositor,
                                                               surface);

  if (surface->role)
    {
      meta_wayland_surface_role_apply_state (surface->role, state);
      g_assert (wl_list_empty (&state->frame_callback_list));
    }
  else
    {
      wl_list_insert_list (surface->unassigned.pending_frame_callback_list.prev,
                           &state->frame_callback_list);
      wl_list_init (&state->frame_callback_list);

      if (state->buffer)
        {
          /* The need to keep the wl_buffer from being released depends on what
           * role the surface is given. That means we need to also keep a use
           * count for wl_buffer's that are used by unassigned wl_surface's.
           */
          surface->unassigned.buffer = g_object_ref (state->buffer);
          meta_wayland_buffer_inc_use_count (surface->unassigned.buffer);
        }
    }

  if (state->subsurface_placement_ops)
    meta_wayland_surface_notify_subsurface_state_changed (surface);

  /* If we need to hold the newly attached buffer, drop its reference from the
   * state, to prevent meta_wayland_transaction_entry_destroy from decreasing
   * the use count.
   */
  if (state->newly_attached && surface->buffer_held)
    g_clear_object (&state->buffer);

  g_signal_emit (state,
                 surface_state_signals[SURFACE_STATE_SIGNAL_APPLIED],
                 0);

  if (had_damage)
    {
      MetaWindow *toplevel_window;

      toplevel_window = meta_wayland_surface_get_toplevel_window (surface);
      if (toplevel_window)
        {
          MetaWindowActor *toplevel_window_actor;

          toplevel_window_actor =
            meta_window_actor_from_window (toplevel_window);
          if (toplevel_window_actor)
            meta_window_actor_notify_damaged (toplevel_window_actor);
        }
    }

  if (surface->role)
    meta_wayland_surface_role_post_apply_state (surface->role, state);
}

MetaWaylandSurfaceState *
meta_wayland_surface_get_pending_state (MetaWaylandSurface *surface)
{
  return surface->pending_state;
}

MetaWaylandTransaction *
meta_wayland_surface_ensure_transaction (MetaWaylandSurface *surface)
{
  if (!surface->sub.transaction)
      surface->sub.transaction = meta_wayland_transaction_new (surface->compositor);

  return surface->sub.transaction;
}

static void
meta_wayland_surface_commit (MetaWaylandSurface *surface)
{
  MetaWaylandSurfaceState *pending = surface->pending_state;
  MetaWaylandBuffer *buffer = pending->buffer;
  MetaWaylandTransaction *transaction;
  MetaWaylandSurface *subsurface_surface;

  COGL_TRACE_BEGIN_SCOPED (MetaWaylandSurfaceCommit,
                           "Meta::WaylandSurface::commit()");

  if (pending->scale > 0)
    surface->committed_state.scale = pending->scale;

  if (buffer)
    {
      g_autoptr (GError) error = NULL;

      g_clear_signal_handler (&pending->buffer_destroy_handler_id,
                              buffer);

      if (!meta_wayland_buffer_is_realized (buffer))
        meta_wayland_buffer_realize (buffer);

      if (!meta_wayland_buffer_attach (buffer,
                                       &surface->committed_state.texture,
                                       &error))
        {
          g_warning ("Could not import pending buffer: %s", error->message);

          wl_resource_post_error (surface->resource, WL_DISPLAY_ERROR_NO_MEMORY,
                                  "Failed to attach buffer to surface %i: %s",
                                  wl_resource_get_id (surface->resource),
                                  error->message);
          return;
        }

      pending->texture = g_object_ref (surface->committed_state.texture);

      g_object_ref (buffer);
      meta_wayland_buffer_inc_use_count (buffer);
    }
  else if (pending->newly_attached)
    {
      g_clear_object (&surface->committed_state.texture);
    }

  if (surface->committed_state.texture)
    {
      MetaMultiTexture *committed_texture = surface->committed_state.texture;
      int committed_scale = surface->committed_state.scale;

      if ((meta_multi_texture_get_width (committed_texture) % committed_scale != 0) ||
          (meta_multi_texture_get_height (committed_texture) % committed_scale != 0))
        {
          if (!surface->role || !META_IS_WAYLAND_CURSOR_SURFACE (surface->role))
            {
              wl_resource_post_error (surface->resource, WL_SURFACE_ERROR_INVALID_SIZE,
                                      "Buffer size (%dx%d) must be an integer multiple "
                                      "of the buffer_scale (%d).",
                                      meta_multi_texture_get_width (committed_texture),
                                      meta_multi_texture_get_height (committed_texture),
                                      committed_scale);
              return;
            }
          else
            {
              struct wl_resource *resource = surface->resource;
              pid_t pid;

              wl_client_get_credentials (wl_resource_get_client (resource), &pid, NULL,
                                         NULL);

              g_warning ("Bug in client with pid %ld: Cursor buffer size (%dx%d) is "
                         "not an integer multiple of the buffer_scale (%d).",
                         (long) pid,
                         meta_multi_texture_get_width (committed_texture),
                         meta_multi_texture_get_height (committed_texture),
                         committed_scale);
            }
        }
    }

  if (meta_wayland_surface_is_synchronized (surface))
    transaction = meta_wayland_surface_ensure_transaction (surface);
  else
    transaction = meta_wayland_transaction_new (surface->compositor);

  if (surface->role)
    meta_wayland_surface_role_commit_state (surface->role, transaction, pending);

  meta_wayland_transaction_merge_pending_state (transaction, surface);

  META_WAYLAND_SURFACE_FOREACH_SUBSURFACE (&surface->committed_state,
                                           subsurface_surface)
    {
      if (!subsurface_surface->sub.transaction)
        continue;

      meta_wayland_transaction_merge_into (subsurface_surface->sub.transaction,
                                           transaction);
      subsurface_surface->sub.transaction = NULL;
    }

  /*
   * If this is a sub-surface and it is in effective synchronous mode, only
   * cache the pending surface state until either one of the following two
   * scenarios happens:
   *  1) Its parent surface gets its state applied.
   *  2) Its mode changes from synchronized to desynchronized and its parent
   *     surface is in effective desynchronized mode.
   */
  if (!meta_wayland_surface_is_synchronized (surface))
    meta_wayland_transaction_commit (transaction);
}

static void
wl_surface_destroy (struct wl_client *client,
                    struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
wl_surface_attach (struct wl_client   *client,
                   struct wl_resource *surface_resource,
                   struct wl_resource *buffer_resource,
                   int32_t             dx,
                   int32_t             dy)
{
  MetaWaylandSurface *surface =
    wl_resource_get_user_data (surface_resource);
  MetaWaylandCompositor *compositor = surface->compositor;
  MetaWaylandSurfaceState *pending = surface->pending_state;
  MetaWaylandBuffer *buffer;

  if (buffer_resource)
    buffer = meta_wayland_buffer_from_resource (compositor, buffer_resource);
  else
    buffer = NULL;

  if (surface->pending_state->buffer)
    {
      g_clear_signal_handler (&pending->buffer_destroy_handler_id,
                              pending->buffer);
    }

  if (wl_resource_get_version (surface_resource) >=
      WL_SURFACE_OFFSET_SINCE_VERSION)
    {
      if (dx != 0 || dy != 0)
        {
          wl_resource_post_error (surface_resource,
                                  WL_SURFACE_ERROR_INVALID_OFFSET,
                                  "Attaching with an offset is no longer allowed");
          return;
        }
    }
  else
    {
      pending->dx = dx;
      pending->dy = dy;
    }

  pending->newly_attached = TRUE;
  pending->buffer = buffer;

  if (buffer)
    {
      pending->buffer_destroy_handler_id =
        g_signal_connect (buffer, "resource-destroyed",
                          G_CALLBACK (pending_buffer_resource_destroyed),
                          pending);
    }
}

static void
wl_surface_damage (struct wl_client   *client,
                   struct wl_resource *surface_resource,
                   int32_t             x,
                   int32_t             y,
                   int32_t             width,
                   int32_t             height)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandSurfaceState *pending = surface->pending_state;
  MtkRectangle rectangle;

  rectangle = (MtkRectangle) {
    .x = x,
    .y = y,
    .width = width,
    .height = height
  };
  mtk_region_union_rectangle (pending->surface_damage, &rectangle);
}

static void
destroy_frame_callback (struct wl_resource *callback_resource)
{
  MetaWaylandFrameCallback *callback =
    wl_resource_get_user_data (callback_resource);

  wl_list_remove (&callback->link);
  g_free (callback);
}

static void
wl_surface_frame (struct wl_client   *client,
                  struct wl_resource *surface_resource,
                  uint32_t            callback_id)
{
  MetaWaylandFrameCallback *callback;
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandSurfaceState *pending = surface->pending_state;

  callback = g_new0 (MetaWaylandFrameCallback, 1);
  callback->surface = surface;
  callback->resource = wl_resource_create (client,
                                           &wl_callback_interface,
                                           META_WL_CALLBACK_VERSION,
                                           callback_id);
  wl_resource_set_implementation (callback->resource, NULL, callback,
                                  destroy_frame_callback);

  wl_list_insert (pending->frame_callback_list.prev, &callback->link);
}

static void
wl_surface_set_opaque_region (struct wl_client   *client,
                              struct wl_resource *surface_resource,
                              struct wl_resource *region_resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandSurfaceState *pending = surface->pending_state;

  g_clear_pointer (&pending->opaque_region, mtk_region_unref);
  if (region_resource)
    {
      MetaWaylandRegion *region = wl_resource_get_user_data (region_resource);
      MtkRegion *mtk_region = meta_wayland_region_peek_region (region);
      pending->opaque_region = mtk_region_copy (mtk_region);
    }
  pending->opaque_region_set = TRUE;
}

static void
wl_surface_set_input_region (struct wl_client   *client,
                             struct wl_resource *surface_resource,
                             struct wl_resource *region_resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandSurfaceState *pending = surface->pending_state;

  g_clear_pointer (&pending->input_region, mtk_region_unref);
  if (region_resource)
    {
      MetaWaylandRegion *region = wl_resource_get_user_data (region_resource);
      MtkRegion *mtk_region = meta_wayland_region_peek_region (region);
      pending->input_region = mtk_region_copy (mtk_region);
    }
  pending->input_region_set = TRUE;
}

static void
wl_surface_commit (struct wl_client   *client,
                   struct wl_resource *resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (resource);

  meta_wayland_surface_commit (surface);
}

static MetaMonitorTransform
transform_from_wl_output_transform (int32_t transform_value)
{
  enum wl_output_transform transform = transform_value;

  switch (transform)
    {
    case WL_OUTPUT_TRANSFORM_NORMAL:
      return META_MONITOR_TRANSFORM_NORMAL;
    case WL_OUTPUT_TRANSFORM_90:
      return META_MONITOR_TRANSFORM_90;
    case WL_OUTPUT_TRANSFORM_180:
      return META_MONITOR_TRANSFORM_180;
    case WL_OUTPUT_TRANSFORM_270:
      return META_MONITOR_TRANSFORM_270;
    case WL_OUTPUT_TRANSFORM_FLIPPED:
      return META_MONITOR_TRANSFORM_FLIPPED;
    case WL_OUTPUT_TRANSFORM_FLIPPED_90:
      return META_MONITOR_TRANSFORM_FLIPPED_90;
    case WL_OUTPUT_TRANSFORM_FLIPPED_180:
      return META_MONITOR_TRANSFORM_FLIPPED_180;
    case WL_OUTPUT_TRANSFORM_FLIPPED_270:
      return META_MONITOR_TRANSFORM_FLIPPED_270;
    default:
      return -1;
    }
}

static void
wl_surface_set_buffer_transform (struct wl_client   *client,
                                 struct wl_resource *resource,
                                 int32_t             transform)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (resource);
  MetaWaylandSurfaceState *pending = surface->pending_state;
  MetaMonitorTransform buffer_transform;

  buffer_transform = transform_from_wl_output_transform (transform);

  if (buffer_transform == -1)
    {
      wl_resource_post_error (resource,
                              WL_SURFACE_ERROR_INVALID_TRANSFORM,
                              "Trying to set invalid buffer_transform of %d",
                              transform);
      return;
    }

  pending->buffer_transform = buffer_transform;
  pending->has_new_buffer_transform = TRUE;
}

static void
wl_surface_set_buffer_scale (struct wl_client *client,
                             struct wl_resource *resource,
                             int scale)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (resource);
  MetaWaylandSurfaceState *pending = surface->pending_state;

  if (scale <= 0)
    {
      wl_resource_post_error (resource,
                              WL_SURFACE_ERROR_INVALID_SCALE,
                              "Trying to set invalid buffer_scale of %d",
                              scale);
      return;
    }

  pending->scale = scale;
}

static void
wl_surface_damage_buffer (struct wl_client   *client,
                          struct wl_resource *surface_resource,
                          int32_t             x,
                          int32_t             y,
                          int32_t             width,
                          int32_t             height)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandSurfaceState *pending = surface->pending_state;
  MtkRectangle rectangle;

  rectangle = (MtkRectangle) {
    .x = x,
    .y = y,
    .width = width,
    .height = height
  };
  mtk_region_union_rectangle (pending->buffer_damage, &rectangle);
}

static void
wl_surface_offset (struct wl_client   *client,
                   struct wl_resource *surface_resource,
                   int32_t             dx,
                   int32_t             dy)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandSurfaceState *pending = surface->pending_state;

  pending->dx = dx;
  pending->dy = dy;
}

static const struct wl_surface_interface meta_wayland_wl_surface_interface = {
  wl_surface_destroy,
  wl_surface_attach,
  wl_surface_damage,
  wl_surface_frame,
  wl_surface_set_opaque_region,
  wl_surface_set_input_region,
  wl_surface_commit,
  wl_surface_set_buffer_transform,
  wl_surface_set_buffer_scale,
  wl_surface_damage_buffer,
  wl_surface_offset,
};

static void
handle_output_destroyed (MetaWaylandOutput  *wayland_output,
                         MetaWaylandSurface *surface)
{
  set_surface_is_on_output (surface, wayland_output, FALSE);
}

static void
handle_output_bound (MetaWaylandOutput  *wayland_output,
                     struct wl_resource *output_resource,
                     MetaWaylandSurface *surface)
{
  if (!surface->resource)
    return;

  if (wl_resource_get_client (output_resource) ==
      wl_resource_get_client (surface->resource))
    wl_surface_send_enter (surface->resource, output_resource);
}

static void
surface_entered_output (MetaWaylandSurface *surface,
                        MetaWaylandOutput *wayland_output)
{
  g_signal_connect (wayland_output, "output-destroyed",
                    G_CALLBACK (handle_output_destroyed),
                    surface);

  if (surface->resource)
    {
      const GList *l;

      for (l = meta_wayland_output_get_resources (wayland_output); l; l = l->next)
        {
          struct wl_resource *resource = l->data;

          if (wl_resource_get_client (resource) !=
              wl_resource_get_client (surface->resource))
            continue;

          wl_surface_send_enter (surface->resource, resource);
        }
    }

  g_signal_connect (wayland_output, "output-bound",
                    G_CALLBACK (handle_output_bound),
                    surface);
}

static void
surface_left_output (MetaWaylandSurface *surface,
                     MetaWaylandOutput *wayland_output)
{
  const GList *l;

  g_signal_handlers_disconnect_by_func (wayland_output,
                                        G_CALLBACK (handle_output_destroyed),
                                        surface);

  g_signal_handlers_disconnect_by_func (wayland_output,
                                        G_CALLBACK (handle_output_bound),
                                        surface);

  if (!surface->resource)
    return;

  for (l = meta_wayland_output_get_resources (wayland_output); l; l = l->next)
    {
      struct wl_resource *resource = l->data;

      if (wl_resource_get_client (resource) !=
          wl_resource_get_client (surface->resource))
        continue;

      wl_surface_send_leave (surface->resource, resource);
    }
}

static void
set_surface_is_on_output (MetaWaylandSurface *surface,
                          MetaWaylandOutput *wayland_output,
                          gboolean is_on_output)
{
  gboolean was_on_output;

  was_on_output = g_hash_table_contains (surface->outputs, wayland_output);

  if (!was_on_output && is_on_output)
    {
      g_hash_table_add (surface->outputs, wayland_output);
      surface_entered_output (surface, wayland_output);
    }
  else if (was_on_output && !is_on_output)
    {
      g_hash_table_remove (surface->outputs, wayland_output);
      surface_left_output (surface, wayland_output);
    }
}

static void
surface_output_disconnect_signals (gpointer key,
                                   gpointer value,
                                   gpointer user_data)
{
  MetaWaylandOutput *wayland_output = key;
  MetaWaylandSurface *surface = user_data;

  g_signal_handlers_disconnect_by_func (wayland_output,
                                        G_CALLBACK (handle_output_destroyed),
                                        surface);

  g_signal_handlers_disconnect_by_func (wayland_output,
                                        G_CALLBACK (handle_output_bound),
                                        surface);
}

double
meta_wayland_surface_get_highest_output_scale (MetaWaylandSurface *surface)
{
  double scale = 0.0;
  MetaWindow *window;
  MetaLogicalMonitor *logical_monitor;

  window = meta_wayland_surface_get_window (surface);
  if (!window)
    goto out;

  logical_monitor = meta_window_get_highest_scale_monitor (window);
  if (!logical_monitor)
    goto out;

  scale = meta_logical_monitor_get_scale (logical_monitor);

out:
  return scale;
}

static MetaMonitorTransform
meta_wayland_surface_get_output_transform (MetaWaylandSurface *surface)
{
  MetaMonitorTransform transform = META_MONITOR_TRANSFORM_NORMAL;
  MetaWindow *window;
  MetaLogicalMonitor *logical_monitor;

  window = meta_wayland_surface_get_window (surface);
  if (!window)
    return transform;

  logical_monitor = meta_window_get_highest_scale_monitor (window);
  if (!logical_monitor)
    return transform;

  transform = meta_logical_monitor_get_transform (logical_monitor);
  return transform;
}

static void
update_surface_output_state (gpointer key, gpointer value, gpointer user_data)
{
  MetaWaylandOutput *wayland_output = value;
  MetaWaylandSurface *surface = user_data;
  MetaLogicalMonitor *logical_monitor;
  gboolean is_on_logical_monitor;

  g_assert (surface->role);

  logical_monitor = meta_wayland_output_get_logical_monitor (wayland_output);
  if (!logical_monitor)
    {
      set_surface_is_on_output (surface, wayland_output, FALSE);
      return;
    }

  is_on_logical_monitor =
    meta_wayland_surface_role_is_on_logical_monitor (surface->role,
                                                     logical_monitor);
  set_surface_is_on_output (surface, wayland_output, is_on_logical_monitor);
}

void
meta_wayland_surface_update_outputs (MetaWaylandSurface *surface)
{
  if (!surface->compositor)
    return;

  g_hash_table_foreach (surface->compositor->outputs,
                        update_surface_output_state,
                        surface);
}

void
meta_wayland_surface_notify_unmapped (MetaWaylandSurface *surface)
{
  g_signal_emit (surface, surface_signals[SURFACE_UNMAPPED], 0);
}

static void
meta_wayland_surface_finalize (GObject *object)
{
  MetaWaylandSurface *surface = META_WAYLAND_SURFACE (object);
  MetaWaylandCompositor *compositor = surface->compositor;
  MetaWaylandFrameCallback *cb, *next;

  g_clear_object (&surface->scanout_candidate);
  g_clear_object (&surface->role);

  if (surface->unassigned.buffer)
    {
      meta_wayland_buffer_dec_use_count (surface->unassigned.buffer);
      g_clear_object (&surface->unassigned.buffer);
    }

  if (surface->buffer_held)
    meta_wayland_buffer_dec_use_count (surface->buffer);
  g_clear_object (&surface->applied_state.texture);
  g_clear_object (&surface->buffer);

  g_clear_pointer (&surface->opaque_region, mtk_region_unref);
  g_clear_pointer (&surface->input_region, mtk_region_unref);

  meta_wayland_compositor_remove_frame_callback_surface (compositor, surface);
  meta_wayland_compositor_remove_presentation_feedback_surface (compositor,
                                                                surface);

  g_hash_table_foreach (surface->outputs,
                        surface_output_disconnect_signals,
                        surface);
  g_hash_table_destroy (surface->outputs);

  wl_list_for_each_safe (cb, next,
                         &surface->unassigned.pending_frame_callback_list,
                         link)
    wl_resource_destroy (cb->resource);

  meta_wayland_surface_discard_presentation_feedback (surface);

  g_clear_pointer (&surface->applied_state.subsurface_branch_node, g_node_destroy);

  g_hash_table_destroy (surface->shortcut_inhibited_seats);

  G_OBJECT_CLASS (meta_wayland_surface_parent_class)->finalize (object);
}

static void
wl_surface_destructor (struct wl_resource *resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (resource);
  MetaWaylandSurface *subsurface_surface;

  g_signal_emit (surface, surface_signals[SURFACE_DESTROY], 0);

  g_clear_object (&surface->pending_state);
  g_clear_pointer (&surface->sub.transaction, meta_wayland_transaction_free);

  if (surface->resource)
    wl_resource_set_user_data (g_steal_pointer (&surface->resource), NULL);

  META_WAYLAND_SURFACE_FOREACH_SUBSURFACE (&surface->committed_state,
                                           subsurface_surface)
    meta_wayland_subsurface_parent_destroyed (subsurface_surface);

  g_clear_pointer (&surface->wl_subsurface, wl_resource_destroy);
  g_clear_pointer (&surface->committed_state.subsurface_branch_node, g_node_destroy);

  g_clear_object (&surface->committed_state.texture);

  /*
   * Any transactions referencing this surface will keep it alive until they get
   * applied/destroyed. The last reference will be dropped in
   * meta_wayland_transaction_free.
   */
  g_object_unref (surface);
}

MetaWaylandSurface *
meta_wayland_surface_create (MetaWaylandCompositor *compositor,
                             struct wl_client      *client,
                             struct wl_resource    *compositor_resource,
                             guint32                id)
{
  MetaWaylandSurface *surface = g_object_new (META_TYPE_WAYLAND_SURFACE, NULL);
  int surface_version;

  surface->compositor = compositor;
  surface->applied_state.scale = 1;
  surface->committed_state.scale = 1;

  surface_version = wl_resource_get_version (compositor_resource);
  surface->resource = wl_resource_create (client,
                                          &wl_surface_interface,
                                          surface_version,
                                          id);
  wl_resource_set_implementation (surface->resource,
                                  &meta_wayland_wl_surface_interface,
                                  surface,
                                  wl_surface_destructor);

  wl_list_init (&surface->unassigned.pending_frame_callback_list);

  surface->outputs = g_hash_table_new (NULL, NULL);
  surface->shortcut_inhibited_seats = g_hash_table_new (NULL, NULL);

  wl_list_init (&surface->presentation_time.feedback_list);

#ifdef HAVE_XWAYLAND
  meta_wayland_compositor_notify_surface_id (compositor, id, surface);
#endif

  return surface;
}

gboolean
meta_wayland_surface_begin_grab_op (MetaWaylandSurface   *surface,
                                    MetaWaylandSeat      *seat,
                                    MetaGrabOp            grab_op,
                                    ClutterInputDevice   *device,
                                    ClutterEventSequence *sequence,
                                    gfloat                x,
                                    gfloat                y)
{
  MetaWindow *window = meta_wayland_surface_get_window (surface);

  if (grab_op == META_GRAB_OP_NONE)
    return FALSE;

  /* This is an input driven operation so we set frame_action to
     constrain it in the same way as it would be if the window was
     being moved/resized via a SSD event. */
  return meta_window_begin_grab_op (window,
                                    grab_op,
                                    device, sequence,
                                    meta_display_get_current_time_roundtrip (window->display),
                                    &GRAPHENE_POINT_INIT (x, y));
}

/**
 * meta_wayland_shell_init:
 * @compositor: The #MetaWaylandCompositor object
 *
 * Initializes the Wayland interfaces providing features that deal with
 * desktop-specific conundrums, like XDG shell, etc.
 */
void
meta_wayland_shell_init (MetaWaylandCompositor *compositor)
{
  meta_wayland_xdg_shell_init (compositor);
  meta_wayland_init_gtk_shell (compositor);
  meta_wayland_init_viewporter (compositor);
  meta_wayland_init_fractional_scale (compositor);
}

void
meta_wayland_surface_configure_notify (MetaWaylandSurface             *surface,
                                       MetaWaylandWindowConfiguration *configuration)
{
  MetaWaylandShellSurface *shell_surface =
    META_WAYLAND_SHELL_SURFACE (surface->role);

  g_signal_emit (surface, surface_signals[SURFACE_CONFIGURE], 0);

  meta_wayland_shell_surface_configure (shell_surface, configuration);
}

void
meta_wayland_surface_ping (MetaWaylandSurface *surface,
                           guint32             serial)
{
  MetaWaylandShellSurface *shell_surface =
    META_WAYLAND_SHELL_SURFACE (surface->role);

  meta_wayland_shell_surface_ping (shell_surface, serial);
}

void
meta_wayland_surface_delete (MetaWaylandSurface *surface)
{
  MetaWaylandShellSurface *shell_surface =
    META_WAYLAND_SHELL_SURFACE (surface->role);

  meta_wayland_shell_surface_close (shell_surface);
}

void
meta_wayland_surface_window_managed (MetaWaylandSurface *surface,
                                     MetaWindow         *window)
{
  MetaWaylandShellSurface *shell_surface =
    META_WAYLAND_SHELL_SURFACE (surface->role);

  meta_wayland_shell_surface_managed (shell_surface, window);
}

void
meta_wayland_surface_drag_dest_focus_in (MetaWaylandSurface   *surface,
                                         MetaWaylandDataOffer *offer)
{
  MetaWaylandCompositor *compositor = surface->compositor;
  MetaWaylandDataDevice *data_device = &compositor->seat->data_device;

  surface->dnd.funcs->focus_in (data_device, surface, offer);
}

void
meta_wayland_surface_drag_dest_motion (MetaWaylandSurface *surface,
                                       float               x,
                                       float               y,
                                       uint32_t            time_ms)
{
  MetaWaylandCompositor *compositor = surface->compositor;
  MetaWaylandDataDevice *data_device = &compositor->seat->data_device;

  surface->dnd.funcs->motion (data_device, surface, x, y, time_ms);
}

void
meta_wayland_surface_drag_dest_focus_out (MetaWaylandSurface *surface)
{
  MetaWaylandCompositor *compositor = surface->compositor;
  MetaWaylandDataDevice *data_device = &compositor->seat->data_device;

  surface->dnd.funcs->focus_out (data_device, surface);
}

void
meta_wayland_surface_drag_dest_drop (MetaWaylandSurface *surface)
{
  MetaWaylandCompositor *compositor = surface->compositor;
  MetaWaylandDataDevice *data_device = &compositor->seat->data_device;

  surface->dnd.funcs->drop (data_device, surface);
}

void
meta_wayland_surface_drag_dest_update (MetaWaylandSurface *surface)
{
  MetaWaylandCompositor *compositor = surface->compositor;
  MetaWaylandDataDevice *data_device = &compositor->seat->data_device;

  surface->dnd.funcs->update (data_device, surface);
}

MetaWaylandSurface *
meta_wayland_surface_get_toplevel (MetaWaylandSurface *surface)
{
  if (surface->role)
    return meta_wayland_surface_role_get_toplevel (surface->role);
  else
    return NULL;
}

MetaWindow *
meta_wayland_surface_get_toplevel_window (MetaWaylandSurface *surface)
{
  MetaWaylandSurface *toplevel;

  toplevel = meta_wayland_surface_get_toplevel (surface);
  if (toplevel)
    return meta_wayland_surface_get_window (toplevel);
  else
    return NULL;
}

void
meta_wayland_surface_get_relative_coordinates (MetaWaylandSurface *surface,
                                               float               abs_x,
                                               float               abs_y,
                                               float               *sx,
                                               float               *sy)
{
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_GET_CLASS (surface->role);

  surface_role_class->get_relative_coordinates (surface->role,
                                                abs_x, abs_y,
                                                sx, sy);
}

void
meta_wayland_surface_get_absolute_coordinates (MetaWaylandSurface  *surface,
                                               float                sx,
                                               float                sy,
                                               float               *x,
                                               float               *y)
{
  ClutterActor *actor =
    CLUTTER_ACTOR (meta_wayland_surface_get_actor (surface));
  MetaWindow *window = meta_wayland_surface_get_window (surface);
  ClutterActor *window_actor =
    CLUTTER_ACTOR (meta_window_actor_from_window (window));
  graphene_point3d_t sv = {
    .x = sx,
    .y = sy,
  };
  graphene_point3d_t v = { 0 };

  clutter_actor_apply_relative_transform_to_point (actor, window_actor, &sv, &v);

  *x = clutter_actor_get_x (window_actor) + v.x;
  *y = clutter_actor_get_y (window_actor) + v.y;
}

static void
meta_wayland_surface_init (MetaWaylandSurface *surface)
{
  surface->pending_state = meta_wayland_surface_state_new ();

  surface->applied_state.subsurface_branch_node = g_node_new (surface);
  surface->applied_state.subsurface_leaf_node =
    g_node_prepend_data (surface->applied_state.subsurface_branch_node, surface);

  surface->committed_state.subsurface_branch_node = g_node_new (surface);
  surface->committed_state.subsurface_leaf_node =
    g_node_prepend_data (surface->committed_state.subsurface_branch_node, surface);
}

static void
meta_wayland_surface_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  MetaWaylandSurface *surface = META_WAYLAND_SURFACE (object);

  switch (prop_id)
    {
    case PROP_SCANOUT_CANDIDATE:
      g_value_set_object (value, surface->scanout_candidate);
      break;
    case PROP_WINDOW:
      g_value_set_object (value, meta_wayland_surface_get_window (surface));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_wayland_surface_class_init (MetaWaylandSurfaceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_wayland_surface_finalize;
  object_class->get_property = meta_wayland_surface_get_property;

  obj_props[PROP_SCANOUT_CANDIDATE] =
    g_param_spec_object ("scanout-candidate", NULL, NULL,
                         META_TYPE_CRTC,
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS);

  obj_props[PROP_WINDOW] =
    g_param_spec_object ("window", NULL, NULL,
                         META_TYPE_WINDOW,
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);

  surface_signals[SURFACE_DESTROY] =
    g_signal_new ("destroy",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  surface_signals[SURFACE_UNMAPPED] =
    g_signal_new ("unmapped",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  surface_signals[SURFACE_CONFIGURE] =
    g_signal_new ("configure",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  surface_signals[SURFACE_SHORTCUTS_INHIBITED] =
    g_signal_new ("shortcuts-inhibited",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  surface_signals[SURFACE_SHORTCUTS_RESTORED] =
    g_signal_new ("shortcuts-restored",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
  surface_signals[SURFACE_GEOMETRY_CHANGED] =
    g_signal_new ("geometry-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
  surface_signals[SURFACE_PRE_STATE_APPLIED] =
    g_signal_new ("pre-state-applied",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
  surface_signals[SURFACE_ACTOR_CHANGED] =
    g_signal_new ("actor-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}

static void
meta_wayland_surface_role_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  MetaWaylandSurfaceRole *surface_role = META_WAYLAND_SURFACE_ROLE (object);
  MetaWaylandSurfaceRolePrivate *priv =
    meta_wayland_surface_role_get_instance_private (surface_role);

  switch (prop_id)
    {
    case SURFACE_ROLE_PROP_SURFACE:
      priv->surface = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_wayland_surface_role_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  MetaWaylandSurfaceRole *surface_role = META_WAYLAND_SURFACE_ROLE (object);
  MetaWaylandSurfaceRolePrivate *priv =
    meta_wayland_surface_role_get_instance_private (surface_role);

  switch (prop_id)
    {
    case SURFACE_ROLE_PROP_SURFACE:
      g_value_set_object (value, priv->surface);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_wayland_surface_role_init (MetaWaylandSurfaceRole *role)
{
}

static void
meta_wayland_surface_role_class_init (MetaWaylandSurfaceRoleClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = meta_wayland_surface_role_set_property;
  object_class->get_property = meta_wayland_surface_role_get_property;

  g_object_class_install_property (object_class,
                                   SURFACE_ROLE_PROP_SURFACE,
                                   g_param_spec_object ("surface", NULL, NULL,
                                                        META_TYPE_WAYLAND_SURFACE,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

static void
meta_wayland_surface_role_assigned (MetaWaylandSurfaceRole *surface_role)
{
  META_WAYLAND_SURFACE_ROLE_GET_CLASS (surface_role)->assigned (surface_role);
}

static void
meta_wayland_surface_role_commit_state (MetaWaylandSurfaceRole  *surface_role,
                                        MetaWaylandTransaction  *transaction,
                                        MetaWaylandSurfaceState *pending)
{
  MetaWaylandSurfaceRoleClass *klass;

  klass = META_WAYLAND_SURFACE_ROLE_GET_CLASS (surface_role);
  if (klass->commit_state)
    klass->commit_state (surface_role, transaction, pending);
}

static void
meta_wayland_surface_role_pre_apply_state (MetaWaylandSurfaceRole  *surface_role,
                                           MetaWaylandSurfaceState *pending)
{
  MetaWaylandSurfaceRoleClass *klass;

  klass = META_WAYLAND_SURFACE_ROLE_GET_CLASS (surface_role);
  if (klass->pre_apply_state)
    klass->pre_apply_state (surface_role, pending);
}

static void
meta_wayland_surface_role_post_apply_state (MetaWaylandSurfaceRole  *surface_role,
                                            MetaWaylandSurfaceState *pending)
{
  MetaWaylandSurfaceRoleClass *klass;

  klass = META_WAYLAND_SURFACE_ROLE_GET_CLASS (surface_role);
  if (klass->post_apply_state)
    klass->post_apply_state (surface_role, pending);
}

static void
meta_wayland_surface_role_apply_state (MetaWaylandSurfaceRole  *surface_role,
                                       MetaWaylandSurfaceState *pending)
{
  META_WAYLAND_SURFACE_ROLE_GET_CLASS (surface_role)->apply_state (surface_role,
                                                                   pending);
}

static gboolean
meta_wayland_surface_role_is_on_logical_monitor (MetaWaylandSurfaceRole *surface_role,
                                                 MetaLogicalMonitor     *logical_monitor)
{
  MetaWaylandSurfaceRoleClass *klass;

  klass = META_WAYLAND_SURFACE_ROLE_GET_CLASS (surface_role);
  if (klass->is_on_logical_monitor)
    return klass->is_on_logical_monitor (surface_role, logical_monitor);
  else
    return FALSE;
}

static MetaWaylandSurface *
meta_wayland_surface_role_get_toplevel (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurfaceRoleClass *klass;

  klass = META_WAYLAND_SURFACE_ROLE_GET_CLASS (surface_role);
  if (klass->get_toplevel)
    return klass->get_toplevel (surface_role);
  else
    return NULL;
}

static MetaWindow *
meta_wayland_surface_role_get_window (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurfaceRoleClass *klass;

  klass = META_WAYLAND_SURFACE_ROLE_GET_CLASS (surface_role);

  if (klass->get_window)
    return klass->get_window (surface_role);
  else
    return NULL;
}

/**
 * meta_wayland_surface_get_window:
 * @surface: a #MetaWaylandSurface
 *
 * Get the #MetaWindow associated with this wayland surface.
 *
 * Returns: (nullable) (transfer none): a #MetaWindow
 */
MetaWindow *
meta_wayland_surface_get_window (MetaWaylandSurface *surface)
{
  if (!surface->role)
    return NULL;

  return meta_wayland_surface_role_get_window (surface->role);
}

static gboolean
meta_wayland_surface_role_is_synchronized (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurfaceRoleClass *klass;

  klass = META_WAYLAND_SURFACE_ROLE_GET_CLASS (surface_role);
  if (klass->is_synchronized)
    return klass->is_synchronized (surface_role);
  else
    return FALSE;
}

gboolean
meta_wayland_surface_is_synchronized (MetaWaylandSurface *surface)
{
  if (!surface->role)
    return FALSE;

  return meta_wayland_surface_role_is_synchronized (surface->role);
}

static void
meta_wayland_surface_role_notify_subsurface_state_changed (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurfaceRoleClass *klass;

  klass = META_WAYLAND_SURFACE_ROLE_GET_CLASS (surface_role);
  g_return_if_fail (klass->notify_subsurface_state_changed);

  klass->notify_subsurface_state_changed (surface_role);
}

void
meta_wayland_surface_notify_subsurface_state_changed (MetaWaylandSurface *surface)
{
  if (surface->role)
    meta_wayland_surface_role_notify_subsurface_state_changed (surface->role);
}

MetaWaylandSurface *
meta_wayland_surface_role_get_surface (MetaWaylandSurfaceRole *role)
{
  MetaWaylandSurfaceRolePrivate *priv =
    meta_wayland_surface_role_get_instance_private (role);

  return priv->surface;
}

MtkRegion *
meta_wayland_surface_calculate_input_region (MetaWaylandSurface *surface)
{
  MtkRegion *region;
  MtkRectangle buffer_rect;

  if (!surface->buffer)
    return NULL;

  buffer_rect = (MtkRectangle) {
    .width = meta_wayland_surface_get_width (surface),
    .height = meta_wayland_surface_get_height (surface),
  };
  region = mtk_region_create_rectangle (&buffer_rect);

  if (surface->input_region)
    mtk_region_intersect (region, surface->input_region);

  return region;
}

void
meta_wayland_surface_inhibit_shortcuts (MetaWaylandSurface *surface,
                                        MetaWaylandSeat    *seat)
{
  g_hash_table_add (surface->shortcut_inhibited_seats, seat);
  g_signal_emit (surface, surface_signals[SURFACE_SHORTCUTS_INHIBITED], 0);
}

void
meta_wayland_surface_restore_shortcuts (MetaWaylandSurface *surface,
                                        MetaWaylandSeat    *seat)
{
  g_signal_emit (surface, surface_signals[SURFACE_SHORTCUTS_RESTORED], 0);
  g_hash_table_remove (surface->shortcut_inhibited_seats, seat);
}

gboolean
meta_wayland_surface_is_shortcuts_inhibited (MetaWaylandSurface *surface,
                                             MetaWaylandSeat    *seat)
{
  if (surface->shortcut_inhibited_seats == NULL)
    return FALSE;

  return g_hash_table_contains (surface->shortcut_inhibited_seats, seat);
}

MetaMultiTexture *
meta_wayland_surface_get_texture (MetaWaylandSurface *surface)
{
  return surface->applied_state.texture;
}

MetaSurfaceActor *
meta_wayland_surface_get_actor (MetaWaylandSurface *surface)
{
  if (!surface->role || !META_IS_WAYLAND_ACTOR_SURFACE (surface->role))
    return NULL;

  return meta_wayland_actor_surface_get_actor (META_WAYLAND_ACTOR_SURFACE (surface->role));
}

void
meta_wayland_surface_notify_geometry_changed (MetaWaylandSurface *surface)
{
  g_signal_emit (surface, surface_signals[SURFACE_GEOMETRY_CHANGED], 0);
}

int
meta_wayland_surface_get_width (MetaWaylandSurface *surface)
{
  if (surface->viewport.has_dst_size)
    {
      return surface->viewport.dst_width;
    }
  else if (surface->viewport.has_src_rect)
    {
      return ceilf (surface->viewport.src_rect.size.width);
    }
  else
    {
      int width;

      if (meta_monitor_transform_is_rotated (surface->buffer_transform))
        width = meta_wayland_surface_get_buffer_height (surface);
      else
        width = meta_wayland_surface_get_buffer_width (surface);

      return width / surface->applied_state.scale;
    }
}

int
meta_wayland_surface_get_height (MetaWaylandSurface *surface)
{
  if (surface->viewport.has_dst_size)
    {
      return surface->viewport.dst_height;
    }
  else if (surface->viewport.has_src_rect)
    {
      return ceilf (surface->viewport.src_rect.size.height);
    }
  else
    {
      int height;

      if (meta_monitor_transform_is_rotated (surface->buffer_transform))
        height = meta_wayland_surface_get_buffer_width (surface);
      else
        height = meta_wayland_surface_get_buffer_height (surface);

      return height / surface->applied_state.scale;
    }
}

int
meta_wayland_surface_get_buffer_width (MetaWaylandSurface *surface)
{
  MetaWaylandBuffer *buffer = meta_wayland_surface_get_buffer (surface);

  if (buffer)
    return meta_multi_texture_get_width (surface->applied_state.texture);
  else
    return 0;
}

int
meta_wayland_surface_get_buffer_height (MetaWaylandSurface *surface)
{
  MetaWaylandBuffer *buffer = meta_wayland_surface_get_buffer (surface);

  if (buffer)
    return meta_multi_texture_get_height (surface->applied_state.texture);
  else
    return 0;
}

CoglScanout *
meta_wayland_surface_try_acquire_scanout (MetaWaylandSurface *surface,
                                          CoglOnscreen       *onscreen,
                                          ClutterStageView   *stage_view)
{
  MetaRendererView *renderer_view;
  MetaSurfaceActor *surface_actor;
  MetaMonitorTransform view_transform;
  ClutterActorBox actor_box;
  MtkRectangle *dst_rect_ptr = NULL;
  MtkRectangle dst_rect;
  graphene_rect_t *src_rect_ptr = NULL;
  graphene_rect_t src_rect;
  MtkRectangle view_rect;
  float view_scale;
  int untransformed_view_width;
  int untransformed_view_height;

  if (!surface->buffer)
    return NULL;

  if (surface->buffer->use_count == 0)
    return NULL;

  renderer_view = META_RENDERER_VIEW (stage_view);
  view_transform = meta_renderer_view_get_transform (renderer_view);
  if (view_transform != surface->buffer_transform)
    {
      meta_topic (META_DEBUG_RENDER,
                  "Surface can not be scanned out: buffer transform does not "
                  "match renderer-view transform");
      return NULL;
    }

  surface_actor = meta_wayland_surface_get_actor (surface);
  if (!surface_actor ||
      !clutter_actor_get_paint_box (CLUTTER_ACTOR (surface_actor), &actor_box))
    return NULL;

  clutter_stage_view_get_layout (stage_view, &view_rect);
  view_scale = clutter_stage_view_get_scale (stage_view);

  dst_rect = (MtkRectangle) {
    .x = roundf ((actor_box.x1 - view_rect.x) * view_scale),
    .y = roundf ((actor_box.y1 - view_rect.y) * view_scale),
    .width = roundf ((actor_box.x2 - actor_box.x1) * view_scale),
    .height = roundf ((actor_box.y2 - actor_box.y1) * view_scale),
  };

  if (meta_monitor_transform_is_rotated (view_transform))
    {
      untransformed_view_width = view_rect.height;
      untransformed_view_height = view_rect.width;
    }
  else
    {
      untransformed_view_width = view_rect.width;
      untransformed_view_height = view_rect.height;
    }

  meta_rectangle_transform (&dst_rect,
                            view_transform,
                            untransformed_view_width,
                            untransformed_view_height,
                            &dst_rect);

  /* Use an implicit destination rect when possible */
  if (surface->viewport.has_dst_size ||
      dst_rect.x != 0 || dst_rect.y != 0 ||
      dst_rect.width != untransformed_view_width ||
      dst_rect.height != untransformed_view_height)
    dst_rect_ptr = &dst_rect;

  if (surface->viewport.has_src_rect)
    {
      src_rect = surface->viewport.src_rect;
      src_rect_ptr = &src_rect;
    }

  return meta_wayland_buffer_try_acquire_scanout (surface->buffer,
                                                  onscreen,
                                                  src_rect_ptr,
                                                  dst_rect_ptr);
}

MetaCrtc *
meta_wayland_surface_get_scanout_candidate (MetaWaylandSurface *surface)
{
  return surface->scanout_candidate;
}

void
meta_wayland_surface_set_scanout_candidate (MetaWaylandSurface *surface,
                                            MetaCrtc           *crtc)
{
  if (surface->scanout_candidate == crtc)
    return;

  g_set_object (&surface->scanout_candidate, crtc);
  g_object_notify_by_pspec (G_OBJECT (surface),
                            obj_props[PROP_SCANOUT_CANDIDATE]);
}

int
meta_wayland_surface_get_geometry_scale (MetaWaylandSurface *surface)
{
  MetaWaylandActorSurface *actor_surface;

  g_return_val_if_fail (META_IS_WAYLAND_ACTOR_SURFACE (surface->role), 1);

  actor_surface = META_WAYLAND_ACTOR_SURFACE (surface->role);
  return meta_wayland_actor_surface_get_geometry_scale (actor_surface);
}

struct wl_resource *
meta_wayland_surface_get_resource (MetaWaylandSurface *surface)
{
  return surface->resource;
}

MetaWaylandCompositor *
meta_wayland_surface_get_compositor (MetaWaylandSurface *surface)
{
  return surface->compositor;
}

gboolean
meta_wayland_surface_is_xwayland (MetaWaylandSurface *surface)
{
#ifdef HAVE_XWAYLAND
  MetaWaylandCompositor *compositor = surface->compositor;
  MetaXWaylandManager *manager = &compositor->xwayland_manager;

  return surface->resource != NULL &&
         wl_resource_get_client (surface->resource) == manager->client;
#else
  return FALSE;
#endif
}

static void
committed_state_handle_highest_scale_monitor (MetaWaylandSurface *surface)
{
  MetaWaylandSurface *subsurface_surface;
  double scale;

  scale = meta_wayland_surface_get_highest_output_scale (surface);

  meta_wayland_fractional_scale_maybe_send_preferred_scale (surface, scale);

  if (wl_resource_get_version (surface->resource) >=
      WL_SURFACE_PREFERRED_BUFFER_SCALE_SINCE_VERSION)
    {
      int ceiled_scale;
      MetaMonitorTransform transform;

      ceiled_scale = ceil (scale);
      if (ceiled_scale > 0 && ceiled_scale != surface->preferred_scale)
        {
          wl_surface_send_preferred_buffer_scale (surface->resource, ceiled_scale);
          surface->preferred_scale = ceiled_scale;
        }

      transform = meta_wayland_surface_get_output_transform (surface);
      if (transform != surface->preferred_transform)
        {
          wl_surface_send_preferred_buffer_transform (surface->resource, ceiled_scale);
          surface->preferred_transform = transform;
        }
    }

  META_WAYLAND_SURFACE_FOREACH_SUBSURFACE (&surface->committed_state,
                                           subsurface_surface)
    committed_state_handle_highest_scale_monitor (subsurface_surface);
}

static void
applied_state_handle_highest_scale_monitor (MetaWaylandSurface *surface)
{
  MetaWaylandSurface *subsurface_surface;
  MetaSurfaceActor *actor = meta_wayland_surface_get_actor (surface);

  if (actor)
    clutter_actor_notify_transform_invalid (CLUTTER_ACTOR (actor));

  META_WAYLAND_SURFACE_FOREACH_SUBSURFACE (&surface->applied_state,
                                           subsurface_surface)
    applied_state_handle_highest_scale_monitor (subsurface_surface);
}

void
meta_wayland_surface_notify_highest_scale_monitor (MetaWaylandSurface *surface)
{
  applied_state_handle_highest_scale_monitor (surface);
  committed_state_handle_highest_scale_monitor (surface);
}

void
meta_wayland_surface_notify_actor_changed (MetaWaylandSurface *surface)
{
  g_signal_emit (surface, surface_signals[SURFACE_ACTOR_CHANGED], 0);
}
