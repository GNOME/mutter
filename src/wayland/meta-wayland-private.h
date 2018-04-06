/*
 * Copyright (C) 2012 Intel Corporation
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef META_WAYLAND_PRIVATE_H
#define META_WAYLAND_PRIVATE_H

#include <wayland-server.h>
#include <clutter/clutter.h>

#include <glib.h>

#include "window-private.h"
#include <meta/meta-cursor-tracker.h>

#include "meta-wayland.h"
#include "meta-wayland-versions.h"
#include "meta-wayland-surface.h"
#include "meta-wayland-seat.h"
#include "meta-wayland-pointer-gestures.h"
#include "meta-wayland-tablet-manager.h"

typedef struct _MetaXWaylandSelection MetaXWaylandSelection;

typedef struct
{
  struct wl_list link;
  struct wl_resource *resource;
  MetaWaylandSurface *surface;
} MetaWaylandFrameCallback;

typedef struct
{
  int display_index;
  char *lock_file;
  int abstract_fd;
  int unix_fd;
  struct wl_client *client;
  struct wl_resource *xserver_resource;
  char *display_name;

  GCancellable *xserver_died_cancellable;
  GSubprocess *proc;
  GMainLoop *init_loop;

  MetaXWaylandSelection *selection_data;
} MetaXWaylandManager;

struct _MetaWaylandCompositor
{
  struct wl_display *wayland_display;
  const char *display_name;
  GHashTable *outputs;
  struct wl_list frame_callbacks;

  MetaXWaylandManager xwayland_manager;

  MetaWaylandSeat *seat;
  MetaWaylandTabletManager *tablet_manager;

  GHashTable *scheduled_surface_associations;
};

#endif /* META_WAYLAND_PRIVATE_H */
