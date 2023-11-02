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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include "backends/meta-dnd-private.h"
#include "compositor/meta-compositor-server.h"
#include "compositor/meta-compositor-view.h"
#include "core/display-private.h"
#include "meta/meta-wayland-compositor.h"
#include "wayland/meta-wayland.h"

G_DEFINE_TYPE (MetaCompositorServer, meta_compositor_server, META_TYPE_COMPOSITOR)

static gboolean
meta_compositor_server_manage (MetaCompositor  *compositor,
                               GError         **error)
{
  return TRUE;
}

static int64_t
meta_compositor_server_monotonic_to_high_res_xserver_time (MetaCompositor *compositor,
                                                           int64_t         monotonic_time_us)
{
  return meta_translate_to_high_res_xserver_time (monotonic_time_us);
}

#ifdef HAVE_WAYLAND
static MetaWaylandCompositor *
wayland_compositor_from_display (MetaDisplay *display)
{
  MetaContext *context = meta_display_get_context (display);

  return meta_context_get_wayland_compositor (context);
}
#endif

static void
meta_compositor_server_grab_begin (MetaCompositor *compositor)
{
  MetaDisplay *display = meta_compositor_get_display (compositor);
#ifdef HAVE_WAYLAND
  MetaWaylandCompositor *wayland_compositor =
    wayland_compositor_from_display (display);
#endif

#ifdef HAVE_WAYLAND
  meta_wayland_compositor_sync_focus (wayland_compositor);
#endif
  meta_display_cancel_touch (display);

#ifdef HAVE_WAYLAND
  meta_dnd_wayland_handle_begin_modal (compositor);
#endif
}

static void
meta_compositor_server_grab_end (MetaCompositor *compositor)
{
#ifdef HAVE_WAYLAND
  MetaDisplay *display = meta_compositor_get_display (compositor);;
  MetaWaylandCompositor *wayland_compositor =
    wayland_compositor_from_display (display);

  meta_dnd_wayland_handle_end_modal (compositor);
  meta_wayland_compositor_sync_focus (wayland_compositor);
#endif
}

static MetaCompositorView *
meta_compositor_server_create_view (MetaCompositor   *compositor,
                                    ClutterStageView *stage_view)
{
  return meta_compositor_view_new (stage_view);
}

MetaCompositorServer *
meta_compositor_server_new (MetaDisplay *display,
                            MetaBackend *backend)
{
  return g_object_new (META_TYPE_COMPOSITOR_SERVER,
                       "display", display,
                       "backend", backend,
                       NULL);
}

static void
meta_compositor_server_init (MetaCompositorServer *compositor_server)
{
}

static void
meta_compositor_server_class_init (MetaCompositorServerClass *klass)
{
  MetaCompositorClass *compositor_class = META_COMPOSITOR_CLASS (klass);

  compositor_class->manage = meta_compositor_server_manage;
  compositor_class->monotonic_to_high_res_xserver_time =
   meta_compositor_server_monotonic_to_high_res_xserver_time;
  compositor_class->grab_begin = meta_compositor_server_grab_begin;
  compositor_class->grab_end = meta_compositor_server_grab_end;
  compositor_class->create_view = meta_compositor_server_create_view;
}
