/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright 2013 Red Hat, Inc.
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
 * Author: Giovanni Campagna <gcampagn@redhat.com>
 */

#ifndef META_CURSOR_TRACKER_PRIVATE_H
#define META_CURSOR_TRACKER_PRIVATE_H

#include <meta/meta-cursor-tracker.h>
#include <wayland-server.h>

#include "meta-cursor.h"

struct _MetaCursorTracker {
  GObject parent_instance;

  gboolean is_showing;

  /* The cursor tracker stores the cursor for the current grab
   * operation, the cursor for the window with pointer focus, and
   * the cursor for the root window, which contains either the
   * default arrow cursor or the 'busy' hourglass if we're launching
   * an app.
   *
   * We choose the first one available -- if there's a grab cursor,
   * we choose that cursor, if there's window cursor, we choose that,
   * otherwise we choose the root cursor.
   *
   * The displayed_cursor contains the chosen cursor.
   */
  MetaCursorReference *displayed_cursor;

  MetaCursorReference *grab_cursor;

  /* Wayland clients can set a NULL buffer as their cursor
   * explicitly, which means that we shouldn't display anything.
   * So, we can't simply store a NULL in window_cursor to
   * determine an unset window cursor; we need an extra boolean.
   */
  gboolean has_window_cursor;
  MetaCursorReference *window_cursor;

  MetaCursorReference *root_cursor;

  MetaCursorReference *theme_cursors[META_CURSOR_LAST];
};

struct _MetaCursorTrackerClass {
  GObjectClass parent_class;

  void (*get_pointer) (MetaCursorTracker   *tracker,
                       int                 *x,
                       int                 *y,
                       ClutterModifierType *mods);

  void (*sync_cursor) (MetaCursorTracker *tracker);

  void (*ensure_cursor) (MetaCursorTracker *tracker);

  void (*load_cursor_pixels) (MetaCursorTracker   *tracker,
                              MetaCursorReference *cursor,
                              uint8_t             *pixels,
                              int                  width,
                              int                  height,
                              int                  rowstride,
                              uint32_t             format);

  void (*load_cursor_buffer) (MetaCursorTracker   *tracker,
                              MetaCursorReference *cursor,
                              struct wl_resource  *buffer);
};

void    _meta_cursor_tracker_set_window_cursor (MetaCursorTracker   *tracker,
                                                gboolean             has_cursor,
                                                MetaCursorReference *cursor);
void    _meta_cursor_tracker_sync_cursor (MetaCursorTracker *tracker);

void     meta_cursor_tracker_set_grab_cursor     (MetaCursorTracker   *tracker,
                                                  MetaCursorReference *cursor);
void     meta_cursor_tracker_set_window_cursor   (MetaCursorTracker   *tracker,
                                                  MetaCursorReference *cursor);
void     meta_cursor_tracker_unset_window_cursor (MetaCursorTracker   *tracker);
void     meta_cursor_tracker_set_root_cursor     (MetaCursorTracker   *tracker,
                                                  MetaCursorReference *cursor);
MetaCursorReference *
meta_cursor_tracker_get_cursor_from_theme (MetaCursorTracker   *tracker,
                                           MetaCursor           cursor);
MetaCursorReference *
meta_cursor_tracker_get_cursor_from_buffer (MetaCursorTracker  *tracker,
                                            struct wl_resource *buffer,
                                            int                 hot_x,
                                            int                 hot_y);

#endif
