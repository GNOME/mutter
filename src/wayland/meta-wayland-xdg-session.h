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

#include <wayland-server-core.h>

#include "wayland/meta-wayland-types.h"
#include "wayland/meta-wayland-xdg-session-state.h"

#define META_TYPE_WAYLAND_XDG_SESSION (meta_wayland_xdg_session_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandXdgSession,
                      meta_wayland_xdg_session,
                      META, WAYLAND_XDG_SESSION,
                      GObject)

MetaWaylandXdgSession * meta_wayland_xdg_session_new (MetaWaylandXdgSessionState *session_state,
                                                      struct wl_client           *wl_client,
                                                      uint32_t                    version,
                                                      uint32_t                    id);

const char * meta_wayland_xdg_session_get_id (MetaWaylandXdgSession *session);

void meta_wayland_xdg_session_emit_created (MetaWaylandXdgSession *session);

void meta_wayland_xdg_session_emit_replaced (MetaWaylandXdgSession *session);

void meta_wayland_xdg_session_emit_restored (MetaWaylandXdgSession *session);

gboolean meta_wayland_xdg_session_is_same_client (MetaWaylandXdgSession *session,
                                                  struct wl_client      *client);
