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

#ifndef __META_SURFACE_ACTOR_EMPTY_H__
#define __META_SURFACE_ACTOR_EMPTY_H__

#include <glib-object.h>

#include "meta-surface-actor.h"

G_BEGIN_DECLS

#define META_TYPE_SURFACE_ACTOR_EMPTY            (meta_surface_actor_empty_get_type ())
#define META_SURFACE_ACTOR_EMPTY(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_SURFACE_ACTOR_EMPTY, MetaSurfaceActorEmpty))
#define META_SURFACE_ACTOR_EMPTY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  META_TYPE_SURFACE_ACTOR_EMPTY, MetaSurfaceActorEmptyClass))
#define META_IS_SURFACE_ACTOR_EMPTY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_SURFACE_ACTOR_EMPTY))
#define META_IS_SURFACE_ACTOR_EMPTY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  META_TYPE_SURFACE_ACTOR_EMPTY))
#define META_SURFACE_ACTOR_EMPTY_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  META_TYPE_SURFACE_ACTOR_EMPTY, MetaSurfaceActorEmptyClass))

typedef struct _MetaSurfaceActorEmpty      MetaSurfaceActorEmpty;
typedef struct _MetaSurfaceActorEmptyClass MetaSurfaceActorEmptyClass;

struct _MetaSurfaceActorEmpty
{
  MetaSurfaceActor parent;
};

struct _MetaSurfaceActorEmptyClass
{
  MetaSurfaceActorClass parent_class;
};

GType meta_surface_actor_empty_get_type (void);

MetaSurfaceActor * meta_surface_actor_empty_new (void);

G_END_DECLS

#endif /* __META_SURFACE_ACTOR_EMPTY_H__ */
