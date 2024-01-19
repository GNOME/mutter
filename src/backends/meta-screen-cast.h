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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include <glib-object.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-dbus-session-manager.h"
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
  META_SCREEN_CAST_FLAG_IS_PLATFORM = 1 << 1,
} MetaScreenCastFlag;

#define META_TYPE_SCREEN_CAST (meta_screen_cast_get_type ())
G_DECLARE_FINAL_TYPE (MetaScreenCast, meta_screen_cast,
                      META, SCREEN_CAST,
                      MetaDbusSessionManager)

MetaBackend * meta_screen_cast_get_backend (MetaScreenCast *screen_cast);

GArray * meta_screen_cast_query_modifiers (MetaScreenCast  *screen_cast,
                                           CoglPixelFormat  format);

gboolean meta_screen_cast_get_preferred_modifier (MetaScreenCast  *screen_cast,
                                                  CoglPixelFormat  format,
                                                  GArray          *modifiers,
                                                  int              width,
                                                  int              height,
                                                  uint64_t        *preferred_modifier);

CoglDmaBufHandle * meta_screen_cast_create_dma_buf_handle (MetaScreenCast  *screen_cast,
                                                           CoglPixelFormat  format,
                                                           uint64_t         modifier,
                                                           int              width,
                                                           int              height);

MetaScreenCast * meta_screen_cast_new (MetaBackend *backend);
