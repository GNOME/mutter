/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2022-2023 Dor Askayo
 * Copyright (C) 2024 GNOME Foundation Inc.
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
 *     Dor Askayo <dor.askayo@gmail.com>
 */

#include "config.h"

#include "compositor/meta-compositor-view-native.h"

#include "backends/meta-crtc.h"
#include "backends/native/meta-crtc-kms.h"
#include "backends/native/meta-onscreen-native.h"
#include "clutter/clutter.h"
#include "compositor/compositor-private.h"
#include "compositor/meta-window-actor-private.h"
#include "core/window-private.h"

#ifdef HAVE_WAYLAND
#include "compositor/meta-surface-actor-wayland.h"
#include "wayland/meta-wayland-surface-private.h"
#endif /* HAVE_WAYLAND */

static void update_frame_sync_surface (MetaCompositorViewNative *view_native,
                                       MetaSurfaceActor         *surface_actor);

struct _MetaCompositorViewNative
{
  MetaCompositorView parent;

#ifdef HAVE_WAYLAND
  MetaWaylandSurface *scanout_candidate;
#endif /* HAVE_WAYLAND */

  MetaSurfaceActor *frame_sync_surface;

  gulong frame_sync_surface_repaint_scheduled_id;
  gulong frame_sync_surface_update_scheduled_id;
  gulong frame_sync_surface_is_frozen_changed_id;
  gulong frame_sync_surface_destroy_id;
};

G_DEFINE_TYPE (MetaCompositorViewNative, meta_compositor_view_native,
               META_TYPE_COMPOSITOR_VIEW)

static void
maybe_schedule_update_now (MetaCompositorViewNative *view_native)
{
  MetaCompositorView *compositor_view = META_COMPOSITOR_VIEW (view_native);
  ClutterStageView *stage_view;
  CoglFramebuffer *framebuffer;

  stage_view = meta_compositor_view_get_stage_view (compositor_view);

  framebuffer = clutter_stage_view_get_onscreen (stage_view);
  if (!META_IS_ONSCREEN_NATIVE (framebuffer))
    return;

  if (meta_onscreen_native_is_frame_sync_enabled (META_ONSCREEN_NATIVE (framebuffer)))
    {
      ClutterFrameClock *frame_clock;

      frame_clock = clutter_stage_view_get_frame_clock (stage_view);
      if (!frame_clock)
        return;

      clutter_frame_clock_schedule_update_now (frame_clock);
    }
}

static void
on_frame_sync_surface_repaint_scheduled (MetaSurfaceActor         *surface_actor,
                                         MetaCompositorViewNative *view_native)
{
  maybe_schedule_update_now (view_native);
}

static void
on_frame_sync_surface_update_scheduled (MetaSurfaceActor         *surface_actor,
                                        MetaCompositorViewNative *view_native)
{
  maybe_schedule_update_now (view_native);
}

static void
on_frame_sync_surface_is_frozen_changed (MetaSurfaceActor         *surface_actor,
                                         GParamSpec               *pspec,
                                         MetaCompositorViewNative *view_native)
{
  if (meta_surface_actor_is_frozen (surface_actor))
    update_frame_sync_surface (view_native, NULL);
}

static void
on_frame_sync_surface_destroyed (MetaSurfaceActor         *surface_actor,
                                 MetaCompositorViewNative *view_native)
{
  update_frame_sync_surface (view_native, NULL);
}

#ifdef HAVE_WAYLAND
static void
update_scanout_candidate (MetaCompositorViewNative *view_native,
                          MetaWaylandSurface       *surface,
                          MetaCrtc                 *crtc)
{
  if (view_native->scanout_candidate &&
      view_native->scanout_candidate != surface)
    {
      meta_wayland_surface_set_scanout_candidate (view_native->scanout_candidate,
                                                  NULL);
      g_clear_weak_pointer (&view_native->scanout_candidate);
    }

  if (surface)
    {
      meta_wayland_surface_set_scanout_candidate (surface, crtc);
      g_set_weak_pointer (&view_native->scanout_candidate,
                          surface);
    }
}

static gboolean
find_scanout_candidate (MetaCompositorView  *compositor_view,
                        MetaCompositor      *compositor,
                        MetaCrtc           **crtc_out,
                        CoglOnscreen       **onscreen_out,
                        MetaWaylandSurface **surface_out)
{
  ClutterStageView *stage_view =
    meta_compositor_view_get_stage_view (compositor_view);
  MetaStageView *view = META_STAGE_VIEW (stage_view);
  MetaRendererView *renderer_view = META_RENDERER_VIEW (stage_view);
  MetaBackend *backend = meta_compositor_get_backend (compositor);
  MetaCursorTracker *cursor_tracker =
    meta_backend_get_cursor_tracker (backend);
  CoglTexture *cursor_sprite;
  MetaCrtc *crtc;
  CoglFramebuffer *framebuffer;
  MetaWindowActor *window_actor;
  MtkRectangle view_rect;
  ClutterActorBox actor_box;
  MetaSurfaceActor *surface_actor;
  MetaSurfaceActorWayland *surface_actor_wayland;
  MetaWaylandSurface *surface;

  if (meta_compositor_is_unredirect_inhibited (compositor))
    {
      meta_topic (META_DEBUG_RENDER,
                  "No direct scanout candidate: unredirect inhibited");
      return FALSE;
    }

  clutter_stage_view_get_layout (stage_view, &view_rect);

  cursor_sprite = meta_cursor_tracker_get_sprite (cursor_tracker);
  if (cursor_sprite &&
      meta_cursor_tracker_get_pointer_visible (cursor_tracker) &&
      !meta_stage_view_is_cursor_overlay_inhibited (view))
    {
      graphene_rect_t graphene_view_rect;
      graphene_rect_t cursor_rect;
      graphene_point_t position;
      float scale;
      int hotspot_x;
      int hotspot_y;

      meta_cursor_tracker_get_pointer (cursor_tracker, &position, NULL);
      meta_cursor_tracker_get_hot (cursor_tracker, &hotspot_x, &hotspot_y);

      scale = (clutter_stage_view_get_scale (stage_view) *
               meta_cursor_tracker_get_scale (cursor_tracker));

      graphene_rect_init (&cursor_rect,
                          position.x - (hotspot_x * scale),
                          position.y - (hotspot_y * scale),
                          cogl_texture_get_width (cursor_sprite) * scale,
                          cogl_texture_get_height (cursor_sprite) * scale);

      graphene_view_rect = mtk_rectangle_to_graphene_rect (&view_rect);
      if (graphene_rect_intersection (&graphene_view_rect,
                                      &cursor_rect,
                                      NULL))
        {
          meta_topic (META_DEBUG_RENDER,
                      "No direct scanout candidate: using software cursor");
          return FALSE;
        }
    }

  crtc = meta_renderer_view_get_crtc (renderer_view);
  if (!META_IS_CRTC_KMS (crtc))
    {
      meta_topic (META_DEBUG_RENDER,
                  "No direct scanout candidate: no KMS CRTC");
      return FALSE;
    }

  framebuffer = clutter_stage_view_get_onscreen (stage_view);
  if (!COGL_IS_ONSCREEN (framebuffer))
    {
      meta_topic (META_DEBUG_RENDER,
                  "No direct scanout candidate: no onscreen framebuffer");
      return FALSE;
    }

  if (clutter_stage_view_has_shadowfb (stage_view))
    {
      meta_topic (META_DEBUG_RENDER,
                  "No direct scanout candidate: stage-view has shadowfb");
      return FALSE;
    }

  window_actor = meta_compositor_view_get_top_window_actor (compositor_view);
  if (!window_actor)
    {
      meta_topic (META_DEBUG_RENDER,
                  "No direct scanout candidate: no top window actor");
      return FALSE;
    }

  if (meta_window_actor_effect_in_progress (window_actor))
    {
      meta_topic (META_DEBUG_RENDER,
                  "No direct scanout candidate: window-actor effects in progress");
      return FALSE;
    }

  if (clutter_actor_has_transitions (CLUTTER_ACTOR (window_actor)))
    {
      meta_topic (META_DEBUG_RENDER,
                  "No direct scanout candidate: window-actor has transition");
      return FALSE;
    }

  if (!clutter_actor_get_paint_box (CLUTTER_ACTOR (window_actor),
                                    &actor_box))
    {
      meta_topic (META_DEBUG_RENDER,
                  "No direct scanout candidate: no window actor paint-box");
      return FALSE;
    }

  if (!G_APPROX_VALUE (actor_box.x1, view_rect.x,
                       CLUTTER_COORDINATE_EPSILON) ||
      !G_APPROX_VALUE (actor_box.y1, view_rect.y,
                       CLUTTER_COORDINATE_EPSILON) ||
      !G_APPROX_VALUE (actor_box.x2, view_rect.x + view_rect.width,
                       CLUTTER_COORDINATE_EPSILON) ||
      !G_APPROX_VALUE (actor_box.y2, view_rect.y + view_rect.height,
                       CLUTTER_COORDINATE_EPSILON))
    {
      meta_topic (META_DEBUG_RENDER,
                  "No direct scanout candidate: paint-box (%f,%f,%f,%f) does "
                  "not match stage-view layout (%d,%d,%d,%d)",
                  actor_box.x1, actor_box.y1,
                  actor_box.x2 - actor_box.x1, actor_box.y2 - actor_box.y1,
                  view_rect.x, view_rect.y, view_rect.width, view_rect.height);
      return FALSE;
    }

  surface_actor = meta_window_actor_get_scanout_candidate (window_actor);
  if (!surface_actor)
    {
      meta_topic (META_DEBUG_RENDER,
                  "No direct scanout candidate: window-actor has no scanout "
                  "candidate");
      return FALSE;
    }

  if (meta_surface_actor_is_effectively_obscured (surface_actor))
    {
      meta_topic (META_DEBUG_RENDER,
                  "No direct scanout candidate: surface-actor is obscured");
      return FALSE;
    }

  surface_actor_wayland = META_SURFACE_ACTOR_WAYLAND (surface_actor);
  surface = meta_surface_actor_wayland_get_surface (surface_actor_wayland);
  if (!surface)
    {
      meta_topic (META_DEBUG_RENDER,
                  "No direct scanout candidate: no surface");
      return FALSE;
    }

  *crtc_out = crtc;
  *onscreen_out = COGL_ONSCREEN (framebuffer);
  *surface_out = surface;

  return TRUE;
}

static void
try_assign_next_scanout (MetaCompositorView *compositor_view,
                         CoglOnscreen       *onscreen,
                         MetaWaylandSurface *surface)
{
  ClutterStageView *stage_view;
  g_autoptr (CoglScanout) scanout = NULL;

  stage_view = meta_compositor_view_get_stage_view (compositor_view);
  scanout = meta_wayland_surface_try_acquire_scanout (surface,
                                                      onscreen,
                                                      stage_view);
  if (!scanout)
    {
      meta_topic (META_DEBUG_RENDER,
                  "Could not acquire scanout");
      return;
    }

  clutter_stage_view_assign_next_scanout (stage_view, scanout);
}

void
meta_compositor_view_native_maybe_assign_scanout (MetaCompositorViewNative *view_native,
                                                  MetaCompositor           *compositor)
{
  MetaCompositorView *compositor_view = META_COMPOSITOR_VIEW (view_native);
  MetaCrtc *crtc = NULL;
  CoglOnscreen *onscreen = NULL;
  MetaWaylandSurface *surface = NULL;
  gboolean candidate_found;

  candidate_found = find_scanout_candidate (compositor_view,
                                            compositor,
                                            &crtc,
                                            &onscreen,
                                            &surface);
  if (candidate_found)
    {
      try_assign_next_scanout (compositor_view,
                               onscreen,
                               surface);
    }

  update_scanout_candidate (view_native, surface, crtc);
}
#endif /* HAVE_WAYLAND */

static MetaSurfaceActor *
find_frame_sync_candidate (MetaCompositorView *compositor_view,
                           MetaCompositor     *compositor)
{
  MetaWindowActor *window_actor;
  MetaWindow *window;
  ClutterStageView *stage_view;
  MtkRectangle view_layout;
  MetaSurfaceActor *surface_actor;

  if (meta_compositor_is_unredirect_inhibited (compositor))
    {
      meta_topic (META_DEBUG_RENDER,
                  "No frame sync candidate: unredirect inhibited");
      return NULL;
    }

  window_actor =
    meta_compositor_view_get_top_window_actor (compositor_view);
  if (!window_actor)
    {
      meta_topic (META_DEBUG_RENDER,
                  "No frame sync candidate: no top window actor");
      return NULL;
    }

  if (meta_window_actor_is_frozen (window_actor))
    {
      meta_topic (META_DEBUG_RENDER,
                  "No frame sync candidate: window-actor is frozen");
      return NULL;
    }

  if (meta_window_actor_effect_in_progress (window_actor))
    {
      meta_topic (META_DEBUG_RENDER,
                  "No frame sync candidate: window-actor effects in progress");
      return NULL;
    }

  if (clutter_actor_has_transitions (CLUTTER_ACTOR (window_actor)))
    {
      meta_topic (META_DEBUG_RENDER,
                  "No frame sync candidate: window-actor has transition");
      return NULL;
    }

  window = meta_window_actor_get_meta_window (window_actor);
  if (!window)
    {
      meta_topic (META_DEBUG_RENDER,
                  "No frame sync candidate: no meta-window");
      return NULL;
    }

  stage_view = meta_compositor_view_get_stage_view (compositor_view);

  clutter_stage_view_get_layout (stage_view, &view_layout);

  if (!meta_window_geometry_contains_rect (window, &view_layout))
    {
      meta_topic (META_DEBUG_RENDER,
                  "No frame sync candidate: stage-view layout not covered "
                  "by meta-window frame");
      return NULL;
    }

  surface_actor = meta_window_actor_get_scanout_candidate (window_actor);
  if (!surface_actor)
    {
      meta_topic (META_DEBUG_RENDER,
                  "No frame sync candidate: window-actor has no scanout candidate");
      return NULL;
    }

  if (meta_surface_actor_is_frozen (surface_actor))
    {
      meta_topic (META_DEBUG_RENDER,
                  "No frame sync candidate: surface-actor is frozen");
      return NULL;
    }

  if (!meta_surface_actor_contains_rect (surface_actor,
                                         &view_layout))
    {
      meta_topic (META_DEBUG_RENDER,
                  "No frame sync candidate: stage-view layout not covered "
                  "by surface-actor");
      return NULL;
    }

  return surface_actor;
}

static void
update_frame_sync_surface (MetaCompositorViewNative *view_native,
                           MetaSurfaceActor         *surface_actor)
{
  MetaCompositorView *compositor_view =
    META_COMPOSITOR_VIEW (view_native);
  ClutterStageView *stage_view;
  CoglFramebuffer *framebuffer;

  g_clear_signal_handler (&view_native->frame_sync_surface_repaint_scheduled_id,
                          view_native->frame_sync_surface);
  g_clear_signal_handler (&view_native->frame_sync_surface_update_scheduled_id,
                          view_native->frame_sync_surface);
  g_clear_signal_handler (&view_native->frame_sync_surface_is_frozen_changed_id,
                          view_native->frame_sync_surface);
  g_clear_signal_handler (&view_native->frame_sync_surface_destroy_id,
                          view_native->frame_sync_surface);

  if (surface_actor)
    {
      view_native->frame_sync_surface_repaint_scheduled_id =
        g_signal_connect (surface_actor, "repaint-scheduled",
                          G_CALLBACK (on_frame_sync_surface_repaint_scheduled),
                          view_native);
      view_native->frame_sync_surface_update_scheduled_id =
        g_signal_connect (surface_actor, "update-scheduled",
                          G_CALLBACK (on_frame_sync_surface_update_scheduled),
                          view_native);
      view_native->frame_sync_surface_is_frozen_changed_id =
        g_signal_connect (surface_actor,
                          "notify::is-frozen",
                          G_CALLBACK (on_frame_sync_surface_is_frozen_changed),
                          view_native);
      view_native->frame_sync_surface_destroy_id =
        g_signal_connect (surface_actor, "destroy",
                          G_CALLBACK (on_frame_sync_surface_destroyed),
                          view_native);
    }

  view_native->frame_sync_surface = surface_actor;

  stage_view = meta_compositor_view_get_stage_view (compositor_view);

  framebuffer = clutter_stage_view_get_onscreen (stage_view);
  if (!META_IS_ONSCREEN_NATIVE (framebuffer))
    return;

  meta_onscreen_native_request_frame_sync (META_ONSCREEN_NATIVE (framebuffer),
                                           surface_actor != NULL);
}

void
meta_compositor_view_native_maybe_update_frame_sync_surface (MetaCompositorViewNative *view_native,
                                                             MetaCompositor           *compositor)
{
  MetaCompositorView *compositor_view = META_COMPOSITOR_VIEW (view_native);
  MetaSurfaceActor *surface_actor;

  surface_actor = find_frame_sync_candidate (compositor_view,
                                             compositor);

  if (G_LIKELY (surface_actor == view_native->frame_sync_surface))
    return;

  update_frame_sync_surface (view_native,
                             surface_actor);
}

MetaCompositorViewNative *
meta_compositor_view_native_new (ClutterStageView *stage_view)
{
  g_assert (stage_view != NULL);

  return g_object_new (META_TYPE_COMPOSITOR_VIEW_NATIVE,
                       "stage-view", stage_view,
                       NULL);
}

static void
meta_compositor_view_native_dispose (GObject *object)
{
  MetaCompositorViewNative *view_native = META_COMPOSITOR_VIEW_NATIVE (object);

  g_clear_signal_handler (&view_native->frame_sync_surface_repaint_scheduled_id,
                          view_native->frame_sync_surface);
  g_clear_signal_handler (&view_native->frame_sync_surface_update_scheduled_id,
                          view_native->frame_sync_surface);
  g_clear_signal_handler (&view_native->frame_sync_surface_destroy_id,
                          view_native->frame_sync_surface);
  g_clear_signal_handler (&view_native->frame_sync_surface_is_frozen_changed_id,
                          view_native->frame_sync_surface);
  view_native->frame_sync_surface = NULL;

  G_OBJECT_CLASS (meta_compositor_view_native_parent_class)->dispose (object);
}

static void
meta_compositor_view_native_finalize (GObject *object)
{
#ifdef HAVE_WAYLAND
  MetaCompositorViewNative *view_native = META_COMPOSITOR_VIEW_NATIVE (object);

  g_clear_weak_pointer (&view_native->scanout_candidate);
#endif /* HAVE_WAYLAND */

  G_OBJECT_CLASS (meta_compositor_view_native_parent_class)->finalize (object);
}

static void
meta_compositor_view_native_class_init (MetaCompositorViewNativeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_compositor_view_native_dispose;
  object_class->finalize = meta_compositor_view_native_finalize;
}

static void
meta_compositor_view_native_init (MetaCompositorViewNative *view_native)
{
}
