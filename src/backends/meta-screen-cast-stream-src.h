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

#ifndef META_SCREEN_CAST_STREAM_SRC_H
#define META_SCREEN_CAST_STREAM_SRC_H

#include <glib-object.h>
#include <spa/param/video/format-utils.h>
#include <spa/buffer/meta.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-cursor-renderer.h"
#include "backends/meta-cursor.h"
#include "backends/meta-renderer.h"
#include "clutter/clutter.h"
#include "meta/boxes.h"
#include "cogl/cogl.h"

typedef struct _MetaSpaType
{
  struct spa_type_media_type media_type;
  struct spa_type_media_subtype media_subtype;
  struct spa_type_format_video format_video;
  struct spa_type_video_format video_format;
  uint32_t meta_cursor;
} MetaSpaType;

typedef struct _MetaScreenCastStream MetaScreenCastStream;

#define META_TYPE_SCREEN_CAST_STREAM_SRC (meta_screen_cast_stream_src_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaScreenCastStreamSrc,
                          meta_screen_cast_stream_src,
                          META, SCREEN_CAST_STREAM_SRC,
                          GObject)

struct _MetaScreenCastStreamSrcClass
{
  GObjectClass parent_class;

  void (* get_specs) (MetaScreenCastStreamSrc *src,
                      int                     *width,
                      int                     *height,
                      float                   *frame_rate);
  void (* enable) (MetaScreenCastStreamSrc *src);
  void (* disable) (MetaScreenCastStreamSrc *src);
  gboolean (* record_frame) (MetaScreenCastStreamSrc *src,
                             uint8_t                 *data);
  gboolean (* get_videocrop) (MetaScreenCastStreamSrc *src,
                              MetaRectangle           *crop_rect);
  void (* set_cursor_metadata) (MetaScreenCastStreamSrc *src,
                                struct spa_meta_cursor  *spa_meta_cursor);

};

void meta_screen_cast_stream_src_maybe_record_frame (MetaScreenCastStreamSrc *src);

MetaScreenCastStream * meta_screen_cast_stream_src_get_stream (MetaScreenCastStreamSrc *src);

MetaSpaType * meta_screen_cast_stream_src_get_spa_type (MetaScreenCastStreamSrc *src);

#endif /* META_SCREEN_CAST_STREAM_SRC_H */
