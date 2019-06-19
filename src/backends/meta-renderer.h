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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by:
 *     Jonas Ådahl <jadahl@gmail.com>
 */

#ifndef META_RENDERER_H
#define META_RENDERER_H

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
  MetaRendererView * (* create_view) (MetaRenderer       *renderer,
                                      MetaLogicalMonitor *logical_monitor);
  void (* rebuild_views) (MetaRenderer *renderer);
};

CoglRenderer * meta_renderer_create_cogl_renderer (MetaRenderer *renderer);

void meta_renderer_rebuild_views (MetaRenderer *renderer);

void meta_renderer_set_legacy_view (MetaRenderer     *renderer,
                                    MetaRendererView *legacy_view);

META_EXPORT_TEST
GList * meta_renderer_get_views (MetaRenderer *renderer);

MetaRendererView * meta_renderer_get_view_from_logical_monitor (MetaRenderer       *renderer,
                                                                MetaLogicalMonitor *logical_monitor);

#endif /* META_RENDERER_H */
