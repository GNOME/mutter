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

/**
 * MetaRendererView:
 *
 * Renders (a part of) the global stage.
 *
 * A MetaRendererView object is responsible for rendering (a part of) the
 * global stage, or more precisely: the part that matches what can be seen on a
 * #MetaLogicalMonitor. By splitting up the rendering into different parts and
 * attaching it to a #MetaLogicalMonitor, we can do the rendering so that each
 * renderer view is responsible for applying the right #MtkMonitorTransform
 * and the right scaling.
 */

#include "config.h"

#include "backends/meta-renderer-view.h"

#include "backends/meta-color-device.h"
#include "backends/meta-crtc.h"
#include "backends/meta-renderer.h"
#include "clutter/clutter-mutter.h"
#include "core/boxes-private.h"
#include "core/meta-debug-control-private.h"

enum
{
  PROP_0,

  PROP_BACKEND,
  PROP_CRTC,
  PROP_COLOR_DEVICE,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

typedef struct _MetaRendererViewPrivate
{
  MetaBackend *backend;
  MetaCrtc *crtc;
  MetaColorDevice *color_device;
} MetaRendererViewPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaRendererView, meta_renderer_view,
                            META_TYPE_STAGE_VIEW)

MetaCrtc *
meta_renderer_view_get_crtc (MetaRendererView *view)
{
  MetaRendererViewPrivate *priv =
    meta_renderer_view_get_instance_private (view);

  return priv->crtc;
}

static void
set_color_states (MetaRendererView *view)
{
  ClutterStageView *clutter_stage_view = CLUTTER_STAGE_VIEW (view);
  MetaRendererViewPrivate *priv =
    meta_renderer_view_get_instance_private (view);
  MetaContext *context = meta_backend_get_context (priv->backend);
  MetaDebugControl *debug_control = meta_context_get_debug_control (context);
  ClutterColorState *output_color_state;
  g_autoptr (ClutterColorState) view_color_state = NULL;
  gboolean force_linear;

  g_return_if_fail (priv->color_device != NULL);

  output_color_state = meta_color_device_get_color_state (priv->color_device);

  force_linear = meta_debug_control_is_linear_blending_forced (debug_control);
  view_color_state = clutter_color_state_get_blending (output_color_state,
                                                       force_linear);

  if (meta_is_topic_enabled (META_DEBUG_RENDER))
    {
      g_autofree char *output_cs_str =
        clutter_color_state_to_string (output_color_state);
      g_autofree char *view_cs_str =
        clutter_color_state_to_string (view_color_state);
      const char *name = clutter_stage_view_get_name (clutter_stage_view);

      meta_topic (META_DEBUG_RENDER, "ColorState for view %s: %s",
                  name,  view_cs_str);

      meta_topic (META_DEBUG_RENDER, "ColorState for output %s: %s",
                  name, output_cs_str);
    }

  clutter_stage_view_set_color_state (clutter_stage_view,
                                      view_color_state);
  clutter_stage_view_set_output_color_state (clutter_stage_view,
                                             output_color_state);
}

static void
on_color_state_changed (MetaColorDevice  *color_device,
                        MetaRendererView *view)
{
  set_color_states (view);
}

static void
meta_renderer_view_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  MetaRendererView *view = META_RENDERER_VIEW (object);
  MetaRendererViewPrivate *priv =
    meta_renderer_view_get_instance_private (view);

  switch (prop_id)
    {
    case PROP_BACKEND:
      g_value_set_object (value, priv->backend);
      break;
    case PROP_CRTC:
      g_value_set_object (value, priv->crtc);
      break;
    case PROP_COLOR_DEVICE:
      g_value_set_object (value, priv->color_device);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_renderer_view_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  MetaRendererView *view = META_RENDERER_VIEW (object);
  MetaRendererViewPrivate *priv =
    meta_renderer_view_get_instance_private (view);

  switch (prop_id)
    {
    case PROP_BACKEND:
      priv->backend = g_value_get_object (value);
      break;
    case PROP_CRTC:
      priv->crtc = g_value_get_object (value);
      break;
    case PROP_COLOR_DEVICE:
      g_set_object (&priv->color_device, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_renderer_view_constructed (GObject *object)
{
  MetaRendererView *view = META_RENDERER_VIEW (object);
  MetaRendererViewPrivate *priv =
    meta_renderer_view_get_instance_private (view);

  g_return_if_fail (priv->backend != NULL);

  if (priv->color_device != NULL)
    {
      set_color_states (view);

      g_signal_connect_object (priv->color_device, "color-state-changed",
                               G_CALLBACK (on_color_state_changed),
                               view,
                               G_CONNECT_DEFAULT);
    }

  G_OBJECT_CLASS (meta_renderer_view_parent_class)->constructed (object);
}

static void
meta_renderer_view_dispose (GObject *object)
{
  MetaRendererView *view = META_RENDERER_VIEW (object);
  MetaRendererViewPrivate *priv =
    meta_renderer_view_get_instance_private (view);

  g_clear_object (&priv->color_device);

  G_OBJECT_CLASS (meta_renderer_view_parent_class)->dispose (object);
}

static void
meta_renderer_view_init (MetaRendererView *view)
{
}

static void
meta_renderer_view_class_init (MetaRendererViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_renderer_view_constructed;
  object_class->dispose = meta_renderer_view_dispose;
  object_class->get_property = meta_renderer_view_get_property;
  object_class->set_property = meta_renderer_view_set_property;

  obj_props[PROP_BACKEND] =
    g_param_spec_object ("backend", NULL, NULL,
                         META_TYPE_BACKEND,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  obj_props[PROP_CRTC] =
    g_param_spec_object ("crtc", NULL, NULL,
                         META_TYPE_CRTC,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  obj_props[PROP_COLOR_DEVICE] =
    g_param_spec_object ("color-device", NULL, NULL,
                         META_TYPE_COLOR_DEVICE,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST, obj_props);
}
