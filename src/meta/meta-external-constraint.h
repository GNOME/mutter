/*
 * Copyright (C) 2025 Red Hat, Inc.
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

#include <glib-object.h>

#include "meta/types.h"
#include "meta/window.h"

G_BEGIN_DECLS

#define META_TYPE_EXTERNAL_CONSTRAINT (meta_external_constraint_get_type ())

META_EXPORT
G_DECLARE_INTERFACE (MetaExternalConstraint,
                     meta_external_constraint,
                     META,
                     EXTERNAL_CONSTRAINT,
                     GObject)

/**
 * MetaExternalConstraintInfo:
 * @new_rect: (inout): the proposed new window rectangle
 * @flags: the constraint flags for this operation
 * @resize_gravity: the gravity for resizing
 *
 * Structure containing parameters for external window constraints.
 */
typedef struct _MetaExternalConstraintInfo MetaExternalConstraintInfo;
struct _MetaExternalConstraintInfo
{
  MtkRectangle *new_rect;
  MetaExternalConstraintFlags flags;
  MetaGravity resize_gravity;
};

/**
 * MetaExternalConstraintInterface:
 * @constrain: virtual function for applying external constraints
 *
 * Interface for objects that can apply external window constraints.
 */
struct _MetaExternalConstraintInterface
{
  GTypeInterface parent_iface;

  /**
   * MetaExternalConstraintInterface::constrain:
   * @constraint: a #MetaExternalConstraint
   * @window: the #MetaWindow being constrained
   * @info: a #MetaExternalConstraintInfo with constraint parameters
   *
   * Virtual function called with other window constraints processing.
   * This allows external components to implement custom window positioning
   * and sizing constraints.
   *
   * The implementation can modify @info->new_rect to enforce its own constraints.
   *
   * Returns: %TRUE if the constraint has fully constrained the window,
              %FALSE otherwise.
   */
  gboolean (*constrain) (MetaExternalConstraint     *constraint,
                         MetaWindow                 *window,
                         MetaExternalConstraintInfo *info);
};

META_EXPORT
gboolean meta_external_constraint_constrain (MetaExternalConstraint     *constraint,
                                             MetaWindow                 *window,
                                             MetaExternalConstraintInfo *info);

G_END_DECLS
