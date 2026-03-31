/*
 * Copyright (C) 2020-2026 Red Hat Inc.
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

#include "backends/meta-stream.h"
#include "clutter/clutter.h"

#define META_TYPE_STREAM_AREA (meta_stream_area_get_type ())
G_DECLARE_FINAL_TYPE (MetaStreamArea, meta_stream_area,
                      META, STREAM_AREA,
                      MetaStream)

MetaStreamArea * meta_stream_area_new (MetaBackend           *backend,
                                       MtkRectangle          *area,
                                       MetaStreamCursorMode   cursor_mode,
                                       GError               **error);

void meta_stream_area_get_area (MetaStreamArea *area_stream,
                                MtkRectangle   *area);

float meta_stream_area_get_scale (MetaStreamArea *stream_area);
