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
#include "backends/x11/meta-backend-x11-types.h"

G_BEGIN_DECLS

#define META_TYPE_BARRIER_IMPL_X11 (meta_barrier_impl_x11_get_type ())
G_DECLARE_FINAL_TYPE (MetaBarrierImplX11,
                      meta_barrier_impl_x11,
                      META, BARRIER_IMPL_X11,
                      MetaBarrierImpl)

MetaBarrierImpl *meta_barrier_impl_x11_new (MetaBarrier *barrier);

MetaX11Barriers * meta_x11_barriers_new (MetaBackendX11 *backend_x11);

void meta_x11_barriers_free (MetaX11Barriers *x11_barriers);

gboolean meta_x11_barriers_process_xevent (MetaX11Barriers *barriers,
                                           XIEvent         *event);

G_END_DECLS
