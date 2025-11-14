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

#include "config.h"

#include "meta/meta-external-constraint.h"

G_DEFINE_INTERFACE (MetaExternalConstraint, meta_external_constraint, G_TYPE_OBJECT)

static void
meta_external_constraint_default_init (MetaExternalConstraintInterface *iface)
{
}

/**
 * meta_external_constraint_constrain:
 * @constraint: a #MetaExternalConstraint
 * @window: the #MetaWindow being constrained
 * @info: a #MetaExternalConstraintInfo with constraint parameters
 *
 * Applies external constraints to a window's position and size.
 *
 * Returns: %TRUE if the constraint has fully constrained the window,
            %FALSE otherwise.
 */
gboolean
meta_external_constraint_constrain (MetaExternalConstraint     *constraint,
                                    MetaWindow                 *window,
                                    MetaExternalConstraintInfo *info)
{
  MetaExternalConstraintInterface *iface;

  g_return_val_if_fail (META_IS_EXTERNAL_CONSTRAINT (constraint), TRUE);
  g_return_val_if_fail (META_IS_WINDOW (window), TRUE);
  g_return_val_if_fail (info != NULL, TRUE);
  g_return_val_if_fail (info->new_rect != NULL, TRUE);

  iface = META_EXTERNAL_CONSTRAINT_GET_IFACE (constraint);

  if (iface->constrain)
    return iface->constrain (constraint, window, info);

  return TRUE;
}
