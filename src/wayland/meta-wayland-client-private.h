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

META_EXPORT_TEST
MetaWaylandClient * meta_wayland_client_new_indirect (MetaContext  *context,
                                                      GError      **error);

META_EXPORT_TEST
int meta_wayland_client_setup_fd (MetaWaylandClient  *client,
                                  GError            **error);

META_EXPORT_TEST
gboolean meta_wayland_client_matches (MetaWaylandClient      *client,
                                      const struct wl_client *wayland_client);

void meta_wayland_client_assign_service_client_type (MetaWaylandClient     *client,
                                                     MetaServiceClientType  service_client_type);

MetaServiceClientType  meta_wayland_client_get_service_client_type (MetaWaylandClient *client);
