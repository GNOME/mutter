/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

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
 *
 */

#include "wayland/meta-wayland-window-surface.h"

gboolean
meta_wayland_window_surface_set_geometry (MetaWaylandShellSurface *shell_surface,
                                          MetaRectangle           *new_geometry,
                                          MetaRectangle           *out_geometry)
{
  MetaRectangle bounding_geometry = { 0 };

  meta_wayland_shell_surface_calculate_geometry (shell_surface,
                                                 &bounding_geometry);

  bounding_geometry.x = new_geometry->x;
  bounding_geometry.y = new_geometry->y;

  meta_rectangle_intersect(new_geometry, &bounding_geometry,
                           new_geometry);

  if (!meta_rectangle_equal (new_geometry, out_geometry))
    {
      memcpy(out_geometry, new_geometry, sizeof(MetaRectangle));
      return TRUE;
    }

  return FALSE;
}
