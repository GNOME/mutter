/*
 * Copyright (C) 2019 Red Hat Inc.
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

#ifndef CLUTTER_FRAME_CLOCK_H
#define CLUTTER_FRAME_CLOCK_H

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <glib.h>
#include <glib-object.h>
#include <stdint.h>

#include "clutter/clutter-types.h"

typedef enum _ClutterFrameResult
{
  CLUTTER_FRAME_RESULT_PENDING_PRESENTED,
  CLUTTER_FRAME_RESULT_IDLE,
} ClutterFrameResult;

#define CLUTTER_TYPE_FRAME_CLOCK (clutter_frame_clock_get_type ())
CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterFrameClock, clutter_frame_clock,
                      CLUTTER, FRAME_CLOCK,
                      GObject)

typedef struct _ClutterFrameListenerIface
{
  void (* before_frame) (ClutterFrameClock *frame_clock,
                         int64_t            frame_count,
                         gpointer           user_data);
  ClutterFrameResult (* frame) (ClutterFrameClock *frame_clock,
                                int64_t            frame_count,
                                int64_t            time_us,
                                gpointer           user_data);
} ClutterFrameListenerIface;

CLUTTER_EXPORT
ClutterFrameClock * clutter_frame_clock_new (float                            refresh_rate,
                                             const ClutterFrameListenerIface *iface,
                                             gpointer                         user_data);

CLUTTER_EXPORT
void clutter_frame_clock_destroy (ClutterFrameClock *frame_clock);

CLUTTER_EXPORT
void clutter_frame_clock_notify_presented (ClutterFrameClock *frame_clock,
                                           ClutterFrameInfo  *frame_info);

CLUTTER_EXPORT
void clutter_frame_clock_schedule_update (ClutterFrameClock *frame_clock);

CLUTTER_EXPORT
void clutter_frame_clock_schedule_update_now (ClutterFrameClock *frame_clock);

CLUTTER_EXPORT
void clutter_frame_clock_inhibit (ClutterFrameClock *frame_clock);

CLUTTER_EXPORT
void clutter_frame_clock_uninhibit (ClutterFrameClock *frame_clock);

void clutter_frame_clock_add_timeline (ClutterFrameClock *frame_clock,
                                       ClutterTimeline   *timeline);

void clutter_frame_clock_remove_timeline (ClutterFrameClock *frame_clock,
                                          ClutterTimeline   *timeline);

CLUTTER_EXPORT
float clutter_frame_clock_get_refresh_rate (ClutterFrameClock *frame_clock);

#endif /* CLUTTER_FRAME_CLOCK_H */
