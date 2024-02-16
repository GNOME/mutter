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

#include "backends/native/meta-frame-native.h"

struct _MetaRendererViewNative
{
  MetaRendererView parent;
};

G_DEFINE_TYPE (MetaRendererViewNative, meta_renderer_view_native,
               META_TYPE_RENDERER_VIEW)

static ClutterFrame *
meta_renderer_view_native_new_frame (ClutterStageView *stage_view)
{
  return (ClutterFrame *) meta_frame_native_new ();
}

static void
meta_renderer_view_native_class_init (MetaRendererViewNativeClass *klass)
{
  ClutterStageViewClass *stage_view_class = CLUTTER_STAGE_VIEW_CLASS (klass);

  stage_view_class->new_frame = meta_renderer_view_native_new_frame;
}

static void
meta_renderer_view_native_init (MetaRendererViewNative *view_native)
{
}
