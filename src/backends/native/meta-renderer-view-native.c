/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2020-2022 Dor Askayo
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

#include "backends/native/meta-renderer-view-native.h"

#include "backends/native/meta-crtc-native.h"
#include "backends/native/meta-frame-native.h"

struct _MetaRendererViewNative
{
  MetaRendererView parent;
};

G_DEFINE_TYPE (MetaRendererViewNative, meta_renderer_view_native,
               META_TYPE_RENDERER_VIEW)

static void
update_frame_clock_deadline_evasion (MetaRendererView *renderer_view)
{
  ClutterStageView *stage_view = CLUTTER_STAGE_VIEW (renderer_view);
  ClutterFrameClock *frame_clock;
  MetaCrtc *crtc;
  MetaCrtcNative *crtc_native;
  int64_t deadline_evasion_us;

  frame_clock = clutter_stage_view_get_frame_clock (stage_view);
  crtc = meta_renderer_view_get_crtc (renderer_view);
  crtc_native = META_CRTC_NATIVE (crtc);

  deadline_evasion_us = meta_crtc_native_get_deadline_evasion (crtc_native);
  clutter_frame_clock_set_deadline_evasion (frame_clock,
                                            deadline_evasion_us);
}

static void
meta_renderer_view_native_constructed (GObject *object)
{
  MetaRendererView *renderer_view = META_RENDERER_VIEW (object);

  G_OBJECT_CLASS (meta_renderer_view_native_parent_class)->constructed (object);

  update_frame_clock_deadline_evasion (renderer_view);
}

static ClutterFrame *
meta_renderer_view_native_new_frame (ClutterStageView *stage_view)
{
  return (ClutterFrame *) meta_frame_native_new ();
}

static void
meta_renderer_view_native_schedule_update (ClutterStageView *stage_view)
{
  MetaRendererView *renderer_view = META_RENDERER_VIEW (stage_view);

  update_frame_clock_deadline_evasion (renderer_view);
}

static void
meta_renderer_view_native_class_init (MetaRendererViewNativeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterStageViewClass *stage_view_class = CLUTTER_STAGE_VIEW_CLASS (klass);

  object_class->constructed = meta_renderer_view_native_constructed;

  stage_view_class->new_frame = meta_renderer_view_native_new_frame;
  stage_view_class->schedule_update = meta_renderer_view_native_schedule_update;
}

static void
meta_renderer_view_native_init (MetaRendererViewNative *view_native)
{
}
