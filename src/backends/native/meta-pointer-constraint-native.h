/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2020 Red Hat
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
 *     Carlos Garnacho <carlosg@gnome.org>
 */

#pragma once

#include <glib-object.h>

#include "clutter/clutter.h"
#include "backends/meta-pointer-constraint.h"

G_BEGIN_DECLS

#define META_TYPE_POINTER_CONSTRAINT_IMPL_NATIVE (meta_pointer_constraint_impl_native_get_type ())
G_DECLARE_FINAL_TYPE (MetaPointerConstraintImplNative,
                      meta_pointer_constraint_impl_native,
                      META, POINTER_CONSTRAINT_IMPL_NATIVE,
                      MetaPointerConstraintImpl)

MetaPointerConstraintImpl * meta_pointer_constraint_impl_native_new (MetaPointerConstraint *constraint_impl,
                                                                     const MtkRegion       *region,
                                                                     double                 min_edge_distance);

G_END_DECLS
