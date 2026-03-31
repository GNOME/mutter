/*
 * Copyright (C) 2021-2026 Red Hat Inc.
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

#include "backends/meta-stream.h"

#define META_TYPE_STREAM_VIRTUAL (meta_stream_virtual_get_type ())
G_DECLARE_FINAL_TYPE (MetaStreamVirtual, meta_stream_virtual,
                      META, STREAM_VIRTUAL,
                      MetaStream)

MetaStreamVirtual * meta_stream_virtual_new (MetaBackend          *backend,
                                             MetaStreamCursorMode  cursor_mode,
                                             GList                *mode_infos);

MetaVirtualMonitor * meta_stream_virtual_get_virtual_monitor (MetaStreamVirtual *stream_virtual);
