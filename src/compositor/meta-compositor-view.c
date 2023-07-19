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

#include "config.h"

#include "compositor/meta-compositor-view.h"

#include "core/window-private.h"
#include "meta/boxes.h"
#include "meta/window.h"

enum
{
  PROP_0,

  PROP_STAGE_VIEW,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MetaCompositorViewPrivate
{
  ClutterStageView *stage_view;

  MetaWindowActor *top_window_actor;
} MetaCompositorViewPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaCompositorView, meta_compositor_view,
                            G_TYPE_OBJECT)

MetaCompositorView *
meta_compositor_view_new (ClutterStageView *stage_view)
{
  g_assert (stage_view);

  return g_object_new (META_TYPE_COMPOSITOR_VIEW,
                       "stage-view", stage_view,
                       NULL);
}

static MetaWindowActor *
find_top_window_actor_on_view (ClutterStageView *stage_view,
                               GList            *window_actors)
{
  GList *l;

  for (l = g_list_last (window_actors); l; l = l->prev)
    {
      MetaWindowActor *window_actor = l->data;
      MetaWindow *window =
        meta_window_actor_get_meta_window (window_actor);
      MtkRectangle buffer_rect;
      MtkRectangle view_layout;

      if (!window->visible_to_compositor)
        continue;

      meta_window_get_buffer_rect (window, &buffer_rect);
      clutter_stage_view_get_layout (stage_view,
                                     &view_layout);

      if (mtk_rectangle_overlap (&view_layout, &buffer_rect))
        return window_actor;
    }

  return NULL;
}

void
meta_compositor_view_update_top_window_actor (MetaCompositorView *compositor_view,
                                              GList              *window_actors)
{
  MetaCompositorViewPrivate *priv =
    meta_compositor_view_get_instance_private (compositor_view);
  MetaWindowActor *top_window_actor;

  top_window_actor = find_top_window_actor_on_view (priv->stage_view,
                                                    window_actors);

  g_set_weak_pointer (&priv->top_window_actor,
                      top_window_actor);
}

MetaWindowActor *
meta_compositor_view_get_top_window_actor (MetaCompositorView *compositor_view)
{
  MetaCompositorViewPrivate *priv =
    meta_compositor_view_get_instance_private (compositor_view);

  return priv->top_window_actor;
}

ClutterStageView *
meta_compositor_view_get_stage_view (MetaCompositorView *compositor_view)
{
  MetaCompositorViewPrivate *priv =
    meta_compositor_view_get_instance_private (compositor_view);

  return priv->stage_view;
}

static void
meta_compositor_view_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  MetaCompositorView *compositor_view = META_COMPOSITOR_VIEW (object);
  MetaCompositorViewPrivate *priv =
    meta_compositor_view_get_instance_private (compositor_view);

  switch (prop_id)
    {
    case PROP_STAGE_VIEW:
      priv->stage_view = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_compositor_view_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  MetaCompositorView *compositor_view = META_COMPOSITOR_VIEW (object);
  MetaCompositorViewPrivate *priv =
    meta_compositor_view_get_instance_private (compositor_view);

  switch (prop_id)
    {
    case PROP_STAGE_VIEW:
      g_value_set_object (value, priv->stage_view);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_compositor_view_finalize (GObject *object)
{
  MetaCompositorView *compositor_view = META_COMPOSITOR_VIEW (object);
  MetaCompositorViewPrivate *priv =
    meta_compositor_view_get_instance_private (compositor_view);

  g_clear_weak_pointer (&priv->top_window_actor);

  G_OBJECT_CLASS (meta_compositor_view_parent_class)->finalize (object);
}

static void
meta_compositor_view_class_init (MetaCompositorViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = meta_compositor_view_set_property;
  object_class->get_property = meta_compositor_view_get_property;
  object_class->finalize = meta_compositor_view_finalize;

  obj_props[PROP_STAGE_VIEW] =
    g_param_spec_object ("stage-view", NULL, NULL,
                         CLUTTER_TYPE_STAGE_VIEW,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
meta_compositor_view_init (MetaCompositorView *compositor_view)
{
}
