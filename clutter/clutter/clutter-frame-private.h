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

#include "clutter/clutter-frame.h"

typedef void (* ClutterFrameRelease) (ClutterFrame *frame);

struct _ClutterFrame
{
  grefcount ref_count;
  ClutterFrameRelease release;

  int64_t frame_count;

  gboolean has_target_presentation_time;
  int64_t target_presentation_time_us;

  gboolean has_frame_deadline;
  int64_t frame_deadline_us;

  gboolean has_result;
  ClutterFrameResult result;
};

CLUTTER_EXPORT
gpointer clutter_frame_new (size_t              size,
                            ClutterFrameRelease release);

#define clutter_frame_new(FrameType, release) \
  ((FrameType *) (clutter_frame_new (sizeof (FrameType), release)))

CLUTTER_EXPORT
ClutterFrameResult clutter_frame_get_result (ClutterFrame *frame);
