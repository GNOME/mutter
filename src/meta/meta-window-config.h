/*
 * Copyright (C) 2024 Red Hat Inc.
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

#include "meta/common.h"

#define META_TYPE_WINDOW_CONFIG (meta_window_config_get_type ())

META_EXPORT
G_DECLARE_FINAL_TYPE (MetaWindowConfig,
                      meta_window_config,
                      META,
                      WINDOW_CONFIG,
                      GObject)

META_EXPORT
MetaWindowConfig *meta_window_config_new (void);

META_EXPORT
gboolean meta_window_config_get_is_initial (MetaWindowConfig *window_config);

META_EXPORT
void meta_window_config_set_rect (MetaWindowConfig *window_config,
                                  MtkRectangle      rect);

META_EXPORT
MtkRectangle meta_window_config_get_rect (MetaWindowConfig *window_config);

META_EXPORT
void meta_window_config_get_position (MetaWindowConfig *window_config,
                                      int              *x,
                                      int              *y);

META_EXPORT
void meta_window_config_set_position (MetaWindowConfig *window_config,
                                      int               x,
                                      int               y);

META_EXPORT
void meta_window_config_get_size (MetaWindowConfig *window_config,
                                  int              *width,
                                  int              *height);

META_EXPORT
void meta_window_config_set_size (MetaWindowConfig *window_config,
                                  int               width,
                                  int               height);

META_EXPORT
void meta_window_config_set_is_fullscreen (MetaWindowConfig *window_config,
                                           gboolean          is_fullscreen);

META_EXPORT
gboolean meta_window_config_get_is_fullscreen (MetaWindowConfig *window_config);
