/*
 * Copyright (C) 2021 Red Hat, Inc.
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
 */

#ifndef META_WAYLAND_TRANSACTION_H
#define META_WAYLAND_TRANSACTION_H

#include "wayland/meta-wayland-types.h"

void meta_wayland_transaction_commit (MetaWaylandTransaction *transaction);

void meta_wayland_transaction_add_state (MetaWaylandTransaction  *transaction,
                                         MetaWaylandSurface      *surface,
                                         MetaWaylandSurfaceState *state);

MetaWaylandTransaction *meta_wayland_transaction_new (void);

void meta_wayland_transaction_free (MetaWaylandTransaction *transaction);

#endif
