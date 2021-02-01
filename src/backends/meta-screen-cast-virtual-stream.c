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

#include "config.h"

#include "backends/meta-screen-cast-virtual-stream.h"

#include "backends/meta-screen-cast-virtual-stream-src.h"
#include "backends/meta-virtual-monitor.h"


struct _MetaScreenCastVirtualStream
{
  MetaScreenCastStream parent;
};

G_DEFINE_TYPE (MetaScreenCastVirtualStream,
               meta_screen_cast_virtual_stream,
               META_TYPE_SCREEN_CAST_STREAM)

MetaScreenCastVirtualStream *
meta_screen_cast_virtual_stream_new (MetaScreenCastSession     *session,
                                     GDBusConnection           *connection,
                                     MetaScreenCastCursorMode   cursor_mode,
                                     MetaScreenCastFlag         flags,
                                     GError                   **error)
{
  MetaScreenCastVirtualStream *virtual_stream;

  virtual_stream = g_initable_new (META_TYPE_SCREEN_CAST_VIRTUAL_STREAM,
                                   NULL,
                                   error,
                                   "session", session,
                                   "connection", connection,
                                   "cursor-mode", cursor_mode,
                                   "flags", flags,
                                   NULL);
  if (!virtual_stream)
    return NULL;

  return virtual_stream;
}

static MetaScreenCastStreamSrc *
meta_screen_cast_virtual_stream_create_src (MetaScreenCastStream  *stream,
                                           GError               **error)
{
  MetaScreenCastVirtualStream *virtual_stream =
    META_SCREEN_CAST_VIRTUAL_STREAM (stream);
  MetaScreenCastVirtualStreamSrc *virtual_stream_src;

  virtual_stream_src = meta_screen_cast_virtual_stream_src_new (virtual_stream,
                                                                error);
  if (!virtual_stream_src)
    return NULL;

  return META_SCREEN_CAST_STREAM_SRC (virtual_stream_src);
}

static void
meta_screen_cast_virtual_stream_set_parameters (MetaScreenCastStream *stream,
                                                GVariantBuilder      *parameters_builder)
{
}

static gboolean
meta_screen_cast_virtual_stream_transform_position (MetaScreenCastStream *stream,
                                                    double                stream_x,
                                                    double                stream_y,
                                                    double               *x,
                                                    double               *y)
{
  MetaScreenCastStreamSrc *src = meta_screen_cast_stream_get_src (stream);
  MetaScreenCastVirtualStreamSrc *virtual_src =
    META_SCREEN_CAST_VIRTUAL_STREAM_SRC (src);
  ClutterStageView *view;
  MetaRectangle view_layout;

  view = meta_screen_cast_virtual_stream_src_get_view (virtual_src);
  if (!view)
    return FALSE;

  clutter_stage_view_get_layout (view, &view_layout);
  *x = stream_x + view_layout.x;
  *y = stream_y + view_layout.y;

  return TRUE;
}

static void
meta_screen_cast_virtual_stream_init (MetaScreenCastVirtualStream *virtual_stream)
{
}

static void
meta_screen_cast_virtual_stream_class_init (MetaScreenCastVirtualStreamClass *klass)
{
  MetaScreenCastStreamClass *stream_class =
    META_SCREEN_CAST_STREAM_CLASS (klass);

  stream_class->create_src = meta_screen_cast_virtual_stream_create_src;
  stream_class->set_parameters = meta_screen_cast_virtual_stream_set_parameters;
  stream_class->transform_position = meta_screen_cast_virtual_stream_transform_position;
}
