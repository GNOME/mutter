/*
 * Copyright (C) 2017-2026 Red Hat Inc.
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

#include "backends/meta-backend-types.h"
#include "cogl/cogl.h"

typedef enum _MetaStreamCursorMode
{
  META_STREAM_CURSOR_MODE_HIDDEN = 0,
  META_STREAM_CURSOR_MODE_EMBEDDED = 1,
  META_STREAM_CURSOR_MODE_METADATA = 2,
} MetaStreamCursorMode;

#define META_TYPE_STREAM (meta_stream_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaStream, meta_stream, META, STREAM, GObject)

struct _MetaStreamClass
{
  GObjectClass parent_class;

  MetaStreamSource * (* create_source) (MetaStream  *stream,
                                        GError     **error);
  gboolean (* transform_position) (MetaStream *stream,
                                   double      stream_x,
                                   double      stream_y,
                                   double     *x,
                                   double     *y);
};

MetaBackend * meta_stream_get_backend (MetaStream *stream);

gboolean meta_stream_start (MetaStream  *stream,
                            GError     **error);

void meta_stream_stop (MetaStream *stream);

MetaStreamSource * meta_stream_get_source (MetaStream *stream);

const char * meta_stream_get_mapping_id (MetaStream *stream);

void meta_stream_map_input (MetaStream *stream,
                            MetaEis    *eis);

gboolean meta_stream_transform_position (MetaStream *stream,
                                         double      stream_x,
                                         double      stream_y,
                                         double     *x,
                                         double     *y);

MetaStreamCursorMode meta_stream_get_cursor_mode (MetaStream *stream);

gboolean meta_stream_is_configured (MetaStream *stream);

gboolean meta_stream_is_started (MetaStream *stream);

void meta_stream_notify_is_configured (MetaStream *stream);

gboolean meta_stream_get_preferred_modifier (MetaStream      *stream,
                                             CoglPixelFormat  format,
                                             GArray          *modifiers,
                                             int              width,
                                             int              height,
                                             uint64_t        *preferred_modifier);

GArray * meta_stream_query_modifiers (MetaStream      *stream,
                                      CoglPixelFormat  format);

CoglDmaBufHandle * meta_stream_create_dma_buf_handle (MetaStream      *stream,
                                                      CoglPixelFormat  format,
                                                      uint64_t         modifier,
                                                      int              width,
                                                      int              height);
