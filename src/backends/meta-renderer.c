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

/**
 * SECTION:meta-renderer
 * @title: MetaRenderer
 * @short_description: Keeps track of the different renderer views.
 *
 * A MetaRenderer object has 2 functions:
 *
 * 1) Keeping a list of #MetaRendererView<!-- -->s, each responsible for
 * rendering a part of the stage, corresponding to each #MetaLogicalMonitor. It
 * keeps track of this list by querying the list of logical monitors in the
 * #MetaBackend's #MetaMonitorManager, and creating a renderer view for each
 * logical monitor it encounters.
 *
 * 2) Creating and setting up an appropriate #CoglRenderer. For example, a
 * #MetaRenderer might call cogl_renderer_set_custom_winsys() to tie the
 * backend-specific mechanisms into Cogl.
 */

#include "config.h"

#include "backends/meta-renderer.h"

#include <glib-object.h>

#include "backends/meta-backend-private.h"

typedef struct _MetaRendererPrivate
{
  GList *views;
} MetaRendererPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaRenderer, meta_renderer, G_TYPE_OBJECT)

/**
 * meta_renderer_create_cogl_renderer:
 * @renderer: a #MetaRenderer object
 *
 * Creates a #CoglRenderer that is appropriate for a certain backend. For
 * example, a #MetaRenderer might call cogl_renderer_set_custom_winsys() to tie
 * the backend-specific mechanisms (such as swapBuffers and vsync) into Cogl.
 *
 * Returns: (transfer full): a newly made #CoglRenderer.
 */
CoglRenderer *
meta_renderer_create_cogl_renderer (MetaRenderer *renderer)
{
  return META_RENDERER_GET_CLASS (renderer)->create_cogl_renderer (renderer);
}

static MetaRendererView *
meta_renderer_create_view (MetaRenderer       *renderer,
                           MetaLogicalMonitor *logical_monitor)
{
  return META_RENDERER_GET_CLASS (renderer)->create_view (renderer,
                                                          logical_monitor);
}

/**
 * meta_renderer_rebuild_views:
 * @renderer: a #MetaRenderer object
 *
 * Rebuilds the internal list of #MetaRendererView objects by querying the
 * current #MetaBackend's #MetaMonitorManager.
 *
 * This also leads to the original list of monitors being unconditionally freed.
 */
void
meta_renderer_rebuild_views (MetaRenderer *renderer)
{
  return META_RENDERER_GET_CLASS (renderer)->rebuild_views (renderer);
}

static void
meta_renderer_real_rebuild_views (MetaRenderer *renderer)
{
  MetaRendererPrivate *priv = meta_renderer_get_instance_private (renderer);
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  GList *logical_monitors, *l;

  g_list_free_full (priv->views, g_object_unref);
  priv->views = NULL;

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);

  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;
      MetaRendererView *view;

      view = meta_renderer_create_view (renderer, logical_monitor);
      priv->views = g_list_append (priv->views, view);
    }
}

void
meta_renderer_set_legacy_view (MetaRenderer     *renderer,
                               MetaRendererView *legacy_view)
{
  MetaRendererPrivate *priv = meta_renderer_get_instance_private (renderer);

  g_assert (!priv->views);

  priv->views = g_list_append (priv->views, legacy_view);
}

/**
 * meta_renderer_get_views:
 * @renderer: a #MetaRenderer object
 *
 * Returns a list of #MetaRendererView objects, each dealing with a part of the
 * stage.
 *
 * Returns: (transfer none) (element-type MetaRendererView): a list of
 * #MetaRendererView objects.
 */
GList *
meta_renderer_get_views (MetaRenderer *renderer)
{
  MetaRendererPrivate *priv = meta_renderer_get_instance_private (renderer);

  return priv->views;
}

MetaRendererView *
meta_renderer_get_view_from_logical_monitor (MetaRenderer       *renderer,
                                             MetaLogicalMonitor *logical_monitor)
{
  GList *l;

  for (l = meta_renderer_get_views (renderer); l; l = l->next)
    {
      MetaRendererView *view = l->data;

      if (meta_renderer_view_get_logical_monitor (view) ==
          logical_monitor)
        return view;
    }

  return NULL;
}

static void
meta_renderer_finalize (GObject *object)
{
  MetaRenderer *renderer = META_RENDERER (object);
  MetaRendererPrivate *priv = meta_renderer_get_instance_private (renderer);

  g_list_free_full (priv->views, g_object_unref);
  priv->views = NULL;

  G_OBJECT_CLASS (meta_renderer_parent_class)->finalize (object);
}

static void
meta_renderer_init (MetaRenderer *renderer)
{
}

static void
meta_renderer_class_init (MetaRendererClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_renderer_finalize;

  klass->rebuild_views = meta_renderer_real_rebuild_views;
}
