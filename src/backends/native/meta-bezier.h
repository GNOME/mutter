/*
 * Authored By Tomas Frydrych  <tf@openedhand.com>
 *
 * Copyright (C) 2006, 2007 OpenedHand
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

#pragma once

#include <glib.h>


G_BEGIN_DECLS

/* This is used in meta_bezier_advance to represent the full
   length of the bezier curve. Anything less than that represents a
   fraction of the length */
#define META_BEZIER_MAX_LENGTH (1 << 18)

typedef struct _MetaBezier MetaBezier;
typedef struct _MetaBezierKnot MetaBezierKnot;

struct _MetaBezierKnot
{
  gint x;
  gint y;
};

MetaBezier *meta_bezier_new (void);

void        meta_bezier_free (MetaBezier *b);

void        meta_bezier_advance (const MetaBezier *b,
                                 gint              L,
                                 MetaBezierKnot   *knot);

void        meta_bezier_init (MetaBezier *b,
                              gint x_0, gint y_0,
                              gint x_1, gint y_1,
                              gint x_2, gint y_2,
                              gint x_3, gint y_3);

guint       meta_bezier_get_length (const MetaBezier *b);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaBezier, meta_bezier_free);

G_END_DECLS
