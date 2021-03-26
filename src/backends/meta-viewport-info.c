/*
 * Copyright (C) 2020 Red Hat
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
 * Written by:
 *     Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "backends/meta-viewport-info.h"
#include "core/main-private.h"
#include "core/boxes-private.h"

typedef struct _ViewInfo ViewInfo;

struct _ViewInfo
{
  MetaRectangle rect;
  float scale;
};

struct _MetaViewportInfo
{
  GObject parent;
  GArray *views;
  gboolean is_views_scaled;
};

G_DEFINE_TYPE (MetaViewportInfo, meta_viewport_info, G_TYPE_OBJECT)

static void
meta_viewport_info_finalize (GObject *object)
{
  MetaViewportInfo *info = META_VIEWPORT_INFO (object);

  g_array_unref (info->views);

  G_OBJECT_CLASS (meta_viewport_info_parent_class)->finalize (object);
}

static void
meta_viewport_info_class_init (MetaViewportInfoClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_viewport_info_finalize;
}

static void
meta_viewport_info_init (MetaViewportInfo *info)
{
  info->views = g_array_new (FALSE, FALSE, sizeof (ViewInfo));
}

MetaViewportInfo *
meta_viewport_info_new (cairo_rectangle_int_t *views,
                        float                 *scales,
                        int                    n_views,
                        gboolean               is_views_scaled)
{
  MetaViewportInfo *viewport_info;
  int i;

  viewport_info = g_object_new (META_TYPE_VIEWPORT_INFO, NULL);

  for (i = 0; i < n_views; i++)
    {
      ViewInfo info;

      info.rect = views[i];
      info.scale = scales[i];
      g_array_append_val (viewport_info->views, info);
    }

  viewport_info->is_views_scaled = is_views_scaled;

  return viewport_info;
}

int
meta_viewport_info_get_view_at (MetaViewportInfo *viewport_info,
                                float             x,
                                float             y)
{
  int i;

  for (i = 0; i < viewport_info->views->len; i++)
    {
      ViewInfo *info = &g_array_index (viewport_info->views, ViewInfo, i);

      if (META_POINT_IN_RECT (x, y, info->rect))
        return i;
    }

  return -1;
}

gboolean
meta_viewport_info_get_view_info (MetaViewportInfo      *viewport_info,
                                  int                    idx,
                                  cairo_rectangle_int_t *rect,
                                  float                 *scale)
{
  ViewInfo *info;

  if (idx < 0 || idx >= viewport_info->views->len)
    return FALSE;

  info = &g_array_index (viewport_info->views, ViewInfo, idx);
  if (rect)
    *rect = info->rect;
  if (scale)
    *scale = info->scale;

  return TRUE;
}

static gboolean
view_has_neighbor (cairo_rectangle_int_t *view,
                   cairo_rectangle_int_t *neighbor,
                   MetaDisplayDirection   neighbor_direction)
{
  switch (neighbor_direction)
    {
    case META_DISPLAY_RIGHT:
      if (neighbor->x == (view->x + view->width) &&
          meta_rectangle_vert_overlap (neighbor, view))
        return TRUE;
      break;
    case META_DISPLAY_LEFT:
      if (view->x == (neighbor->x + neighbor->width) &&
          meta_rectangle_vert_overlap (neighbor, view))
        return TRUE;
      break;
    case META_DISPLAY_UP:
      if (view->y == (neighbor->y + neighbor->height) &&
          meta_rectangle_horiz_overlap (neighbor, view))
        return TRUE;
      break;
    case META_DISPLAY_DOWN:
      if (neighbor->y == (view->y + view->height) &&
          meta_rectangle_horiz_overlap (neighbor, view))
        return TRUE;
      break;
    }

  return FALSE;
}

int
meta_viewport_info_get_neighbor (MetaViewportInfo     *viewport_info,
                                 int                   idx,
                                 MetaDisplayDirection  direction)
{
  cairo_rectangle_int_t rect;
  int i;

  if (!meta_viewport_info_get_view_info (viewport_info, idx, &rect, NULL))
    return -1;

  for (i = 0; i < viewport_info->views->len; i++)
    {
      ViewInfo *info = &g_array_index (viewport_info->views, ViewInfo, i);

      if (idx == i)
        continue;
      if (view_has_neighbor (&rect, &info->rect, direction))
        return i;
    }

  return -1;
}

int
meta_viewport_info_get_num_views (MetaViewportInfo *info)
{
  return info->views->len;
}

void
meta_viewport_info_get_extents (MetaViewportInfo *viewport_info,
                                float            *width,
                                float            *height)
{
  int min_x = G_MAXINT, min_y = G_MAXINT, max_x = G_MININT, max_y = G_MININT, i;

  g_return_if_fail (viewport_info != NULL);

  for (i = 0; i < viewport_info->views->len; i++)
    {
      ViewInfo *info = &g_array_index (viewport_info->views, ViewInfo, i);

      min_x = MIN (min_x, info->rect.x);
      max_x = MAX (max_x, info->rect.x + info->rect.width);
      min_y = MIN (min_y, info->rect.y);
      max_y = MAX (max_y, info->rect.y + info->rect.height);
    }

  if (width)
    *width = (float) max_x - min_x;
  if (height)
    *height = (float) max_y - min_y;
}

gboolean
meta_viewport_info_is_views_scaled (MetaViewportInfo *viewport_info)
{
  return viewport_info->is_views_scaled;
}
