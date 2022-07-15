/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2022 Dor Askayo
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
 * Written by:
 *     Dor Askayo <dor.askayo@gmail.com>
 */

#include "config.h"

#include "compositor/meta-compositor-view-native.h"

struct _MetaCompositorViewNative
{
  MetaCompositorView parent;
};

G_DEFINE_TYPE (MetaCompositorViewNative, meta_compositor_view_native,
               META_TYPE_COMPOSITOR_VIEW)

MetaCompositorViewNative *
meta_compositor_view_native_new (ClutterStageView *stage_view)
{
  g_assert (stage_view != NULL);

  return g_object_new (META_TYPE_COMPOSITOR_VIEW_NATIVE,
                       "stage-view", stage_view,
                       NULL);
}

static void
meta_compositor_view_native_class_init (MetaCompositorViewNativeClass *klass)
{
}

static void
meta_compositor_view_native_init (MetaCompositorViewNative *view_native)
{
}
