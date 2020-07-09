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

#include "backends/meta-cursor.h"
#include "backends/meta-cursor-renderer.h"
#include "meta/meta-cursor-tracker.h"

struct _MetaCursorTrackerClass
{
  GObjectClass parent_class;

  void (* set_force_track_position) (MetaCursorTracker *tracker,
                                     gboolean           is_enabled);
  MetaCursorSprite * (* get_sprite) (MetaCursorTracker *tracker);
};

void     meta_cursor_tracker_set_window_cursor   (MetaCursorTracker *tracker,
                                                  MetaCursorSprite  *cursor_sprite);
void     meta_cursor_tracker_unset_window_cursor (MetaCursorTracker *tracker);
void     meta_cursor_tracker_set_root_cursor     (MetaCursorTracker *tracker,
                                                  MetaCursorSprite  *cursor_sprite);

void     meta_cursor_tracker_update_position (MetaCursorTracker *tracker,
                                              float              new_x,
                                              float              new_y);

void meta_cursor_tracker_track_position (MetaCursorTracker *tracker);

void meta_cursor_tracker_untrack_position (MetaCursorTracker *tracker);

MetaBackend * meta_cursor_tracker_get_backend (MetaCursorTracker *tracker);

void meta_cursor_tracker_notify_cursor_changed (MetaCursorTracker *tracker);

#endif
