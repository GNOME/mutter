/*
 * Copyright (C) 2024 Red Hat
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

#include "core/meta-session-state.h"
#include "wayland/meta-wayland-types.h"

typedef struct _MetaWaylandXdgToplevelState MetaWaylandXdgToplevelState;

#define META_TYPE_WAYLAND_XDG_SESSION_STATE (meta_wayland_xdg_session_state_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandXdgSessionState,
                      meta_wayland_xdg_session_state,
                      META, WAYLAND_XDG_SESSION_STATE,
                      MetaSessionState)
