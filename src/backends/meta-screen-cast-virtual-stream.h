/*
 * Copyright (C) 2021 Red Hat Inc.
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

#ifndef META_SCREEN_CAST_VIRTUAL_STREAM_H
#define META_SCREEN_CAST_VIRTUAL_STREAM_H

#include "backends/meta-screen-cast-stream.h"

#define META_TYPE_SCREEN_CAST_VIRTUAL_STREAM (meta_screen_cast_virtual_stream_get_type ())
G_DECLARE_FINAL_TYPE (MetaScreenCastVirtualStream,
                      meta_screen_cast_virtual_stream,
                      META, SCREEN_CAST_VIRTUAL_STREAM,
                      MetaScreenCastStream)

MetaScreenCastVirtualStream * meta_screen_cast_virtual_stream_new (MetaScreenCastSession     *session,
                                                                   GDBusConnection           *connection,
                                                                   MetaScreenCastCursorMode   cursor_mode,
                                                                   MetaScreenCastFlag         flags,
                                                                   GError                   **error);

MetaVirtualMonitor * meta_screen_cast_virtual_stream_get_virtual_monitor (MetaScreenCastVirtualStream *virtual_stream);

#endif /* META_SCREEN_CAST_VIRTUAL_STREAM_H */
