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

#include "meta/meta-window-config.h"
#include "meta/types.h"

typedef enum
{
  META_TILE_NONE,
  META_TILE_LEFT,
  META_TILE_RIGHT,
  META_TILE_MAXIMIZED
} MetaTileMode;

MetaWindowConfig * meta_window_config_initial_new (void);

MetaWindowConfig * meta_window_config_new_from (MetaWindowConfig *other_config);

void meta_window_config_set_initial (MetaWindowConfig *config);

gboolean meta_window_config_is_maximized (MetaWindowConfig *config);

gboolean meta_window_config_is_any_maximized (MetaWindowConfig *config);

gboolean meta_window_config_is_maximized_horizontally (MetaWindowConfig *config);

gboolean meta_window_config_is_maximized_vertically (MetaWindowConfig *config);

void meta_window_config_set_maximized_directions (MetaWindowConfig *window_config,
                                                  gboolean          horizontally,
                                                  gboolean          vertically);

MetaTileMode meta_window_config_get_tile_mode (MetaWindowConfig *config);

int meta_window_config_get_tile_monitor_number (MetaWindowConfig *config);

double meta_window_config_get_tile_hfraction (MetaWindowConfig *config);

MetaWindow * meta_window_config_get_tile_match (MetaWindowConfig *config);

void meta_window_config_set_tile_mode (MetaWindowConfig *config,
                                       MetaTileMode      tile_mode);

void meta_window_config_set_tile_monitor_number (MetaWindowConfig *config,
                                                 int               tile_monitor_number);

void meta_window_config_set_tile_hfraction (MetaWindowConfig *config,
                                            double            hfraction);

void meta_window_config_set_tile_match (MetaWindowConfig *config,
                                        MetaWindow       *tile_match);

gboolean meta_window_config_is_floating (MetaWindowConfig *config);

gboolean meta_window_config_has_position (MetaWindowConfig *config);

gboolean meta_window_config_is_equivalent (MetaWindowConfig *config,
                                           MetaWindowConfig *other_config);

gboolean meta_window_config_is_tiled_side_by_side (MetaWindowConfig *config);
