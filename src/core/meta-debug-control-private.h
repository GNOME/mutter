/*
 * Copyright (C) 2023 Red Hat
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
 *
 */

#pragma once

#include "meta/meta-debug-control.h"

gboolean meta_debug_control_is_color_management_protocol_enabled (MetaDebugControl *debug_control);

gboolean meta_debug_control_is_linear_blending_forced (MetaDebugControl *debug_control);

gboolean meta_debug_control_is_hdr_enabled (MetaDebugControl *debug_control);

unsigned int meta_debug_control_get_luminance_percentage (MetaDebugControl *debug_control);

gboolean meta_debug_control_is_session_management_protocol_enabled (MetaDebugControl *debug_control);
