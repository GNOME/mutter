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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Written by:
 *     Dor Askayo <dor.askayo@gmail.com>
 */

#pragma once

#include "clutter/clutter-mutter.h"
#include "compositor/meta-compositor-view.h"
#include "meta/compositor.h"

#define META_TYPE_COMPOSITOR_VIEW_NATIVE (meta_compositor_view_native_get_type ())
G_DECLARE_FINAL_TYPE (MetaCompositorViewNative, meta_compositor_view_native,
                      META, COMPOSITOR_VIEW_NATIVE, MetaCompositorView)

MetaCompositorViewNative *meta_compositor_view_native_new (ClutterStageView *stage_view);

#ifdef HAVE_WAYLAND
void meta_compositor_view_native_maybe_assign_scanout (MetaCompositorViewNative *view_native,
                                                       MetaCompositor           *compositor);
#endif /* HAVE_WAYLAND */

void meta_compositor_view_native_maybe_update_frame_sync_surface (MetaCompositorViewNative *view_native,
                                                                  MetaCompositor           *compositor);
