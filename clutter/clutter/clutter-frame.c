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

#include "config.h"

#include "clutter/clutter-frame-private.h"

G_DEFINE_BOXED_TYPE (ClutterFrame, clutter_frame,
                     clutter_frame_ref,
                     clutter_frame_unref)

ClutterFrame *
clutter_frame_ref (ClutterFrame *frame)
{
  g_ref_count_inc (&frame->ref_count);
  return frame;
}

void
clutter_frame_unref (ClutterFrame *frame)
{
  if (g_ref_count_dec (&frame->ref_count))
    {
      if (frame->release)
        frame->release (frame);
      g_free (frame);
    }
}

gpointer
(clutter_frame_new) (size_t              size,
                     ClutterFrameRelease release)
{
  ClutterFrame *frame;

  g_assert (size >= sizeof (ClutterFrame));

  frame = g_malloc0 (size);
  g_ref_count_init (&frame->ref_count);
  frame->release = release;

  return frame;
}

int64_t
clutter_frame_get_count (ClutterFrame *frame)
{
  return frame->frame_count;
}

gboolean
clutter_frame_get_target_presentation_time (ClutterFrame *frame,
                                            int64_t      *target_presentation_time_us)
{
  if (frame->has_target_presentation_time)
    {
      *target_presentation_time_us = frame->target_presentation_time_us;
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

gboolean
clutter_frame_get_frame_deadline (ClutterFrame *frame,
                                  int64_t      *frame_deadline_us)
{
  if (frame->has_frame_deadline)
    {
      *frame_deadline_us = frame->frame_deadline_us;
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

ClutterFrameResult
clutter_frame_get_result (ClutterFrame *frame)
{
  g_return_val_if_fail (frame->has_result, CLUTTER_FRAME_RESULT_IDLE);

  return frame->result;
}

gboolean
clutter_frame_has_result (ClutterFrame *frame)
{
  return frame->has_result;
}

void
clutter_frame_set_result (ClutterFrame       *frame,
                          ClutterFrameResult  result)
{
  g_warn_if_fail (!frame->has_result);

  frame->result = result;
  frame->has_result = TRUE;
}
