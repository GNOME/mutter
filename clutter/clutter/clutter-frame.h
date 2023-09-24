/*
 * Copyright (C) 2020 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-frame-clock.h"

typedef struct _ClutterFrame ClutterFrame;

#define CLUTTER_TYPE_FRAME (clutter_frame_get_type ())

CLUTTER_EXPORT
GType clutter_frame_get_type (void);

CLUTTER_EXPORT
ClutterFrame * clutter_frame_ref (ClutterFrame *frame);

CLUTTER_EXPORT
void clutter_frame_unref (ClutterFrame *frame);

CLUTTER_EXPORT
int64_t clutter_frame_get_count (ClutterFrame *frame);

CLUTTER_EXPORT
gboolean clutter_frame_get_target_presentation_time (ClutterFrame *frame,
                                                     int64_t      *target_presentation_time_us);

CLUTTER_EXPORT
gboolean clutter_frame_get_frame_deadline (ClutterFrame *frame,
                                           int64_t      *frame_deadline_us);

CLUTTER_EXPORT
void clutter_frame_set_result (ClutterFrame       *frame,
                               ClutterFrameResult  result);

CLUTTER_EXPORT
gboolean clutter_frame_has_result (ClutterFrame *frame);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterFrame, clutter_frame_unref)
