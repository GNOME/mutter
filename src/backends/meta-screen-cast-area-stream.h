/*
 * Copyright (C) 2020 Red Hat Inc.
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

#ifndef META_SCREEN_CAST_AREA_STREAM_H
#define META_SCREEN_CAST_AREA_STREAM_H

#include <glib-object.h>

#include "backends/meta-screen-cast-stream.h"
#include "backends/meta-screen-cast.h"

#define META_TYPE_SCREEN_CAST_AREA_STREAM (meta_screen_cast_area_stream_get_type ())
G_DECLARE_FINAL_TYPE (MetaScreenCastAreaStream,
                      meta_screen_cast_area_stream,
                      META, SCREEN_CAST_AREA_STREAM,
                      MetaScreenCastStream)

MetaScreenCastAreaStream * meta_screen_cast_area_stream_new (MetaScreenCastSession     *session,
                                                             GDBusConnection           *connection,
                                                             MetaRectangle             *area,
                                                             ClutterStage              *stage,
                                                             MetaScreenCastCursorMode   cursor_mode,
                                                             MetaScreenCastFlag         flags,
                                                             GError                   **error);

ClutterStage * meta_screen_cast_area_stream_get_stage (MetaScreenCastAreaStream *area_stream);

MetaRectangle * meta_screen_cast_area_stream_get_area (MetaScreenCastAreaStream *area_stream);

float meta_screen_cast_area_stream_get_scale (MetaScreenCastAreaStream *area_stream);

#endif /* META_SCREEN_CAST_AREA_STREAM_H */
