/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Mutter X window decorations */

/*
 * Copyright (C) 2001 Havoc Pennington
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

#include "x11/meta-sync-counter.h"

struct _MetaFrame
{
  /* window we frame */
  MetaWindow *window;

  /* reparent window */
  Window xwindow;

  /* This rect is trusted info from where we put the
   * frame, not the result of ConfigureNotify
   */
  MtkRectangle rect;

  MetaFrameBorders cached_borders; /* valid if borders_cached is set */

  MtkRegion *opaque_region;

  MetaSyncCounter sync_counter;

  /* position of client, size of frame */
  int child_x;
  int child_y;
  int right_width;
  int bottom_height;

  guint borders_cached : 1;
};

void     meta_window_ensure_frame           (MetaWindow *window);
void     meta_window_destroy_frame          (MetaWindow *window);

Window         meta_frame_get_xwindow (MetaFrame *frame);

/* These should ONLY be called from meta_window_move_resize_internal */
void meta_frame_calc_borders      (MetaFrame        *frame,
                                   MetaFrameBorders *borders);

gboolean meta_frame_sync_to_window (MetaFrame         *frame,
                                    gboolean           need_resize);

void meta_frame_clear_cached_borders (MetaFrame *frame);

MtkRegion *meta_frame_get_frame_bounds (MetaFrame *frame);

gboolean meta_frame_handle_xevent (MetaFrame *frame,
                                   XEvent    *event);

GSubprocess * meta_frame_launch_client (MetaX11Display *x11_display,
                                        const char     *display_name);

MetaSyncCounter * meta_frame_get_sync_counter (MetaFrame *frame);

void meta_frame_set_opaque_region (MetaFrame *frame,
                                   MtkRegion *region);
