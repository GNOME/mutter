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
#include "clutter/clutter-private.h"
#include "clutter/clutter-timeline-private.h"
#include "cogl/cogl-trace.h"

enum
{
  DESTROY,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

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
  int64_t last_dispatch_time_us;
  int64_t last_presentation_time_us;

  gboolean is_next_presentation_time_valid;
  int64_t next_presentation_time_us;

  gboolean pending_reschedule;
  gboolean pending_reschedule_now;

  int inhibit_count;

  GList *timelines;
};

G_DEFINE_TYPE (ClutterFrameClock, clutter_frame_clock,
               G_TYPE_OBJECT)

float
clutter_frame_clock_get_refresh_rate (ClutterFrameClock *frame_clock)
{
  return frame_clock->refresh_rate;
}

void
clutter_frame_clock_add_timeline (ClutterFrameClock *frame_clock,
                                  ClutterTimeline   *timeline)
{
  gboolean is_first;

  if (g_list_find (frame_clock->timelines, timeline))
    return;

  is_first = !frame_clock->timelines;

  frame_clock->timelines = g_list_prepend (frame_clock->timelines, timeline);

  if (is_first)
    clutter_frame_clock_schedule_update (frame_clock);
}

void
clutter_frame_clock_remove_timeline (ClutterFrameClock *frame_clock,
                                     ClutterTimeline   *timeline)
{
  frame_clock->timelines = g_list_remove (frame_clock->timelines, timeline);
}

static void
advance_timelines (ClutterFrameClock *frame_clock,
                   int64_t            time_us)
{
  GList *timelines;
  GList *l;

  /* we protect ourselves from timelines being removed during
   * the advancement by other timelines by copying the list of
   * timelines, taking a reference on them, iterating over the
   * copied list and then releasing the reference.
   *
   * we cannot simply take a reference on the timelines and still
   * use the list held by the master clock because the do_tick()
   * might result in the creation of a new timeline, which gets
   * added at the end of the list with no reference increase and
   * thus gets disposed at the end of the iteration.
   *
   * this implies that a newly added timeline will not be advanced
   * by this clock iteration, which is perfectly fine since we're
   * in its first cycle.
   *
   * we also cannot steal the frame clock timelines list because
   * a timeline might be removed as the direct result of do_tick()
   * and remove_timeline() would not find the timeline, failing
   * and leaving a dangling pointer behind.
   */

  timelines = g_list_copy (frame_clock->timelines);
  g_list_foreach (timelines, (GFunc) g_object_ref, NULL);

  for (l = timelines; l; l = l->next)
    {
      ClutterTimeline *timeline = l->data;

      _clutter_timeline_do_tick (timeline, time_us / 1000);
    }

  g_list_free_full (timelines, g_object_unref);
}

static void
maybe_reschedule_update (ClutterFrameClock *frame_clock)
{
  if (frame_clock->pending_reschedule ||
      frame_clock->timelines)
    {
      frame_clock->pending_reschedule = FALSE;

      if (frame_clock->pending_reschedule_now)
        {
          frame_clock->pending_reschedule_now = FALSE;
          clutter_frame_clock_schedule_update_now (frame_clock);
        }
      else
        {
          clutter_frame_clock_schedule_update (frame_clock);
        }
    }
}

void
clutter_frame_clock_notify_presented (ClutterFrameClock *frame_clock,
                                      ClutterFrameInfo  *frame_info)
{
  frame_clock->last_presentation_time_us = frame_info->presentation_time;

  if (frame_info->refresh_rate > 1)
    frame_clock->refresh_rate = frame_info->refresh_rate;

  switch (frame_clock->state)
    {
    case CLUTTER_FRAME_CLOCK_STATE_INIT:
    case CLUTTER_FRAME_CLOCK_STATE_IDLE:
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED:
      g_warn_if_reached ();
      break;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHING:
    case CLUTTER_FRAME_CLOCK_STATE_PENDING_PRESENTED:
      frame_clock->state = CLUTTER_FRAME_CLOCK_STATE_IDLE;
      maybe_reschedule_update (frame_clock);
      break;
    }
}

void
clutter_frame_clock_notify_ready (ClutterFrameClock *frame_clock)
{
  switch (frame_clock->state)
    {
    case CLUTTER_FRAME_CLOCK_STATE_INIT:
    case CLUTTER_FRAME_CLOCK_STATE_IDLE:
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED:
      g_warn_if_reached ();
      break;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHING:
    case CLUTTER_FRAME_CLOCK_STATE_PENDING_PRESENTED:
      frame_clock->state = CLUTTER_FRAME_CLOCK_STATE_IDLE;
      maybe_reschedule_update (frame_clock);
      break;
    }
}

static void
calculate_next_update_time_us (ClutterFrameClock *frame_clock,
                               int64_t           *out_next_update_time_us,
                               int64_t           *out_next_presentation_time_us)
{
  int64_t last_presentation_time_us;
  int64_t now_us;
  float refresh_rate;
  int64_t refresh_interval_us;
  int64_t min_render_time_allowed_us;
  int64_t max_render_time_allowed_us;
  int64_t last_next_presentation_time_us;
  int64_t time_since_last_next_presentation_time_us;
  int64_t next_presentation_time_us;
  int64_t next_update_time_us;

  now_us = g_get_monotonic_time ();

  refresh_rate = frame_clock->refresh_rate;
  refresh_interval_us = (int64_t) (0.5 + G_USEC_PER_SEC / refresh_rate);

  if (frame_clock->last_presentation_time_us == 0)
    {
      *out_next_update_time_us =
        frame_clock->last_dispatch_time_us ?
        frame_clock->last_dispatch_time_us + refresh_interval_us :
        now_us;

      *out_next_presentation_time_us = 0;
      return;
    }

  min_render_time_allowed_us = refresh_interval_us / 2;
  max_render_time_allowed_us = refresh_interval_us - SYNC_DELAY_US;

  if (min_render_time_allowed_us > max_render_time_allowed_us)
    min_render_time_allowed_us = max_render_time_allowed_us;

  /*
   * The common case is that the next presentation happens 1 refresh interval
   * after the last presentation:
   *
   *        last_presentation_time_us
   *       /       next_presentation_time_us
   *      /       /
   *     /       /
   * |--|--o----|-------|--> presentation times
   * |  |  \    |
   * |  |   now_us
   * |  \______/
   * | refresh_interval_us
   * |
   * 0
   *
   */
  last_presentation_time_us = frame_clock->last_presentation_time_us;
  next_presentation_time_us = last_presentation_time_us + refresh_interval_us;

  /*
   * However, the last presentation could have happened more than a frame ago.
   * For example, due to idling (nothing on screen changed, so no need to
   * redraw) or due to frames missing deadlines (GPU busy with heavy rendering).
   * The following code adjusts next_presentation_time_us to be in the future,
   * but still aligned to display presentation times. Instead of
   * next presentation = last presentation + 1 * refresh interval, it will be
   * next presentation = last presentation + N * refresh interval.
   */
  if (next_presentation_time_us < now_us)
    {
      int64_t presentation_phase_us;
      int64_t current_phase_us;
      int64_t current_refresh_interval_start_us;

      /*
       * Let's say we're just past next_presentation_time_us.
       *
       * First, we compute presentation_phase_us. Real presentation times don't
       * have to be exact multiples of refresh_interval_us and
       * presentation_phase_us represents this difference. Next, we compute
       * current phase and the refresh interval start corresponding to now_us.
       * Finally, add presentation_phase_us and a refresh interval to get the
       * next presentation after now_us.
       *
       *        last_presentation_time_us
       *       /       next_presentation_time_us
       *      /       /   now_us
       *     /       /   /   new next_presentation_time_us
       * |--|-------|---o---|-------|--> presentation times
       * |        __|
       * |       |presentation_phase_us
       * |       |
       * |       |     now_us - presentation_phase_us
       * |       |    /
       * |-------|---o---|-------|-----> integer multiples of refresh_interval_us
       * |       \__/
       * |       |current_phase_us
       * |       \
       * |        current_refresh_interval_start_us
       * 0
       *
       */

      presentation_phase_us = last_presentation_time_us % refresh_interval_us;
      current_phase_us = (now_us - presentation_phase_us) % refresh_interval_us;
      current_refresh_interval_start_us =
        now_us - presentation_phase_us - current_phase_us;

      next_presentation_time_us =
        current_refresh_interval_start_us +
        presentation_phase_us +
        refresh_interval_us;
    }

  /*
   * Skip one interval if we got an early presented event.
   *
   *        last frame this was last_presentation_time
   *       /       frame_clock->next_presentation_time_us
   *      /       /
   * |---|-o-----|-x----->
   *       |       \
   *       \        next_presentation_time_us is thus right after the last one
   *        but got an unexpected early presentation
   *             \_/
   *             time_since_last_next_presentation_time_us
   *
   */
  last_next_presentation_time_us = frame_clock->next_presentation_time_us;
  time_since_last_next_presentation_time_us =
      next_presentation_time_us - last_next_presentation_time_us;
  if (frame_clock->is_next_presentation_time_valid &&
      time_since_last_next_presentation_time_us < (refresh_interval_us / 2))
    {
      next_presentation_time_us =
        frame_clock->next_presentation_time_us + refresh_interval_us;
    }

  while (next_presentation_time_us < now_us + min_render_time_allowed_us)
    next_presentation_time_us += refresh_interval_us;

  next_update_time_us = next_presentation_time_us - max_render_time_allowed_us;

  *out_next_update_time_us = next_update_time_us;
  *out_next_presentation_time_us = next_presentation_time_us;
}

void
clutter_frame_clock_inhibit (ClutterFrameClock *frame_clock)
{
  frame_clock->inhibit_count++;

  if (frame_clock->inhibit_count == 1)
    {
      switch (frame_clock->state)
        {
        case CLUTTER_FRAME_CLOCK_STATE_INIT:
        case CLUTTER_FRAME_CLOCK_STATE_IDLE:
          break;
        case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED:
          frame_clock->pending_reschedule = TRUE;
          frame_clock->state = CLUTTER_FRAME_CLOCK_STATE_IDLE;
          break;
        case CLUTTER_FRAME_CLOCK_STATE_DISPATCHING:
        case CLUTTER_FRAME_CLOCK_STATE_PENDING_PRESENTED:
          break;
        }

      g_source_set_ready_time (frame_clock->source, -1);
    }
}

void
clutter_frame_clock_uninhibit (ClutterFrameClock *frame_clock)
{
  g_return_if_fail (frame_clock->inhibit_count > 0);

  frame_clock->inhibit_count--;

  if (frame_clock->inhibit_count == 0)
    maybe_reschedule_update (frame_clock);
}

void
clutter_frame_clock_schedule_update_now (ClutterFrameClock *frame_clock)
{
  int64_t next_update_time_us = -1;

  if (frame_clock->inhibit_count > 0)
    {
      frame_clock->pending_reschedule = TRUE;
      frame_clock->pending_reschedule_now = TRUE;
      return;
    }

  switch (frame_clock->state)
    {
    case CLUTTER_FRAME_CLOCK_STATE_INIT:
    case CLUTTER_FRAME_CLOCK_STATE_IDLE:
      next_update_time_us = g_get_monotonic_time ();
      break;
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED:
      return;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHING:
    case CLUTTER_FRAME_CLOCK_STATE_PENDING_PRESENTED:
      frame_clock->pending_reschedule = TRUE;
      frame_clock->pending_reschedule_now = TRUE;
      return;
    }

  g_warn_if_fail (next_update_time_us != -1);

  g_source_set_ready_time (frame_clock->source, next_update_time_us);
  frame_clock->state = CLUTTER_FRAME_CLOCK_STATE_SCHEDULED;
  frame_clock->is_next_presentation_time_valid = FALSE;
}

void
clutter_frame_clock_schedule_update (ClutterFrameClock *frame_clock)
{
  int64_t next_update_time_us = -1;

  if (frame_clock->inhibit_count > 0)
    {
      frame_clock->pending_reschedule = TRUE;
      return;
    }

  switch (frame_clock->state)
    {
    case CLUTTER_FRAME_CLOCK_STATE_INIT:
      next_update_time_us = g_get_monotonic_time ();
      break;
    case CLUTTER_FRAME_CLOCK_STATE_IDLE:
      calculate_next_update_time_us (frame_clock,
                                     &next_update_time_us,
                                     &frame_clock->next_presentation_time_us);
      frame_clock->is_next_presentation_time_valid =
        (frame_clock->next_presentation_time_us != 0);
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

static void
clutter_frame_clock_dispatch (ClutterFrameClock *frame_clock,
                              int64_t            time_us)
{
  int64_t frame_count;
  ClutterFrameResult result;

  COGL_TRACE_BEGIN_SCOPED (ClutterFrameClockDispatch, "Frame Clock (dispatch)");

  frame_clock->last_dispatch_time_us = time_us;
  g_source_set_ready_time (frame_clock->source, -1);

  frame_clock->state = CLUTTER_FRAME_CLOCK_STATE_DISPATCHING;

  frame_count = frame_clock->frame_count++;

  COGL_TRACE_BEGIN (ClutterFrameClockEvents, "Frame Clock (before frame)");
  if (frame_clock->listener.iface->before_frame)
    {
      frame_clock->listener.iface->before_frame (frame_clock,
                                                 frame_count,
                                                 frame_clock->listener.user_data);
    }
  COGL_TRACE_END (ClutterFrameClockEvents);

  COGL_TRACE_BEGIN (ClutterFrameClockTimelines, "Frame Clock (timelines)");
  advance_timelines (frame_clock, time_us);
  COGL_TRACE_END (ClutterFrameClockTimelines);

  COGL_TRACE_BEGIN (ClutterFrameClockFrame, "Frame Clock (frame)");
  result = frame_clock->listener.iface->frame (frame_clock,
                                               frame_count,
                                               time_us,
                                               frame_clock->listener.user_data);
  COGL_TRACE_END (ClutterFrameClockFrame);

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
          maybe_reschedule_update (frame_clock);
          break;
        }
      break;
    }
}

static gboolean
frame_clock_source_dispatch (GSource     *source,
                             GSourceFunc  callback,
                             gpointer     user_data)
{
  ClutterClockSource *clock_source = (ClutterClockSource *) source;
  ClutterFrameClock *frame_clock = clock_source->frame_clock;
  int64_t dispatch_time_us;

  dispatch_time_us = g_source_get_time (source);
  clutter_frame_clock_dispatch (frame_clock, dispatch_time_us);

  return G_SOURCE_CONTINUE;
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

void
clutter_frame_clock_destroy (ClutterFrameClock *frame_clock)
{
  g_object_run_dispose (G_OBJECT (frame_clock));
  g_object_unref (frame_clock);
}

static void
clutter_frame_clock_dispose (GObject *object)
{
  ClutterFrameClock *frame_clock = CLUTTER_FRAME_CLOCK (object);

  if (frame_clock->source)
    {
      g_signal_emit (frame_clock, signals[DESTROY], 0);
      g_source_destroy (frame_clock->source);
      g_clear_pointer (&frame_clock->source, g_source_unref);
    }

  G_OBJECT_CLASS (clutter_frame_clock_parent_class)->dispose (object);
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

  object_class->dispose = clutter_frame_clock_dispose;

  signals[DESTROY] =
    g_signal_new (I_("destroy"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  0);
}
