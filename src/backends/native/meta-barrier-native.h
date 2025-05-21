/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2015 Red Hat
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

#pragma once

#include "backends/meta-barrier-private.h"

G_BEGIN_DECLS

#define META_TYPE_BARRIER_IMPL_NATIVE (meta_barrier_impl_native_get_type ())
G_DECLARE_FINAL_TYPE (MetaBarrierImplNative,
                      meta_barrier_impl_native,
                      META, BARRIER_IMPL_NATIVE,
                      MetaBarrierImpl)

typedef struct _MetaBarrierManagerNative     MetaBarrierManagerNative;


MetaBarrierImpl *meta_barrier_impl_native_new (MetaBarrier *barrier);

MetaBarrierManagerNative *meta_barrier_manager_native_new (void);
void meta_barrier_manager_native_process_in_impl (MetaBarrierManagerNative *manager,
                                                  uint32_t                  time,
                                                  graphene_point_t          prev,
                                                  graphene_point_t         *new_inout);

G_END_DECLS
