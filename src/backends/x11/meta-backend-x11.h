/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
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
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#pragma once

#include <stdint.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput.h>
#include <X11/extensions/XInput2.h>

#include "backends/meta-backend-private.h"
#include "backends/x11/meta-backend-x11-types.h"
#include "backends/x11/meta-clutter-backend-x11.h"

#define META_TYPE_BACKEND_X11 (meta_backend_x11_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaBackendX11, meta_backend_x11,
                          META, BACKEND_X11, MetaBackend)

struct _MetaBackendX11Class
{
  MetaBackendClass parent_class;

  gboolean (* handle_host_xevent) (MetaBackendX11 *x11,
                                   XEvent         *event);
  void (* translate_device_event) (MetaBackendX11 *x11,
                                   XIDeviceEvent  *device_event);
  void (* translate_crossing_event) (MetaBackendX11 *x11,
                                     XIEnterEvent   *enter_event);
};

Display * meta_backend_x11_get_xdisplay (MetaBackendX11 *backend);

Screen * meta_backend_x11_get_xscreen (MetaBackendX11 *backend);

Window meta_backend_x11_get_root_xwindow (MetaBackendX11 *backend_x11);

Window meta_backend_x11_get_xwindow (MetaBackendX11 *backend);

uint8_t meta_backend_x11_get_xkb_event_base (MetaBackendX11 *x11);

void meta_backend_x11_reload_cursor (MetaBackendX11 *x11);

void meta_backend_x11_sync_pointer (MetaBackendX11 *backend_x11);

MetaX11Barriers * meta_backend_x11_get_barriers (MetaBackendX11 *backend_x11);

void meta_backend_x11_reset_cached_logical_monitor (MetaBackendX11 *backend_x11);
