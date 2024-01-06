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

#include "compositor/compositor-private.h"
#include "compositor/meta-feedback-actor-private.h"
#include "core/display-private.h"

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
meta_feedback_actor_constructed (GObject *object)
{
  MetaFeedbackActor *self = META_FEEDBACK_ACTOR (object);
  MetaFeedbackActorPrivate *priv =
    meta_feedback_actor_get_instance_private (self);
  MetaDisplay *display = meta_compositor_get_display (priv->compositor);
  ClutterActor *feedback_group;

  feedback_group = meta_compositor_get_feedback_group (priv->compositor);
  clutter_actor_add_child (feedback_group, CLUTTER_ACTOR (object));
  meta_disable_unredirect_for_display (display);
}

static void
meta_feedback_actor_finalize (GObject *object)
{
  MetaFeedbackActor *self = META_FEEDBACK_ACTOR (object);
  MetaFeedbackActorPrivate *priv =
    meta_feedback_actor_get_instance_private (self);
  MetaDisplay *display = meta_compositor_get_display (priv->compositor);

  meta_enable_unredirect_for_display (display);

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
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *pspec;

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
meta_feedback_actor_update (MetaFeedbackActor  *self,
                            const ClutterEvent *event)
{
  graphene_point_t point;

  g_return_if_fail (META_IS_FEEDBACK_ACTOR (self));
  g_return_if_fail (event != NULL);

  clutter_event_get_position (event, &point);
  meta_feedback_actor_set_position (self, point.x, point.y);
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
