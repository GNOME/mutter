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

#include "core/util-private.h"
#include "meta/common.h"
#include "meta/window.h"

typedef enum _MetaDragWindowFlags MetaDragWindowFlags;

enum _MetaDragWindowFlags
{
  META_DRAG_WINDOW_FLAG_NONE = 0,
  META_DRAG_WINDOW_FLAG_FOREIGN_GRAB = 1 << 0,
};

#define META_TYPE_WINDOW_DRAG (meta_window_drag_get_type ())
G_DECLARE_FINAL_TYPE (MetaWindowDrag, meta_window_drag,
                      META, WINDOW_DRAG, GObject)

MetaWindowDrag * meta_window_drag_new (MetaWindow *window,
                                       MetaGrabOp  grab_op);

gboolean meta_window_drag_begin (MetaWindowDrag      *drag,
                                 ClutterSprite       *sprite,
                                 uint32_t             timestamp,
                                 MetaDragWindowFlags  flags);

META_EXPORT_TEST
void meta_window_drag_end (MetaWindowDrag *drag);

void meta_window_drag_update_resize (MetaWindowDrag *drag);

void meta_window_drag_calculate_window_position (MetaWindowDrag *window_drag,
                                                 int             window_width,
                                                 int             window_height,
                                                 int            *out_x,
                                                 int            *out_y);

META_EXPORT_TEST
MetaWindow * meta_window_drag_get_window (MetaWindowDrag *window_drag);

MetaGrabOp meta_window_drag_get_grab_op (MetaWindowDrag *window_drag);

void meta_window_drag_update_edges (MetaWindowDrag *window_drag);

void meta_window_drag_set_position_hint (MetaWindowDrag   *window_drag,
                                         graphene_point_t *pos_hint);

gboolean meta_window_drag_process_event (MetaWindowDrag     *window_drag,
                                         const ClutterEvent *event);

void meta_window_drag_calculate_window_size (MetaWindowDrag *window_drag,
                                             int            *out_width,
                                             int            *out_height);

void meta_window_drag_destroy (MetaWindowDrag *window_drag);
