/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat Inc.
 * Copyright (C) 2017 Intel Corporation
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
 *     Daniel Stone <daniels@collabora.com>
 */

#pragma once

#include <glib.h>
#include <glib-object.h>

#include "cogl/cogl.h"
#include "meta/meta-multi-texture.h"
#include "wayland/meta-wayland-types.h"

#define META_TYPE_WAYLAND_DMA_BUF_BUFFER (meta_wayland_dma_buf_buffer_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandDmaBufBuffer, meta_wayland_dma_buf_buffer,
                      META, WAYLAND_DMA_BUF_BUFFER, GObject);

#define META_TYPE_WAYLAND_DMA_BUF_MANAGER (meta_wayland_dma_buf_manager_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandDmaBufManager, meta_wayland_dma_buf_manager,
                      META, WAYLAND_DMA_BUF_MANAGER, GObject)

typedef struct _MetaWaylandDmaBufBuffer MetaWaylandDmaBufBuffer;

MetaWaylandDmaBufManager * meta_wayland_dma_buf_manager_new (MetaWaylandCompositor  *compositor,
                                                             GError                **error);

gboolean
meta_wayland_dma_buf_buffer_attach (MetaWaylandBuffer  *buffer,
                                    MetaMultiTexture  **texture,
                                    GError            **error);

MetaWaylandDmaBufBuffer *
meta_wayland_dma_buf_fds_for_wayland_buffer (MetaWaylandBuffer *buffer);

MetaWaylandDmaBufBuffer *
meta_wayland_dma_buf_from_buffer (MetaWaylandBuffer *buffer);

typedef void (*MetaWaylandDmaBufSourceDispatch) (MetaWaylandBuffer *buffer,
                                                 gpointer           user_data);

GSource *
meta_wayland_dma_buf_create_source (MetaWaylandBuffer               *buffer,
                                    MetaWaylandDmaBufSourceDispatch  dispatch,
                                    gpointer                         user_data);

GSource *
meta_wayland_drm_syncobj_create_source (MetaWaylandBuffer                *buffer,
                                        MetaWaylandSyncobjTimeline       *timeline,
                                        uint64_t                          sync_point,
                                        MetaWaylandDmaBufSourceDispatch   dispatch,
                                        gpointer                          user_data);

CoglScanout *
meta_wayland_dma_buf_try_acquire_scanout (MetaWaylandBuffer     *buffer,
                                          CoglOnscreen          *onscreen,
                                          ClutterStageView      *stage_view,
                                          const graphene_rect_t *src_rect,
                                          const MtkRectangle    *dst_rect);
