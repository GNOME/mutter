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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "wayland/meta-wayland-types.h"
#include "wayland/meta-wayland-subsurface.h"

void meta_wayland_transaction_commit (MetaWaylandTransaction *transaction);

MetaWaylandTransactionEntry *meta_wayland_transaction_ensure_entry (MetaWaylandTransaction *transaction,
                                                                    MetaWaylandSurface     *surface);

void meta_wayland_transaction_add_placement_op (MetaWaylandTransaction           *transaction,
                                                MetaWaylandSurface               *surface,
                                                MetaWaylandSubsurfacePlacementOp *op);

void meta_wayland_transaction_add_subsurface_position (MetaWaylandTransaction *transaction,
                                                       MetaWaylandSurface     *surface,
                                                       int                     x,
                                                       int                     y);

void meta_wayland_transaction_add_xdg_popup_reposition (MetaWaylandTransaction *transaction,
                                                        MetaWaylandSurface     *surface,
                                                        void                   *xdg_positioner,
                                                        uint32_t               token);

void meta_wayland_transaction_merge_into (MetaWaylandTransaction *from,
                                          MetaWaylandTransaction *to);

void meta_wayland_transaction_merge_pending_state (MetaWaylandTransaction *transaction,
                                                   MetaWaylandSurface *surface);

MetaWaylandTransaction *meta_wayland_transaction_new (MetaWaylandCompositor *compositor);

void meta_wayland_transaction_free (MetaWaylandTransaction *transaction);

void meta_wayland_transaction_finalize (MetaWaylandCompositor *compositor);

void meta_wayland_transaction_init (MetaWaylandCompositor *compositor);

int64_t meta_wayland_transaction_get_target_presentation_time_us (const MetaWaylandTransaction *transaction);

gboolean meta_wayland_transaction_unblock_timed (MetaWaylandTransaction *transaction,
                                                 int64_t                 target_time_us);

void meta_wayland_transaction_unblock_surface (MetaWaylandSurface *surface);

void meta_wayland_transaction_consider_surface (MetaWaylandSurface *surface);
