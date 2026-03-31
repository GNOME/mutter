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

#include "backends/meta-stream-source.h"
#include "backends/meta-stream-window.h"

#define META_TYPE_STREAM_SOURCE_WINDOW (meta_stream_source_window_get_type ())
G_DECLARE_FINAL_TYPE (MetaStreamSourceWindow,
                      meta_stream_source_window,
                      META, STREAM_SOURCE_WINDOW,
                      MetaStreamSource)

MetaStreamSourceWindow * meta_stream_source_window_new (MetaStreamWindow  *window_stream,
                                                        GError           **error);
