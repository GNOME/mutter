/*
 * Copyright (C) 2021 Red Hat
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
 * Author: Bilal Elmoussaoui <belmous@redhat.com>
 * 
 * The code is a modified version of the GDK implementation
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/keyval.h> can be included directly."
#endif

#include "clutter/clutter-types.h"

G_BEGIN_DECLS

CLUTTER_EXPORT
void clutter_keyval_convert_case (unsigned int  symbol,
                                  unsigned int *lower,
                                  unsigned int *upper);

CLUTTER_EXPORT
const char * clutter_keyval_name (unsigned int keyval);

G_END_DECLS
