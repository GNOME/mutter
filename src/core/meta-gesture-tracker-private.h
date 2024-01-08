/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
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

#include <glib-object.h>

#include "backends/meta-backend-private.h"
#include "clutter/clutter.h"
#include "meta/window.h"

#define META_TYPE_GESTURE_TRACKER            (meta_gesture_tracker_get_type ())

typedef struct _MetaGestureTracker MetaGestureTracker;
typedef struct _MetaGestureTrackerClass MetaGestureTrackerClass;

G_DECLARE_DERIVABLE_TYPE (MetaGestureTracker,
                          meta_gesture_tracker,
                          META, GESTURE_TRACKER,
                          GObject)

struct _MetaGestureTrackerClass
{
  GObjectClass parent_class;

  void (* state_changed) (MetaGestureTracker   *tracker,
                          ClutterEventSequence *sequence,
                          MetaSequenceState     state);
};

MetaGestureTracker * meta_gesture_tracker_new                (void);

gboolean             meta_gesture_tracker_handle_event       (MetaGestureTracker   *tracker,
                                                              ClutterStage         *stage,
                                                              const ClutterEvent   *event);
gboolean             meta_gesture_tracker_set_sequence_state (MetaGestureTracker   *tracker,
                                                              ClutterEventSequence *sequence,
                                                              MetaSequenceState     state);
gint                 meta_gesture_tracker_get_n_current_touches (MetaGestureTracker *tracker);
