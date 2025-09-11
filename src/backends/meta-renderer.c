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

/**
 * MetaRenderer:
 *
 * Keeps track of the different renderer views.
 *
 * A MetaRenderer object has 2 functions:
 *
 * 1) Keeping a list of `MetaRendererView`s, each responsible for
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
#include "backends/meta-logical-monitor-private.h"

enum
{
  PROP_0,

  PROP_BACKEND,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MetaRendererPrivate
{
  MetaBackend *backend;
  GList *views;
  gboolean is_paused;
} MetaRendererPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaRenderer, meta_renderer, G_TYPE_OBJECT)

MetaBackend *
meta_renderer_get_backend (MetaRenderer *renderer)
{
  MetaRendererPrivate *priv = meta_renderer_get_instance_private (renderer);

  return priv->backend;
}

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
meta_renderer_create_view (MetaRenderer        *renderer,
                           MetaLogicalMonitor  *logical_monitor,
                           MetaMonitor         *monitor,
                           MetaOutput          *output,
                           MetaCrtc            *crtc,
                           GError             **error)
{
  MetaRendererView *view;

  view = META_RENDERER_GET_CLASS (renderer)->create_view (renderer,
                                                          logical_monitor,
                                                          monitor,
                                                          output,
                                                          crtc,
                                                          error);

  if (view)
    meta_renderer_add_view (renderer, view);

  return view;
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
create_crtc_view (MetaLogicalMonitor *logical_monitor,
                  MetaMonitor        *monitor,
                  MetaOutput         *output,
                  MetaCrtc           *crtc,
                  gpointer            user_data)
{
  MetaRenderer *renderer = user_data;
  MetaRendererView *view;
  g_autoptr (GError) error = NULL;

  view = meta_renderer_create_view (renderer,
                                    logical_monitor,
                                    monitor,
                                    output,
                                    crtc,
                                    &error);
  if (!view)
    {
      g_warning ("Failed to create view for %s on %s: %s",
                 meta_monitor_get_display_name (monitor),
                 meta_output_get_name (output),
                 error->message);
    }
}

static void
meta_renderer_real_rebuild_views (MetaRenderer *renderer)
{
  MetaRendererPrivate *priv = meta_renderer_get_instance_private (renderer);
  MetaBackend *backend = priv->backend;
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  GList *logical_monitors, *l;

  g_clear_list (&priv->views, (GDestroyNotify) clutter_stage_view_destroy);

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);

  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;

      if (meta_logical_monitor_is_primary (logical_monitor))
        {
          ClutterBackend *clutter_backend;
          float scale;

          clutter_backend = meta_backend_get_clutter_backend (backend);
          scale = meta_backend_is_stage_views_scaled (backend)
            ? meta_logical_monitor_get_scale (logical_monitor)
            : 1.f;

          clutter_backend_set_fallback_resource_scale (clutter_backend, scale);
        }

      meta_logical_monitor_foreach_crtc (logical_monitor,
                                         create_crtc_view,
                                         renderer);
    }
}

MetaRendererView *
meta_renderer_get_view_for_crtc (MetaRenderer *renderer,
                                 MetaCrtc     *crtc)
{
  MetaRendererPrivate *priv = meta_renderer_get_instance_private (renderer);
  GList *l;

  for (l = priv->views; l; l = l->next)
    {
      MetaRendererView *view = l->data;

      if (meta_renderer_view_get_crtc (view) == crtc)
        return view;
    }

  return NULL;
}

typedef struct _CollectViewsData
{
  MetaRenderer *renderer;
  GList *out_views;
} CollectViewsData;

static gboolean
collect_views (MetaMonitor          *monitor,
               MetaMonitorMode      *mode,
               MetaMonitorCrtcMode  *monitor_crtc_mode,
               gpointer              user_data,
               GError              **error)
{
  CollectViewsData *data = user_data;
  MetaCrtc *crtc;
  MetaRendererView *view;

  crtc = meta_output_get_assigned_crtc (monitor_crtc_mode->output);
  view = meta_renderer_get_view_for_crtc (data->renderer, crtc);
  if (!g_list_find (data->out_views, view))
    data->out_views = g_list_prepend (data->out_views, view);

  return TRUE;
}

static GList *
meta_renderer_real_get_views_for_monitor (MetaRenderer *renderer,
                                          MetaMonitor  *monitor)
{
  CollectViewsData data = { 0 };
  MetaMonitorMode *monitor_mode;

  data.renderer = renderer;

  monitor_mode = meta_monitor_get_current_mode (monitor);
  meta_monitor_mode_foreach_crtc (monitor, monitor_mode,
                                  collect_views,
                                  &data,
                                  NULL);

  return data.out_views;
}

GList *
meta_renderer_get_views_for_monitor (MetaRenderer *renderer,
                                     MetaMonitor  *monitor)
{
  return META_RENDERER_GET_CLASS (renderer)->get_views_for_monitor (renderer,
                                                                    monitor);
}

void
meta_renderer_add_view (MetaRenderer     *renderer,
                        MetaRendererView *view)
{
  MetaRendererPrivate *priv = meta_renderer_get_instance_private (renderer);

  priv->views = g_list_append (priv->views, view);

  if (priv->is_paused)
    {
      ClutterFrameClock *frame_clock =
        clutter_stage_view_get_frame_clock (CLUTTER_STAGE_VIEW (view));

      clutter_frame_clock_inhibit (frame_clock);
    }
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

void
meta_renderer_pause (MetaRenderer *renderer)
{
  MetaRendererPrivate *priv = meta_renderer_get_instance_private (renderer);
  GList *l;

  g_return_if_fail (!priv->is_paused);
  priv->is_paused = TRUE;

  for (l = priv->views; l; l = l->next)
    {
      ClutterStageView *stage_view = l->data;
      ClutterFrameClock *frame_clock =
        clutter_stage_view_get_frame_clock (stage_view);

      clutter_frame_clock_inhibit (frame_clock);
    }
}

void
meta_renderer_resume (MetaRenderer *renderer)
{
  MetaRendererPrivate *priv = meta_renderer_get_instance_private (renderer);
  MetaRendererClass *klass = META_RENDERER_GET_CLASS (renderer);
  GList *l;

  g_return_if_fail (priv->is_paused);
  priv->is_paused = FALSE;

  for (l = priv->views; l; l = l->next)
    {
      ClutterStageView *stage_view = l->data;
      ClutterFrameClock *frame_clock =
        clutter_stage_view_get_frame_clock (stage_view);

      clutter_frame_clock_uninhibit (frame_clock);
    }

  if (klass->resume)
    klass->resume (renderer);
}

gboolean
meta_renderer_is_hardware_accelerated (MetaRenderer *renderer)
{
  MetaRendererPrivate *priv = meta_renderer_get_instance_private (renderer);
  MetaBackend *backend = priv->backend;
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_backend);
  CoglDriver *cogl_driver =
    cogl_context_get_driver (cogl_context);

  return cogl_driver_is_hardware_accelerated (cogl_driver);
}

static void
meta_renderer_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  MetaRenderer *renderer = META_RENDERER (object);
  MetaRendererPrivate *priv = meta_renderer_get_instance_private (renderer);

  switch (prop_id)
    {
    case PROP_BACKEND:
      g_value_set_object (value, priv->backend);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_renderer_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  MetaRenderer *renderer = META_RENDERER (object);
  MetaRendererPrivate *priv = meta_renderer_get_instance_private (renderer);

  switch (prop_id)
    {
    case PROP_BACKEND:
      priv->backend = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_renderer_dispose (GObject *object)
{
  MetaRenderer *renderer = META_RENDERER (object);
  MetaRendererPrivate *priv = meta_renderer_get_instance_private (renderer);

  g_clear_list (&priv->views, g_object_unref);

  G_OBJECT_CLASS (meta_renderer_parent_class)->dispose (object);
}

static void
meta_renderer_init (MetaRenderer *renderer)
{
}

static void
meta_renderer_class_init (MetaRendererClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = meta_renderer_get_property;
  object_class->set_property = meta_renderer_set_property;
  object_class->dispose = meta_renderer_dispose;

  klass->rebuild_views = meta_renderer_real_rebuild_views;
  klass->get_views_for_monitor = meta_renderer_real_get_views_for_monitor;

  obj_props[PROP_BACKEND] =
    g_param_spec_object ("backend", NULL, NULL,
                         META_TYPE_BACKEND,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}
