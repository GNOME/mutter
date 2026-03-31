/*
 * Copyright (C) 2018-2026 Red Hat Inc.
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
#include "meta/window.h"

#define META_TYPE_STREAM_WINDOW (meta_stream_window_get_type ())
G_DECLARE_FINAL_TYPE (MetaStreamWindow,
                      meta_stream_window,
                      META, STREAM_WINDOW,
                      MetaStream)

MetaStreamWindow * meta_stream_window_new (MetaBackend           *backend,
                                           MetaWindow            *window,
                                           MetaStreamCursorMode   cursor_mode,
                                           GError               **error);

MetaWindow * meta_stream_window_get_window (MetaStreamWindow *window_stream);

int meta_stream_window_get_width  (MetaStreamWindow *window_stream);

int meta_stream_window_get_height (MetaStreamWindow *window_stream);
