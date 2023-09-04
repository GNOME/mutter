/*
 * Copyright (C) 2023 Red Hat Inc.
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

#define META_TYPE_EIS_VIEWPORT (meta_eis_viewport_get_type ())
G_DECLARE_INTERFACE (MetaEisViewport, meta_eis_viewport,
                     META, EIS_VIEWPORT, GObject)

struct _MetaEisViewportInterface
{
  GTypeInterface parent_iface;

  gboolean (* is_standalone) (MetaEisViewport *viewport);

  const char * (* get_mapping_id) (MetaEisViewport *viewport);

  gboolean (* get_position) (MetaEisViewport *viewport,
                             int             *out_x,
                             int             *out_y);

  void (* get_size) (MetaEisViewport *viewport,
                     int             *out_width,
                     int             *out_height);

  double (* get_physical_scale) (MetaEisViewport *viewport);

  gboolean (* transform_coordinate) (MetaEisViewport *viewport,
                                     double           x,
                                     double           y,
                                     double          *out_x,
                                     double          *out_y);
};

gboolean meta_eis_viewport_is_standalone (MetaEisViewport *viewport);

const char * meta_eis_viewport_get_mapping_id (MetaEisViewport *viewport);

gboolean meta_eis_viewport_get_position (MetaEisViewport *viewport,
                                         int             *out_x,
                                         int             *out_y);

void meta_eis_viewport_get_size (MetaEisViewport *viewport,
                                 int             *out_width,
                                 int             *out_height);

double meta_eis_viewport_get_physical_scale (MetaEisViewport *viewport);

gboolean meta_eis_viewport_transform_coordinate (MetaEisViewport *viewport,
                                                 double           x,
                                                 double           y,
                                                 double          *out_x,
                                                 double          *out_y);

void meta_eis_viewport_notify_changed (MetaEisViewport *viewport);
