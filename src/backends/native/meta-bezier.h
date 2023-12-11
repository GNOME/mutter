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

#include "core/util-private.h"

G_BEGIN_DECLS

typedef struct _MetaBezier MetaBezier;

META_EXPORT_TEST
MetaBezier * meta_bezier_new (unsigned int precision);

META_EXPORT_TEST
void        meta_bezier_free (MetaBezier *b);

META_EXPORT_TEST
void        meta_bezier_init (MetaBezier *b,
                              double      x_1,
                              double      y_1,
                              double      x_2,
                              double      y_2);

META_EXPORT_TEST
double      meta_bezier_lookup (const MetaBezier *b,
                                double            pos);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaBezier, meta_bezier_free);

G_END_DECLS
