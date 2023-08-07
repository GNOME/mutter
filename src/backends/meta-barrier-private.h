/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014-2015 Red Hat
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
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 *     Jonas Ådahl <jadahl@gmail.com>
 */

#pragma once

#include "core/meta-border.h"
#include "meta/barrier.h"

G_BEGIN_DECLS

#define META_TYPE_BARRIER_IMPL (meta_barrier_impl_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaBarrierImpl,
                          meta_barrier_impl,
                          META, BARRIER_IMPL,
                          GObject)

struct _MetaBarrierImplClass
{
  GObjectClass parent_class;

  gboolean (*is_active) (MetaBarrierImpl *barrier);
  void (*release) (MetaBarrierImpl  *barrier,
                   MetaBarrierEvent *event);
  void (*destroy) (MetaBarrierImpl *barrier);
};

void meta_barrier_emit_hit_signal (MetaBarrier      *barrier,
                                   MetaBarrierEvent *event);
void meta_barrier_emit_left_signal (MetaBarrier      *barrier,
                                    MetaBarrierEvent *event);

void meta_barrier_event_unref (MetaBarrierEvent *event);

MetaBackend * meta_barrier_get_backend (MetaBarrier *barrier);

MetaBorder * meta_barrier_get_border (MetaBarrier *barrier);

MetaBarrierFlags meta_barrier_get_flags (MetaBarrier *barrier);

G_END_DECLS
