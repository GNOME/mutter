/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat
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

#include "backends/x11/cm/meta-renderer-x11-cm.h"

#include "backends/meta-renderer-view.h"

struct _MetaRendererX11Cm
{
  MetaRendererX11 parent;

  MetaRendererView *screen_view;
};

G_DEFINE_TYPE (MetaRendererX11Cm, meta_renderer_x11_cm,
               META_TYPE_RENDERER_X11)

void
meta_renderer_x11_cm_init_screen_view (MetaRendererX11Cm *renderer_x11_cm,
                                       CoglOnscreen      *onscreen,
                                       int                width,
                                       int                height)
{
  MetaRenderer *renderer = META_RENDERER (renderer_x11_cm);
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  ClutterActor *stage = meta_backend_get_stage (backend);
  cairo_rectangle_int_t view_layout;

  g_return_if_fail (!renderer_x11_cm->screen_view);

  view_layout = (cairo_rectangle_int_t) {
    .width = width,
    .height = height,
  };
  renderer_x11_cm->screen_view = g_object_new (META_TYPE_RENDERER_VIEW,
                                               "name", "X11 screen",
                                               "stage", stage,
                                               "layout", &view_layout,
                                               "framebuffer", onscreen,
                                               NULL);
  meta_renderer_add_view (META_RENDERER (renderer_x11_cm),
                          renderer_x11_cm->screen_view);
}

void
meta_renderer_x11_cm_resize (MetaRendererX11Cm *renderer_x11_cm,
                             int                width,
                             int                height)
{
  cairo_rectangle_int_t view_layout;

  view_layout = (cairo_rectangle_int_t) {
    .width = width,
    .height = height,
  };

  g_object_set (G_OBJECT (renderer_x11_cm->screen_view),
                "layout", &view_layout,
                NULL);
}

static void
meta_renderer_x11_cm_rebuild_views (MetaRenderer *renderer)
{
  MetaRendererX11Cm *renderer_x11_cm = META_RENDERER_X11_CM (renderer);

  g_return_if_fail (!meta_renderer_get_views (renderer));

  meta_renderer_add_view (renderer, renderer_x11_cm->screen_view);
}

static GList *
meta_renderer_x11_cm_get_views_for_monitor (MetaRenderer *renderer,
                                            MetaMonitor  *monitor)
{
  return g_list_prepend (NULL, meta_renderer_get_views (renderer)->data);
}

static void
meta_renderer_x11_cm_init (MetaRendererX11Cm *renderer_x11_cm)
{
}

static void
meta_renderer_x11_cm_class_init (MetaRendererX11CmClass *klass)
{
  MetaRendererClass *renderer_class = META_RENDERER_CLASS (klass);

  renderer_class->rebuild_views = meta_renderer_x11_cm_rebuild_views;
  renderer_class->get_views_for_monitor =
    meta_renderer_x11_cm_get_views_for_monitor;
}
