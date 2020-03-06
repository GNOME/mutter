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

#include "clutter-build-config.h"

#include "clutter/clutter-frame-clock.h"

#include "clutter/clutter-main.h"

static inline uint64_t
us (uint64_t us)
{
  return us;
}

static inline uint64_t
ms2us (uint64_t ms)
{
  return us (ms * 1000);
}

/* Wait 2ms after vblank before starting to draw next frame */
#define SYNC_DELAY_US ms2us (2)

typedef struct _ClutterFrameListener
{
  const ClutterFrameListenerIface *iface;
  gpointer user_data;
} ClutterFrameListener;

typedef struct _ClutterClockSource
{
  GSource source;

  ClutterFrameClock *frame_clock;
} ClutterClockSource;

typedef enum _ClutterFrameClockState
{
  CLUTTER_FRAME_CLOCK_STATE_INIT,
  CLUTTER_FRAME_CLOCK_STATE_IDLE,
  CLUTTER_FRAME_CLOCK_STATE_SCHEDULED,
  CLUTTER_FRAME_CLOCK_STATE_DISPATCHING,
  CLUTTER_FRAME_CLOCK_STATE_PENDING_PRESENTED,
} ClutterFrameClockState;

struct _ClutterFrameClock
{
  GObject parent;

  float refresh_rate;
  ClutterFrameListener listener;

  GSource *source;

  int64_t frame_count;

  ClutterFrameClockState state;
  int64_t last_presentation_time_us;

  gboolean pending_reschedule;
};

G_DEFINE_TYPE (ClutterFrameClock, clutter_frame_clock,
               G_TYPE_OBJECT)

void
clutter_frame_clock_notify_presented (ClutterFrameClock *frame_clock,
                                      int64_t            presentation_time_us)
{
  if (presentation_time_us > frame_clock->last_presentation_time_us ||
      ((presentation_time_us - frame_clock->last_presentation_time_us) >
       INT64_MAX / 2))
    {
      frame_clock->last_presentation_time_us = presentation_time_us;
    }
  else
    {
      g_warning_once ("Bogus presentation time %" G_GINT64_FORMAT
                      " travelled back in time, using current time.",
                      presentation_time_us);
      frame_clock->last_presentation_time_us = g_get_monotonic_time ();
    }

  switch (frame_clock->state)
    {
    case CLUTTER_FRAME_CLOCK_STATE_INIT:
    case CLUTTER_FRAME_CLOCK_STATE_IDLE:
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED:
      g_warn_if_reached ();
      break;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHING:
    case CLUTTER_FRAME_CLOCK_STATE_PENDING_PRESENTED:
      if (frame_clock->pending_reschedule)
        {
          frame_clock->pending_reschedule = FALSE;
          clutter_frame_clock_schedule_update (frame_clock);
        }
      else
        {
          frame_clock->state = CLUTTER_FRAME_CLOCK_STATE_IDLE;
        }
      break;
    }
}

static void
calculate_next_update_time_us (ClutterFrameClock *frame_clock,
                               int64_t           *out_next_update_time_us)
{
  int64_t last_presentation_time_us;
  int64_t now_us;
  float refresh_rate;
  int64_t refresh_interval_us;
  int64_t min_render_time_allowed_us;
  int64_t max_render_time_allowed_us;
  int64_t next_presentation_time_us;
  int64_t next_update_time_us;

  now_us = g_get_monotonic_time ();

  refresh_rate = frame_clock->refresh_rate;
  refresh_interval_us = (int64_t) (0.5 + G_USEC_PER_SEC / refresh_rate);

  min_render_time_allowed_us = refresh_interval_us / 2;
  max_render_time_allowed_us = refresh_interval_us - SYNC_DELAY_US;

  if (min_render_time_allowed_us > max_render_time_allowed_us)
    min_render_time_allowed_us = max_render_time_allowed_us;

  last_presentation_time_us = frame_clock->last_presentation_time_us;
  next_presentation_time_us = last_presentation_time_us + refresh_interval_us;

  /* Skip ahead to get close to the actual next presentation time. */
  if (next_presentation_time_us < now_us)
    {
      int64_t logical_clock_offset_us;
      int64_t logical_clock_phase_us;
      int64_t hw_clock_offset_us;

      logical_clock_offset_us = now_us % refresh_interval_us;
      logical_clock_phase_us = now_us - logical_clock_offset_us;
      hw_clock_offset_us = last_presentation_time_us % refresh_interval_us;

      next_presentation_time_us = logical_clock_phase_us + hw_clock_offset_us;
    }

  while (next_presentation_time_us < now_us + min_render_time_allowed_us)
    next_presentation_time_us += refresh_interval_us;

  next_update_time_us = next_presentation_time_us - max_render_time_allowed_us;

  *out_next_update_time_us = next_update_time_us;
}

void
clutter_frame_clock_schedule_update (ClutterFrameClock *frame_clock)
{
  int64_t next_update_time_us = -1;

  switch (frame_clock->state)
    {
    case CLUTTER_FRAME_CLOCK_STATE_INIT:
      next_update_time_us = g_get_monotonic_time ();
      break;
    case CLUTTER_FRAME_CLOCK_STATE_IDLE:
      calculate_next_update_time_us (frame_clock, &next_update_time_us);
      break;
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED:
      return;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHING:
    case CLUTTER_FRAME_CLOCK_STATE_PENDING_PRESENTED:
      frame_clock->pending_reschedule = TRUE;
      return;
    }

  g_warn_if_fail (next_update_time_us != -1);

  g_source_set_ready_time (frame_clock->source, next_update_time_us);
  frame_clock->state = CLUTTER_FRAME_CLOCK_STATE_SCHEDULED;
}

static gboolean
clutter_frame_clock_dispatch (gpointer user_data)
{
  ClutterFrameClock *frame_clock = user_data;
  ClutterFrameResult result;

  g_source_set_ready_time (frame_clock->source, -1);

  frame_clock->state = CLUTTER_FRAME_CLOCK_STATE_DISPATCHING;

  result = frame_clock->listener.iface->frame (frame_clock,
                                               frame_clock->frame_count++,
                                               frame_clock->listener.user_data);

  switch (frame_clock->state)
    {
    case CLUTTER_FRAME_CLOCK_STATE_INIT:
    case CLUTTER_FRAME_CLOCK_STATE_PENDING_PRESENTED:
      g_warn_if_reached ();
      break;
    case CLUTTER_FRAME_CLOCK_STATE_IDLE:
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED:
      break;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHING:
      switch (result)
        {
        case CLUTTER_FRAME_RESULT_PENDING_PRESENTED:
          frame_clock->state = CLUTTER_FRAME_CLOCK_STATE_PENDING_PRESENTED;
          break;
        case CLUTTER_FRAME_RESULT_IDLE:
          frame_clock->state = CLUTTER_FRAME_CLOCK_STATE_IDLE;
          break;
        }
      break;
    }

  return G_SOURCE_CONTINUE;
}

static gboolean
frame_clock_source_dispatch (GSource     *source,
                             GSourceFunc  callback,
                             gpointer     user_data)
{
  return callback (user_data);
}

static GSourceFuncs frame_clock_source_funcs = {
  NULL,
  NULL,
  frame_clock_source_dispatch,
  NULL
};

static void
init_frame_clock_source (ClutterFrameClock *frame_clock)
{
  GSource *source;
  ClutterClockSource *clock_source;
  g_autofree char *name = NULL;

  source = g_source_new (&frame_clock_source_funcs, sizeof (ClutterClockSource));
  clock_source = (ClutterClockSource *) source;

  name = g_strdup_printf ("Clutter frame clock (%p)", frame_clock);
  g_source_set_name (source, name);
  g_source_set_priority (source, CLUTTER_PRIORITY_REDRAW);
  g_source_set_can_recurse (source, FALSE);
  g_source_set_callback (source, clutter_frame_clock_dispatch, frame_clock, NULL);
  clock_source->frame_clock = frame_clock;

  frame_clock->source = source;
  g_source_attach (source, NULL);
}

ClutterFrameClock *
clutter_frame_clock_new (float                            refresh_rate,
                         const ClutterFrameListenerIface *iface,
                         gpointer                         user_data)
{
  ClutterFrameClock *frame_clock;

  g_assert_cmpfloat (refresh_rate, >, 0.0);

  frame_clock = g_object_new (CLUTTER_TYPE_FRAME_CLOCK, NULL);

  frame_clock->listener.iface = iface;
  frame_clock->listener.user_data = user_data;

  init_frame_clock_source (frame_clock);

  frame_clock->refresh_rate = refresh_rate;

  return frame_clock;
}

static void
clutter_frame_clock_finalize (GObject *object)
{
  ClutterFrameClock *frame_clock = CLUTTER_FRAME_CLOCK (object);

  if (frame_clock->source)
    {
      g_source_destroy (frame_clock->source);
      g_source_unref (frame_clock->source);
    }

  G_OBJECT_CLASS (clutter_frame_clock_parent_class)->finalize (object);
}

static void
clutter_frame_clock_init (ClutterFrameClock *frame_clock)
{
  frame_clock->state = CLUTTER_FRAME_CLOCK_STATE_INIT;
}

static void
clutter_frame_clock_class_init (ClutterFrameClockClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = clutter_frame_clock_finalize;
}
