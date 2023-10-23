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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <glib.h>
#include <wayland-server.h>

#include "clutter/clutter.h"
#include "core/window-private.h"
#include "meta/meta-cursor-tracker.h"
#include "meta/meta-wayland-compositor.h"
#include "wayland/meta-wayland-pointer-gestures.h"
#include "wayland/meta-wayland-presentation-time-private.h"
#include "wayland/meta-wayland-seat.h"
#include "wayland/meta-wayland-surface-private.h"
#include "wayland/meta-wayland-tablet-manager.h"
#include "wayland/meta-wayland-versions.h"
#include "wayland/meta-wayland.h"

typedef struct _MetaXWaylandDnd MetaXWaylandDnd;

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
  char *name;
} MetaXWaylandConnection;

struct _MetaXWaylandManager
{
  MetaWaylandCompositor *compositor;

  MetaXWaylandConnection private_connection;
  MetaXWaylandConnection public_connection;

  guint abstract_fd_watch_id;
  guint unix_fd_watch_id;

  gulong prepare_shutdown_id;

  struct wl_display *wayland_display;
  struct wl_client *client;
  struct wl_resource *xserver_resource;
  char *auth_file;

  GCancellable *xserver_died_cancellable;
  GSubprocess *proc;

  MetaXWaylandDnd *dnd;

  gboolean has_xrandr;
  int rr_event_base;
  int rr_error_base;

  gboolean should_enable_ei_portal;
};

struct _MetaWaylandCompositor
{
  GObject parent;

  MetaContext *context;

  struct wl_display *wayland_display;
  char *display_name;
  GSource *source;

  GHashTable *outputs;
  GList *frame_callback_surfaces;

#ifdef HAVE_XWAYLAND
  MetaXWaylandManager xwayland_manager;
#endif

  MetaWaylandSeat *seat;
  MetaWaylandTabletManager *tablet_manager;
  MetaWaylandActivation *activation;
  MetaWaylandXdgForeign *foreign;

  GHashTable *scheduled_surface_associations;

  MetaWaylandPresentationTime presentation_time;
  MetaWaylandDmaBufManager *dma_buf_manager;

  /*
   * Queue of transactions which have been committed but not applied yet, in the
   * order they were committed.
   */
  GQueue committed_transactions;
};

gboolean meta_wayland_compositor_is_egl_display_bound (MetaWaylandCompositor *compositor);
