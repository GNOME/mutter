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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include "backends/x11/nested/meta-renderer-x11-nested.h"

#include <glib-object.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-output.h"
#include "backends/meta-renderer.h"
#include "backends/meta-renderer-view.h"
#include "backends/x11/meta-clutter-backend-x11.h"
#include "core/boxes-private.h"
#include "meta/meta-backend.h"
#include "meta/util.h"

struct _MetaRendererX11Nested
{
  MetaRendererX11 parent;
};

G_DEFINE_TYPE (MetaRendererX11Nested, meta_renderer_x11_nested,
               META_TYPE_RENDERER_X11)

static MetaMonitorTransform
calculate_view_transform (MetaMonitorManager *monitor_manager,
                          MetaLogicalMonitor *logical_monitor)
{
  MetaMonitor *main_monitor;
  MetaOutput *main_output;
  MetaCrtc *crtc;
  MetaMonitorTransform crtc_transform;

  main_monitor = meta_logical_monitor_get_monitors (logical_monitor)->data;
  main_output = meta_monitor_get_main_output (main_monitor);
  crtc = meta_output_get_assigned_crtc (main_output);
  crtc_transform =
    meta_monitor_logical_to_crtc_transform (main_monitor,
                                            logical_monitor->transform);
  /*
   * Pick any monitor and output and check; all CRTCs of a logical monitor will
   * always have the same transform assigned to them.
   */

  if (meta_monitor_manager_is_transform_handled (monitor_manager,
                                                 crtc,
                                                 crtc_transform))
    return META_MONITOR_TRANSFORM_NORMAL;
  else
    return crtc_transform;
}

static CoglOffscreen *
create_offscreen (CoglContext *cogl_context,
                  int          width,
                  int          height)
{
  CoglTexture *texture_2d;
  CoglOffscreen *offscreen;
  GError *error = NULL;

  texture_2d = cogl_texture_2d_new_with_size (cogl_context, width, height);
  offscreen = cogl_offscreen_new_with_texture (texture_2d);

  if (!cogl_framebuffer_allocate (COGL_FRAMEBUFFER (offscreen), &error))
    meta_fatal ("Couldn't allocate framebuffer: %s", error->message);

  return offscreen;
}

static MetaRendererView *
meta_renderer_x11_nested_create_view (MetaRenderer       *renderer,
                                      MetaLogicalMonitor *logical_monitor,
                                      MetaOutput         *output,
                                      MetaCrtc           *crtc)
{
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  MetaMonitorTransform view_transform;
  float view_scale;
  const MetaCrtcConfig *crtc_config;
  int width, height;
  CoglOffscreen *fake_onscreen;
  CoglOffscreen *offscreen;
  MtkRectangle view_layout;
  const MetaCrtcModeInfo *mode_info;
  MetaRendererView *view;

  view_transform = calculate_view_transform (monitor_manager, logical_monitor);

  if (meta_backend_is_stage_views_scaled (backend))
    view_scale = logical_monitor->scale;
  else
    view_scale = 1.0;

  crtc_config = meta_crtc_get_config (crtc);
  width = roundf (crtc_config->layout.size.width * view_scale);
  height = roundf (crtc_config->layout.size.height * view_scale);

  fake_onscreen = create_offscreen (cogl_context, width, height);

  if (view_transform != META_MONITOR_TRANSFORM_NORMAL)
    offscreen = create_offscreen (cogl_context, width, height);
  else
    offscreen = NULL;

  mtk_rectangle_from_graphene_rect (&crtc_config->layout,
                                    MTK_ROUNDING_STRATEGY_ROUND,
                                    &view_layout);

  mode_info = meta_crtc_mode_get_info (crtc_config->mode);

  view = g_object_new (META_TYPE_RENDERER_VIEW,
                       "name", meta_output_get_name (output),
                       "stage", meta_backend_get_stage (backend),
                       "layout", &view_layout,
                       "crtc", crtc,
                       "refresh-rate", mode_info->refresh_rate,
                       "framebuffer", COGL_FRAMEBUFFER (fake_onscreen),
                       "offscreen", COGL_FRAMEBUFFER (offscreen),
                       "transform", view_transform,
                       "scale", view_scale,
                       NULL);
  g_object_set_data (G_OBJECT (view), "crtc", crtc);

  return view;
}

static void
meta_renderer_x11_nested_init (MetaRendererX11Nested *renderer_x11_nested)
{
}

static void
meta_renderer_x11_nested_class_init (MetaRendererX11NestedClass *klass)
{
  MetaRendererClass *renderer_class = META_RENDERER_CLASS (klass);

  renderer_class->create_view = meta_renderer_x11_nested_create_view;
}
