/*
 * Copyright (C) 2018 Red Hat Inc.
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

#include "backends/meta-screen-cast-window.h"

G_DEFINE_INTERFACE (MetaScreenCastWindow, meta_screen_cast_window, G_TYPE_OBJECT)

static void
meta_screen_cast_window_default_init (MetaScreenCastWindowInterface *iface)
{
}

void
meta_screen_cast_window_get_buffer_bounds (MetaScreenCastWindow *screen_cast_window,
                                           MtkRectangle         *bounds)
{
  META_SCREEN_CAST_WINDOW_GET_IFACE (screen_cast_window)->get_buffer_bounds (screen_cast_window,
                                                                             bounds);
}

void
meta_screen_cast_window_transform_relative_position (MetaScreenCastWindow *screen_cast_window,
                                                     double                x,
                                                     double                y,
                                                     double               *x_out,
                                                     double               *y_out)
{
  META_SCREEN_CAST_WINDOW_GET_IFACE (screen_cast_window)->transform_relative_position (screen_cast_window,
                                                                                       x,
                                                                                       y,
                                                                                       x_out,
                                                                                       y_out);
}

gboolean
meta_screen_cast_window_transform_cursor_position (MetaScreenCastWindow *screen_cast_window,
                                                   MetaCursorSprite     *cursor_sprite,
                                                   graphene_point_t     *cursor_position,
                                                   graphene_point_t     *out_relative_cursor_position,
                                                   float                *out_view_scale)
{
  MetaScreenCastWindowInterface *iface =
    META_SCREEN_CAST_WINDOW_GET_IFACE (screen_cast_window);

  return iface->transform_cursor_position (screen_cast_window,
                                           cursor_sprite,
                                           cursor_position,
                                           out_relative_cursor_position,
                                           out_view_scale);
}

void
meta_screen_cast_window_capture_into (MetaScreenCastWindow *screen_cast_window,
                                      MtkRectangle         *bounds,
                                      uint8_t              *data)
{
  META_SCREEN_CAST_WINDOW_GET_IFACE (screen_cast_window)->capture_into (screen_cast_window,
                                                                        bounds,
                                                                        data);
}

gboolean
meta_screen_cast_window_blit_to_framebuffer (MetaScreenCastWindow *screen_cast_window,
                                             MtkRectangle         *bounds,
                                             CoglFramebuffer      *framebuffer)
{
  MetaScreenCastWindowInterface *iface =
    META_SCREEN_CAST_WINDOW_GET_IFACE (screen_cast_window);

  return iface->blit_to_framebuffer (screen_cast_window, bounds, framebuffer);
}

gboolean
meta_screen_cast_window_has_damage (MetaScreenCastWindow *screen_cast_window)
{
  MetaScreenCastWindowInterface *iface =
    META_SCREEN_CAST_WINDOW_GET_IFACE (screen_cast_window);

  return iface->has_damage (screen_cast_window);
}

void
meta_screen_cast_window_inc_usage (MetaScreenCastWindow *screen_cast_window)
{
  META_SCREEN_CAST_WINDOW_GET_IFACE (screen_cast_window)->inc_usage (screen_cast_window);
}

void
meta_screen_cast_window_dec_usage (MetaScreenCastWindow *screen_cast_window)
{
  META_SCREEN_CAST_WINDOW_GET_IFACE (screen_cast_window)->dec_usage (screen_cast_window);
}
