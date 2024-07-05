/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat
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

#pragma once

#include <glib-object.h>

#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-renderer-view.h"
#include "core/util-private.h"
#include "clutter/clutter-mutter.h"
#include "cogl/cogl.h"

#define META_TYPE_RENDERER (meta_renderer_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaRenderer, meta_renderer, META, RENDERER, GObject)

struct _MetaRendererClass
{
  GObjectClass parent_class;

  CoglRenderer * (* create_cogl_renderer) (MetaRenderer *renderer);
  MetaRendererView * (* create_view) (MetaRenderer        *renderer,
                                      MetaLogicalMonitor  *logical_monitor,
                                      MetaMonitor         *monitor,
                                      MetaOutput          *output,
                                      MetaCrtc            *crtc,
                                      GError             **error);
  void (* rebuild_views) (MetaRenderer *renderer);
  void (* resume) (MetaRenderer *renderer);
  GList * (* get_views_for_monitor) (MetaRenderer *renderer,
                                     MetaMonitor  *monitor);
};

MetaBackend * meta_renderer_get_backend (MetaRenderer *renderer);

CoglRenderer * meta_renderer_create_cogl_renderer (MetaRenderer *renderer);

void meta_renderer_rebuild_views (MetaRenderer *renderer);

void meta_renderer_add_view (MetaRenderer     *renderer,
                             MetaRendererView *view);

GList * meta_renderer_get_views_for_monitor (MetaRenderer *renderer,
                                             MetaMonitor  *monitor);

META_EXPORT_TEST
MetaRendererView * meta_renderer_get_view_for_crtc (MetaRenderer *renderer,
                                                    MetaCrtc     *crtc);

META_EXPORT_TEST
GList * meta_renderer_get_views (MetaRenderer *renderer);

gboolean meta_renderer_is_hardware_accelerated (MetaRenderer *renderer);

void meta_renderer_pause (MetaRenderer *renderer);

void meta_renderer_resume (MetaRenderer *renderer);
