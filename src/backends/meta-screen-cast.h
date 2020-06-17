/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2015-2017 Red Hat Inc.
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
 *
 */

#ifndef META_SCREEN_CAST_H
#define META_SCREEN_CAST_H

#include <glib-object.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-dbus-session-watcher.h"

#include "meta-dbus-screen-cast.h"

typedef enum _MetaScreenCastCursorMode
{
  META_SCREEN_CAST_CURSOR_MODE_HIDDEN = 0,
  META_SCREEN_CAST_CURSOR_MODE_EMBEDDED = 1,
  META_SCREEN_CAST_CURSOR_MODE_METADATA = 2,
} MetaScreenCastCursorMode;

typedef enum _MetaScreenCastFlag
{
  META_SCREEN_CAST_FLAG_NONE = 0,
  META_SCREEN_CAST_FLAG_IS_RECORDING = 1 << 0,
} MetaScreenCastFlag;

#define META_TYPE_SCREEN_CAST (meta_screen_cast_get_type ())
G_DECLARE_FINAL_TYPE (MetaScreenCast, meta_screen_cast,
                      META, SCREEN_CAST,
                      MetaDBusScreenCastSkeleton)

void meta_screen_cast_inhibit (MetaScreenCast *screen_cast);

void meta_screen_cast_uninhibit (MetaScreenCast *screen_cast);

GDBusConnection * meta_screen_cast_get_connection (MetaScreenCast *screen_cast);

MetaBackend * meta_screen_cast_get_backend (MetaScreenCast *screen_cast);

void meta_screen_cast_disable_dma_bufs (MetaScreenCast *screen_cast);

CoglDmaBufHandle * meta_screen_cast_create_dma_buf_handle (MetaScreenCast *screen_cast,
                                                           int             width,
                                                           int             height);

MetaScreenCast * meta_screen_cast_new (MetaBackend            *backend,
                                       MetaDbusSessionWatcher *session_watcher);

#endif /* META_SCREEN_CAST_H */
