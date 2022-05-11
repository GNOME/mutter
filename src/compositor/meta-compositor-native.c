/*
 * Copyright (C) 2019 Red Hat Inc.
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

#include "compositor/meta-compositor-native.h"

#include "backends/meta-logical-monitor.h"
#include "backends/native/meta-crtc-kms.h"
#include "compositor/meta-surface-actor-wayland.h"

struct _MetaCompositorNative
{
  MetaCompositorServer parent;

  MetaWaylandSurface *current_scanout_candidate;
};

G_DEFINE_TYPE (MetaCompositorNative, meta_compositor_native,
               META_TYPE_COMPOSITOR_SERVER)

#ifdef HAVE_WAYLAND

static MetaRendererView *
get_window_view (MetaRenderer *renderer,
                 MetaWindow   *window)
{
  GList *l;
  MetaRendererView *view_found = NULL;

  for (l = meta_renderer_get_views (renderer); l; l = l->next)
    {
      ClutterStageView *stage_view = l->data;
      MetaRectangle view_layout;

      clutter_stage_view_get_layout (stage_view, &view_layout);

      if (meta_rectangle_equal (&window->buffer_rect,
                                &view_layout))
        {
          if (view_found)
            return NULL;
          view_found = META_RENDERER_VIEW (stage_view);
        }
    }

  return view_found;
}

static void
maybe_assign_primary_plane (MetaCompositor *compositor)
{
  MetaCompositorNative *compositor_native = META_COMPOSITOR_NATIVE (compositor);
  MetaBackend *backend = meta_get_backend ();
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  MetaWindowActor *window_actor;
  MetaWindow *window;
  MetaRendererView *view;
  MetaCrtc *crtc;
  CoglFramebuffer *framebuffer;
  CoglOnscreen *onscreen;
  MetaSurfaceActor *surface_actor;
  MetaSurfaceActorWayland *surface_actor_wayland;
  MetaWaylandSurface *surface;
  int geometry_scale;
  MetaWaylandSurface *old_candidate =
    compositor_native->current_scanout_candidate;
  MetaWaylandSurface *new_candidate = NULL;
  g_autoptr (CoglScanout) scanout = NULL;

  if (meta_compositor_is_unredirect_inhibited (compositor))
    goto done;

  window_actor = meta_compositor_get_top_window_actor (compositor);
  if (!window_actor)
    goto done;

  if (meta_window_actor_effect_in_progress (window_actor))
    goto done;

  if (clutter_actor_has_transitions (CLUTTER_ACTOR (window_actor)))
    goto done;

  window = meta_window_actor_get_meta_window (window_actor);
  if (!window)
    goto done;

  view = get_window_view (renderer, window);
  if (!view)
    goto done;

  crtc = meta_renderer_view_get_crtc (META_RENDERER_VIEW (view));
  if (!META_IS_CRTC_KMS (crtc))
    goto done;

  framebuffer = clutter_stage_view_get_framebuffer (CLUTTER_STAGE_VIEW (view));
  if (!COGL_IS_ONSCREEN (framebuffer))
    goto done;

  surface_actor = meta_window_actor_get_scanout_candidate (window_actor);
  if (!surface_actor)
    goto done;

  surface_actor_wayland = META_SURFACE_ACTOR_WAYLAND (surface_actor);
  surface = meta_surface_actor_wayland_get_surface (surface_actor_wayland);
  if (!surface)
    goto done;

  geometry_scale = meta_window_actor_get_geometry_scale (window_actor);
  if (!meta_wayland_surface_can_scanout_untransformed (surface, view,
                                                       geometry_scale))
    goto done;

  new_candidate = surface;

  onscreen = COGL_ONSCREEN (framebuffer);
  scanout = meta_surface_actor_wayland_try_acquire_scanout (surface_actor_wayland,
                                                            onscreen);
  if (!scanout)
    goto done;

  clutter_stage_view_assign_next_scanout (CLUTTER_STAGE_VIEW (view), scanout);

done:

  if (old_candidate && old_candidate != new_candidate)
    {
      meta_wayland_surface_set_scanout_candidate (old_candidate, NULL);
      g_clear_weak_pointer (&compositor_native->current_scanout_candidate);
    }

  if (new_candidate)
    {
      meta_wayland_surface_set_scanout_candidate (surface, crtc);
      g_set_weak_pointer (&compositor_native->current_scanout_candidate,
                          surface);
    }
}
#endif /* HAVE_WAYLAND */

static void
meta_compositor_native_before_paint (MetaCompositor   *compositor,
                                     ClutterStageView *stage_view)
{
  MetaCompositorClass *parent_class;

#ifdef HAVE_WAYLAND
  maybe_assign_primary_plane (compositor);
#endif

  parent_class = META_COMPOSITOR_CLASS (meta_compositor_native_parent_class);
  parent_class->before_paint (compositor, stage_view);
}

MetaCompositorNative *
meta_compositor_native_new (MetaDisplay *display,
                            MetaBackend *backend)
{
  return g_object_new (META_TYPE_COMPOSITOR_NATIVE,
                       "display", display,
                       "backend", backend,
                       NULL);
}

static void
meta_compositor_native_finalize (GObject *object)
{
  MetaCompositorNative *compositor_native = META_COMPOSITOR_NATIVE (object);

  g_clear_weak_pointer (&compositor_native->current_scanout_candidate);

  G_OBJECT_CLASS (meta_compositor_native_parent_class)->finalize (object);
}

static void
meta_compositor_native_init (MetaCompositorNative *compositor_native)
{
}

static void
meta_compositor_native_class_init (MetaCompositorNativeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaCompositorClass *compositor_class = META_COMPOSITOR_CLASS (klass);

  object_class->finalize = meta_compositor_native_finalize;

  compositor_class->before_paint = meta_compositor_native_before_paint;
}
