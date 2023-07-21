/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Edge resistance for move/resize operations */

/*
 * Copyright (C) 2005 Elijah Newren
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
 */

#pragma once

#include "core/window-private.h"

#include "compositor/meta-window-drag.h"

void meta_window_drag_edge_resistance_cleanup    (MetaWindowDrag          *window_drag);

void meta_window_drag_edge_resistance_for_move   (MetaWindowDrag          *window_drag,
                                                  int                     *new_x,
                                                  int                     *new_y,
                                                  MetaEdgeResistanceFlags  flags);

void meta_window_drag_edge_resistance_for_resize (MetaWindowDrag          *window_drag,
                                                  int                     *new_width,
                                                  int                     *new_height,
                                                  MetaGravity              gravity,
                                                  MetaEdgeResistanceFlags  flags);
