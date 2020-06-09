/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright 2009 Sander Dijkhuis
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
 * Portions adapted from gnome-shell/src/shell-global.c
 */

#include "config.h"

#include "compositor/meta-background-actor-private.h"
#include "compositor/meta-background-content-private.h"

#include "compositor/meta-cullable.h"

enum
{
  PROP_META_DISPLAY = 1,
  PROP_MONITOR,
};

struct _MetaBackgroundActor
{
  ClutterActor parent;

  MetaDisplay *display;
  int monitor;

  MetaBackgroundContent *content;
};

static void cullable_iface_init (MetaCullableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaBackgroundActor, meta_background_actor, CLUTTER_TYPE_ACTOR,
                         G_IMPLEMENT_INTERFACE (META_TYPE_CULLABLE, cullable_iface_init));

static void
maybe_create_content (MetaBackgroundActor *self)
{
  g_autoptr (ClutterContent) content = NULL;

  if (self->content || !self->display || self->monitor == -1)
      return;

  content = meta_background_content_new (self->display, self->monitor);
  self->content = META_BACKGROUND_CONTENT (content);
  clutter_actor_set_content (CLUTTER_ACTOR (self), content);
}

static void
meta_background_actor_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  MetaBackgroundActor *self = META_BACKGROUND_ACTOR (object);

  switch (prop_id)
    {
    case PROP_META_DISPLAY:
      self->display = g_value_get_object (value);
      maybe_create_content (self);
      break;
    case PROP_MONITOR:
      self->monitor = g_value_get_int (value);
      maybe_create_content (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_background_actor_get_property (GObject      *object,
                                    guint         prop_id,
                                    GValue       *value,
                                    GParamSpec   *pspec)
{
  MetaBackgroundActor *self = META_BACKGROUND_ACTOR (object);

  switch (prop_id)
    {
    case PROP_META_DISPLAY:
      g_value_set_object (value, self->display);
      break;
    case PROP_MONITOR:
      g_value_set_int (value, self->monitor);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_background_actor_class_init (MetaBackgroundActorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  object_class->set_property = meta_background_actor_set_property;
  object_class->get_property = meta_background_actor_get_property;

  param_spec = g_param_spec_object ("meta-display",
                                    "MetaDisplay",
                                    "MetaDisplay",
                                    META_TYPE_DISPLAY,
                                    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_property (object_class,
                                   PROP_META_DISPLAY,
                                   param_spec);

  param_spec = g_param_spec_int ("monitor",
                                 "monitor",
                                 "monitor",
                                 0, G_MAXINT, 0,
                                 G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_property (object_class,
                                   PROP_MONITOR,
                                   param_spec);
}

static void
meta_background_actor_init (MetaBackgroundActor *self)
{
  self->monitor = -1;

  clutter_actor_set_request_mode (CLUTTER_ACTOR (self),
                                  CLUTTER_REQUEST_CONTENT_SIZE);
}

/**
 * meta_background_actor_new:
 * @monitor: Index of the monitor for which to draw the background
 *
 * Creates a new actor to draw the background for the given monitor.
 *
 * Return value: the newly created background actor
 */
ClutterActor *
meta_background_actor_new (MetaDisplay *display,
                           int          monitor)
{
  MetaBackgroundActor *self;

  self = g_object_new (META_TYPE_BACKGROUND_ACTOR,
                       "meta-display", display,
                       "monitor", monitor,
                       NULL);

  return CLUTTER_ACTOR (self);
}

static void
meta_background_actor_cull_out (MetaCullable   *cullable,
                                cairo_region_t *unobscured_region,
                                cairo_region_t *clip_region)
{
  MetaBackgroundActor *self = META_BACKGROUND_ACTOR (cullable);

  if (!self->content)
    return;

  meta_background_content_cull_out (self->content,
                                    unobscured_region,
                                    clip_region);
}

static void
meta_background_actor_reset_culling (MetaCullable *cullable)
{
  MetaBackgroundActor *self = META_BACKGROUND_ACTOR (cullable);

  if (!self->content)
    return;

  meta_background_content_reset_culling (self->content);
}

static void
cullable_iface_init (MetaCullableInterface *iface)
{
  iface->cull_out = meta_background_actor_cull_out;
  iface->reset_culling = meta_background_actor_reset_culling;
}

/**
 * meta_background_actor_get_clip_region:
 * @self: a #MetaBackgroundActor
 *
 * Return value (transfer none): a #cairo_region_t that represents the part of
 * the background not obscured by other #MetaBackgroundActor or
 * #MetaWindowActor objects.
 */
cairo_region_t *
meta_background_actor_get_clip_region (MetaBackgroundActor *self)
{
  if (!self->content)
    return NULL;

  return meta_background_content_get_clip_region (self->content);
}
