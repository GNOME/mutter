/*
 * Copyright (C) 2017-2026 Red Hat Inc.
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

#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-stream-monitor.h"
#include "backends/meta-stream-source.h"

#define META_TYPE_STREAM_SOURCE_MONITOR (meta_stream_source_monitor_get_type ())
G_DECLARE_FINAL_TYPE (MetaStreamSourceMonitor,
                      meta_stream_source_monitor,
                      META, STREAM_SOURCE_MONITOR,
                      MetaStreamSource)

MetaStreamSourceMonitor * meta_stream_source_monitor_new (MetaStreamMonitor  *stream_monitor,
                                                          GError            **error);
