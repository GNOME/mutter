/*
 * Copyright (C) 2022 Red Hat Inc.
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#pragma once

#include "meta/common.h"
#include "meta/window.h"

#define META_TYPE_WINDOW_DRAG (meta_window_drag_get_type ())
G_DECLARE_FINAL_TYPE (MetaWindowDrag, meta_window_drag,
                      META, WINDOW_DRAG, GObject)

MetaWindowDrag * meta_window_drag_new (MetaWindow *window,
                                       MetaGrabOp  grab_op);

gboolean meta_window_drag_begin (MetaWindowDrag       *drag,
                                 ClutterInputDevice   *device,
                                 ClutterEventSequence *sequence,
                                 uint32_t              timestamp);

void meta_window_drag_end (MetaWindowDrag *drag);

void meta_window_drag_update_resize (MetaWindowDrag *drag);

MetaWindow * meta_window_drag_get_window (MetaWindowDrag *window_drag);

MetaGrabOp meta_window_drag_get_grab_op (MetaWindowDrag *window_drag);

void meta_window_drag_update_edges (MetaWindowDrag *window_drag);

void meta_window_drag_set_position_hint (MetaWindowDrag   *window_drag,
                                         graphene_point_t *pos_hint);
