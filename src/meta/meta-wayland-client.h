/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright 2019 Sergio Costas (rastersoft@gmail.com)
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

#include <glib-object.h>
#include <gio/gio.h>

#include "meta/display.h"

G_BEGIN_DECLS

#define META_TYPE_WAYLAND_CLIENT (meta_wayland_client_get_type ())
META_EXPORT
G_DECLARE_FINAL_TYPE (MetaWaylandClient, meta_wayland_client, META, WAYLAND_CLIENT, GObject)

META_EXPORT
MetaWaylandClient *meta_wayland_client_new (MetaContext          *context,
                                            GSubprocessLauncher  *launcher,
                                            GError              **error);

META_EXPORT
GSubprocess *meta_wayland_client_spawn (MetaWaylandClient  *client,
                                        MetaDisplay        *display,
                                        GError            **error,
                                        const char         *argv0,
                                        ...) G_GNUC_NULL_TERMINATED;

META_EXPORT
GSubprocess *meta_wayland_client_spawnv (MetaWaylandClient   *client,
                                         MetaDisplay         *display,
                                         const char * const  *argv,
                                         GError             **error);

META_EXPORT
gboolean meta_wayland_client_owns_window (MetaWaylandClient *client,
                                          MetaWindow        *window);

META_EXPORT
void meta_wayland_client_hide_from_window_list (MetaWaylandClient *client,
                                                MetaWindow        *window);

META_EXPORT
void meta_wayland_client_show_in_window_list (MetaWaylandClient *client,
                                              MetaWindow        *window);

META_EXPORT
void meta_wayland_client_make_desktop (MetaWaylandClient *client,
                                       MetaWindow        *window);

META_EXPORT
void meta_wayland_client_make_dock (MetaWaylandClient *client,
                                    MetaWindow        *window);

G_END_DECLS

