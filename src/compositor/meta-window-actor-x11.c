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

#include "compositor/meta-window-actor-x11.h"

#include "backends/meta-logical-monitor.h"
#include "clutter/clutter-frame-clock.h"
#include "compositor/compositor-private.h"
#include "compositor/meta-cullable.h"
#include "compositor/meta-shaped-texture-private.h"
#include "compositor/meta-surface-actor.h"
#include "compositor/meta-surface-actor-x11.h"
#include "core/frame.h"
#include "core/window-private.h"
#include "meta/compositor.h"
#include "meta/meta-enum-types.h"
#include "meta/meta-shadow-factory.h"
#include "meta/meta-window-actor.h"
#include "meta/window.h"
#include "x11/window-x11.h"
#include "x11/meta-sync-counter.h"
#include "x11/meta-x11-display-private.h"
#include "x11/window-x11.h"

enum
{
  PROP_SHADOW_MODE = 1,
  PROP_SHADOW_CLASS
};

struct _MetaWindowActorX11
{
  MetaWindowActor parent;

  guint send_frame_messages_timer;
  gboolean pending_schedule_update_now;

  gulong repaint_scheduled_id;
  gulong size_changed_id;

  gboolean repaint_scheduled;

  /*
   * MetaShadowFactory only caches shadows that are actually in use;
   * to avoid unnecessary recomputation we do two things: 1) we store
   * both a focused and unfocused shadow for the window. If the window
   * doesn't have different focused and unfocused shadow parameters,
   * these will be the same. 2) when the shadow potentially changes we
   * don't immediately unreference the old shadow, we just flag it as
   * dirty and recompute it when we next need it (recompute_focused_shadow,
   * recompute_unfocused_shadow.) Because of our extraction of
   * size-invariant window shape, we'll often find that the new shadow
   * is the same as the old shadow.
   */
  MetaShadow *focused_shadow;
  MetaShadow *unfocused_shadow;

  /* A region that matches the shape of the window, including frame bounds */
  MtkRegion *shape_region;
  /* The region we should clip to when painting the shadow */
  MtkRegion *shadow_clip;
  /* The frame region */
  MtkRegion *frame_bounds;

  /* Extracted size-invariant shape used for shadows */
  MetaWindowShape *shadow_shape;
  char *shadow_class;

  MetaShadowFactory *shadow_factory;
  gulong shadow_factory_changed_handler_id;

  MetaShadowMode shadow_mode;

  gboolean needs_reshape;
  gboolean recompute_focused_shadow;
  gboolean recompute_unfocused_shadow;
  gboolean is_frozen;
};

static void cullable_iface_init (MetaCullableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaWindowActorX11, meta_window_actor_x11, META_TYPE_WINDOW_ACTOR,
                         G_IMPLEMENT_INTERFACE (META_TYPE_CULLABLE, cullable_iface_init))

static void
surface_repaint_scheduled (MetaSurfaceActor *actor,
                           gpointer          user_data)
{
  MetaWindowActorX11 *actor_x11 = META_WINDOW_ACTOR_X11 (user_data);

  actor_x11->repaint_scheduled = TRUE;
}

static void
remove_frame_messages_timer (MetaWindowActorX11 *actor_x11)
{
  g_assert (actor_x11->send_frame_messages_timer != 0);

  g_clear_handle_id (&actor_x11->send_frame_messages_timer, g_source_remove);
}

static gboolean
send_frame_messages_timeout (gpointer data)
{
  MetaWindowActorX11 *actor_x11 = META_WINDOW_ACTOR_X11 (data);
  MetaWindow *window =
    meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_x11));
  MetaSyncCounter *sync_counter;

  sync_counter = meta_window_x11_get_sync_counter (window);
  meta_sync_counter_finish_incomplete (sync_counter);

  if (window->frame)
    {
      sync_counter = meta_frame_get_sync_counter (window->frame);
      meta_sync_counter_finish_incomplete (sync_counter);
    }

  actor_x11->send_frame_messages_timer = 0;

  return G_SOURCE_REMOVE;
}

static void
queue_send_frame_messages_timeout (MetaWindowActorX11 *actor_x11)
{
  MetaWindow *window =
    meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_x11));
  MetaDisplay *display = meta_window_get_display (window);
  MetaLogicalMonitor *logical_monitor;
  MetaSyncCounter *sync_counter;
  int64_t now_us;
  int64_t current_time;
  float refresh_rate;
  int interval, offset;

  if (actor_x11->send_frame_messages_timer != 0)
    return;

  logical_monitor = meta_window_get_main_logical_monitor (window);
  if (logical_monitor)
    {
      GList *monitors = meta_logical_monitor_get_monitors (logical_monitor);
      MetaMonitor *monitor;
      MetaMonitorMode *mode;

      monitor = g_list_first (monitors)->data;
      mode = meta_monitor_get_current_mode (monitor);

      refresh_rate = meta_monitor_mode_get_refresh_rate (mode);
    }
  else
    {
      refresh_rate = 60.0f;
    }

  now_us = g_get_monotonic_time ();
  current_time =
    meta_compositor_monotonic_to_high_res_xserver_time (display->compositor,
                                                        now_us);
  interval = (int) (1000000 / refresh_rate) * 6;
  sync_counter = meta_window_x11_get_sync_counter (window);
  offset = MAX (0, sync_counter->frame_drawn_time + interval - current_time) / 1000;

 /* The clutter master clock source has already been added with META_PRIORITY_REDRAW,
  * so the timer will run *after* the clutter frame handling, if a frame is ready
  * to be drawn when the timer expires.
  */
  actor_x11->send_frame_messages_timer =
    g_timeout_add_full (META_PRIORITY_REDRAW, offset,
                        send_frame_messages_timeout,
                        actor_x11, NULL);
  g_source_set_name_by_id (actor_x11->send_frame_messages_timer,
                           "[mutter] send_frame_messages_timeout");
}

static void
assign_frame_counter_to_frames (MetaWindowActorX11 *actor_x11)
{
  MetaWindow *window =
    meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_x11));
  MetaCompositor *compositor = window->display->compositor;
  ClutterStage *stage = meta_compositor_get_stage (compositor);
  MetaSyncCounter *sync_counter;

  /* If the window is obscured, then we're expecting to deal with sending
   * frame messages in a timeout, rather than in this paint cycle.
   */
  if (actor_x11->send_frame_messages_timer != 0)
    return;

  sync_counter = meta_window_x11_get_sync_counter (window);
  meta_sync_counter_assign_counter_to_frames (sync_counter,
                                              clutter_stage_get_frame_counter (stage));

  if (window->frame)
    {
      sync_counter = meta_frame_get_sync_counter (window->frame);
      meta_sync_counter_assign_counter_to_frames (sync_counter,
                                                  clutter_stage_get_frame_counter (stage));
    }
}

static void
meta_window_actor_x11_frame_complete (MetaWindowActor  *actor,
                                      ClutterFrameInfo *frame_info,
                                      int64_t           presentation_time)
{
  MetaWindow *window = meta_window_actor_get_meta_window (actor);
  MetaSyncCounter *sync_counter;

  if (meta_window_actor_is_destroyed (actor))
    return;

  sync_counter = meta_window_x11_get_sync_counter (window);
  meta_sync_counter_complete_frame (sync_counter,
                                    frame_info,
                                    presentation_time);

  if (window->frame)
    {
      sync_counter = meta_frame_get_sync_counter (window->frame);
      meta_sync_counter_complete_frame (sync_counter,
                                        frame_info,
                                        presentation_time);
    }
}

static MetaSurfaceActor *
meta_window_actor_x11_get_scanout_candidate (MetaWindowActor *actor)
{
  MetaSurfaceActor *surface_actor;

  surface_actor = meta_window_actor_get_surface (actor);

  if (!surface_actor)
    {
      meta_topic (META_DEBUG_RENDER,
                  "No surface-actor for window-actor");
      return NULL;
    }

  if (CLUTTER_ACTOR (surface_actor) !=
      clutter_actor_get_last_child (CLUTTER_ACTOR (actor)))
    {
      meta_topic (META_DEBUG_RENDER,
                  "Top child of window-actor not a surface");
      return NULL;
    }

  if (!meta_window_actor_is_opaque (actor))
    {
      meta_topic (META_DEBUG_RENDER,
                  "Window-actor is not opaque");
      return NULL;
    }

  return surface_actor;
}

static void
surface_size_changed (MetaSurfaceActor *actor,
                      gpointer          user_data)
{
  MetaWindowActorX11 *actor_x11 = META_WINDOW_ACTOR_X11 (user_data);

  meta_window_actor_x11_update_shape (actor_x11);
}

static void
meta_window_actor_x11_assign_surface_actor (MetaWindowActor  *actor,
                                            MetaSurfaceActor *surface_actor)
{
  MetaWindowActorClass *parent_class =
    META_WINDOW_ACTOR_CLASS (meta_window_actor_x11_parent_class);
  MetaWindowActorX11 *actor_x11 = META_WINDOW_ACTOR_X11 (actor);
  MetaSurfaceActor *prev_surface_actor;

  prev_surface_actor = meta_window_actor_get_surface (actor);
  if (prev_surface_actor)
    {
      g_warn_if_fail (meta_is_wayland_compositor ());

      g_clear_signal_handler (&actor_x11->size_changed_id, prev_surface_actor);
      clutter_actor_remove_child (CLUTTER_ACTOR (actor),
                                  CLUTTER_ACTOR (prev_surface_actor));
    }

  parent_class->assign_surface_actor (actor, surface_actor);

  clutter_actor_add_child (CLUTTER_ACTOR (actor),
                           CLUTTER_ACTOR (surface_actor));

  meta_window_actor_x11_update_shape (actor_x11);

  actor_x11->size_changed_id =
    g_signal_connect (surface_actor, "size-changed",
                      G_CALLBACK (surface_size_changed),
                      actor_x11);
  actor_x11->repaint_scheduled_id =
    g_signal_connect (surface_actor, "repaint-scheduled",
                      G_CALLBACK (surface_repaint_scheduled),
                      actor_x11);
}

static void
meta_window_actor_x11_queue_frame_drawn (MetaWindowActor *actor,
                                         gboolean         skip_sync_delay)
{
  MetaWindowActorX11 *actor_x11 = META_WINDOW_ACTOR_X11 (actor);

  if (meta_window_actor_is_destroyed (actor))
    return;

  if (skip_sync_delay)
    {
      ClutterFrameClock *frame_clock;

      frame_clock = clutter_actor_pick_frame_clock (CLUTTER_ACTOR (actor),
                                                    NULL);
      if (frame_clock)
        clutter_frame_clock_schedule_update_now (frame_clock);
      else
        actor_x11->pending_schedule_update_now = TRUE;
    }

  if (!actor_x11->repaint_scheduled)
    {
      MetaSurfaceActor *surface;
      gboolean is_obscured;

      surface = meta_window_actor_get_surface (META_WINDOW_ACTOR (actor_x11));

      if (surface)
        is_obscured = meta_surface_actor_is_effectively_obscured (surface);
      else
        is_obscured = FALSE;

      /* A frame was marked by the client without actually doing any
       * damage or any unobscured, or while we had the window frozen
       * (e.g. during an interactive resize.) We need to make sure that the
       * before_paint/after_paint functions get called, enabling us to
       * send a _NET_WM_FRAME_DRAWN. We need to do full damage to ensure that
       * the window is actually repainted, otherwise any subregion we would pass
       * might end up being either outside of any stage view, or be occluded by
       * something else, which could potentially result in no frame being drawn
       * after all. If the window is completely obscured, or completely off
       * screen we fire off the send_frame_messages timeout.
       */
      if (is_obscured ||
          !clutter_actor_peek_stage_views (CLUTTER_ACTOR (actor)))
        {
          queue_send_frame_messages_timeout (actor_x11);
        }
      else if (surface)
        {
          clutter_actor_queue_redraw (CLUTTER_ACTOR (surface));
          actor_x11->repaint_scheduled = TRUE;
        }
    }
}

static gboolean
has_shadow (MetaWindowActorX11 *actor_x11)
{
  MetaWindow *window =
    meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_x11));

  if (actor_x11->shadow_mode == META_SHADOW_MODE_FORCED_OFF)
    return FALSE;
  if (actor_x11->shadow_mode == META_SHADOW_MODE_FORCED_ON)
    return TRUE;

  /* Leaving out shadows for maximized and fullscreen windows is an efficiency
   * win and also prevents the unsightly effect of the shadow of maximized
   * window appearing on an adjacent window */
  if ((meta_window_get_maximized (window) == META_MAXIMIZE_BOTH) ||
      meta_window_is_fullscreen (window))
    return FALSE;

  /*
   * If we have two snap-tiled windows, we don't want the shadow to obstruct
   * the other window.
   */
  if (meta_window_get_tile_match (window))
    return FALSE;

  /*
   * Let the frames client put a shadow around frames - This should override
   * the restriction about not putting a shadow around ARGB windows.
   */
  if (meta_window_get_frame (window))
    return FALSE;

  /*
   * Do not add shadows to non-opaque (ARGB32) windows, as we can't easily
   * generate shadows for them.
   */
  if (!meta_window_actor_is_opaque (META_WINDOW_ACTOR (actor_x11)))
    return FALSE;

  /*
   * If a window specifies that it has custom frame extents, that likely
   * means that it is drawing a shadow itself. Don't draw our own.
   */
  if (window->has_custom_frame_extents)
    return FALSE;

  /*
   * Generate shadows for all other windows.
   */
  return TRUE;
}

gboolean
meta_window_actor_x11_should_unredirect (MetaWindowActorX11 *actor_x11)
{
  MetaWindow *window =
    meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_x11));
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaSurfaceActor *surface;
  MetaSurfaceActorX11 *surface_x11;

  if (meta_window_actor_is_destroyed (META_WINDOW_ACTOR (actor_x11)))
    return FALSE;

  if (!meta_window_x11_can_unredirect (window_x11))
    return FALSE;

  surface = meta_window_actor_get_surface (META_WINDOW_ACTOR (actor_x11));
  if (!surface)
    return FALSE;

  if (!META_IS_SURFACE_ACTOR_X11 (surface))
    return FALSE;

  surface_x11 = META_SURFACE_ACTOR_X11 (surface);
  return meta_surface_actor_x11_should_unredirect (surface_x11);
}

void
meta_window_actor_x11_set_unredirected (MetaWindowActorX11 *actor_x11,
                                        gboolean            unredirected)
{
  MetaSurfaceActor *surface;
  MetaSurfaceActorX11 *surface_x11;

  surface = meta_window_actor_get_surface (META_WINDOW_ACTOR (actor_x11));
  g_assert (surface);

  g_return_if_fail (META_IS_SURFACE_ACTOR_X11 (surface));

  surface_x11 = META_SURFACE_ACTOR_X11 (surface);
  meta_surface_actor_x11_set_unredirected (surface_x11, unredirected);
}

static const char *
get_shadow_class (MetaWindowActorX11 *actor_x11)
{
  if (actor_x11->shadow_class)
    {
      return actor_x11->shadow_class;
    }
  else
    {
      MetaWindow *window =
        meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_x11));
      MetaWindowType window_type;

      window_type = meta_window_get_window_type (window);
      switch (window_type)
        {
        case META_WINDOW_DROPDOWN_MENU:
        case META_WINDOW_COMBO:
          return "dropdown-menu";
        case META_WINDOW_POPUP_MENU:
          return "popup-menu";
        default:
          {
            MetaFrameType frame_type;

            frame_type = meta_window_get_frame_type (window);
            return meta_frame_type_to_string (frame_type);
          }
        }
    }
}

static void
get_shadow_params (MetaWindowActorX11 *actor_x11,
                   gboolean            appears_focused,
                   MetaShadowParams   *params)
{
  const char *shadow_class = get_shadow_class (actor_x11);

  meta_shadow_factory_get_params (actor_x11->shadow_factory,
                                  shadow_class, appears_focused,
                                  params);
}

static void
get_shape_bounds (MetaWindowActorX11 *actor_x11,
                  MtkRectangle       *bounds)
{
  *bounds = mtk_region_get_extents (actor_x11->shape_region);
}

static void
get_shadow_bounds (MetaWindowActorX11 *actor_x11,
                   gboolean            appears_focused,
                   MtkRectangle       *bounds)
{
  MetaShadow *shadow;
  MtkRectangle shape_bounds;
  MetaShadowParams params;

  shadow = appears_focused ? actor_x11->focused_shadow
                           : actor_x11->unfocused_shadow;

  get_shape_bounds (actor_x11, &shape_bounds);
  get_shadow_params (actor_x11, appears_focused, &params);

  meta_shadow_get_bounds (shadow,
                          params.x_offset + shape_bounds.x,
                          params.y_offset + shape_bounds.y,
                          shape_bounds.width,
                          shape_bounds.height,
                          bounds);
}

/* If we have an ARGB32 window that we decorate with a frame, it's
 * probably something like a translucent terminal - something where
 * the alpha channel represents transparency rather than a shape.  We
 * don't want to show the shadow through the translucent areas since
 * the shadow is wrong for translucent windows (it should be
 * translucent itself and colored), and not only that, will /look/
 * horribly wrong - a misplaced big black blob. As a hack, what we
 * want to do is just draw the shadow as normal outside the frame, and
 * inside the frame draw no shadow.  This is also not even close to
 * the right result, but looks OK. We also apply this approach to
 * windows set to be partially translucent with _NET_WM_WINDOW_OPACITY.
 */
static gboolean
clip_shadow_under_window (MetaWindowActorX11 *actor_x11)
{
  MetaWindow *window =
    meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_x11));

  if (window->frame)
    return TRUE;

  return meta_window_actor_is_opaque (META_WINDOW_ACTOR (actor_x11));
}

/**
 * set_clip_region_beneath:
 * @actor_x11: a #MetaWindowActorX11
 * @clip_region: the region of the screen that isn't completely
 *  obscured beneath the main window texture.
 *
 * Provides a hint as to what areas need to be drawn *beneath*
 * the main window texture.  This is the relevant clip region
 * when drawing the shadow, properly accounting for areas of the
 * shadow hid by the window itself. This will be set before painting
 * then unset afterwards.
 */
static void
set_clip_region_beneath (MetaWindowActorX11 *actor_x11,
                         MtkRegion          *beneath_region)
{
  MetaWindow *window;
  gboolean appears_focused;

  window = meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_x11));
  appears_focused = meta_window_appears_focused (window);
  if (appears_focused ? actor_x11->focused_shadow : actor_x11->unfocused_shadow)
    {
      g_clear_pointer (&actor_x11->shadow_clip, mtk_region_unref);

      if (beneath_region)
        {
          actor_x11->shadow_clip = mtk_region_copy (beneath_region);

          if (clip_shadow_under_window (actor_x11))
            {
              if (actor_x11->frame_bounds)
                mtk_region_subtract (actor_x11->shadow_clip, actor_x11->frame_bounds);
            }
        }
      else
        {
          actor_x11->shadow_clip = NULL;
        }
    }
}

static void
check_needs_shadow (MetaWindowActorX11 *actor_x11)
{
  MetaWindow *window =
    meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_x11));
  MetaShadow *old_shadow = NULL;
  MetaShadow **shadow_location;
  gboolean recompute_shadow;
  gboolean should_have_shadow;
  gboolean appears_focused;

  /* Calling has_shadow() here at every pre-paint is cheap
   * and avoids the need to explicitly handle window type changes, which
   * we would do if tried to keep track of when we might be adding or removing
   * a shadow more explicitly. We only keep track of changes to the *shape* of
   * the shadow with actor_x11->recompute_shadow.
   */

  should_have_shadow = has_shadow (actor_x11);
  appears_focused = meta_window_appears_focused (window);

  if (appears_focused)
    {
      recompute_shadow = actor_x11->recompute_focused_shadow;
      actor_x11->recompute_focused_shadow = FALSE;
      shadow_location = &actor_x11->focused_shadow;
    }
  else
    {
      recompute_shadow = actor_x11->recompute_unfocused_shadow;
      actor_x11->recompute_unfocused_shadow = FALSE;
      shadow_location = &actor_x11->unfocused_shadow;
    }

  if (!should_have_shadow || recompute_shadow)
    {
      if (*shadow_location != NULL)
        {
          old_shadow = *shadow_location;
          *shadow_location = NULL;
        }
    }

  if (!*shadow_location && should_have_shadow)
    {
      MetaShadowFactory *factory = actor_x11->shadow_factory;
      const char *shadow_class = get_shadow_class (actor_x11);
      MtkRectangle shape_bounds;

      if (!actor_x11->shadow_shape)
        {
          actor_x11->shadow_shape =
            meta_window_shape_new (actor_x11->shape_region);
        }

      get_shape_bounds (actor_x11, &shape_bounds);
      *shadow_location =
        meta_shadow_factory_get_shadow (factory,
                                        actor_x11->shadow_shape,
                                        shape_bounds.width, shape_bounds.height,
                                        shadow_class, appears_focused);
    }

  if (old_shadow)
    meta_shadow_unref (old_shadow);
}

void
meta_window_actor_x11_process_damage (MetaWindowActorX11 *actor_x11,
                                      XDamageNotifyEvent *event)
{
  MetaSurfaceActor *surface;

  surface = meta_window_actor_get_surface (META_WINDOW_ACTOR (actor_x11));
  if (surface)
    meta_surface_actor_process_damage (surface,
                                       event->area.x,
                                       event->area.y,
                                       event->area.width,
                                       event->area.height);

  meta_window_actor_notify_damaged (META_WINDOW_ACTOR (actor_x11));
}

static MtkRegion *
scan_visible_region (guchar    *mask_data,
                     int        stride,
                     MtkRegion *scan_area)
{
  int i, n_rects = mtk_region_num_rectangles (scan_area);
  MtkRegionBuilder builder;

  mtk_region_builder_init (&builder);

  for (i = 0; i < n_rects; i++)
    {
      int x, y;
      MtkRectangle rect;

      rect = mtk_region_get_rectangle (scan_area, i);

      for (y = rect.y; y < (rect.y + rect.height); y++)
        {
          for (x = rect.x; x < (rect.x + rect.width); x++)
            {
              int x2 = x;
              while (x2 < (rect.x + rect.width) && mask_data[y * stride + x2] == 255)
                x2++;

              if (x2 > x)
                {
                  mtk_region_builder_add_rectangle (&builder, x, y, x2 - x, 1);
                  x = x2;
                }
            }
        }
    }

  return mtk_region_builder_finish (&builder);
}

static void
get_client_area_rect_from_texture (MetaWindowActorX11 *actor_x11,
                                   MetaShapedTexture  *shaped_texture,
                                   MtkRectangle       *client_area)
{
  MetaWindow *window =
    meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_x11));
  MtkRectangle surface_rect = { 0 };

  surface_rect.width = meta_shaped_texture_get_width (shaped_texture);
  surface_rect.height = meta_shaped_texture_get_height (shaped_texture);
  meta_window_x11_surface_rect_to_client_rect (window,
                                               &surface_rect,
                                               client_area);
}

static void
get_client_area_rect (MetaWindowActorX11 *actor_x11,
                      MtkRectangle       *client_area)
{
  MetaSurfaceActor *surface =
    meta_window_actor_get_surface (META_WINDOW_ACTOR (actor_x11));
  MetaWindow *window =
    meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_x11));
  MetaShapedTexture *stex = meta_surface_actor_get_texture (surface);

  if (!meta_window_x11_always_update_shape (window) || !stex)
    {
      meta_window_get_client_area_rect (window, client_area);
      return;
    }

  get_client_area_rect_from_texture (actor_x11, stex, client_area);
}

static void
region_to_cairo_path (MtkRegion *region,
                      cairo_t   *cr)
{
  MtkRectangle rect;
  int n_rects, i;

  n_rects = mtk_region_num_rectangles (region);

  for (i = 0; i < n_rects; i++)
    {
      rect = mtk_region_get_rectangle (region, i);
      cairo_rectangle (cr, rect.x, rect.y, rect.width, rect.height);
    }
}

static void
build_and_scan_frame_mask (MetaWindowActorX11 *actor_x11,
                           MtkRegion          *shape_region)
{
  ClutterBackend *backend = clutter_get_default_backend ();
  MetaWindow *window =
    meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_x11));
  CoglContext *ctx = clutter_backend_get_cogl_context (backend);
  MetaSurfaceActor *surface =
    meta_window_actor_get_surface (META_WINDOW_ACTOR (actor_x11));
  uint8_t *mask_data;
  unsigned int tex_width, tex_height;
  MetaShapedTexture *stex;
  CoglTexture *mask_texture;
  int stride;
  cairo_t *cr;
  cairo_surface_t *image;
  GError *error = NULL;

  stex = meta_surface_actor_get_texture (surface);
  g_return_if_fail (stex);

  meta_shaped_texture_set_mask_texture (stex, NULL);

  tex_width = meta_shaped_texture_get_width (stex);
  tex_height = meta_shaped_texture_get_height (stex);

  stride = cairo_format_stride_for_width (CAIRO_FORMAT_A8, tex_width);

  /* Create data for an empty image */
  mask_data = g_malloc0 (stride * tex_height);

  image = cairo_image_surface_create_for_data (mask_data,
                                               CAIRO_FORMAT_A8,
                                               tex_width,
                                               tex_height,
                                               stride);
  cr = cairo_create (image);

  region_to_cairo_path (shape_region, cr);
  cairo_fill (cr);

  if (window->frame)
    {
      g_autoptr (MtkRegion) frame_paint_region = NULL;
      g_autoptr (MtkRegion) scanned_region = NULL;
      MtkRectangle rect = { 0, 0, tex_width, tex_height };
      MtkRectangle client_area;
      MtkRectangle frame_rect;

      /* If we update the shape regardless of the frozen state of the actor,
       * as with Xwayland to avoid the black shadow effect, we ought to base
       * the frame size on the buffer size rather than the reported window's
       * frame size, as the buffer may not have been committed yet at this
       * point.
       */
      if (meta_window_x11_always_update_shape (window))
        {
          meta_window_x11_surface_rect_to_frame_rect (window, &rect, &frame_rect);
          get_client_area_rect_from_texture (actor_x11, stex, &client_area);
        }
      else
        {
          meta_window_get_frame_rect (window, &frame_rect);
          meta_window_get_client_area_rect (window, &client_area);
        }

      /* Make sure we don't paint the frame over the client window. */
      frame_paint_region = mtk_region_create_rectangle (&rect);
      mtk_region_subtract_rectangle (frame_paint_region, &client_area);

      region_to_cairo_path (frame_paint_region, cr);
      cairo_clip (cr);

      meta_frame_get_mask (window->frame, &frame_rect, cr);

      cairo_surface_flush (image);
      scanned_region = scan_visible_region (mask_data, stride, frame_paint_region);
      mtk_region_union (shape_region, scanned_region);
    }

  cairo_destroy (cr);
  cairo_surface_destroy (image);

  mask_texture = cogl_texture_2d_new_from_data (ctx, tex_width, tex_height,
                                                COGL_PIXEL_FORMAT_A_8,
                                                stride, mask_data, &error);

  if (error)
    {
      g_warning ("Failed to allocate mask texture: %s", error->message);
      g_error_free (error);
    }

  if (mask_texture)
    {
      meta_shaped_texture_set_mask_texture (stex, mask_texture);
      g_object_unref (mask_texture);
    }
  else
    {
      meta_shaped_texture_set_mask_texture (stex, NULL);
    }

  g_free (mask_data);
}

static void
invalidate_shadow (MetaWindowActorX11 *actor_x11)
{
  actor_x11->recompute_focused_shadow = TRUE;
  actor_x11->recompute_unfocused_shadow = TRUE;

  if (meta_window_actor_is_frozen (META_WINDOW_ACTOR (actor_x11)))
    return;

  clutter_actor_queue_redraw (CLUTTER_ACTOR (actor_x11));
  clutter_actor_invalidate_paint_volume (CLUTTER_ACTOR (actor_x11));
}

static void
update_shape_region (MetaWindowActorX11 *actor_x11)
{
  MetaWindow *window =
    meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_x11));
  MtkRegion *region = NULL;
  MtkRectangle client_area;

  get_client_area_rect (actor_x11, &client_area);

  if (window->frame && window->shape_region)
    {
      region = mtk_region_copy (window->shape_region);
      mtk_region_translate (region, client_area.x, client_area.y);
    }
  else if (window->shape_region != NULL)
    {
      region = mtk_region_ref (window->shape_region);
    }
  else
    {
      /* If we don't have a shape on the server, that means that
       * we have an implicit shape of one rectangle covering the
       * entire window. */
      region = mtk_region_create_rectangle (&client_area);
    }

  if (window->shape_region || window->frame)
    build_and_scan_frame_mask (actor_x11, region);

  g_clear_pointer (&actor_x11->shape_region, mtk_region_unref);
  actor_x11->shape_region = region;

  g_clear_pointer (&actor_x11->shadow_shape, meta_window_shape_unref);

  invalidate_shadow (actor_x11);
}

static void
update_input_region (MetaWindowActorX11 *actor_x11)
{
  MetaWindow *window =
    meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_x11));
  MetaSurfaceActor *surface =
    meta_window_actor_get_surface (META_WINDOW_ACTOR (actor_x11));
  g_autoptr (MtkRegion) region = NULL;

  if (window->shape_region && window->input_region)
    {
      MtkRectangle client_area;
      g_autoptr (MtkRegion) frames_input = NULL;
      g_autoptr (MtkRegion) client_input = NULL;

      get_client_area_rect (actor_x11, &client_area);

      frames_input = mtk_region_copy (window->input_region);
      mtk_region_subtract_rectangle (frames_input, &client_area);

      client_input = mtk_region_copy (actor_x11->shape_region);
      mtk_region_intersect (client_input, window->input_region);

      mtk_region_union (frames_input, client_input);

      region = g_steal_pointer (&frames_input);
    }
  else if (window->shape_region)
    {
      MtkRectangle client_area;

      meta_window_get_client_area_rect (window, &client_area);

      region = mtk_region_copy (window->shape_region);
      mtk_region_translate (region, client_area.x, client_area.y);
    }
  else if (window->input_region)
    region = mtk_region_ref (window->input_region);
  else
    region = NULL;

  meta_surface_actor_set_input_region (surface, region);
}

static gboolean
is_actor_maybe_transparent (MetaWindowActorX11 *actor_x11)
{
  MetaSurfaceActor *surface;
  MetaShapedTexture *stex;

  surface = meta_window_actor_get_surface (META_WINDOW_ACTOR (actor_x11));
  if (!surface)
    return TRUE;

  if (META_IS_SURFACE_ACTOR_X11 (surface) &&
      meta_surface_actor_x11_is_unredirected (META_SURFACE_ACTOR_X11 (surface)))
    return FALSE;

  stex = meta_surface_actor_get_texture (surface);
  if (!meta_shaped_texture_has_alpha (stex))
    return FALSE;

  return TRUE;
}

static void
update_opaque_region (MetaWindowActorX11 *actor_x11)
{
  MetaWindow *window =
    meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_x11));
  gboolean is_maybe_transparent;
  g_autoptr (MtkRegion) opaque_region = NULL;
  MetaSurfaceActor *surface;

  is_maybe_transparent = is_actor_maybe_transparent (actor_x11);
  if (is_maybe_transparent &&
      (window->opaque_region ||
       (window->frame && window->frame->opaque_region)))
    {
      MtkRectangle client_area;

      if (window->frame && window->frame->opaque_region)
        opaque_region = mtk_region_copy (window->frame->opaque_region);

      get_client_area_rect (actor_x11, &client_area);

      if (opaque_region && meta_window_x11_has_alpha_channel (window))
        mtk_region_subtract_rectangle (opaque_region, &client_area);

      if (window->opaque_region)
        {
          g_autoptr (MtkRegion) client_opaque_region = NULL;

          /* The opaque region is defined to be a part of the
           * window which ARGB32 will always paint with opaque
           * pixels. For these regions, we want to avoid painting
           * windows and shadows beneath them.
           *
           * If the client gives bad coordinates where it does not
           * fully paint, the behavior is defined by the specification
           * to be undefined, and considered a client bug. In mutter's
           * case, graphical glitches will occur.
           */
          client_opaque_region = mtk_region_copy (window->opaque_region);
          mtk_region_translate (client_opaque_region,
                                client_area.x, client_area.y);

          if (opaque_region)
            mtk_region_union (opaque_region, client_opaque_region);
          else
            opaque_region = mtk_region_ref (client_opaque_region);
        }

      mtk_region_intersect (opaque_region, actor_x11->shape_region);
    }
  else if (!is_maybe_transparent)
    {
      opaque_region = mtk_region_ref (actor_x11->shape_region);
    }

  surface = meta_window_actor_get_surface (META_WINDOW_ACTOR (actor_x11));
  meta_surface_actor_set_opaque_region (surface, opaque_region);
}

static void
update_frame_bounds (MetaWindowActorX11 *actor_x11)
{
  MetaWindow *window =
    meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_x11));
  MtkRegion *frame_bounds = meta_window_get_frame_bounds (window);
  g_clear_pointer (&actor_x11->frame_bounds, mtk_region_unref);

  if (frame_bounds)
    actor_x11->frame_bounds = mtk_region_copy (frame_bounds);
}

static void
update_regions (MetaWindowActorX11 *actor_x11)
{
  if (!actor_x11->needs_reshape)
    return;

  update_shape_region (actor_x11);
  update_input_region (actor_x11);
  update_opaque_region (actor_x11);

  actor_x11->needs_reshape = FALSE;
}

static void
check_needs_reshape (MetaWindowActorX11 *actor_x11)
{
  MetaWindow *window =
    meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_x11));

  if (meta_window_x11_always_update_shape (window))
    return;

  update_regions (actor_x11);
}

void
meta_window_actor_x11_update_shape (MetaWindowActorX11 *actor_x11)
{
  MetaSurfaceActor *surface =
    meta_window_actor_get_surface (META_WINDOW_ACTOR (actor_x11));

  actor_x11->needs_reshape = TRUE;

  if (meta_window_actor_is_frozen (META_WINDOW_ACTOR (actor_x11)))
    return;

  clutter_actor_queue_redraw (CLUTTER_ACTOR (surface));
}

static void
handle_updates (MetaWindowActorX11 *actor_x11)
{
  MetaSurfaceActor *surface =
    meta_window_actor_get_surface (META_WINDOW_ACTOR (actor_x11));
  MetaWindow *window;

  if (META_IS_SURFACE_ACTOR_X11 (surface) &&
      meta_surface_actor_x11_is_unredirected (META_SURFACE_ACTOR_X11 (surface)))
    return;

  window = meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_x11));
  if (meta_window_actor_is_frozen (META_WINDOW_ACTOR (actor_x11)))
    {
      /* The window is frozen due to a pending animation: we'll wait until
       * the animation finishes to repair the window.
       *
       * However, with Xwayland, we still might need to update the shape
       * region as the wl_buffer will be set to plain black on resize,
       * which causes the shadows to look bad.
       */
      if (surface && meta_window_x11_always_update_shape (window))
        check_needs_reshape (actor_x11);

      return;
    }

  if (META_IS_SURFACE_ACTOR_X11 (surface))
    {
      MetaSurfaceActorX11 *surface_x11 = META_SURFACE_ACTOR_X11 (surface);

      meta_surface_actor_x11_handle_updates (surface_x11);
    }

  if (META_IS_SURFACE_ACTOR_X11 (surface) &&
      !meta_surface_actor_x11_is_visible (META_SURFACE_ACTOR_X11 (surface)))
    return;

  update_frame_bounds (actor_x11);
  check_needs_reshape (actor_x11);
  check_needs_shadow (actor_x11);
}

static void
handle_stage_views_changed (MetaWindowActorX11 *actor_x11)
{
  ClutterActor *actor = CLUTTER_ACTOR (actor_x11);

  if (actor_x11->pending_schedule_update_now)
    {
      ClutterFrameClock *frame_clock;

      frame_clock = clutter_actor_pick_frame_clock (actor, NULL);
      if (frame_clock)
        {
          clutter_frame_clock_schedule_update_now (frame_clock);
          actor_x11->pending_schedule_update_now = FALSE;
        }
    }
}

static void
meta_window_actor_x11_before_paint (MetaWindowActor  *actor,
                                    ClutterStageView *stage_view)
{
  MetaWindowActorX11 *actor_x11 = META_WINDOW_ACTOR_X11 (actor);

  handle_updates (actor_x11);

  assign_frame_counter_to_frames (actor_x11);
}

static void
meta_window_actor_x11_paint (ClutterActor        *actor,
                             ClutterPaintContext *paint_context)
{
  MetaWindowActorX11 *actor_x11 = META_WINDOW_ACTOR_X11 (actor);
  MetaWindow *window;
  gboolean appears_focused;
  MetaShadow *shadow;

 /* This window got damage when obscured; we set up a timer
  * to send frame completion events, but since we're drawing
  * the window now (for some other reason) cancel the timer
  * and send the completion events normally */
  if (actor_x11->send_frame_messages_timer != 0)
    {
      remove_frame_messages_timer (actor_x11);
      assign_frame_counter_to_frames (actor_x11);
    }

  window = meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_x11));
  appears_focused = meta_window_appears_focused (window);
  shadow = appears_focused ? actor_x11->focused_shadow
                           : actor_x11->unfocused_shadow;

  if (shadow)
    {
      MetaShadowParams params;
      MtkRectangle shape_bounds;
      MtkRegion *clip = actor_x11->shadow_clip;
      CoglFramebuffer *framebuffer;

      get_shape_bounds (actor_x11, &shape_bounds);
      get_shadow_params (actor_x11, appears_focused, &params);

      /* The frame bounds are already subtracted from actor_x11->shadow_clip
       * if that exists.
       */
      if (!clip && clip_shadow_under_window (actor_x11))
        {
          MtkRectangle bounds;

          get_shadow_bounds (actor_x11, appears_focused, &bounds);
          clip = mtk_region_create_rectangle (&bounds);

          if (actor_x11->frame_bounds)
            mtk_region_subtract (clip, actor_x11->frame_bounds);
        }

      framebuffer = clutter_paint_context_get_framebuffer (paint_context);
      meta_shadow_paint (shadow,
                         framebuffer,
                         params.x_offset + shape_bounds.x,
                         params.y_offset + shape_bounds.y,
                         shape_bounds.width,
                         shape_bounds.height,
                         (clutter_actor_get_paint_opacity (actor) *
                          params.opacity * window->opacity) / (255 * 255),
                         clip,
                         clip_shadow_under_window (actor_x11));

      if (clip && clip != actor_x11->shadow_clip)
        mtk_region_unref (clip);
    }

  CLUTTER_ACTOR_CLASS (meta_window_actor_x11_parent_class)->paint (actor,
                                                                   paint_context);
}

static void
meta_window_actor_x11_after_paint (MetaWindowActor  *actor,
                                   ClutterStageView *stage_view)
{
  MetaWindowActorX11 *actor_x11 = META_WINDOW_ACTOR_X11 (actor);
  MetaSyncCounter *sync_counter;
  MetaWindowDrag *window_drag;
  MetaWindow *window;

  actor_x11->repaint_scheduled = FALSE;

  if (meta_window_actor_is_destroyed (actor))
    return;

  window = meta_window_actor_get_meta_window (actor);

  /* If the window had damage, but wasn't actually redrawn because
   * it is obscured, we should wait until timer expiration before
   * sending _NET_WM_FRAME_* messages.
   */
  if (actor_x11->send_frame_messages_timer == 0)
    {
      sync_counter = meta_window_x11_get_sync_counter (window);
      meta_sync_counter_send_frame_drawn (sync_counter);

      if (window->frame)
        {
          sync_counter = meta_frame_get_sync_counter (window->frame);
          meta_sync_counter_send_frame_drawn (sync_counter);
        }
    }

  /* This is for Xwayland, and a no-op on plain Xorg */
  if (meta_window_x11_should_thaw_after_paint (window))
    {
      meta_window_x11_thaw_commits (window);
      meta_window_x11_set_thaw_after_paint (window, FALSE);
    }

  window_drag = meta_compositor_get_current_window_drag (window->display->compositor);

  if (window_drag &&
      window == meta_window_drag_get_window (window_drag) &&
      meta_grab_op_is_resizing (meta_window_drag_get_grab_op (window_drag)))
    {
      /* This means we are ready for another configure;
       * no pointer round trip here, to keep in sync */
      meta_window_x11_check_update_resize (window);
    }
}

static gboolean
meta_window_actor_x11_get_paint_volume (ClutterActor       *actor,
                                        ClutterPaintVolume *volume)
{
  MetaWindowActorX11 *actor_x11 = META_WINDOW_ACTOR_X11 (actor);
  MetaWindow *window;
  gboolean appears_focused;
  MetaSurfaceActor *surface;

  /* The paint volume is computed before paint functions are called
   * so our bounds might not be updated yet. Force an update. */
  handle_updates (actor_x11);

  window = meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_x11));
  appears_focused = meta_window_appears_focused (window);
  if (appears_focused ? actor_x11->focused_shadow : actor_x11->unfocused_shadow)
    {
      MtkRectangle shadow_bounds;
      ClutterActorBox shadow_box;

      /* We could compute an full clip region as we do for the window
       * texture, but the shadow is relatively cheap to draw, and
       * a little more complex to clip, so we just catch the case where
       * the shadow is completely obscured and doesn't need to be drawn
       * at all.
       */

      get_shadow_bounds (actor_x11, appears_focused, &shadow_bounds);
      shadow_box.x1 = shadow_bounds.x;
      shadow_box.x2 = shadow_bounds.x + shadow_bounds.width;
      shadow_box.y1 = shadow_bounds.y;
      shadow_box.y2 = shadow_bounds.y + shadow_bounds.height;

      clutter_paint_volume_union_box (volume, &shadow_box);
    }

  surface = meta_window_actor_get_surface (META_WINDOW_ACTOR (actor_x11));
  if (surface)
    {
      ClutterActor *surface_actor = CLUTTER_ACTOR (surface);
      g_autoptr (ClutterPaintVolume) child_volume = NULL;

      child_volume = clutter_actor_get_transformed_paint_volume (surface_actor,
                                                                 actor);
      if (!child_volume)
        return FALSE;

      clutter_paint_volume_union (volume, child_volume);
    }

  return TRUE;
}

static void
meta_window_actor_x11_queue_destroy (MetaWindowActor *actor)
{
  MetaWindowActorX11 *actor_x11 = META_WINDOW_ACTOR_X11 (actor);

  if (actor_x11->send_frame_messages_timer != 0)
    remove_frame_messages_timer (actor_x11);
}

static void
meta_window_actor_x11_set_frozen (MetaWindowActor *actor,
                                  gboolean         frozen)
{
  MetaWindowActorX11 *actor_x11 = META_WINDOW_ACTOR_X11 (actor);
  MetaWindow *window = meta_window_actor_get_meta_window (actor);

  if (actor_x11->is_frozen == frozen)
    return;

  actor_x11->is_frozen = frozen;
  meta_surface_actor_set_frozen (meta_window_actor_get_surface (actor), frozen);

  if (frozen)
    meta_window_x11_freeze_commits (window);
  else
    meta_window_x11_thaw_commits (window);
}

static void
meta_window_actor_x11_update_regions (MetaWindowActor *actor)
{
  update_regions (META_WINDOW_ACTOR_X11 (actor));
}

static gboolean
meta_window_actor_x11_can_freeze_commits (MetaWindowActor *actor)
{
  ClutterActor *clutter_actor = CLUTTER_ACTOR (actor);

  return clutter_actor_is_mapped (clutter_actor);
}

static gboolean
meta_window_actor_x11_is_single_surface_actor (MetaWindowActor *actor)
{
  return clutter_actor_get_n_children (CLUTTER_ACTOR (actor)) == 1;
}

static void
meta_window_actor_x11_sync_geometry (MetaWindowActor *actor)
{
}

static void
meta_window_actor_x11_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  MetaWindowActorX11 *actor_x11 = META_WINDOW_ACTOR_X11 (object);

  switch (prop_id)
    {
    case PROP_SHADOW_MODE:
      {
        MetaShadowMode newv = g_value_get_enum (value);

        if (newv == actor_x11->shadow_mode)
          return;

        actor_x11->shadow_mode = newv;

        invalidate_shadow (actor_x11);
      }
      break;
    case PROP_SHADOW_CLASS:
      {
        const char *newv = g_value_get_string (value);

        if (g_strcmp0 (newv, actor_x11->shadow_class) == 0)
          return;

        g_free (actor_x11->shadow_class);
        actor_x11->shadow_class = g_strdup (newv);

        invalidate_shadow (actor_x11);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_window_actor_x11_get_property (GObject      *object,
                                    guint         prop_id,
                                    GValue       *value,
                                    GParamSpec   *pspec)
{
  MetaWindowActorX11 *actor_x11 = META_WINDOW_ACTOR_X11 (object);

  switch (prop_id)
    {
    case PROP_SHADOW_MODE:
      g_value_set_enum (value, actor_x11->shadow_mode);
      break;
    case PROP_SHADOW_CLASS:
      g_value_set_string (value, actor_x11->shadow_class);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_window_actor_x11_constructed (GObject *object)
{
  MetaWindowActorX11 *actor_x11 = META_WINDOW_ACTOR_X11 (object);
  MetaWindowActor *actor = META_WINDOW_ACTOR (actor_x11);
  MetaWindow *window = meta_window_actor_get_meta_window (actor);
  MetaSyncCounter *sync_counter = meta_window_x11_get_sync_counter (window);

  /*
   * Start off with an empty shape region to maintain the invariant that it's
   * always set.
   */
  actor_x11->shape_region = mtk_region_create ();

  G_OBJECT_CLASS (meta_window_actor_x11_parent_class)->constructed (object);

  /* If a window doesn't start off with updates frozen, we should
   * we should send a _NET_WM_FRAME_DRAWN immediately after the first drawn.
   */
  if (sync_counter->extended_sync_request_counter &&
      !meta_window_updates_are_frozen (window))
    {
      meta_sync_counter_queue_frame_drawn (sync_counter);
      meta_window_actor_queue_frame_drawn (actor, FALSE);
    }
}

static void
meta_window_actor_x11_cull_unobscured (MetaCullable *cullable,
                                       MtkRegion    *unobscured_region)
{
  meta_cullable_cull_unobscured_children (cullable, unobscured_region);
}

static void
meta_window_actor_x11_cull_redraw_clip (MetaCullable *cullable,
                                        MtkRegion    *clip_region)
{
  MetaWindowActorX11 *self = META_WINDOW_ACTOR_X11 (cullable);

  meta_cullable_cull_redraw_clip_children (cullable, clip_region);

  set_clip_region_beneath (self, clip_region);
}

static void
cullable_iface_init (MetaCullableInterface *iface)
{
  iface->cull_unobscured = meta_window_actor_x11_cull_unobscured;
  iface->cull_redraw_clip = meta_window_actor_x11_cull_redraw_clip;
}

static void
meta_window_actor_x11_dispose (GObject *object)
{
  MetaWindowActorX11 *actor_x11 = META_WINDOW_ACTOR_X11 (object);
  MetaSurfaceActor *surface_actor;

  g_clear_signal_handler (&actor_x11->shadow_factory_changed_handler_id,
                          actor_x11->shadow_factory);

  if (actor_x11->send_frame_messages_timer != 0)
    remove_frame_messages_timer (actor_x11);

  surface_actor = meta_window_actor_get_surface (META_WINDOW_ACTOR (actor_x11));
  if (surface_actor)
    {
      g_clear_signal_handler (&actor_x11->repaint_scheduled_id, surface_actor);
      g_clear_signal_handler (&actor_x11->size_changed_id, surface_actor);

      clutter_actor_remove_child (CLUTTER_ACTOR (object),
                                  CLUTTER_ACTOR (surface_actor));
    }

  g_clear_pointer (&actor_x11->shape_region, mtk_region_unref);
  g_clear_pointer (&actor_x11->shadow_clip, mtk_region_unref);
  g_clear_pointer (&actor_x11->frame_bounds, mtk_region_unref);

  g_clear_pointer (&actor_x11->shadow_class, g_free);
  g_clear_pointer (&actor_x11->focused_shadow, meta_shadow_unref);
  g_clear_pointer (&actor_x11->unfocused_shadow, meta_shadow_unref);
  g_clear_pointer (&actor_x11->shadow_shape, meta_window_shape_unref);

  G_OBJECT_CLASS (meta_window_actor_x11_parent_class)->dispose (object);
}

static void
meta_window_actor_x11_class_init (MetaWindowActorX11Class *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  MetaWindowActorClass *window_actor_class = META_WINDOW_ACTOR_CLASS (klass);
  GParamSpec *pspec;

  window_actor_class->frame_complete = meta_window_actor_x11_frame_complete;
  window_actor_class->get_scanout_candidate = meta_window_actor_x11_get_scanout_candidate;
  window_actor_class->assign_surface_actor = meta_window_actor_x11_assign_surface_actor;
  window_actor_class->queue_frame_drawn = meta_window_actor_x11_queue_frame_drawn;
  window_actor_class->before_paint = meta_window_actor_x11_before_paint;
  window_actor_class->after_paint = meta_window_actor_x11_after_paint;
  window_actor_class->queue_destroy = meta_window_actor_x11_queue_destroy;
  window_actor_class->set_frozen = meta_window_actor_x11_set_frozen;
  window_actor_class->update_regions = meta_window_actor_x11_update_regions;
  window_actor_class->can_freeze_commits = meta_window_actor_x11_can_freeze_commits;
  window_actor_class->sync_geometry = meta_window_actor_x11_sync_geometry;
  window_actor_class->is_single_surface_actor = meta_window_actor_x11_is_single_surface_actor;

  actor_class->paint = meta_window_actor_x11_paint;
  actor_class->get_paint_volume = meta_window_actor_x11_get_paint_volume;

  object_class->constructed = meta_window_actor_x11_constructed;
  object_class->set_property = meta_window_actor_x11_set_property;
  object_class->get_property = meta_window_actor_x11_get_property;
  object_class->dispose = meta_window_actor_x11_dispose;

  pspec = g_param_spec_enum ("shadow-mode", NULL, NULL,
                             META_TYPE_SHADOW_MODE,
                             META_SHADOW_MODE_AUTO,
                             G_PARAM_READWRITE);

  g_object_class_install_property (object_class,
                                   PROP_SHADOW_MODE,
                                   pspec);

  pspec = g_param_spec_string ("shadow-class", NULL, NULL,
                               NULL,
                               G_PARAM_READWRITE);

  g_object_class_install_property (object_class,
                                   PROP_SHADOW_CLASS,
                                   pspec);
}

static void
meta_window_actor_x11_init (MetaWindowActorX11 *self)
{
  /* We do this now since we might be going right back into the frozen state. */
  g_signal_connect (self, "thawed", G_CALLBACK (handle_updates), NULL);

  g_signal_connect (self, "stage-views-changed",
                    G_CALLBACK (handle_stage_views_changed), NULL);

  self->shadow_factory = meta_shadow_factory_get_default ();
  self->shadow_factory_changed_handler_id =
    g_signal_connect_swapped (self->shadow_factory,
                              "changed",
                              G_CALLBACK (invalidate_shadow),
                              self);
}
