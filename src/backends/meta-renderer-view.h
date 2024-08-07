/*
 * Copyright (C) 2016 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-stage-impl-private.h"
#include "backends/meta-stage-view-private.h"

#define META_TYPE_RENDERER_VIEW (meta_renderer_view_get_type ())
META_EXPORT_TEST
G_DECLARE_DERIVABLE_TYPE (MetaRendererView, meta_renderer_view,
                          META, RENDERER_VIEW,
                          MetaStageView)

struct _MetaRendererViewClass
{
  MetaStageViewClass parent_class;
};

META_EXPORT_TEST
MetaCrtc *meta_renderer_view_get_crtc (MetaRendererView *view);
