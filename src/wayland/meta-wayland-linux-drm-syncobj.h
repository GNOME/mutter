/*
 * Copyright (C) 2023 NVIDIA Corporation.
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
 *     Austin Shafer <ashafer@nvidia.com>
 */

#pragma once

#include <glib.h>

#include "wayland/meta-wayland-types.h"
#include "wayland/meta-drm-timeline.h"

#include "linux-drm-syncobj-v1-server-protocol.h"

#define META_TYPE_WAYLAND_SYNC_POINT (meta_wayland_sync_point_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandSyncPoint,
                      meta_wayland_sync_point,
                      META, WAYLAND_SYNC_POINT,
                      GObject)

typedef struct _MetaWaylandSyncPoint {
  GObject parent;

  MetaWaylandSyncobjTimeline *timeline;
  uint64_t sync_point;
} MetaWaylandSyncPoint;

bool
meta_wayland_surface_explicit_sync_validate (MetaWaylandSurface      *surface,
                                             MetaWaylandSurfaceState *state);

void
meta_wayland_drm_syncobj_init (MetaWaylandCompositor *compositor);

gboolean
meta_wayland_sync_timeline_set_sync_point (MetaWaylandSyncobjTimeline  *timeline,
                                           uint64_t                     sync_point,
                                           int                          sync_fd,
                                           GError                     **error);

int
meta_wayland_sync_timeline_get_eventfd (MetaWaylandSyncobjTimeline  *timeline,
                                        uint64_t                     sync_point,
                                        GError                     **error);
