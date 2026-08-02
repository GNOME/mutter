/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright 2014 Red Hat, Inc.
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

/**
 * MetaFeedbackActor:
 *
 * Actor for painting user interaction feedback
 */

#include "config.h"

#include "backends/meta-logical-monitor-private.h"
#include "compositor/compositor-private.h"
#include "compositor/meta-feedback-actor-private.h"
#include "core/display-private.h"
#include "core/window-private.h"

enum
{
  PROP_0,

  PROP_COMPOSITOR,
  PROP_ANCHOR_X,
  PROP_ANCHOR_Y,

  N_PROPS
};

typedef struct _MetaFeedbackActorPrivate MetaFeedbackActorPrivate;

struct _MetaFeedbackActorPrivate
{
  MetaCompositor *compositor;

  float anchor_x;
  float anchor_y;
  float pos_x;
  float pos_y;

  int geometry_scale;
};

G_DEFINE_TYPE_WITH_PRIVATE (MetaFeedbackActor, meta_feedback_actor, CLUTTER_TYPE_ACTOR)

static void
meta_feedback_actor_apply_transform (ClutterActor *actor, graphene_matrix_t *matrix)
{
  MetaFeedbackActor *self = META_FEEDBACK_ACTOR (actor);
  ClutterActor *parent = clutter_actor_get_parent (actor);
  MetaFeedbackActorPrivate *priv = meta_feedback_actor_get_instance_private (self);
  MetaDisplay *display = meta_compositor_get_display (priv->compositor);
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);
  MetaLogicalMonitor *logical_monitor;
  MtkRectangle monitor_rect;
  float scale;
  float rel_x, rel_y;
  float abs_x, abs_y;
  float adj_rel_x, adj_rel_y;
  float x_off, y_off;

  CLUTTER_ACTOR_CLASS (meta_feedback_actor_parent_class)->apply_transform (actor, matrix);

  if (!parent)
    return;

  logical_monitor =
    meta_monitor_manager_get_logical_monitor_at (monitor_manager, priv->pos_x, priv->pos_y);

  if (!logical_monitor)
    return;

  scale = meta_logical_monitor_get_scale (logical_monitor);
  monitor_rect = meta_logical_monitor_get_layout (logical_monitor);

  abs_x = clutter_actor_get_x (parent) + clutter_actor_get_x (actor);
  abs_y = clutter_actor_get_y (parent) + clutter_actor_get_y (actor);

  rel_x = abs_x - monitor_rect.x;
  rel_y = abs_y - monitor_rect.y;

  adj_rel_x = roundf (rel_x * scale) / scale;
  adj_rel_y = roundf (rel_y * scale) / scale;

  x_off = adj_rel_x - rel_x;
  y_off = adj_rel_y - rel_y;

  if (!G_APPROX_VALUE (x_off, 0.0, FLT_EPSILON) ||
      !G_APPROX_VALUE (y_off, 0.0, FLT_EPSILON))
    {
      graphene_matrix_translate (matrix,
                                 &GRAPHENE_POINT3D_INIT (x_off, y_off, 0));
    }
}

static void
meta_feedback_actor_constructed (GObject *object)
{
  MetaFeedbackActor *self = META_FEEDBACK_ACTOR (object);
  MetaFeedbackActorPrivate *priv =
    meta_feedback_actor_get_instance_private (self);
  ClutterActor *feedback_group;

  G_OBJECT_CLASS (meta_feedback_actor_parent_class)->constructed (object);

  feedback_group = meta_compositor_get_feedback_group (priv->compositor);
  clutter_actor_add_child (feedback_group, CLUTTER_ACTOR (object));
  meta_compositor_disable_unredirect (priv->compositor);
}

static void
meta_feedback_actor_finalize (GObject *object)
{
  MetaFeedbackActor *self = META_FEEDBACK_ACTOR (object);
  MetaFeedbackActorPrivate *priv =
    meta_feedback_actor_get_instance_private (self);

  meta_compositor_enable_unredirect (priv->compositor);

  G_OBJECT_CLASS (meta_feedback_actor_parent_class)->finalize (object);
}

static void
meta_feedback_actor_update_position (MetaFeedbackActor *self)
{
  MetaFeedbackActorPrivate *priv = meta_feedback_actor_get_instance_private (self);

  clutter_actor_set_position (CLUTTER_ACTOR (self),
                              priv->pos_x -
                              (priv->anchor_x * priv->geometry_scale),
                              priv->pos_y -
                              (priv->anchor_y * priv->geometry_scale));
}

static void
meta_feedback_actor_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  MetaFeedbackActor *self = META_FEEDBACK_ACTOR (object);
  MetaFeedbackActorPrivate *priv = meta_feedback_actor_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_COMPOSITOR:
      priv->compositor = g_value_get_object (value);
      break;
    case PROP_ANCHOR_X:
      priv->anchor_x = g_value_get_int (value);
      meta_feedback_actor_update_position (self);
      break;
    case PROP_ANCHOR_Y:
      priv->anchor_y = g_value_get_int (value);
      meta_feedback_actor_update_position (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_feedback_actor_get_property (GObject      *object,
                                  guint         prop_id,
                                  GValue       *value,
                                  GParamSpec   *pspec)
{
  MetaFeedbackActor *self = META_FEEDBACK_ACTOR (object);
  MetaFeedbackActorPrivate *priv = meta_feedback_actor_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_COMPOSITOR:
      g_value_set_object (value, priv->compositor);
      break;
    case PROP_ANCHOR_X:
      g_value_set_float (value, priv->anchor_x);
      break;
    case PROP_ANCHOR_Y:
      g_value_set_float (value, priv->anchor_y);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_feedback_actor_class_init (MetaFeedbackActorClass *klass)
{
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *pspec;

  actor_class->apply_transform = meta_feedback_actor_apply_transform;

  object_class->constructed = meta_feedback_actor_constructed;
  object_class->finalize = meta_feedback_actor_finalize;
  object_class->set_property = meta_feedback_actor_set_property;
  object_class->get_property = meta_feedback_actor_get_property;

  pspec = g_param_spec_object ("compositor", NULL, NULL,
                               META_TYPE_COMPOSITOR,
                               G_PARAM_READWRITE |
                               G_PARAM_STATIC_STRINGS |
                               G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_property (object_class,
                                   PROP_COMPOSITOR,
                                   pspec);

  pspec = g_param_spec_float ("anchor-x", NULL, NULL,
                              0, G_MAXFLOAT, 0,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS);

  g_object_class_install_property (object_class,
                                   PROP_ANCHOR_X,
                                   pspec);

  pspec = g_param_spec_float ("anchor-y", NULL, NULL,
                              0, G_MAXFLOAT, 0,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS);

  g_object_class_install_property (object_class,
                                   PROP_ANCHOR_Y,
                                   pspec);
}

static void
meta_feedback_actor_init (MetaFeedbackActor *self)
{
  clutter_actor_set_reactive (CLUTTER_ACTOR (self), FALSE);
}

void
meta_feedback_actor_set_anchor (MetaFeedbackActor *self,
                                float              anchor_x,
                                float              anchor_y)
{
  MetaFeedbackActorPrivate *priv;

  g_return_if_fail (META_IS_FEEDBACK_ACTOR (self));

  priv = meta_feedback_actor_get_instance_private (self);

  if (priv->anchor_x == anchor_x && priv->anchor_y == anchor_y)
    return;

  if (priv->anchor_x != anchor_x)
    {
      priv->anchor_x = anchor_x;
      g_object_notify (G_OBJECT (self), "anchor-x");
    }

  if (priv->anchor_y != anchor_y)
    {
      priv->anchor_y = anchor_y;
      g_object_notify (G_OBJECT (self), "anchor-y");
    }

  meta_feedback_actor_update_position (self);
}

void
meta_feedback_actor_get_anchor (MetaFeedbackActor *self,
                                float             *anchor_x,
                                float             *anchor_y)
{
  MetaFeedbackActorPrivate *priv;

  g_return_if_fail (META_IS_FEEDBACK_ACTOR (self));

  priv = meta_feedback_actor_get_instance_private (self);

  if (anchor_x)
    *anchor_x = priv->anchor_x;
  if (anchor_y)
    *anchor_y = priv->anchor_y;
}

void
meta_feedback_actor_set_position (MetaFeedbackActor  *self,
                                  float               x,
                                  float               y)
{
  MetaFeedbackActorPrivate *priv;

  g_return_if_fail (META_IS_FEEDBACK_ACTOR (self));

  priv = meta_feedback_actor_get_instance_private (self);
  priv->pos_x = x;
  priv->pos_y = y;

  meta_feedback_actor_update_position (self);
}

void
meta_feedback_actor_set_geometry_scale (MetaFeedbackActor *self,
                                        int                geometry_scale)
{
  MetaFeedbackActorPrivate *priv =
    meta_feedback_actor_get_instance_private (self);
  graphene_matrix_t child_transform;

  if (priv->geometry_scale == geometry_scale)
    return;

  priv->geometry_scale = geometry_scale;

  graphene_matrix_init_scale (&child_transform,
                              geometry_scale,
                              geometry_scale,
                              1);
  clutter_actor_set_child_transform (CLUTTER_ACTOR (self),
                                     &child_transform);
}

int
meta_feedback_actor_get_geometry_scale (MetaFeedbackActor *self)
{
  MetaFeedbackActorPrivate *priv =
    meta_feedback_actor_get_instance_private (self);

  return priv->geometry_scale;
}
