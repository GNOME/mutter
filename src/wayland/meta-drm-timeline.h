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
#include <glib-object.h>
#include <stdint.h>

#define META_TYPE_DRM_TIMELINE (meta_drm_timeline_get_type ())
G_DECLARE_FINAL_TYPE (MetaDrmTimeline, meta_drm_timeline,
                      META, DRM_TIMELINE, GObject);

typedef struct _MetaDrmTimeline MetaDrmTimeline;

MetaDrmTimeline * meta_drm_timeline_create (int      fd,
                                            GError **error);

MetaDrmTimeline * meta_drm_timeline_import_syncobj (int       fd,
                                                    int       drm_syncobj,
                                                    GError  **error);

int meta_drm_timeline_get_eventfd (MetaDrmTimeline *timeline,
                                   uint64_t         sync_point,
                                   GError         **error);

gboolean meta_drm_timeline_set_sync_point (MetaDrmTimeline *timeline,
                                           uint64_t         sync_point,
                                           int              sync_fd,
                                           GError         **error);
