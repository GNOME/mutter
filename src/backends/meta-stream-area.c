/*
 * Copyright (C) 2020-2026 Red Hat Inc.
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

#include "backends/meta-stream-area.h"

#include "backends/meta-eis.h"
#include "backends/meta-stream-source-area.h"

struct _MetaStreamArea
{
  MetaStream parent;

  MtkRectangle area;
  float scale;
};

static void meta_eis_viewport_iface_init (MetaEisViewportInterface *eis_viewport_iface);

G_DEFINE_TYPE_WITH_CODE (MetaStreamArea, meta_stream_area,
                         META_TYPE_STREAM,
                         G_IMPLEMENT_INTERFACE (META_TYPE_EIS_VIEWPORT,
                                                meta_eis_viewport_iface_init))

static gboolean
meta_stream_area_is_standalone (MetaEisViewport *viewport)
{
  return TRUE;
}

static const char *
meta_stream_area_get_mapping_id (MetaEisViewport *viewport)
{
  MetaStream *stream = META_STREAM (viewport);

  return meta_stream_get_mapping_id (stream);
}

static gboolean
meta_stream_area_get_position (MetaEisViewport *viewport,
                               int             *out_x,
                               int             *out_y)
{
  return FALSE;
}

static void
meta_stream_area_get_size (MetaEisViewport *viewport,
                           int             *out_width,
                           int             *out_height)
{
  MetaStreamArea *stream_area =
    META_STREAM_AREA (viewport);

  *out_width = (int) roundf (stream_area->area.width * stream_area->scale);
  *out_height = (int) roundf (stream_area->area.height * stream_area->scale);
}

static double
meta_stream_area_get_physical_scale (MetaEisViewport *viewport)
{
  MetaStreamArea *stream_area =
    META_STREAM_AREA (viewport);

  return stream_area->scale;
}

static void
transform_position (MetaStreamArea *stream_area,
                    double          x,
                    double          y,
                    double         *out_x,
                    double         *out_y)
{
  *out_x = stream_area->area.x + (int) round (x / stream_area->scale);
  *out_y = stream_area->area.y + (int) round (y / stream_area->scale);
}

static gboolean
meta_stream_area_transform_coordinate (MetaEisViewport *viewport,
                                       double           x,
                                       double           y,
                                       double          *out_x,
                                       double          *out_y)
{
  MetaStreamArea *stream_area =
    META_STREAM_AREA (viewport);

  transform_position (stream_area, x, y, out_x, out_y);
  return TRUE;
}

static void
meta_eis_viewport_iface_init (MetaEisViewportInterface *eis_viewport_iface)
{
  eis_viewport_iface->is_standalone = meta_stream_area_is_standalone;
  eis_viewport_iface->get_mapping_id = meta_stream_area_get_mapping_id;
  eis_viewport_iface->get_position = meta_stream_area_get_position;
  eis_viewport_iface->get_size = meta_stream_area_get_size;
  eis_viewport_iface->get_physical_scale = meta_stream_area_get_physical_scale;
  eis_viewport_iface->transform_coordinate = meta_stream_area_transform_coordinate;
}

static MetaStreamSource *
meta_stream_area_create_source (MetaStream  *stream,
                                GError     **error)
{
  MetaStreamArea *stream_area = META_STREAM_AREA (stream);
  MetaStreamSourceArea *stream_source_area;

  stream_source_area = meta_stream_source_area_new (stream_area, error);
  if (!stream_source_area)
    return NULL;

  return META_STREAM_SOURCE (stream_source_area);
}

static gboolean
meta_stream_area_transform_position (MetaStream *stream,
                                     double      stream_x,
                                     double      stream_y,
                                     double     *x,
                                     double     *y)
{
  MetaStreamArea *stream_area =
    META_STREAM_AREA (stream);

  transform_position (stream_area, stream_x, stream_y, x, y);

  return TRUE;
}

static void
meta_stream_area_class_init (MetaStreamAreaClass *klass)
{
  MetaStreamClass *stream_class = META_STREAM_CLASS (klass);

  stream_class->create_source = meta_stream_area_create_source;
  stream_class->transform_position = meta_stream_area_transform_position;
}

static void
meta_stream_area_init (MetaStreamArea *stream_area)
{
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

MetaStreamArea *
meta_stream_area_new (MetaBackend           *backend,
                      MtkRectangle          *area,
                      MetaStreamCursorMode   cursor_mode,
                      GError               **error)
{
  ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
  MetaStreamArea *stream_area;
  float scale;

  if (!calculate_scale (stage, area, &scale))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Area is off-screen");
      return NULL;
    }

  stream_area = g_object_new (META_TYPE_STREAM_AREA,
                              "backend", backend,
                              "cursor-mode", cursor_mode,
                              "is-configured", TRUE,
                              NULL);
  stream_area->area = *area;
  stream_area->scale = scale;

  return stream_area;
}

void
meta_stream_area_get_area (MetaStreamArea *stream_area,
                           MtkRectangle   *area)
{
  *area = stream_area->area;
}

float
meta_stream_area_get_scale (MetaStreamArea *stream_area)
{
  return stream_area->scale;
}
