/*
 * Copyright 2013, 2018 Red Hat, Inc.
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
 */

#pragma once

#include <glib-object.h>

#include "clutter/clutter.h"
#include "core/util-private.h"
#include "meta/meta-cursor.h"
#include "meta/types.h"

#define META_TYPE_CURSOR_XCURSOR meta_cursor_xcursor_get_type ()
G_DECLARE_FINAL_TYPE (MetaCursorXcursor, meta_cursor_xcursor,
                      META, CURSOR_XCURSOR, MetaCursor)

const char * meta_cursor_get_legacy_name (ClutterCursorType cursor);
