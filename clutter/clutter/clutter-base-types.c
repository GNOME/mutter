/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *
 * Copyright (C) 2006 OpenedHand
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * SECTION:clutter-geometric-types
 * @Title: Base geometric types
 * @Short_Description: Common geometric data types used by Clutter
 *
 * Clutter defines a set of geometric data structures that are commonly used
 * across the whole API.
 */

#include "clutter-build-config.h"

#include "clutter-types.h"
#include "clutter-private.h"

#include <math.h>

#define FLOAT_EPSILON   (1e-15)



/*
 * ClutterMargin
 */

/**
 * clutter_margin_new:
 *
 * Creates a new #ClutterMargin.
 *
 * Return value: (transfer full): a newly allocated #ClutterMargin. Use
 *   clutter_margin_free() to free the resources associated with it when
 *   done.
 *
 * Since: 1.10
 */
ClutterMargin *
clutter_margin_new (void)
{
  return g_new0 (ClutterMargin, 1);
}

/**
 * clutter_margin_copy:
 * @margin_: a #ClutterMargin
 *
 * Creates a new #ClutterMargin and copies the contents of @margin_ into
 * the newly created structure.
 *
 * Return value: (transfer full): a copy of the #ClutterMargin.
 *
 * Since: 1.10
 */
ClutterMargin *
clutter_margin_copy (const ClutterMargin *margin_)
{
  if (G_LIKELY (margin_ != NULL))
    return g_memdup2 (margin_, sizeof (ClutterMargin));

  return NULL;
}

/**
 * clutter_margin_free:
 * @margin_: a #ClutterMargin
 *
 * Frees the resources allocated by clutter_margin_new() and
 * clutter_margin_copy().
 *
 * Since: 1.10
 */
void
clutter_margin_free (ClutterMargin *margin_)
{
  if (G_LIKELY (margin_ != NULL))
    g_free (margin_);
}

G_DEFINE_BOXED_TYPE (ClutterMargin, clutter_margin,
                     clutter_margin_copy,
                     clutter_margin_free)
