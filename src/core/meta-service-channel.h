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

#ifdef HAVE_WAYLAND

#include "meta/meta-context.h"

#include "core/util-private.h"
#include "wayland/meta-wayland-types.h"

#include "meta-dbus-service-channel.h"

typedef enum _MetaServiceClientType
{
  META_SERVICE_CLIENT_TYPE_NONE,
  META_SERVICE_CLIENT_TYPE_PORTAL_BACKEND,
  META_SERVICE_CLIENT_TYPE_FILECHOOSER_PORTAL_BACKEND,
  META_SERVICE_CLIENT_TYPE_GLOBAL_SHORTCUTS_PORTAL_BACKEND,
} MetaServiceClientType;

#define META_TYPE_SERVICE_CHANNEL (meta_service_channel_get_type ())
G_DECLARE_FINAL_TYPE (MetaServiceChannel, meta_service_channel,
                      META, SERVICE_CHANNEL,
                      MetaDBusServiceChannelSkeleton)

MetaServiceChannel * meta_service_channel_new (MetaContext *context);

META_EXPORT_TEST
MetaWaylandClient * meta_service_channel_get_service_client (MetaServiceChannel    *service_channel,
                                                             MetaServiceClientType  service_client_type);

#endif /* HAVE_WAYLAND */
