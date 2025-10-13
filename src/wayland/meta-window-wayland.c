/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
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
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include "config.h"

#include "wayland/meta-window-wayland.h"

#include <errno.h>
#include <string.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-backend-private.h"
#include "backends/meta-logical-monitor-private.h"
#include "compositor/compositor-private.h"
#include "compositor/meta-surface-actor-wayland.h"
#include "compositor/meta-window-actor-private.h"
#include "core/boxes-private.h"
#include "core/stack-tracker.h"
#include "core/window-private.h"
#include "wayland/meta-wayland-actor-surface.h"
#include "wayland/meta-wayland-client-private.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-surface-private.h"
#include "wayland/meta-wayland-toplevel-drag.h"
#include "wayland/meta-wayland-transaction.h"
#include "wayland/meta-wayland-window-configuration.h"
#include "wayland/meta-wayland-xdg-shell.h"

enum
{
  PROP_0,

  PROP_SURFACE,
  PROP_CLIENT,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

typedef struct _UnmaximizeDrag
{
  uint32_t serial;
} UnmaximizeDrag;

struct _MetaWindowWayland
{
  MetaWindow parent;

  int geometry_scale;

  MetaWaylandClient *client;
  MetaWaylandSurface *surface;

  GList *pending_configurations;
  gboolean has_pending_state_change;

  MetaWaylandWindowConfiguration *last_sent_configuration;
  MetaWaylandWindowConfiguration *last_acked_configuration;

  gboolean has_been_shown;

  gboolean is_suspended;
};

struct _MetaWindowWaylandClass
{
  MetaWindowClass parent_class;
};

G_DEFINE_TYPE (MetaWindowWayland, meta_window_wayland, META_TYPE_WINDOW)

static GQuark unmaximize_drag_quark;

static gboolean meta_window_wayland_get_oldest_pending_serial (MetaWindowWayland *wl_window,
                                                               uint32_t          *serial);

static void
set_geometry_scale_for_window (MetaWindowWayland *wl_window,
                               int                geometry_scale)
{
  MetaWindowActor *window_actor;

  wl_window->geometry_scale = geometry_scale;

  window_actor = meta_window_actor_from_window (META_WINDOW (wl_window));
  if (window_actor)
    meta_window_actor_set_geometry_scale (window_actor, geometry_scale);
}

static int
get_window_geometry_scale_for_logical_monitor (MetaLogicalMonitor *logical_monitor)
{
  MetaMonitorManager *monitor_manager =
    meta_logical_monitor_get_monitor_manager (logical_monitor);
  MetaBackend *backend = meta_monitor_manager_get_backend (monitor_manager);

  if (meta_backend_is_stage_views_scaled (backend))
    return 1;
  else
    return (int) meta_logical_monitor_get_scale (logical_monitor);
}

static void
meta_window_wayland_manage (MetaWindow *window)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);
  MetaDisplay *display = window->display;

  wl_window->geometry_scale = meta_window_wayland_get_geometry_scale (window);

  meta_display_register_wayland_window (display, window);

  {
    meta_stack_tracker_record_add (window->display->stack_tracker,
                                   window->stamp,
                                   0);
  }

  meta_wayland_surface_window_managed (wl_window->surface, window);
}

static void
meta_window_wayland_unmanage (MetaWindow *window)
{
  {
    meta_stack_tracker_record_remove (window->display->stack_tracker,
                                      window->stamp,
                                      0);
  }

  meta_display_unregister_wayland_window (window->display, window);
}

static void
meta_window_wayland_ping (MetaWindow *window,
                          guint32     serial)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);

  meta_wayland_surface_ping (wl_window->surface, serial);
}

static void
meta_window_wayland_delete (MetaWindow *window,
                            guint32     timestamp)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);

  meta_wayland_surface_delete (wl_window->surface);
}

static void
meta_window_wayland_kill (MetaWindow *window)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);
  MetaWaylandSurface *surface;
  struct wl_client *client;

  /* Send the client an unrecoverable error to kill the client. */

  surface = meta_window_get_wayland_surface (window);
  if (surface->resource)
    {
      wl_resource_post_error (surface->resource,
                              WL_DISPLAY_ERROR_NO_MEMORY,
                              "User requested that we kill you. Sorry. "
                              "Don't take it too personally.");
      return;
    }

  client = meta_wayland_client_get_wl_client (wl_window->client);
  if (client)
    wl_client_post_no_memory (client);
}

static void
meta_window_wayland_focus (MetaWindow *window,
                           guint32     timestamp)
{
  if (meta_window_is_focusable (window))
    {
      meta_display_set_input_focus (window->display,
                                    window,
                                    timestamp);
    }
}

void
meta_window_wayland_configure (MetaWindowWayland              *wl_window,
                               MetaWaylandWindowConfiguration *configuration)
{
  meta_wayland_surface_configure_notify (wl_window->surface, configuration);

  wl_window->pending_configurations =
    g_list_prepend (wl_window->pending_configurations,
                    meta_wayland_window_configuration_ref (configuration));

  g_clear_pointer (&wl_window->last_sent_configuration,
                   meta_wayland_window_configuration_unref);
  wl_window->last_sent_configuration =
    meta_wayland_window_configuration_ref (configuration);
}

static gboolean
is_drag_resizing_window (MetaWindowDrag *window_drag,
                         MetaWindow     *window)
{
  return (window_drag &&
          meta_grab_op_is_resizing (meta_window_drag_get_grab_op (window_drag)) &&
          (meta_window_drag_get_window (window_drag) == window ||
           meta_window_drag_get_window (window_drag) ==
           meta_window_config_get_tile_match (window->config)));
}

static void
surface_state_changed (MetaWindow *window)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);
  MetaWaylandWindowConfiguration *last_sent_configuration =
    wl_window->last_sent_configuration;
  MetaWaylandWindowConfiguration *last_acked_configuration;
  g_autoptr (MetaWaylandWindowConfiguration) configuration = NULL;
  gboolean is_configuration_up_to_date;
  MetaWindowDrag *window_drag;

  /* don't send notify when the window is being unmanaged */
  if (window->unmanaging)
    return;

  g_return_if_fail (last_sent_configuration);

  configuration =
    meta_wayland_window_configuration_new_from_other (last_sent_configuration);
  configuration->flags = META_MOVE_RESIZE_STATE_CHANGED;
  configuration->is_suspended = wl_window->is_suspended;

  last_acked_configuration = wl_window->last_acked_configuration;

  is_configuration_up_to_date =
    last_acked_configuration &&
    last_acked_configuration->serial == last_sent_configuration->serial;

  if (is_configuration_up_to_date &&
      last_sent_configuration->config &&
      meta_window_config_is_floating (last_sent_configuration->config))
    {
      configuration->has_position = FALSE;
      configuration->x = 0;
      configuration->y = 0;
    }

  window_drag =
    meta_compositor_get_current_window_drag (window->display->compositor);
  if (is_drag_resizing_window (window_drag, window))
    {
      configuration->has_size = TRUE;
      meta_window_drag_calculate_window_size (window_drag,
                                              &configuration->width,
                                              &configuration->height);
    }
  else if (is_configuration_up_to_date &&
           last_sent_configuration->config &&
           meta_window_config_is_floating (last_sent_configuration->config))
    {
      MtkRectangle frame_rect = meta_window_config_get_rect (window->config);

      configuration->has_size = TRUE;
      configuration->width = frame_rect.width;
      configuration->height = frame_rect.height;
    }

  meta_window_wayland_configure (wl_window, configuration);
}

static void
meta_window_wayland_grab_op_began (MetaWindow *window,
                                   MetaGrabOp  op)
{
  if (meta_grab_op_is_resizing (op))
    surface_state_changed (window);

  META_WINDOW_CLASS (meta_window_wayland_parent_class)->grab_op_began (window, op);
}

static void
meta_window_wayland_grab_op_ended (MetaWindow *window,
                                   MetaGrabOp  op)
{
  if (meta_grab_op_is_resizing (op))
    surface_state_changed (window);

  META_WINDOW_CLASS (meta_window_wayland_parent_class)->grab_op_ended (window, op);
}

static gboolean
should_configure (MetaWindow          *window,
                  MtkRectangle         constrained_rect,
                  MetaMoveResizeFlags  flags)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);
  MetaWaylandWindowConfiguration *last_sent_configuration =
    wl_window->last_sent_configuration;
  MtkRectangle frame_rect;

  frame_rect = meta_window_config_get_rect (window->config);

  /* Initial configuration, always need to configure. */
  if (!last_sent_configuration)
    return TRUE;

  /* The constrained size changed from last time, also explicit, thus need to
   * configure the new size. */
  if (last_sent_configuration->has_size &&
      flags & META_MOVE_RESIZE_RESIZE_ACTION &&
      (constrained_rect.width != last_sent_configuration->width ||
       constrained_rect.height != last_sent_configuration->height))
    return TRUE;

  /* Something wants to resize our mapped window. */
  if (meta_wayland_surface_get_buffer (wl_window->surface) &&
      (constrained_rect.width != frame_rect.width ||
       constrained_rect.height != frame_rect.height))
    return TRUE;

  /* The state was changed, or the change was explicitly marked as a configure
   * request. */
  if (flags & META_MOVE_RESIZE_STATE_CHANGED ||
      flags & META_MOVE_RESIZE_WAYLAND_FORCE_CONFIGURE)
    return TRUE;

  return FALSE;
}

static gboolean
maybe_update_pending_configuration_from_drag (MetaWindowWayland  *wl_window,
                                              const MtkRectangle *constrained_rect)
{
  MetaWindow *window = META_WINDOW (wl_window);
  MetaDisplay *display = window->display;
  MetaWindowDrag *window_drag;
  UnmaximizeDrag *unmaximize_drag;
  MetaWaylandWindowConfiguration *unmaximize_configuration;

  window_drag =
    meta_compositor_get_current_window_drag (display->compositor);
  if (!window_drag)
    return FALSE;

  unmaximize_drag = g_object_get_qdata (G_OBJECT (window_drag),
                                        unmaximize_drag_quark);
  if (!unmaximize_drag)
    return FALSE;

  unmaximize_configuration =
    meta_window_wayland_peek_configuration (wl_window,
                                            unmaximize_drag->serial);
  if (!unmaximize_configuration)
    return FALSE;

  unmaximize_configuration->has_position = TRUE;
  unmaximize_configuration->x = constrained_rect->x;
  unmaximize_configuration->y = constrained_rect->y;

  return TRUE;
}

static void
meta_window_wayland_move_resize_internal (MetaWindow                *window,
                                          MtkRectangle               unconstrained_rect,
                                          MtkRectangle               constrained_rect,
                                          MtkRectangle               temporary_rect,
                                          int                        rel_x,
                                          int                        rel_y,
                                          MetaMoveResizeFlags        flags,
                                          MetaMoveResizeResultFlags *result)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);
  MetaDisplay *display = window->display;
  MetaWaylandWindowConfiguration *last_sent_configuration =
    wl_window->last_sent_configuration;
  gboolean can_move_now = FALSE;
  MtkRectangle configured_rect;
  MtkRectangle frame_rect;
  MetaGravity gravity;
  int geometry_scale;
  int new_x;
  int new_y;
  int new_buffer_x;
  int new_buffer_y;
  MetaWaylandWindowConfiguration *last_acked_configuration;

  /* don't do anything if we're dropping the window, see #751847 */
  if (window->unmanaging)
    return;

  gravity = meta_window_get_gravity (window);

  configured_rect.x = constrained_rect.x;
  configured_rect.y = constrained_rect.y;

  /* The scale the window is drawn in might change depending on what monitor it
   * is mainly on. Scale the configured rectangle to be in logical pixel
   * coordinate space so that we can have a scale independent size to pass
   * to the Wayland surface. */
  geometry_scale = meta_window_wayland_get_geometry_scale (window);
  frame_rect = meta_window_config_get_rect (window->config);

  configured_rect.width = constrained_rect.width;
  configured_rect.height = constrained_rect.height;

  /* The size is determined by the client, except when the client is explicitly
   * fullscreen, in which case the compositor compensates for the size
   * difference between what surface configuration the client provided, and the
   * size of the area a fullscreen window state is expected to fill.
   *
   * For non-explicit-fullscreen states, since the size is always determined by
   * the client, the we cannot use the size calculated by the constraints.
   */

  if (flags & META_MOVE_RESIZE_FORCE_MOVE)
    {
      can_move_now = TRUE;
    }
  else if (flags & META_MOVE_RESIZE_WAYLAND_FINISH_MOVE_RESIZE)
    {
      MetaWaylandWindowConfiguration *configuration;
      int new_width, new_height;

      configuration = wl_window->last_acked_configuration;
      if (configuration &&
          configuration->config &&
          meta_window_config_get_is_fullscreen (configuration->config))
        {
          new_width = constrained_rect.width;
          new_height = constrained_rect.height;
        }
      else
        {
          new_width = unconstrained_rect.width;
          new_height = unconstrained_rect.height;
        }
      if (frame_rect.width != new_width ||
          frame_rect.height != new_height)
        {
          *result |= META_MOVE_RESIZE_RESULT_RESIZED;
          meta_window_config_set_size (window->config,
                                       new_width, new_height);
        }

      frame_rect = meta_window_config_get_rect (window->config);
      window->buffer_rect.width =
        frame_rect.width +
        window->custom_frame_extents.left +
        window->custom_frame_extents.right;
      window->buffer_rect.height =
        frame_rect.height +
        window->custom_frame_extents.top +
        window->custom_frame_extents.bottom;

      /* This is a commit of an attach. We should move the window to match the
       * new position the client wants. */
      can_move_now = TRUE;
      if (window->placement.state == META_PLACEMENT_STATE_CONSTRAINED_CONFIGURED)
        window->placement.state = META_PLACEMENT_STATE_CONSTRAINED_FINISHED;
    }
  else
    {
      if (window->placement.rule)
        {
          switch (window->placement.state)
            {
            case META_PLACEMENT_STATE_UNCONSTRAINED:
            case META_PLACEMENT_STATE_CONSTRAINED_CONFIGURED:
            case META_PLACEMENT_STATE_INVALIDATED:
              can_move_now = FALSE;
              break;
            case META_PLACEMENT_STATE_CONSTRAINED_PENDING:
              {
                if (flags & META_MOVE_RESIZE_PLACEMENT_CHANGED ||
                    flags & META_MOVE_RESIZE_WAYLAND_FORCE_CONFIGURE ||
                    !last_sent_configuration ||
                    rel_x != last_sent_configuration->rel_x ||
                    rel_y != last_sent_configuration->rel_y ||
                    constrained_rect.width != frame_rect.width ||
                    constrained_rect.height != frame_rect.height)
                  {
                    g_autoptr (MetaWaylandWindowConfiguration) configuration = NULL;

                    configuration =
                      meta_wayland_window_configuration_new_relative (window,
                                                                      rel_x,
                                                                      rel_y,
                                                                      configured_rect.width,
                                                                      configured_rect.height,
                                                                      geometry_scale);
                    if (flags & META_MOVE_RESIZE_WAYLAND_FORCE_CONFIGURE ||
                        !meta_wayland_window_configuration_is_equivalent (
                          configuration,
                          wl_window->last_sent_configuration))
                      {
                        meta_window_wayland_configure (wl_window,
                                                       configuration);

                        window->placement.state =
                          META_PLACEMENT_STATE_CONSTRAINED_CONFIGURED;

                        can_move_now = FALSE;
                      }
                  }
                else
                  {
                    window->placement.state =
                      META_PLACEMENT_STATE_CONSTRAINED_FINISHED;

                    can_move_now = TRUE;
                  }
                break;
              }
            case META_PLACEMENT_STATE_CONSTRAINED_FINISHED:
              can_move_now = TRUE;
              break;
            }
        }
      else if (should_configure (window, constrained_rect, flags))
        {
          g_autoptr (MetaWaylandWindowConfiguration) configuration = NULL;
          int bounds_width;
          int bounds_height;

          if (!meta_window_calculate_bounds (window,
                                             &bounds_width,
                                             &bounds_height))
            {
              bounds_width = 0;
              bounds_height = 0;
            }

          configuration =
            meta_wayland_window_configuration_new (window,
                                                   configured_rect,
                                                   bounds_width, bounds_height,
                                                   geometry_scale,
                                                   flags,
                                                   gravity);
          if (flags & META_MOVE_RESIZE_WAYLAND_FORCE_CONFIGURE ||
              !meta_wayland_window_configuration_is_equivalent (
                configuration,
                wl_window->last_sent_configuration))
            {
              MetaWindowDrag *window_drag;

              window_drag =
                meta_compositor_get_current_window_drag (display->compositor);
              if (window_drag &&
                  meta_window_drag_get_window (window_drag) == window &&
                  meta_grab_op_is_moving (meta_window_drag_get_grab_op (window_drag)) &&
                  meta_window_config_is_floating (window->config) &&
                  flags & META_MOVE_RESIZE_UNMAXIMIZE)
                {
                  UnmaximizeDrag *unmaximize_drag;

                  unmaximize_drag = g_new0 (UnmaximizeDrag, 1);
                  g_object_set_qdata_full (G_OBJECT (window_drag),
                                           unmaximize_drag_quark,
                                           unmaximize_drag,
                                           g_free);
                  unmaximize_drag->serial = configuration->serial;
                  g_set_object (&configuration->window_drag, window_drag);
                }

              meta_window_wayland_configure (wl_window, configuration);
              can_move_now = FALSE;
            }
        }
      else
        {
          if (!maybe_update_pending_configuration_from_drag (wl_window,
                                                             &constrained_rect))
            can_move_now = TRUE;
        }
    }

  if (can_move_now)
    {
      new_x = constrained_rect.x;
      new_y = constrained_rect.y;
    }
  else
    {
      new_x = temporary_rect.x;
      new_y = temporary_rect.y;

      wl_window->has_pending_state_change |=
        !!(flags & META_MOVE_RESIZE_STATE_CHANGED);
    }

  if (new_x != frame_rect.x || new_y != frame_rect.y)
    {
      *result |= META_MOVE_RESIZE_RESULT_MOVED;
      meta_window_config_set_position (window->config, new_x, new_y);
    }

  if (window->placement.rule &&
      window->placement.state == META_PLACEMENT_STATE_CONSTRAINED_FINISHED)
    {
      window->placement.current.rel_x = rel_x;
      window->placement.current.rel_y = rel_y;
    }

  new_buffer_x = new_x - window->custom_frame_extents.left;
  new_buffer_y = new_y - window->custom_frame_extents.top;

  if (new_buffer_x != window->buffer_rect.x ||
      new_buffer_y != window->buffer_rect.y)
    {
      *result |= META_MOVE_RESIZE_RESULT_MOVED;
      window->buffer_rect.x = new_buffer_x;
      window->buffer_rect.y = new_buffer_y;
    }

  if (can_move_now &&
      flags & META_MOVE_RESIZE_WAYLAND_STATE_CHANGED)
    *result |= META_MOVE_RESIZE_RESULT_STATE_CHANGED;

  last_acked_configuration = wl_window->last_acked_configuration;
  if ((last_acked_configuration &&
       last_acked_configuration->config &&
       meta_window_config_is_floating (last_acked_configuration->config)) ||
      (can_move_now &&
       !(flags & META_MOVE_RESIZE_WAYLAND_FINISH_MOVE_RESIZE)))
    *result |= META_MOVE_RESIZE_RESULT_UPDATE_UNCONSTRAINED;
}

static void
scale_size (int  *width,
            int  *height,
            float scale)
{
  if (*width < G_MAXINT)
    {
      float new_width;

      new_width = (*width * scale);
      if (new_width > G_MAXINT)
        *width = G_MAXINT;
      else
        *width = (int) new_width;
    }

  if (*height < G_MAXINT)
    {
      float new_height;

      new_height = (*height * scale);
      if (new_height > G_MAXINT)
        *height = G_MAXINT;
      else
        *height = (int) new_height;
    }
}

static void
scale_rect_size (MtkRectangle  *rect,
                 float          scale)
{
  scale_size (&rect->width, &rect->height, scale);
}

static void
meta_window_wayland_update_main_monitor (MetaWindow                   *window,
                                         MetaWindowUpdateMonitorFlags  flags)
{
  MetaDisplay *display = meta_window_get_display (window);
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);
  MetaWindow *toplevel_window;
  MetaLogicalMonitor *from;
  MetaLogicalMonitor *to;
  MetaLogicalMonitor *scaled_new;
  float from_scale, to_scale;
  float scale;
  MtkRectangle frame_rect;

  from = window->monitor;

  /* If the window is not a toplevel window (i.e. it's a popup window) just use
   * the monitor of the toplevel. */
  toplevel_window = meta_wayland_surface_get_toplevel_window (wl_window->surface);
  if (toplevel_window != window)
    {
      meta_window_update_monitor (toplevel_window, flags);
      g_set_object (&window->monitor, toplevel_window->monitor);
      return;
    }

  frame_rect = meta_window_config_get_rect (window->config);
  if (frame_rect.width == 0 || frame_rect.height == 0)
    {
      g_set_object (&window->monitor, meta_window_find_monitor_from_id (window));
      return;
    }

  /* Require both the current and the new monitor would be the new main monitor,
   * even given the resulting scale the window would end up having. This is
   * needed to avoid jumping back and forth between the new and the old, since
   * changing main monitor may cause the window to be resized so that it no
   * longer have that same new main monitor. */
  to = meta_window_find_monitor_from_frame_rect (window);

  if (from == to)
    return;

  if (from == NULL || to == NULL)
    {
      g_set_object (&window->monitor, to);
      return;
    }

  if (flags & META_WINDOW_UPDATE_MONITOR_FLAGS_FORCE)
    {
      g_set_object (&window->monitor, to);
      return;
    }

  from_scale = meta_logical_monitor_get_scale (from);
  to_scale = meta_logical_monitor_get_scale (to);

  if (from_scale == to_scale)
    {
      g_set_object (&window->monitor, to);
      return;
    }

  if (meta_backend_is_stage_views_scaled (backend))
    {
      g_set_object (&window->monitor, to);
      return;
    }

  /* To avoid a window alternating between two main monitors because scaling
   * changes the main monitor, wait until both the current and the new scale
   * will result in the same main monitor. */
  scale = to_scale / from_scale;
  scale_rect_size (&frame_rect, scale);
  scaled_new =
    meta_monitor_manager_get_logical_monitor_from_rect (monitor_manager, &frame_rect);
  if (to != scaled_new)
    return;

  g_set_object (&window->monitor, to);
}

static void
meta_window_wayland_main_monitor_changed (MetaWindow               *window,
                                          const MetaLogicalMonitor *old)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);
  int old_geometry_scale = wl_window->geometry_scale;
  int geometry_scale;
  float scale_factor;
  MetaWaylandSurface *surface;
  MtkRectangle frame_rect;

  if (!window->monitor)
    return;

  geometry_scale = meta_window_wayland_get_geometry_scale (window);

  /* This function makes sure that window geometry, window actor geometry and
   * surface actor geometry gets set according the old and current main monitor
   * scale. If there either is no past or current main monitor, or if the scale
   * didn't change, there is nothing to do. */
  if (old == NULL ||
      window->monitor == NULL ||
      old_geometry_scale == geometry_scale)
    return;

  /* MetaWindow keeps its rectangles in the physical pixel coordinate space.
   * When the main monitor of a window changes, it can cause the corresponding
   * window surfaces to be scaled given the monitor scale, so we need to scale
   * the rectangles in MetaWindow accordingly. */

  scale_factor = (float) geometry_scale / old_geometry_scale;

  /* Window size. */
  frame_rect = meta_window_config_get_rect (window->config);
  scale_rect_size (&frame_rect, scale_factor);
  scale_rect_size (&window->unconstrained_rect, scale_factor);
  scale_rect_size (&window->saved_rect, scale_factor);
  scale_size (&window->size_hints.min_width, &window->size_hints.min_height, scale_factor);
  scale_size (&window->size_hints.max_width, &window->size_hints.max_height, scale_factor);

  /* Window geometry offset (XXX: Need a better place, see
   * meta_window_wayland_finish_move_resize). */
  window->custom_frame_extents.left =
    (int)(scale_factor * window->custom_frame_extents.left);
  window->custom_frame_extents.top =
    (int)(scale_factor * window->custom_frame_extents.top);
  window->custom_frame_extents.right =
    (int)(scale_factor * window->custom_frame_extents.right);
  window->custom_frame_extents.bottom =
    (int)(scale_factor * window->custom_frame_extents.bottom);

  /* Buffer rect. */
  scale_rect_size (&window->buffer_rect, scale_factor);
  window->buffer_rect.x =
    frame_rect.x - window->custom_frame_extents.left;
  window->buffer_rect.y =
    frame_rect.y - window->custom_frame_extents.top;

  meta_compositor_sync_window_geometry (window->display->compositor,
                                        window,
                                        TRUE);

  surface = wl_window->surface;
  if (surface)
    {
      MetaWaylandActorSurface *actor_surface =
        META_WAYLAND_ACTOR_SURFACE (surface->role);

      meta_wayland_actor_surface_sync_actor_state (actor_surface);
    }

  set_geometry_scale_for_window (wl_window, geometry_scale);
  meta_window_emit_size_changed (window);
}

static pid_t
meta_window_wayland_get_client_pid (MetaWindow *window)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);

  return meta_wayland_client_get_pid (wl_window->client);
}

static void
appears_focused_changed (GObject    *object,
                         GParamSpec *pspec,
                         gpointer    user_data)
{
  MetaWindow *window = META_WINDOW (object);

  if (window->placement.rule)
    return;

  surface_state_changed (window);
}

static void
suspend_state_changed (GObject    *object,
                       GParamSpec *pspec,
                       gpointer    user_data)
{
  MetaWindow *window = META_WINDOW (object);
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);
  gboolean is_suspended;

  is_suspended = meta_window_is_suspended (window);
  if (wl_window->is_suspended == is_suspended)
    return;

  wl_window->is_suspended = is_suspended;
  surface_state_changed (window);
}

static void
on_window_shown (MetaWindow *window)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);
  gboolean has_been_shown;

  has_been_shown = wl_window->has_been_shown;
  wl_window->has_been_shown = TRUE;

  if (!has_been_shown)
    meta_compositor_sync_updates_frozen (window->display->compositor, window);
}

static MetaWaylandToplevelDrag *
get_toplevel_drag (MetaWindow *window)
{
  MetaWaylandToplevelDrag *toplevel_drag;
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);
  MetaDisplay *display = meta_window_get_display (window);
  MetaContext *context = meta_display_get_context (display);
  MetaWaylandCompositor *compositor = meta_context_get_wayland_compositor (context);

  toplevel_drag = meta_wayland_data_device_get_toplevel_drag (&compositor->seat->data_device);
  if (!toplevel_drag || toplevel_drag->dragged_surface != wl_window->surface)
    return NULL;
  return toplevel_drag;
}

static void
meta_window_wayland_init (MetaWindowWayland *wl_window)
{
  MetaWindow *window = META_WINDOW (wl_window);

  wl_window->geometry_scale = 1;

  g_signal_connect (window, "notify::appears-focused",
                    G_CALLBACK (appears_focused_changed), NULL);
  g_signal_connect (window, "notify::suspend-state",
                    G_CALLBACK (suspend_state_changed), NULL);
  g_signal_connect (window, "shown",
                    G_CALLBACK (on_window_shown), NULL);
}

static void
meta_window_wayland_force_restore_shortcuts (MetaWindow         *window,
                                             ClutterInputDevice *source)
{
  MetaDisplay *display = meta_window_get_display (window);
  MetaContext *context = meta_display_get_context (display);
  MetaWaylandCompositor *compositor =
    meta_context_get_wayland_compositor (context);

  meta_wayland_compositor_restore_shortcuts (compositor, source);
}

static gboolean
meta_window_wayland_shortcuts_inhibited (MetaWindow         *window,
                                         ClutterInputDevice *source)
{
  MetaDisplay *display = meta_window_get_display (window);
  MetaContext *context = meta_display_get_context (display);
  MetaWaylandCompositor *compositor =
    meta_context_get_wayland_compositor (context);

  return meta_wayland_compositor_is_shortcuts_inhibited (compositor, source);
}

static gboolean
meta_window_wayland_is_focusable (MetaWindow *window)
{
  return window->input;
}

static gboolean
meta_window_wayland_can_ping (MetaWindow *window)
{
  return TRUE;
}

static gboolean
meta_window_wayland_is_stackable (MetaWindow *window)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);
  return meta_wayland_surface_get_buffer (wl_window->surface) != NULL;
}

static gboolean
meta_window_wayland_are_updates_frozen (MetaWindow *window)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);

  return !wl_window->has_been_shown;
}

static gboolean
meta_window_wayland_is_focus_async (MetaWindow *window)
{
  return FALSE;
}

static MetaWaylandSurface *
meta_window_wayland_get_wayland_surface (MetaWindow *window)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);

  return wl_window->surface;
}

static gboolean
meta_window_wayland_set_transient_for (MetaWindow *window,
                                       MetaWindow *parent)
{
  if (window->attached != meta_window_should_attach_to_parent (window))
    {
      window->attached = meta_window_should_attach_to_parent (window);
      meta_window_recalc_features (window);
    }
  return TRUE;
}

static void
meta_window_wayland_stage_to_protocol (MetaWindow          *window,
                                       int                  stage_x,
                                       int                  stage_y,
                                       int                 *protocol_x,
                                       int                 *protocol_y,
                                       MtkRoundingStrategy  rounding_strategy)
{
  if (protocol_x)
    *protocol_x = stage_x;
  if (protocol_y)
    *protocol_y = stage_y;
}

static void
meta_window_wayland_protocol_to_stage (MetaWindow          *window,
                                       int                  protocol_x,
                                       int                  protocol_y,
                                       int                 *stage_x,
                                       int                 *stage_y,
                                       MtkRoundingStrategy  rounding_strategy)
{
  if (stage_x)
    *stage_x = protocol_x;
  if (stage_y)
    *stage_y = protocol_y;
}

static MetaGravity
meta_window_wayland_get_gravity (MetaWindow *window)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);
  MetaWaylandSurface *surface = wl_window->surface;
  MetaWaylandToplevelDrag *toplevel_drag;

  /* Force nortwest gravity on toplevel drags */
  toplevel_drag = get_toplevel_drag (window);

  if (toplevel_drag && surface == toplevel_drag->dragged_surface)
    return META_GRAVITY_NORTH_WEST;

  return META_WINDOW_CLASS (meta_window_wayland_parent_class)->get_gravity (window);
}

static gboolean
maybe_save_rect (MetaWindow                     *window,
                 MetaWindowConfig               *config,
                 MetaWaylandWindowConfiguration *configuration)
{
  MtkRectangle frame_rect;
  MetaWindowDrag *window_drag;

  if (!meta_window_config_is_floating (config))
    return FALSE;

  window_drag =
    meta_compositor_get_current_window_drag (window->display->compositor);
  if (window_drag &&
      meta_window_drag_get_window (window_drag) == window)
    return FALSE;

  frame_rect = meta_window_config_get_rect (config);
  if (!meta_window_config_is_maximized_horizontally (config))
    {
      if (configuration)
        {
          if (configuration->has_position)
            window->saved_rect.x = configuration->x;
        }
      else
        {
          window->saved_rect.x = frame_rect.x;
        }
      if (configuration)
        {
          if (configuration->has_position)
            window->saved_rect.width = configuration->width;
        }
      else
        {
          window->saved_rect.width = frame_rect.width;
        }
    }
  if (!meta_window_config_is_maximized_vertically (config))
    {
      if (configuration)
        {
          if (configuration->has_position)
            window->saved_rect.y = configuration->y;
        }
      else
        {
          window->saved_rect.y = frame_rect.y;
        }
      if (configuration)
        {
          if (configuration->has_size)
            window->saved_rect.height = configuration->height;
        }
      else
        {
          window->saved_rect.height = frame_rect.height;
        }
    }

  return TRUE;
}

static void
meta_window_wayland_save_rect (MetaWindow *window)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);
  GList *l;

  if (!meta_wayland_surface_get_buffer (wl_window->surface))
    return;

  for (l = wl_window->pending_configurations; l; l = l->next)
    {
      MetaWaylandWindowConfiguration *configuration = l->data;

      if (maybe_save_rect (window, configuration->config, configuration))
        return;
    }

  if (!wl_window->pending_configurations)
    {
      maybe_save_rect (window, window->config, NULL);
      return;
    }
}

static MetaStackLayer
meta_window_wayland_calculate_layer (MetaWindow *window)
{
  return meta_window_get_default_layer (window);
}

static void
meta_window_wayland_constructed (GObject *object)
{
  MetaWindow *window = META_WINDOW (object);

  window->client_type = META_WINDOW_CLIENT_TYPE_WAYLAND;

  window->override_redirect = FALSE;
  /* size_hints are the "request" */
  window->size_hints.x = 0;
  window->size_hints.y = 0;
  window->size_hints.width = 0;
  window->size_hints.height = 0;

  window->depth = 24;

  window->mapped = FALSE;

  window->decorated = FALSE;
  window->hidden = TRUE;

  window->config = meta_window_config_new ();

  G_OBJECT_CLASS (meta_window_wayland_parent_class)->constructed (object);
}

static void
meta_window_wayland_finalize (GObject *object)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (object);

  g_clear_object (&wl_window->client);
  g_clear_pointer (&wl_window->last_acked_configuration,
                   meta_wayland_window_configuration_unref);
  g_clear_pointer (&wl_window->last_sent_configuration,
                   meta_wayland_window_configuration_unref);
  g_list_free_full (wl_window->pending_configurations,
                    (GDestroyNotify) meta_wayland_window_configuration_unref);

  G_OBJECT_CLASS (meta_window_wayland_parent_class)->finalize (object);
}

static void
meta_window_wayland_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  MetaWindowWayland *window = META_WINDOW_WAYLAND (object);

  switch (prop_id)
    {
    case PROP_SURFACE:
      g_value_set_object (value, window->surface);
      break;
    case PROP_CLIENT:
      g_value_set_object (value, window->client);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_window_wayland_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  MetaWindowWayland *window = META_WINDOW_WAYLAND (object);

  switch (prop_id)
    {
    case PROP_SURFACE:
      window->surface = g_value_get_object (value);
      break;
    case PROP_CLIENT:
      window->client = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_window_wayland_class_init (MetaWindowWaylandClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaWindowClass *window_class = META_WINDOW_CLASS (klass);

  object_class->finalize = meta_window_wayland_finalize;
  object_class->get_property = meta_window_wayland_get_property;
  object_class->set_property = meta_window_wayland_set_property;
  object_class->constructed = meta_window_wayland_constructed;

  window_class->manage = meta_window_wayland_manage;
  window_class->unmanage = meta_window_wayland_unmanage;
  window_class->ping = meta_window_wayland_ping;
  window_class->delete = meta_window_wayland_delete;
  window_class->kill = meta_window_wayland_kill;
  window_class->focus = meta_window_wayland_focus;
  window_class->grab_op_began = meta_window_wayland_grab_op_began;
  window_class->grab_op_ended = meta_window_wayland_grab_op_ended;
  window_class->move_resize_internal = meta_window_wayland_move_resize_internal;
  window_class->update_main_monitor = meta_window_wayland_update_main_monitor;
  window_class->main_monitor_changed = meta_window_wayland_main_monitor_changed;
  window_class->get_client_pid = meta_window_wayland_get_client_pid;
  window_class->force_restore_shortcuts = meta_window_wayland_force_restore_shortcuts;
  window_class->shortcuts_inhibited = meta_window_wayland_shortcuts_inhibited;
  window_class->is_focusable = meta_window_wayland_is_focusable;
  window_class->is_stackable = meta_window_wayland_is_stackable;
  window_class->can_ping = meta_window_wayland_can_ping;
  window_class->are_updates_frozen = meta_window_wayland_are_updates_frozen;
  window_class->calculate_layer = meta_window_wayland_calculate_layer;
  window_class->is_focus_async = meta_window_wayland_is_focus_async;
  window_class->get_wayland_surface = meta_window_wayland_get_wayland_surface;
  window_class->set_transient_for = meta_window_wayland_set_transient_for;
  window_class->stage_to_protocol = meta_window_wayland_stage_to_protocol;
  window_class->protocol_to_stage = meta_window_wayland_protocol_to_stage;
  window_class->get_gravity = meta_window_wayland_get_gravity;
  window_class->save_rect = meta_window_wayland_save_rect;

  obj_props[PROP_SURFACE] =
    g_param_spec_object ("surface", NULL, NULL,
                         META_TYPE_WAYLAND_SURFACE,
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

  obj_props[PROP_CLIENT] =
    g_param_spec_object ("client", NULL, NULL,
                         META_TYPE_WAYLAND_CLIENT,
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

  g_object_class_install_properties (object_class, PROP_LAST, obj_props);

  unmaximize_drag_quark =
    g_quark_from_static_string ("window-wayland-drag-unmaximize-quark");
}

static void
meta_window_wayland_maybe_apply_custom_tag (MetaWindow *window)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);
  const char *window_tag;

  window_tag = meta_wayland_client_get_window_tag (wl_window->client);
  if (window_tag)
    meta_window_set_tag (window, window_tag);
}

MetaWindow *
meta_window_wayland_new (MetaDisplay        *display,
                         MetaWaylandSurface *surface)
{
  MetaWindowWayland *wl_window;
  MetaWindow *window;
  struct wl_client *wl_client;
  MetaWaylandClient *client;

  wl_client = wl_resource_get_client (surface->resource);
  client = meta_get_wayland_client (wl_client);

  window = g_initable_new (META_TYPE_WINDOW_WAYLAND,
                           NULL, NULL,
                           "display", display,
                           "effect", META_COMP_EFFECT_CREATE,
                           "surface", surface,
                           "client", client,
                           NULL);
  wl_window = META_WINDOW_WAYLAND (window);
  set_geometry_scale_for_window (wl_window, wl_window->geometry_scale);
  meta_window_wayland_maybe_apply_custom_tag (window);

  return window;
}

MetaWaylandWindowConfiguration *
meta_window_wayland_peek_configuration (MetaWindowWayland *wl_window,
                                        uint32_t           serial)
{
  GList *l;

  for (l = wl_window->pending_configurations; l; l = l->next)
    {
      MetaWaylandWindowConfiguration *configuration = l->data;

      if (configuration->serial == serial)
        return configuration;
    }

  return NULL;
}

static MetaWaylandWindowConfiguration *
acquire_acked_configuration (MetaWindowWayland       *wl_window,
                             MetaWaylandSurfaceState *pending,
                             gboolean                *is_client_resize)
{
  GList *l;
  gboolean has_pending_resize = FALSE;
  uint32_t acked_serial = 0;

  /* There can be 3 different cases where a resizing configurations can be found
   * in the list of pending configurations. We consider resizes in any of these
   * cases to be requested by the server:
   * 1. Acked serial is resizing. This is obviously a server requested resize.
   * 2. Acked serial is larger than the serial of a pending resizing
   *    configuration. This means there was a server requested resize in the
   *    past that has not been acked yet. This covers cases such as a resizing
   *    configure followed by a status change configure before the client had
   *    time to ack the former.
   * 3. Acked serial is smaller than the serial of a pending resizing
   *    configuration. This means there will be a server requested resize in the
   *    future. In this case we want to avoid marking this as a client resize,
   *    because it will change in the future again anyway and considering it
   *    a client resize could trigger another move_resize on the server due to
   *    enforcing constraints based on an already outdated size. */
  for (l = wl_window->pending_configurations; l; l = l->next)
    {
      MetaWaylandWindowConfiguration *configuration = l->data;

      if (configuration->is_resizing)
        {
          has_pending_resize = TRUE;
          break;
        }
    }

  *is_client_resize = !has_pending_resize;

  if (!pending->has_acked_configure_serial)
    {
      if (meta_wayland_surface_get_buffer (wl_window->surface) &&
          wl_window->pending_configurations &&
          !wl_window->last_acked_configuration)
        {
          MetaWindow *toplevel_window;

          toplevel_window =
            meta_wayland_surface_get_toplevel_window (wl_window->surface);

          g_warning ("Buggy client (%s) committed initial non-empty content "
                     "without acknowledging configuration, working around.",
                     toplevel_window->res_class);
          meta_window_wayland_get_oldest_pending_serial (wl_window,
                                                         &acked_serial);
        }
      else
        {
          return NULL;
        }
    }
  else
    {
      acked_serial = pending->acked_configure_serial;
    }

  for (l = wl_window->pending_configurations; l; l = l->next)
    {
      MetaWaylandWindowConfiguration *configuration = l->data;
      GList *tail;
      gboolean is_matching_configuration;

      if (configuration->serial > acked_serial)
        continue;

      tail = l;

      if (tail->prev)
        {
          tail->prev->next = NULL;
          tail->prev = NULL;
        }
      else
        {
          wl_window->pending_configurations = NULL;
        }

      is_matching_configuration =
        configuration->serial == acked_serial;

      if (is_matching_configuration)
        tail = g_list_delete_link (tail, l);
      g_list_free_full (tail,
                        (GDestroyNotify) meta_wayland_window_configuration_unref);

      if (is_matching_configuration)
        return configuration;
      else
        return NULL;
    }

  return NULL;
}

gboolean
meta_window_wayland_is_resize (MetaWindowWayland *wl_window,
                               int                width,
                               int                height)
{
  MetaWaylandWindowConfiguration *last_sent_configuration =
    wl_window->last_sent_configuration;
  int old_width;
  int old_height;

  if (wl_window->pending_configurations)
    {
      old_width = last_sent_configuration->width;
      old_height = last_sent_configuration->height;
    }
  else
    {
      MetaWindow *window = META_WINDOW (wl_window);
      meta_window_config_get_size (window->config, &old_width, &old_height);
    }

  return !last_sent_configuration ||
         old_width != width ||
         old_height != height;
}

int
meta_window_wayland_get_geometry_scale (MetaWindow *window)
{
  if (!window->monitor)
    return 1;

  return get_window_geometry_scale_for_logical_monitor (window->monitor);
}

static gboolean
maybe_derive_position_from_drag (MetaWaylandWindowConfiguration *configuration,
                                 const MtkRectangle             *geometry,
                                 MtkRectangle                   *rect)
{
  MetaWindowDrag *window_drag;
  UnmaximizeDrag *unmaximize_drag;

  window_drag = configuration->window_drag;
  if (!window_drag)
    return FALSE;

  unmaximize_drag = g_object_get_qdata (G_OBJECT (window_drag),
                                        unmaximize_drag_quark);
  if (!unmaximize_drag)
    return FALSE;

  if (unmaximize_drag->serial != configuration->serial)
    return FALSE;

  meta_window_drag_calculate_window_position (window_drag,
                                              geometry->width,
                                              geometry->height,
                                              &rect->x,
                                              &rect->y);
  return TRUE;
}

static void
calculate_position (MetaWaylandWindowConfiguration *configuration,
                    const MtkRectangle             *geometry,
                    MtkRectangle                   *rect)
{
  int offset_x;
  int offset_y;

  rect->x = configuration->x;
  rect->y = configuration->y;

  offset_x = configuration->width - geometry->width;
  offset_y = configuration->height - geometry->height;
  switch (configuration->gravity)
    {
    case META_GRAVITY_SOUTH:
    case META_GRAVITY_SOUTH_WEST:
      rect->y += offset_y;
      break;
    case META_GRAVITY_EAST:
    case META_GRAVITY_NORTH_EAST:
      rect->x += offset_x;
      break;
    case META_GRAVITY_SOUTH_EAST:
      rect->x += offset_x;
      rect->y += offset_y;
      break;
    default:
      break;
    }
}

/**
 * meta_window_move_resize_wayland:
 *
 * Complete a resize operation from a wayland client.
 */
void
meta_window_wayland_finish_move_resize (MetaWindow              *window,
                                        MtkRectangle             new_geom,
                                        MetaWaylandSurfaceState *pending)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);
  gboolean has_position = FALSE;
  MetaDisplay *display = window->display;
  MetaWaylandSurface *surface = wl_window->surface;
  int dx, dy;
  int geometry_scale;
  MtkRectangle rect;
  MetaMoveResizeFlags flags;
  MetaWaylandWindowConfiguration *acked_configuration;
  gboolean is_window_being_resized;
  gboolean is_client_resize;
  MetaWindowDrag *window_drag;
  MtkRectangle frame_rect;
  MetaWindowActor *window_actor;
  MetaWaylandToplevelDrag *toplevel_drag;
  MetaPlaceFlag place_flags = META_PLACE_FLAG_NONE;

  g_return_if_fail (!mtk_rectangle_is_empty (&new_geom));

  /* new_geom is in the logical pixel coordinate space, but MetaWindow wants its
   * rects to represent what in turn will end up on the stage, i.e. we need to
   * scale new_geom to physical pixels given what buffer scale and texture scale
   * is in use. */

  geometry_scale = meta_window_wayland_get_geometry_scale (window);
  new_geom.x *= geometry_scale;
  new_geom.y *= geometry_scale;
  new_geom.width *= geometry_scale;
  new_geom.height *= geometry_scale;

  /* The (dx, dy) offset is also in logical pixel coordinate space and needs
   * to be scaled in the same way as new_geom. */
  dx = pending->dx * geometry_scale;
  dy = pending->dy * geometry_scale;

  /* XXX: Find a better place to store the window geometry offsets. */
  if (meta_wayland_surface_get_buffer (surface))
    {
      window->custom_frame_extents.left = new_geom.x;
      window->custom_frame_extents.top = new_geom.y;
      window->custom_frame_extents.right =
        meta_wayland_surface_get_width (surface) * geometry_scale -
        new_geom.x - new_geom.width;
      window->custom_frame_extents.bottom =
        meta_wayland_surface_get_height (surface) * geometry_scale -
        new_geom.y - new_geom.height;
    }
  else
    {
      window->custom_frame_extents = (MetaFrameBorder) { 0 };
    }

  flags = META_MOVE_RESIZE_WAYLAND_FINISH_MOVE_RESIZE;

  acked_configuration = acquire_acked_configuration (wl_window, pending,
                                                     &is_client_resize);

  if (meta_is_topic_enabled (META_DEBUG_WAYLAND))
    {
      g_autoptr (GString) string = NULL;

      string = g_string_new ("");
      g_string_append_printf (string,
                              "Applying window state for wl_surface#%u: ",
                              wl_resource_get_id (surface->resource));
      g_string_append_printf (string, "size=%dx%d",
                              new_geom.width, new_geom.height);
      if (acked_configuration)
        {
          g_string_append_printf (string, ", serial=%u",
                                  acked_configuration->serial);
        }
      meta_topic (META_DEBUG_WAYLAND, "%s", string->str);
    }

  if (acked_configuration &&
      acked_configuration->has_size &&
      acked_configuration->config &&
      meta_window_config_get_is_fullscreen (acked_configuration->config) &&
      (new_geom.width > acked_configuration->width ||
       new_geom.height > acked_configuration->height))
    {
      g_warning ("Window %s (wl_surface#%u) size %dx%d exceeds "
                 "allowed maximum size %dx%d",
                 window->desc,
                 surface->resource
                   ? wl_resource_get_id (surface->resource)
                   : 0,
                 new_geom.width / geometry_scale,
                 new_geom.height / geometry_scale,
                 acked_configuration->width / geometry_scale,
                 acked_configuration->height / geometry_scale);
    }

  window_drag = meta_compositor_get_current_window_drag (display->compositor);
  is_window_being_resized = is_drag_resizing_window (window_drag, window);

  frame_rect = meta_window_config_get_rect (window->config);
  rect = (MtkRectangle) {
    .x = frame_rect.x,
    .y = frame_rect.y,
    .width = new_geom.width,
    .height = new_geom.height
  };

  if (!is_window_being_resized)
    {
      if (acked_configuration)
        {
          if (window->placement.rule)
            {
              MetaWindow *parent;
              MtkRectangle parent_rect;

              parent = meta_window_get_transient_for (window);
              parent_rect = meta_window_config_get_rect (parent->config);
              rect.x = parent_rect.x + acked_configuration->rel_x;
              rect.y = parent_rect.y + acked_configuration->rel_y;
            }
          else
            {
              if (!meta_window_config_is_floating (acked_configuration->config))
                {
                  flags |= META_MOVE_RESIZE_CONSTRAIN;
                }
              else if (!window->placed && !window->minimized)
                {
                  place_flags |= META_PLACE_FLAG_CALCULATE;
                  flags |= META_MOVE_RESIZE_CONSTRAIN;
                }

              if (acked_configuration->has_position)
                {
                  has_position = TRUE;

                  if (maybe_derive_position_from_drag (acked_configuration,
                                                       &new_geom,
                                                       &rect))
                    window->placed = TRUE;
                  else
                    calculate_position (acked_configuration, &new_geom, &rect);
                }
            }
        }
      else
        {
          if (!window->placed &&
              meta_window_config_is_floating (window->config))
            {
              place_flags |= META_PLACE_FLAG_CALCULATE;
              flags |= META_MOVE_RESIZE_CONSTRAIN;
            }

          if (window->placed)
            has_position = TRUE;
        }
    }
  else
    {
      if (acked_configuration && acked_configuration->has_position)
        calculate_position (acked_configuration, &new_geom, &rect);
    }

  if (!has_position)
    flags |= META_MOVE_RESIZE_RECT_INVALID;

  toplevel_drag = get_toplevel_drag (window);
  if (toplevel_drag && !is_window_being_resized && !window->mapped &&
      rect.width > 0 && rect.height > 0)
    {
      meta_wayland_toplevel_drag_calc_origin_for_dragged_window (toplevel_drag,
                                                                 &rect);
    }

  rect.x += dx;
  rect.y += dy;

  if (rect.x != frame_rect.x || rect.y != frame_rect.y)
    flags |= META_MOVE_RESIZE_MOVE_ACTION;

  if (wl_window->has_pending_state_change && acked_configuration)
    {
      flags |= META_MOVE_RESIZE_WAYLAND_STATE_CHANGED;
      wl_window->has_pending_state_change = FALSE;
    }

  if (rect.width != frame_rect.width || rect.height != frame_rect.height)
    {
      flags |= META_MOVE_RESIZE_RESIZE_ACTION;

      if (is_client_resize)
        {
          flags |= META_MOVE_RESIZE_WAYLAND_CLIENT_RESIZE;
          flags |= META_MOVE_RESIZE_CONSTRAIN;
        }
    }

  if (acked_configuration)
    {
      g_clear_pointer (&wl_window->last_acked_configuration,
                       meta_wayland_window_configuration_unref);
      wl_window->last_acked_configuration =
        g_steal_pointer (&acked_configuration);
    }

  /* Force unconstrained move when running toplevel drags */
  if (toplevel_drag && surface == toplevel_drag->dragged_surface)
    {
      window_actor = meta_window_actor_from_window (window);
      meta_window_actor_set_tied_to_drag (window_actor, TRUE);
    }

  meta_window_move_resize_internal (window, flags, place_flags, rect, NULL);

  if (place_flags & META_PLACE_FLAG_CALCULATE)
    window->placed = TRUE;
}

void
meta_window_place_with_placement_rule (MetaWindow        *window,
                                       MetaPlacementRule *placement_rule)
{
  gboolean first_placement;
  MetaPlaceFlag place_flags = META_PLACE_FLAG_NONE;

  first_placement = !window->placement.rule;

  g_clear_pointer (&window->placement.rule, g_free);
  window->placement.rule = g_new0 (MetaPlacementRule, 1);
  *window->placement.rule = *placement_rule;

  meta_window_config_get_position (window->config,
                                   &window->unconstrained_rect.x,
                                   &window->unconstrained_rect.y);
  window->unconstrained_rect.width = placement_rule->width;
  window->unconstrained_rect.height = placement_rule->height;

  if (first_placement)
    place_flags |= META_PLACE_FLAG_CALCULATE;

  meta_window_move_resize_internal (window,
                                    (META_MOVE_RESIZE_WAYLAND_FORCE_CONFIGURE |
                                     META_MOVE_RESIZE_MOVE_ACTION |
                                     META_MOVE_RESIZE_RESIZE_ACTION |
                                     META_MOVE_RESIZE_PLACEMENT_CHANGED |
                                     META_MOVE_RESIZE_CONSTRAIN),
                                    place_flags,
                                    window->unconstrained_rect,
                                    NULL);
}

void
meta_window_update_placement_rule (MetaWindow        *window,
                                   MetaPlacementRule *placement_rule)
{
  window->placement.state = META_PLACEMENT_STATE_INVALIDATED;
  meta_window_place_with_placement_rule (window, placement_rule);
}

void
meta_window_wayland_set_min_size (MetaWindow *window,
                                  int         width,
                                  int         height)
{
  gint64 new_width, new_height;
  float scale;

  meta_topic (META_DEBUG_GEOMETRY, "Window %s sets min size %d x %d",
              window->desc, width, height);

  if (width == 0 && height == 0)
    {
      window->size_hints.min_width = 0;
      window->size_hints.min_height = 0;
      window->size_hints.flags &= ~META_SIZE_HINTS_PROGRAM_MIN_SIZE;

      return;
    }

  scale = (float) meta_window_wayland_get_geometry_scale (window);
  scale_size (&width, &height, scale);

  new_width = width + (window->custom_frame_extents.left +
                       window->custom_frame_extents.right);
  new_height = height + (window->custom_frame_extents.top +
                         window->custom_frame_extents.bottom);

  window->size_hints.min_width = (int) MIN (new_width, G_MAXINT);
  window->size_hints.min_height = (int) MIN (new_height, G_MAXINT);
  window->size_hints.flags |= META_SIZE_HINTS_PROGRAM_MIN_SIZE;
}

void
meta_window_wayland_set_max_size (MetaWindow *window,
                                  int         width,
                                  int         height)

{
  gint64 new_width, new_height;
  float scale;

  meta_topic (META_DEBUG_GEOMETRY, "Window %s sets max size %d x %d",
              window->desc, width, height);

  if (width == 0 && height == 0)
    {
      window->size_hints.max_width = G_MAXINT;
      window->size_hints.max_height = G_MAXINT;
      window->size_hints.flags &= ~META_SIZE_HINTS_PROGRAM_MAX_SIZE;

      return;
    }

  scale = (float) meta_window_wayland_get_geometry_scale (window);
  scale_size (&width, &height, scale);

  new_width = width + (window->custom_frame_extents.left +
                       window->custom_frame_extents.right);
  new_height = height + (window->custom_frame_extents.top +
                         window->custom_frame_extents.bottom);

  window->size_hints.max_width = (int) ((new_width > 0 && new_width < G_MAXINT) ?
                                        new_width : G_MAXINT);
  window->size_hints.max_height = (int)  ((new_height > 0 && new_height < G_MAXINT) ?
                                          new_height : G_MAXINT);
  window->size_hints.flags |= META_SIZE_HINTS_PROGRAM_MAX_SIZE;
}

void
meta_window_wayland_get_min_size (MetaWindow *window,
                                  int        *width,
                                  int        *height)
{
  gint64 current_width, current_height;
  float scale;

  if (!(window->size_hints.flags & META_SIZE_HINTS_PROGRAM_MIN_SIZE))
    {
      /* Zero means unlimited */
      *width = 0;
      *height = 0;

      return;
    }

  current_width = window->size_hints.min_width -
                  (window->custom_frame_extents.left +
                   window->custom_frame_extents.right);
  current_height = window->size_hints.min_height -
                   (window->custom_frame_extents.top +
                    window->custom_frame_extents.bottom);

  *width = MAX (current_width, 0);
  *height = MAX (current_height, 0);

  scale = 1.0f / meta_window_wayland_get_geometry_scale (window);
  scale_size (width, height, scale);
}

void
meta_window_wayland_get_max_size (MetaWindow *window,
                                  int        *width,
                                  int        *height)
{
  gint64 current_width = 0;
  gint64 current_height = 0;
  float scale;

  if (!(window->size_hints.flags & META_SIZE_HINTS_PROGRAM_MAX_SIZE))
    {
      /* Zero means unlimited */
      *width = 0;
      *height = 0;

      return;
    }

  if (window->size_hints.max_width < G_MAXINT)
    current_width = window->size_hints.max_width -
                    (window->custom_frame_extents.left +
                     window->custom_frame_extents.right);

  if (window->size_hints.max_height < G_MAXINT)
    current_height = window->size_hints.max_height -
                     (window->custom_frame_extents.top +
                      window->custom_frame_extents.bottom);

  *width = CLAMP (current_width, 0, G_MAXINT);
  *height = CLAMP (current_height, 0, G_MAXINT);

  scale = 1.0f / meta_window_wayland_get_geometry_scale (window);
  scale_size (width, height, scale);
}

gboolean
meta_window_wayland_is_acked_fullscreen (MetaWindowWayland *wl_window)
{
  MetaWaylandWindowConfiguration *last_acked_configuration =
    wl_window->last_acked_configuration;
  MetaWindowConfig *config;

  if (!last_acked_configuration)
    return FALSE;

  config = last_acked_configuration->config;
  if (!config)
    return FALSE;

  return meta_window_config_get_is_fullscreen (config);
}

gboolean
meta_window_wayland_get_pending_serial (MetaWindowWayland *wl_window,
                                        uint32_t          *serial)
{
  if (wl_window->pending_configurations)
    {
      MetaWaylandWindowConfiguration *configuration =
        wl_window->pending_configurations->data;

      *serial = configuration->serial;
      return TRUE;
    }

  return FALSE;
}

static gboolean
meta_window_wayland_get_oldest_pending_serial (MetaWindowWayland *wl_window,
                                               uint32_t          *serial)
{
  if (wl_window->pending_configurations)
    {
      MetaWaylandWindowConfiguration *configuration =
        g_list_last (wl_window->pending_configurations)->data;

      *serial = configuration->serial;
      return TRUE;
    }

  return FALSE;
}

MetaWaylandClient *
meta_window_wayland_get_client (MetaWindowWayland *wl_window)
{
  return wl_window->client;
}
