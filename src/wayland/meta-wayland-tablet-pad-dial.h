/*
 * Wayland Support
 *
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
 */

#pragma once

#include <glib.h>
#include <wayland-server.h>

#include "backends/meta-cursor-renderer.h"
#include "wayland/meta-wayland-types.h"

struct _MetaWaylandTabletPadDial
{
  MetaWaylandTabletPad *pad;
  MetaWaylandTabletPadGroup *group;

  struct wl_list resource_list;
  struct wl_list focus_resource_list;

  gchar *feedback;
};

MetaWaylandTabletPadDial * meta_wayland_tablet_pad_dial_new  (MetaWaylandTabletPad      *pad);
void                       meta_wayland_tablet_pad_dial_free  (MetaWaylandTabletPadDial *dial);

void                       meta_wayland_tablet_pad_dial_set_group (MetaWaylandTabletPadDial  *dial,
                                                                   MetaWaylandTabletPadGroup *group);

struct wl_resource *
meta_wayland_tablet_pad_dial_create_new_resource (MetaWaylandTabletPadDial *dial,
                                                  struct wl_client         *client,
                                                  struct wl_resource       *group_resource,
                                                  uint32_t                  id);

gboolean     meta_wayland_tablet_pad_dial_handle_event        (MetaWaylandTabletPadDial *dial,
                                                               const ClutterEvent       *event);

void         meta_wayland_tablet_pad_dial_sync_focus          (MetaWaylandTabletPadDial *dial);
