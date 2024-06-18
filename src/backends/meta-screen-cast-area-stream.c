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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include "backends/meta-screen-cast-area-stream.h"

#include "backends/meta-eis.h"
#include "backends/meta-screen-cast-area-stream-src.h"

struct _MetaScreenCastAreaStream
{
  MetaScreenCastStream parent;

  ClutterStage *stage;

  MtkRectangle area;
  float scale;
};

static void meta_eis_viewport_iface_init (MetaEisViewportInterface *eis_viewport_iface);

G_DEFINE_TYPE_WITH_CODE (MetaScreenCastAreaStream,
                         meta_screen_cast_area_stream,
                         META_TYPE_SCREEN_CAST_STREAM,
                         G_IMPLEMENT_INTERFACE (META_TYPE_EIS_VIEWPORT,
                                                meta_eis_viewport_iface_init))

ClutterStage *
meta_screen_cast_area_stream_get_stage (MetaScreenCastAreaStream *area_stream)
{
  return area_stream->stage;
}

MtkRectangle *
meta_screen_cast_area_stream_get_area (MetaScreenCastAreaStream *area_stream)
{
  return &area_stream->area;
}

float
meta_screen_cast_area_stream_get_scale (MetaScreenCastAreaStream *area_stream)
{
  return area_stream->scale;
}

static gboolean
calculate_scale (ClutterStage *stage,
                 MtkRectangle *area,
                 float        *out_scale)
{
  GList *l;
  float scale = 0.0;

  for (l = clutter_stage_peek_stage_views (stage); l; l = l->next)
    {
      ClutterStageView *stage_view = l->data;
      MtkRectangle view_layout;

      clutter_stage_view_get_layout (stage_view, &view_layout);
      if (mtk_rectangle_overlap (area, &view_layout))
        scale = MAX (clutter_stage_view_get_scale (stage_view), scale);
    }

  if (scale == 0.0)
    return FALSE;

  *out_scale = scale;
  return TRUE;
}

MetaScreenCastAreaStream *
meta_screen_cast_area_stream_new (MetaScreenCastSession     *session,
                                  GDBusConnection           *connection,
                                  MtkRectangle              *area,
                                  ClutterStage              *stage,
                                  MetaScreenCastCursorMode   cursor_mode,
                                  MetaScreenCastFlag         flags,
                                  GError                   **error)
{
  MetaScreenCastAreaStream *area_stream;
  float scale;

  if (!calculate_scale (stage, area, &scale))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Area is off-screen");
      return NULL;
    }

  area_stream = g_initable_new (META_TYPE_SCREEN_CAST_AREA_STREAM,
                                NULL,
                                error,
                                "session", session,
                                "connection", connection,
                                "cursor-mode", cursor_mode,
                                "flags", flags,
                                "is-configured", TRUE,
                                NULL);
  if (!area_stream)
    return NULL;

  area_stream->area = *area;
  area_stream->scale = scale;
  area_stream->stage = stage;

  return area_stream;
}

static gboolean
meta_screen_cast_area_stream_is_standalone (MetaEisViewport *viewport)
{
  return TRUE;
}

static const char *
meta_screen_cast_area_stream_get_mapping_id (MetaEisViewport *viewport)
{
  MetaScreenCastStream *stream = META_SCREEN_CAST_STREAM (viewport);

  return meta_screen_cast_stream_get_mapping_id (stream);
}

static gboolean
meta_screen_cast_area_stream_get_position (MetaEisViewport *viewport,
                                           int             *out_x,
                                           int             *out_y)
{
  return FALSE;
}

static void
meta_screen_cast_area_stream_get_size (MetaEisViewport *viewport,
                                       int             *out_width,
                                       int             *out_height)
{
  MetaScreenCastAreaStream *area_stream =
    META_SCREEN_CAST_AREA_STREAM (viewport);

  *out_width = (int) roundf (area_stream->area.width * area_stream->scale);
  *out_height = (int) roundf (area_stream->area.height * area_stream->scale);
}

static double
meta_screen_cast_area_stream_get_physical_scale (MetaEisViewport *viewport)
{
  MetaScreenCastAreaStream *area_stream =
    META_SCREEN_CAST_AREA_STREAM (viewport);

  return area_stream->scale;
}

static void
transform_position (MetaScreenCastAreaStream *area_stream,
                    double                    x,
                    double                    y,
                    double                   *out_x,
                    double                   *out_y)
{
  *out_x = area_stream->area.x + (int) round (x / area_stream->scale);
  *out_y = area_stream->area.y + (int) round (y / area_stream->scale);
}

static gboolean
meta_screen_cast_area_stream_transform_coordinate (MetaEisViewport *viewport,
                                                   double           x,
                                                   double           y,
                                                   double          *out_x,
                                                   double          *out_y)
{
  MetaScreenCastAreaStream *area_stream =
    META_SCREEN_CAST_AREA_STREAM (viewport);

  transform_position (area_stream, x, y, out_x, out_y);
  return TRUE;
}

static void
meta_eis_viewport_iface_init (MetaEisViewportInterface *eis_viewport_iface)
{
  eis_viewport_iface->is_standalone = meta_screen_cast_area_stream_is_standalone;
  eis_viewport_iface->get_mapping_id = meta_screen_cast_area_stream_get_mapping_id;
  eis_viewport_iface->get_position = meta_screen_cast_area_stream_get_position;
  eis_viewport_iface->get_size = meta_screen_cast_area_stream_get_size;
  eis_viewport_iface->get_physical_scale = meta_screen_cast_area_stream_get_physical_scale;
  eis_viewport_iface->transform_coordinate = meta_screen_cast_area_stream_transform_coordinate;
}

static MetaScreenCastStreamSrc *
meta_screen_cast_area_stream_create_src (MetaScreenCastStream  *stream,
                                         GError               **error)
{
  MetaScreenCastAreaStream *area_stream =
    META_SCREEN_CAST_AREA_STREAM (stream);
  MetaScreenCastAreaStreamSrc *area_stream_src;

  area_stream_src = meta_screen_cast_area_stream_src_new (area_stream,
                                                          error);
  if (!area_stream_src)
    return NULL;

  return META_SCREEN_CAST_STREAM_SRC (area_stream_src);
}

static void
meta_screen_cast_area_stream_set_parameters (MetaScreenCastStream *stream,
                                             GVariantBuilder      *parameters_builder)
{
  MetaScreenCastAreaStream *area_stream =
    META_SCREEN_CAST_AREA_STREAM (stream);

  g_variant_builder_add (parameters_builder, "{sv}",
                         "size",
                         g_variant_new ("(ii)",
                                        area_stream->area.width,
                                        area_stream->area.height));
}

static gboolean
meta_screen_cast_area_stream_transform_position (MetaScreenCastStream *stream,
                                                 double                stream_x,
                                                 double                stream_y,
                                                 double               *x,
                                                 double               *y)
{
  MetaScreenCastAreaStream *area_stream =
    META_SCREEN_CAST_AREA_STREAM (stream);

  transform_position (area_stream, stream_x, stream_y, x, y);

  return TRUE;
}

static void
meta_screen_cast_area_stream_init (MetaScreenCastAreaStream *area_stream)
{
}

static void
meta_screen_cast_area_stream_class_init (MetaScreenCastAreaStreamClass *klass)
{
  MetaScreenCastStreamClass *stream_class =
    META_SCREEN_CAST_STREAM_CLASS (klass);

  stream_class->create_src = meta_screen_cast_area_stream_create_src;
  stream_class->set_parameters = meta_screen_cast_area_stream_set_parameters;
  stream_class->transform_position = meta_screen_cast_area_stream_transform_position;
}
