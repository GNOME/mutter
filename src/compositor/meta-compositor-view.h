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

#include <glib-object.h>

#include "clutter/clutter-mutter.h"
#include "meta/meta-window-actor.h"

struct _MetaCompositorViewClass
{
  GObjectClass parent_class;
};

#define META_TYPE_COMPOSITOR_VIEW (meta_compositor_view_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaCompositorView, meta_compositor_view,
                          META, COMPOSITOR_VIEW, GObject)

MetaCompositorView *meta_compositor_view_new (ClutterStageView *stage_view);

void meta_compositor_view_update_top_window_actor (MetaCompositorView *compositor_view,
                                                   GList              *window_actors);

MetaWindowActor *meta_compositor_view_get_top_window_actor (MetaCompositorView *compositor_view);

ClutterStageView *meta_compositor_view_get_stage_view (MetaCompositorView *compositor_view);
