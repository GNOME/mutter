/*
 * Copyright (C) 2024 Red Hat
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

#include <glib-object.h>

#include "meta-dbus-debug-control.h"

#include "meta/meta-base.h"

#define META_TYPE_DEBUG_CONTROL (meta_debug_control_get_type ())
META_EXPORT
G_DECLARE_FINAL_TYPE (MetaDebugControl,
                      meta_debug_control,
                      META, DEBUG_CONTROL,
                      MetaDBusDebugControlSkeleton)

META_EXPORT
void meta_debug_control_set_exported (MetaDebugControl *debug_control,
                                      gboolean          exported);
