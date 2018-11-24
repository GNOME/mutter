/*
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
 */

#include "wayland/meta-wayland-surface-helper.h"

static void
transformed_coord (int width,
                   int height,
                   MetaMonitorTransform transform,
                   int scale,
                   float sx,
                   float sy,
                   float *bx,
                   float *by)
{
  switch (transform)
    {
    default:
    case META_MONITOR_TRANSFORM_NORMAL:
      *bx = sx;
      *by = sy;
      break;
    case META_MONITOR_TRANSFORM_90:
      *bx = width - sy;
      *by = sx;
      break;
    case META_MONITOR_TRANSFORM_180:
      *bx = width - sx;
      *by = height - sy;
      break;
    case META_MONITOR_TRANSFORM_270:
      *bx = sy;
      *by = height - sx;
      break;
    case META_MONITOR_TRANSFORM_FLIPPED:
      *bx = width - sx;
      *by = sy;
      break;
    case META_MONITOR_TRANSFORM_FLIPPED_90:
      *bx = width - sy;
      *by = height - sx;
      break;
    case META_MONITOR_TRANSFORM_FLIPPED_180:
      *bx = sx;
      *by = height - sy;
      break;
    case META_MONITOR_TRANSFORM_FLIPPED_270:
      *bx = sy;
      *by = sx;
      break;
    }

  *bx *= scale;
  *by *= scale;
}

static cairo_rectangle_int_t
transformed_rect (int width,
                  int height,
                  MetaMonitorTransform transform,
                  int scale,
                  cairo_rectangle_int_t rect)
{
  cairo_rectangle_int_t ret;
  float x1;
  float x2;
  float y1;
  float y2;

  transformed_coord(width,
                    height,
                    transform,
                    scale,
                    rect.x,
                    rect.y,
                    &x1,
                    &y1);
  transformed_coord(width,
                    height,
                    transform,
                    scale,
                    rect.x + rect.width,
                    rect.y + rect.height,
                    &x2,
                    &y2);

  if (x1 <= x2)
    {
      ret.x = x1;
      ret.width = x2 - x1;
    }
  else
    {
      ret.x = x2;
      ret.width = x1 - x2;
    }

  if (y1 <= y2)
    {
      ret.y = y1;
      ret.height = y2;
    }
  else
    {
      ret.y = y2;
      ret.height = y1 - y2;
    }

  return ret;
}

static void
surface_to_buffer_coordinate (MetaWaylandSurface *surface,
                              float sx,
                              float sy,
                              float *bx,
                              float *by)
{
  if (!surface->has_viewport_src_rect)
    {
      *bx = sx;
      *by = sy;
    }
  else
    {
      float surface_width;
      float surface_height;

      if (surface->has_viewport_dest)
        {
          surface_width = surface->viewport_dest_width;
          surface_height = surface->viewport_dest_height;
        }
      else
        {
          surface_width = meta_wayland_surface_get_buffer_width (surface) /
                          surface->scale;
          surface_height = meta_wayland_surface_get_buffer_height (surface) /
                           surface->scale;
        }

      *bx = sx * surface->viewport_src_width / surface_width +
            surface->viewport_src_x;
      *by = sy * surface->viewport_src_height / surface_height +
            surface->viewport_src_y;
    }
}

cairo_region_t *
meta_wayland_surface_helper_surface_to_buffer_region (MetaWaylandSurface *surface,
                                                      cairo_region_t     *region)
{
  int n_rects, i;
  cairo_rectangle_int_t *rects;
  cairo_rectangle_int_t surface_rect;
  cairo_region_t *scaled_region;
  float x1;
  float x2;
  float y1;
  float y2;

  if (surface->scale == 1 &&
      surface->viewport_src_width <= 0 &&
      surface->viewport_dest_width <= 0 &&
      surface->buffer_transform == META_MONITOR_TRANSFORM_NORMAL)
    {
      return cairo_region_copy (region);
    }

  n_rects = cairo_region_num_rectangles (region);

  rects = g_malloc (sizeof(cairo_rectangle_int_t) * n_rects);

  for (i = 0; i < n_rects; i++)
    {
      int width;
      int height;

      cairo_region_get_rectangle (region, i, &rects[i]);

      surface_to_buffer_coordinate (surface, rects[i].x, rects[i].y, &x1, &y1);
      surface_to_buffer_coordinate (surface, rects[i].x + rects[i].width,
                                    rects[i].y + rects[i].height, &x2, &y2);
      rects[i].x = floorf(x1);
      rects[i].y = floorf(y1);
      rects[i].width  = ceilf(x2) - rects[i].x;
      rects[i].height = ceilf(y2) - rects[i].y;

      width = meta_wayland_surface_get_buffer_width (surface) / surface->scale;
      height = meta_wayland_surface_get_buffer_height (surface) / surface->scale;

      rects[i] = transformed_rect(width,
                                  height,
                                  surface->buffer_transform,
                                  surface->scale,
                                  rects[i]);
    }

  scaled_region = cairo_region_create_rectangles (rects, n_rects);

  /* Intersect the scaled region to make sure no rounding errors made
   * it to big */
  surface_rect = (cairo_rectangle_int_t) {
    .width  = meta_wayland_surface_get_buffer_width (surface),
    .height = meta_wayland_surface_get_buffer_height (surface),
  };
  cairo_region_intersect_rectangle (scaled_region, &surface_rect);
  g_free (rects);

  return scaled_region;
}

MetaMonitorTransform
meta_wayland_surface_helper_transform_from_wl_output_transform (int32_t transform_value)
{
  enum wl_output_transform transform = transform_value;

  switch (transform)
    {
    case WL_OUTPUT_TRANSFORM_NORMAL:
      return META_MONITOR_TRANSFORM_NORMAL;
    case WL_OUTPUT_TRANSFORM_90:
      return META_MONITOR_TRANSFORM_90;
    case WL_OUTPUT_TRANSFORM_180:
      return META_MONITOR_TRANSFORM_180;
    case WL_OUTPUT_TRANSFORM_270:
      return META_MONITOR_TRANSFORM_270;
    case WL_OUTPUT_TRANSFORM_FLIPPED:
      return META_MONITOR_TRANSFORM_FLIPPED;
    case WL_OUTPUT_TRANSFORM_FLIPPED_90:
      return META_MONITOR_TRANSFORM_FLIPPED_90;
    case WL_OUTPUT_TRANSFORM_FLIPPED_180:
      return META_MONITOR_TRANSFORM_FLIPPED_180;
    case WL_OUTPUT_TRANSFORM_FLIPPED_270:
      return META_MONITOR_TRANSFORM_FLIPPED_270;
    default:
      return -1;
    }
}
