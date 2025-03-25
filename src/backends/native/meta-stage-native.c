/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat Inc.
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
 *     Jonas Ådahl <jadahl@gmail.com>
 */

#include "config.h"

#include "backends/native/meta-stage-native.h"

#include "backends/meta-backend-private.h"
#include "backends/native/meta-crtc-virtual.h"
#include "backends/native/meta-cursor-renderer-native.h"
#include "backends/native/meta-renderer-native.h"
#include "meta/meta-backend.h"
#include "meta/meta-monitor-manager.h"
#include "meta/util.h"

static GQuark quark_view_frame_closure  = 0;

struct _MetaStageNative
{
  MetaStageImpl parent;

  CoglClosure *frame_closure;

  int64_t presented_frame_counter_sync;
  int64_t presented_frame_counter_complete;
};

static ClutterStageWindowInterface *clutter_stage_window_parent_iface = NULL;

static void
clutter_stage_window_iface_init (ClutterStageWindowInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaStageNative, meta_stage_native,
                         META_TYPE_STAGE_IMPL,
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_STAGE_WINDOW,
                                                clutter_stage_window_iface_init))

static gboolean
meta_stage_native_can_clip_redraws (ClutterStageWindow *stage_window)
{
  return TRUE;
}

static void
meta_stage_native_get_geometry (ClutterStageWindow *stage_window,
                                MtkRectangle       *geometry)
{
  MetaStageImpl *stage_impl = META_STAGE_IMPL (stage_window);
  MetaBackend *backend = meta_stage_impl_get_backend (stage_impl);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  if (monitor_manager)
    {
      int width, height;

      meta_monitor_manager_get_screen_size (monitor_manager, &width, &height);
      *geometry = (MtkRectangle) {
        .width = width,
        .height = height,
      };
    }
  else
    {
      *geometry = (MtkRectangle) {
        .width = 1,
        .height = 1,
      };
    }
}

static GList *
meta_stage_native_get_views (ClutterStageWindow *stage_window)
{
  MetaStageImpl *stage_impl = META_STAGE_IMPL (stage_window);
  MetaBackend *backend = meta_stage_impl_get_backend (stage_impl);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);

  return meta_renderer_get_views (renderer);
}

static void
meta_stage_native_prepare_frame (ClutterStageWindow *stage_window,
                                 ClutterStageView   *stage_view,
                                 ClutterFrame       *frame)
{
  MetaStageImpl *stage_impl = META_STAGE_IMPL (stage_window);
  MetaBackend *backend = meta_stage_impl_get_backend (stage_impl);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  MetaRendererNative *renderer_native = META_RENDERER_NATIVE (renderer);
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);
  MetaCursorRendererNative *cursor_renderer_native =
    META_CURSOR_RENDERER_NATIVE (cursor_renderer);

  meta_renderer_native_prepare_frame (renderer_native,
                                      META_RENDERER_VIEW (stage_view),
                                      frame);
  meta_cursor_renderer_native_prepare_frame (cursor_renderer_native,
                                             META_RENDERER_VIEW (stage_view),
                                             frame);
}

static void
meta_stage_native_redraw_view (ClutterStageWindow *stage_window,
                               ClutterStageView   *view,
                               ClutterFrame       *frame)
{
  MetaStageImpl *stage_impl = META_STAGE_IMPL (stage_window);
  MetaBackend *backend = meta_stage_impl_get_backend (stage_impl);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  MetaCrtc *crtc;

  meta_renderer_native_before_redraw (META_RENDERER_NATIVE (renderer),
                                      META_RENDERER_VIEW (view), frame);

  clutter_stage_window_parent_iface->redraw_view (stage_window, view, frame);

  crtc = meta_renderer_view_get_crtc (META_RENDERER_VIEW (view));

  if (!clutter_frame_has_result (frame))
    {
      g_warn_if_fail (!META_IS_CRTC_KMS (crtc));

      clutter_frame_set_result (frame, CLUTTER_FRAME_RESULT_PENDING_PRESENTED);
    }
}

static void
meta_stage_native_finish_frame (ClutterStageWindow *stage_window,
                                ClutterStageView   *stage_view,
                                ClutterFrame       *frame)
{
  MetaStageImpl *stage_impl = META_STAGE_IMPL (stage_window);
  MetaBackend *backend = meta_stage_impl_get_backend (stage_impl);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);

  meta_renderer_native_finish_frame (META_RENDERER_NATIVE (renderer),
                                     META_RENDERER_VIEW (stage_view),
                                     frame);

  if (!clutter_frame_has_result (frame))
    clutter_frame_set_result (frame, CLUTTER_FRAME_RESULT_IDLE);
}

static void
meta_stage_native_init (MetaStageNative *stage_native)
{
  stage_native->presented_frame_counter_sync = -1;
  stage_native->presented_frame_counter_complete = -1;
}

static void
meta_stage_native_class_init (MetaStageNativeClass *klass)
{
  quark_view_frame_closure =
    g_quark_from_static_string ("-meta-native-stage-view-frame-closure");
}

static void
clutter_stage_window_iface_init (ClutterStageWindowInterface *iface)
{
  clutter_stage_window_parent_iface = g_type_interface_peek_parent (iface);

  iface->can_clip_redraws = meta_stage_native_can_clip_redraws;
  iface->get_geometry = meta_stage_native_get_geometry;
  iface->get_views = meta_stage_native_get_views;
  iface->prepare_frame = meta_stage_native_prepare_frame;
  iface->redraw_view = meta_stage_native_redraw_view;
  iface->finish_frame = meta_stage_native_finish_frame;
}
