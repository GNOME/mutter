/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat Inc.
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
 *     Jonas Ådahl <jadahl@gmail.com>
 */

#pragma once

#include <glib.h>
#include <glib-object.h>

#include "cogl/cogl.h"
#include "wayland/meta-wayland-types.h"

gboolean meta_wayland_eglstream_controller_init (MetaWaylandCompositor *compositor);

#define META_TYPE_WAYLAND_EGL_STREAM (meta_wayland_egl_stream_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandEglStream, meta_wayland_egl_stream,
                      META, WAYLAND_EGL_STREAM, GObject);

gboolean meta_wayland_is_egl_stream_buffer (MetaWaylandBuffer *buffer);

MetaWaylandEglStream * meta_wayland_egl_stream_new (MetaWaylandBuffer *buffer,
                                                    GError           **error);

gboolean meta_wayland_egl_stream_attach (MetaWaylandEglStream *stream,
                                         GError              **error);

CoglTexture * meta_wayland_egl_stream_create_texture (MetaWaylandEglStream *stream,
                                                      GError              **error);
CoglSnippet * meta_wayland_egl_stream_create_snippet (MetaWaylandEglStream *stream);

gboolean meta_wayland_egl_stream_is_y_inverted (MetaWaylandEglStream *stream);
