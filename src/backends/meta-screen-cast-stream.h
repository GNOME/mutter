/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2017 Red Hat Inc.
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

#include <glib-object.h>

#include "backends/meta-screen-cast.h"

#include "meta-dbus-screen-cast.h"

#define META_TYPE_SCREEN_CAST_STREAM (meta_screen_cast_stream_get_type ())
G_DECLARE_FINAL_TYPE (MetaScreenCastStream, meta_screen_cast_stream,
                      META, SCREEN_CAST_STREAM,
                      MetaDBusScreenCastStreamSkeleton)

MetaScreenCastSession * meta_screen_cast_stream_get_session (MetaScreenCastStream *screen_cast_stream);

gboolean meta_screen_cast_stream_start (MetaScreenCastStream *screen_cast_stream,
                                        GError              **error);

void meta_screen_cast_stream_close (MetaScreenCastStream *screen_cast_stream);

char * meta_screen_cast_stream_get_object_path (MetaScreenCastStream *screen_cast_stream);

MetaScreenCastFlag meta_screen_cast_stream_get_flags (MetaScreenCastStream *screen_cast_stream);

MetaScreenCastStream * meta_screen_cast_stream_new (MetaScreenCastSession  *session,
                                                    GDBusConnection        *connection,
                                                    MetaStream             *stream,
                                                    MetaScreenCastFlag      flags,
                                                    GError                **error);

MetaStream * meta_screen_cast_stream_get_stream (MetaScreenCastStream *screen_cast_stream);
