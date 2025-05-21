/*
 * Wayland Support
 *
 * Copyright (C) 2015 Red Hat, Inc.
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

#include "wayland/meta-wayland-cursor-surface.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-logical-monitor-private.h"
#include "cogl/cogl.h"
#include "core/boxes-private.h"
#include "wayland/meta-cursor-sprite-wayland.h"
#include "wayland/meta-wayland-buffer.h"
#include "wayland/meta-wayland-presentation-time-private.h"
#include "wayland/meta-wayland-private.h"

typedef struct _MetaWaylandCursorSurfacePrivate MetaWaylandCursorSurfacePrivate;

struct _MetaWaylandCursorSurfacePrivate
{
  int hot_x;
  int hot_y;
  MetaCursorSpriteWayland *cursor_sprite;
  MetaCursorRenderer *cursor_renderer;
  MetaWaylandBuffer *buffer;
  struct wl_list frame_callbacks;
  gulong cursor_painted_handler_id;
};

G_DEFINE_TYPE_WITH_PRIVATE (MetaWaylandCursorSurface,
                            meta_wayland_cursor_surface,
                            META_TYPE_WAYLAND_SURFACE_ROLE)

static void
update_cursor_sprite_texture (MetaWaylandCursorSurface *cursor_surface)
{
  MetaWaylandCursorSurfacePrivate *priv =
    meta_wayland_cursor_surface_get_instance_private (cursor_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (META_WAYLAND_SURFACE_ROLE (cursor_surface));
  MetaCursorSprite *cursor_sprite = META_CURSOR_SPRITE (priv->cursor_sprite);
  MetaMultiTexture *texture;

  if (!priv->cursor_renderer)
    return;

  texture = meta_wayland_surface_get_texture (surface);

  if (texture && meta_multi_texture_is_simple (texture))
    {
      float hotspot_scale_x;
      float hotspot_scale_y;

      if (surface->viewport.has_dst_size)
        {
          int dst_width;
          int dst_height;
          int tex_width;
          int tex_height;

          tex_width = meta_multi_texture_get_width (texture);
          tex_height = meta_multi_texture_get_height (texture);
          dst_width = surface->viewport.dst_width;
          dst_height = surface->viewport.dst_height;
          hotspot_scale_x = (float) tex_width / dst_width;
          hotspot_scale_y = (float) tex_height / dst_height;
        }
      else
        {
          hotspot_scale_x = surface->applied_state.scale;
          hotspot_scale_y = surface->applied_state.scale;
        }

      meta_cursor_sprite_set_texture (cursor_sprite,
                                      meta_multi_texture_get_plane (texture, 0),
                                      (int) roundf (priv->hot_x * hotspot_scale_x),
                                      (int) roundf (priv->hot_y * hotspot_scale_y));
    }
  else
    {
      meta_cursor_sprite_set_texture (cursor_sprite, NULL, 0, 0);
    }

  meta_cursor_renderer_force_update (priv->cursor_renderer);
}

static void
meta_wayland_cursor_surface_assigned (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandCursorSurface *cursor_surface =
    META_WAYLAND_CURSOR_SURFACE (surface_role);
  MetaWaylandCursorSurfacePrivate *priv =
    meta_wayland_cursor_surface_get_instance_private (cursor_surface);

  wl_list_insert_list (&priv->frame_callbacks,
                       &surface->unassigned.pending_frame_callback_list);
  wl_list_init (&surface->unassigned.pending_frame_callback_list);
}

static void
meta_wayland_cursor_surface_pre_apply_state (MetaWaylandSurfaceRole  *surface_role,
                                             MetaWaylandSurfaceState *pending)
{
  MetaWaylandCursorSurface *cursor_surface =
    META_WAYLAND_CURSOR_SURFACE (surface_role);
  MetaWaylandCursorSurfacePrivate *priv =
    meta_wayland_cursor_surface_get_instance_private (cursor_surface);

  if (pending->newly_attached && priv->buffer)
    {
      meta_wayland_buffer_dec_use_count (priv->buffer);
      g_clear_object (&priv->buffer);
    }
}

static void
meta_wayland_cursor_schedule_update (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaContext *context =
    meta_wayland_compositor_get_context (surface->compositor);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  MetaLogicalMonitor *logical_monitor;
  MetaMonitor *monitor;
  MetaOutput *output;
  MetaCrtc *crtc;
  MetaRendererView *renderer_view;
  ClutterStageView *stage_view;

  logical_monitor = meta_wayland_surface_get_main_monitor (surface);
  if (!logical_monitor)
    return;

  monitor = meta_logical_monitor_get_monitors (logical_monitor)->data;
  output = meta_monitor_get_main_output (monitor);
  crtc = meta_output_get_assigned_crtc (output);
  renderer_view = meta_renderer_get_view_for_crtc (renderer, crtc);
  stage_view = CLUTTER_STAGE_VIEW (renderer_view);
  clutter_stage_view_schedule_update (stage_view);
}

static void
meta_wayland_cursor_surface_apply_state (MetaWaylandSurfaceRole  *surface_role,
                                         MetaWaylandSurfaceState *pending)
{
  MetaWaylandCursorSurface *cursor_surface =
    META_WAYLAND_CURSOR_SURFACE (surface_role);
  MetaWaylandCursorSurfacePrivate *priv =
    meta_wayland_cursor_surface_get_instance_private (cursor_surface);

  if (pending->buffer)
    {
      priv->buffer = g_object_ref (pending->buffer);
      meta_wayland_buffer_inc_use_count (priv->buffer);
    }

  if (!wl_list_empty (&pending->frame_callback_list))
    {
      gboolean was_empty = wl_list_empty (&priv->frame_callbacks);

      wl_list_insert_list (&priv->frame_callbacks,
                           &pending->frame_callback_list);
      wl_list_init (&pending->frame_callback_list);

      if (was_empty)
        meta_wayland_cursor_schedule_update (surface_role);
    }

  if (pending->newly_attached &&
      ((!mtk_region_is_empty (pending->surface_damage) ||
        !mtk_region_is_empty (pending->buffer_damage)) ||
       !priv->buffer))
    update_cursor_sprite_texture (META_WAYLAND_CURSOR_SURFACE (surface_role));

  meta_wayland_cursor_surface_set_hotspot (META_WAYLAND_CURSOR_SURFACE (surface_role),
                                           priv->hot_x - pending->dx,
                                           priv->hot_y - pending->dy);
}

static gboolean
meta_wayland_cursor_surface_is_on_logical_monitor (MetaWaylandSurfaceRole *role,
                                                   MetaLogicalMonitor     *logical_monitor)
{
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (role);
  MetaWaylandCursorSurface *cursor_surface =
    META_WAYLAND_CURSOR_SURFACE (surface->role);
  MetaWaylandCursorSurfacePrivate *priv =
    meta_wayland_cursor_surface_get_instance_private (cursor_surface);
  MetaContext *context =
    meta_wayland_compositor_get_context (surface->compositor);
  MetaBackend *backend = meta_context_get_backend (context);
  ClutterBackend *clutter_backend =
    meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);
  ClutterSprite *sprite;
  graphene_point_t point;
  graphene_rect_t logical_monitor_rect;

  if (!priv->cursor_renderer)
    return FALSE;

  logical_monitor_rect =
    mtk_rectangle_to_graphene_rect (&logical_monitor->rect);

  sprite = meta_cursor_renderer_get_sprite (priv->cursor_renderer);
  clutter_seat_query_state (seat, sprite, &point, NULL);

  return graphene_rect_contains_point (&logical_monitor_rect, &point);
}

static MetaLogicalMonitor *
meta_wayland_cursor_surface_get_preferred_scale_monitor (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandCursorSurface *cursor_surface =
    META_WAYLAND_CURSOR_SURFACE (surface_role);
  MetaWaylandCursorSurfacePrivate *priv =
    meta_wayland_cursor_surface_get_instance_private (cursor_surface);
  MetaWaylandSurface *surface = meta_wayland_surface_role_get_surface (surface_role);
  MetaContext *context =
    meta_wayland_compositor_get_context (surface->compositor);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  ClutterBackend *clutter_backend =
    meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);
  ClutterSprite *sprite;
  graphene_point_t point;

  if (!priv->cursor_renderer)
    return FALSE;

  sprite = meta_cursor_renderer_get_sprite (priv->cursor_renderer);
  clutter_seat_query_state (seat, sprite, &point, NULL);

  return meta_monitor_manager_get_logical_monitor_at (monitor_manager,
                                                      point.x,
                                                      point.y);
}

static void
meta_wayland_cursor_surface_dispose (GObject *object)
{
  MetaWaylandCursorSurface *cursor_surface =
    META_WAYLAND_CURSOR_SURFACE (object);
  MetaWaylandCursorSurfacePrivate *priv =
    meta_wayland_cursor_surface_get_instance_private (cursor_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (META_WAYLAND_SURFACE_ROLE (object));
  MetaWaylandSeat *seat = surface->compositor->seat;
  MetaWaylandPointer *pointer = seat->pointer;
  MetaWaylandFrameCallback *cb, *next;

  wl_list_for_each_safe (cb, next, &priv->frame_callbacks, link)
    wl_resource_destroy (cb->resource);

  g_clear_object (&priv->cursor_renderer);
  g_clear_object (&priv->cursor_sprite);

  if (priv->buffer)
    {
      meta_wayland_buffer_dec_use_count (priv->buffer);
      g_clear_object (&priv->buffer);
    }

  meta_wayland_pointer_update_cursor_surface (pointer);

  G_OBJECT_CLASS (meta_wayland_cursor_surface_parent_class)->dispose (object);
}

static void
meta_wayland_cursor_surface_constructed (GObject *object)
{
  MetaWaylandCursorSurface *cursor_surface =
    META_WAYLAND_CURSOR_SURFACE (object);
  MetaWaylandCursorSurfacePrivate *priv =
    meta_wayland_cursor_surface_get_instance_private (cursor_surface);
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (cursor_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandCompositor *compositor = surface->compositor;
  MetaBackend *backend = meta_context_get_backend (compositor->context);
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);
  MetaWaylandBuffer *buffer;

  buffer = meta_wayland_surface_get_buffer (surface);

  g_warn_if_fail (!buffer || buffer->resource);

  if (buffer && buffer->resource)
    {
      priv->buffer = g_object_ref (surface->buffer);
      meta_wayland_buffer_inc_use_count (priv->buffer);
    }

  priv->cursor_sprite = meta_cursor_sprite_wayland_new (surface,
                                                        cursor_tracker);
}

static void
meta_wayland_cursor_surface_init (MetaWaylandCursorSurface *role)
{
  MetaWaylandCursorSurfacePrivate *priv =
    meta_wayland_cursor_surface_get_instance_private (role);

  wl_list_init (&priv->frame_callbacks);
}

static void
meta_wayland_cursor_surface_class_init (MetaWaylandCursorSurfaceClass *klass)
{
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  surface_role_class->assigned = meta_wayland_cursor_surface_assigned;
  surface_role_class->pre_apply_state =
    meta_wayland_cursor_surface_pre_apply_state;
  surface_role_class->apply_state = meta_wayland_cursor_surface_apply_state;
  surface_role_class->is_on_logical_monitor =
    meta_wayland_cursor_surface_is_on_logical_monitor;
  surface_role_class->get_preferred_scale_monitor =
    meta_wayland_cursor_surface_get_preferred_scale_monitor;

  object_class->constructed = meta_wayland_cursor_surface_constructed;
  object_class->dispose = meta_wayland_cursor_surface_dispose;
}

MetaCursorSprite *
meta_wayland_cursor_surface_get_sprite (MetaWaylandCursorSurface *cursor_surface)
{
  MetaWaylandCursorSurfacePrivate *priv =
    meta_wayland_cursor_surface_get_instance_private (cursor_surface);

  return META_CURSOR_SPRITE (priv->cursor_sprite);
}

void
meta_wayland_cursor_surface_set_hotspot (MetaWaylandCursorSurface *cursor_surface,
                                         int                       hotspot_x,
                                         int                       hotspot_y)
{
  MetaWaylandCursorSurfacePrivate *priv =
    meta_wayland_cursor_surface_get_instance_private (cursor_surface);

  if (priv->hot_x == hotspot_x &&
      priv->hot_y == hotspot_y)
    return;

  priv->hot_x = hotspot_x;
  priv->hot_y = hotspot_y;
  update_cursor_sprite_texture (cursor_surface);
}

void
meta_wayland_cursor_surface_get_hotspot (MetaWaylandCursorSurface *cursor_surface,
                                         int                      *hotspot_x,
                                         int                      *hotspot_y)
{
  MetaWaylandCursorSurfacePrivate *priv =
    meta_wayland_cursor_surface_get_instance_private (cursor_surface);

  if (hotspot_x)
    *hotspot_x = priv->hot_x;
  if (hotspot_y)
    *hotspot_y = priv->hot_y;
}

static void
on_cursor_painted (MetaCursorRenderer       *renderer,
                   MetaCursorSprite         *displayed_sprite,
                   ClutterStageView         *stage_view,
                   int64_t                   view_frame_counter,
                   MetaWaylandCursorSurface *cursor_surface)
{
  MetaWaylandCursorSurfacePrivate *priv =
    meta_wayland_cursor_surface_get_instance_private (cursor_surface);
  guint32 time = (guint32) (g_get_monotonic_time () / 1000);
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (cursor_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaContext *context =
    meta_wayland_compositor_get_context (surface->compositor);
  MetaWaylandCompositor *compositor =
    meta_context_get_wayland_compositor (context);

  if (displayed_sprite != META_CURSOR_SPRITE (priv->cursor_sprite))
    return;

  while (!wl_list_empty (&priv->frame_callbacks))
    {
      MetaWaylandFrameCallback *callback =
        wl_container_of (priv->frame_callbacks.next, callback, link);

      wl_callback_send_done (callback->resource, time);
      wl_resource_destroy (callback->resource);
    }

  meta_wayland_presentation_time_cursor_painted (&compositor->presentation_time,
                                                 stage_view,
                                                 view_frame_counter,
                                                 cursor_surface);
}

void
meta_wayland_cursor_surface_set_renderer (MetaWaylandCursorSurface *cursor_surface,
                                          MetaCursorRenderer       *renderer)
{
  MetaWaylandCursorSurfacePrivate *priv =
    meta_wayland_cursor_surface_get_instance_private (cursor_surface);
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (cursor_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);

  if (priv->cursor_renderer == renderer)
    return;

  if (priv->cursor_renderer)
    {
      g_clear_signal_handler (&priv->cursor_painted_handler_id,
                              priv->cursor_renderer);
      g_object_unref (priv->cursor_renderer);
    }
  if (renderer)
    {
      priv->cursor_painted_handler_id =
        g_signal_connect_object (renderer, "cursor-painted",
                                 G_CALLBACK (on_cursor_painted), cursor_surface, 0);
      g_object_ref (renderer);
    }

  priv->cursor_renderer = renderer;
  update_cursor_sprite_texture (cursor_surface);
  meta_wayland_surface_notify_preferred_scale_monitor (surface);
}

MetaCursorRenderer *
meta_wayland_cursor_surface_get_renderer (MetaWaylandCursorSurface *cursor_surface)
{
  MetaWaylandCursorSurfacePrivate *priv =
    meta_wayland_cursor_surface_get_instance_private (cursor_surface);

  return priv->cursor_renderer;
}
