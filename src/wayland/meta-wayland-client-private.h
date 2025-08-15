/*
 * Copyright 2023 Red Hat
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
 */

#pragma once

#include <wayland-server-core.h>

#include "core/meta-service-channel.h"
#include "core/util-private.h"

#include "meta/meta-wayland-client.h"

typedef enum _MetaWaylandClientKind
{
  META_WAYLAND_CLIENT_KIND_PUBLIC,
  META_WAYLAND_CLIENT_KIND_CREATED,
  META_WAYLAND_CLIENT_KIND_SUBPROCESS,
} MetaWaylandClientKind;

typedef enum _MetaWaylandClientCaps
{
  META_WAYLAND_CLIENT_CAPS_X11_INTEROP = (1 << 0),
} MetaWaylandClientCaps;

META_EXPORT_TEST
MetaWaylandClient * meta_wayland_client_new_from_wl (MetaContext      *context,
                                                     struct wl_client *wayland_client);

META_EXPORT_TEST
MetaWaylandClient * meta_wayland_client_new_create (MetaContext  *context,
                                                    pid_t         pid,
                                                    GError      **error);

META_EXPORT_TEST
void meta_wayland_client_destroy (MetaWaylandClient *wayland_client);

META_EXPORT_TEST
MetaContext * meta_wayland_client_get_context (MetaWaylandClient *wayland_client);

META_EXPORT_TEST
struct wl_client * meta_wayland_client_get_wl_client (MetaWaylandClient *wayland_client);

META_EXPORT_TEST
gboolean meta_wayland_client_matches (MetaWaylandClient      *wayland_client,
                                      const struct wl_client *wl_client);

META_EXPORT_TEST
MetaWaylandClientKind meta_wayland_client_get_kind (MetaWaylandClient *wayland_client);

META_EXPORT_TEST
void meta_wayland_client_set_caps (MetaWaylandClient     *wayland_client,
                                   MetaWaylandClientCaps  caps);

META_EXPORT_TEST
MetaWaylandClientCaps meta_wayland_client_get_caps (MetaWaylandClient *wayland_client);

META_EXPORT_TEST
gboolean meta_wayland_client_has_caps (MetaWaylandClient     *wayland_client,
                                       MetaWaylandClientCaps  caps);

META_EXPORT_TEST
int meta_wayland_client_take_client_fd (MetaWaylandClient *client);

META_EXPORT_TEST
MetaWaylandClient * meta_get_wayland_client (const struct wl_client *wl_client);

void meta_wayland_client_set_window_tag (MetaWaylandClient *client,
                                         const char *window_tag);

const char * meta_wayland_client_get_window_tag (MetaWaylandClient *client);