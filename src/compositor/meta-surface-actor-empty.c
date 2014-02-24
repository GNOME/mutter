/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2013 Red Hat
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
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include "config.h"

#include "meta-surface-actor-empty.h"

G_DEFINE_TYPE (MetaSurfaceActorEmpty, meta_surface_actor_empty, META_TYPE_SURFACE_ACTOR)

static void
meta_surface_actor_empty_process_damage (MetaSurfaceActor *actor,
                                         int x, int y, int width, int height)
{
}

static void
meta_surface_actor_empty_pre_paint (MetaSurfaceActor *actor)
{
}

static gboolean
meta_surface_actor_empty_is_argb32 (MetaSurfaceActor *actor)
{
  return FALSE;
}

static gboolean
meta_surface_actor_empty_is_visible (MetaSurfaceActor *actor)
{
  return FALSE;
}

static gboolean
meta_surface_actor_empty_should_unredirect (MetaSurfaceActor *actor)
{
  return FALSE;
}

static void
meta_surface_actor_empty_set_unredirected (MetaSurfaceActor *actor,
                                           gboolean          unredirected)
{
}

static gboolean
meta_surface_actor_empty_is_unredirected (MetaSurfaceActor *actor)
{
  return FALSE;
}

static void
meta_surface_actor_empty_class_init (MetaSurfaceActorEmptyClass *klass)
{
  MetaSurfaceActorClass *surface_actor_class = META_SURFACE_ACTOR_CLASS (klass);

  surface_actor_class->process_damage = meta_surface_actor_empty_process_damage;
  surface_actor_class->pre_paint = meta_surface_actor_empty_pre_paint;
  surface_actor_class->is_argb32 = meta_surface_actor_empty_is_argb32;
  surface_actor_class->is_visible = meta_surface_actor_empty_is_visible;

  surface_actor_class->should_unredirect = meta_surface_actor_empty_should_unredirect;
  surface_actor_class->set_unredirected = meta_surface_actor_empty_set_unredirected;
  surface_actor_class->is_unredirected = meta_surface_actor_empty_is_unredirected;
}

static void
meta_surface_actor_empty_init (MetaSurfaceActorEmpty *self)
{
}

MetaSurfaceActor *
meta_surface_actor_empty_new (void)
{
  return g_object_new (META_TYPE_SURFACE_ACTOR_EMPTY, NULL);
}
