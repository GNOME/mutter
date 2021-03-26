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

#ifndef META_VIEWPORT_INFO_H
#define META_VIEWPORT_INFO_H

#include <cairo.h>
#include <glib-object.h>

#include "meta/display.h"

#define META_TYPE_VIEWPORT_INFO (meta_viewport_info_get_type ())
G_DECLARE_FINAL_TYPE (MetaViewportInfo, meta_viewport_info,
                      META, VIEWPORT_INFO, GObject)

MetaViewportInfo * meta_viewport_info_new (cairo_rectangle_int_t *views,
                                           float                 *scales,
                                           int                    n_views,
                                           gboolean               is_views_scaled);

int meta_viewport_info_get_view_at (MetaViewportInfo *info,
                                    float             x,
                                    float             y);

gboolean meta_viewport_info_get_view_info (MetaViewportInfo      *viewport_info,
                                           int                    idx,
                                           cairo_rectangle_int_t *rect,
                                           float                 *scale);

int meta_viewport_info_get_neighbor (MetaViewportInfo     *info,
                                     int                   idx,
                                     MetaDisplayDirection  direction);

int meta_viewport_info_get_num_views (MetaViewportInfo *info);

void meta_viewport_info_get_extents (MetaViewportInfo *info,
                                     float            *width,
                                     float            *height);

gboolean meta_viewport_info_is_views_scaled (MetaViewportInfo *info);

#endif /* META_VIEWPORT_INFO_H */
