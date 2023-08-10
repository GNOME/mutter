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

#include "clutter/clutter-build-config.h"

#include "clutter/clutter-frame-clock.h"

#include "clutter/clutter-debug.h"
#include "clutter/clutter-frame-private.h"
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

#define SYNC_DELAY_FALLBACK_FRACTION 0.875

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
  int64_t refresh_interval_us;
  ClutterFrameListener listener;

  GSource *source;

  int64_t frame_count;

  ClutterFrameClockState state;
  int64_t last_dispatch_time_us;
  int64_t last_dispatch_lateness_us;
  int64_t last_presentation_time_us;
  int64_t next_update_time_us;

  gboolean is_next_presentation_time_valid;
  int64_t next_presentation_time_us;
  int64_t min_render_time_allowed_us;

  /* Buffer must be submitted to KMS and GPU rendering must be finished
   * this amount of time before the next presentation time.
   */
  int64_t vblank_duration_us;
  /* Last KMS buffer submission time. */
  int64_t last_flip_time_us;

  /* Last time we promoted short-term maximum to long-term one */
  int64_t longterm_promotion_us;
  /* Long-term maximum update duration */
  int64_t longterm_max_update_duration_us;
  /* Short-term maximum update duration */
  int64_t shortterm_max_update_duration_us;

  /* If we got new measurements last frame. */
  gboolean got_measurements_last_frame;
  gboolean ever_got_measurements;

  gboolean pending_reschedule;
  gboolean pending_reschedule_now;

  int inhibit_count;

  GList *timelines;

  int n_missed_frames;
  int64_t missed_frame_report_time_us;

  int64_t last_dispatch_interval_us;
};

G_DEFINE_TYPE (ClutterFrameClock, clutter_frame_clock,
               G_TYPE_OBJECT)

float
clutter_frame_clock_get_refresh_rate (ClutterFrameClock *frame_clock)
{
  return frame_clock->refresh_rate;
}

static void
clutter_frame_clock_set_refresh_rate (ClutterFrameClock *frame_clock,
                                      float              refresh_rate)
{
  frame_clock->refresh_rate = refresh_rate;
  frame_clock->refresh_interval_us =
    (int64_t) (0.5 + G_USEC_PER_SEC / refresh_rate);
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

static void
maybe_update_longterm_max_duration_us (ClutterFrameClock *frame_clock,
                                       ClutterFrameInfo  *frame_info)
{
  /* Do not update long-term max if there has been no measurement */
  if (!frame_clock->shortterm_max_update_duration_us)
    return;

  if ((frame_info->presentation_time - frame_clock->longterm_promotion_us) <
      G_USEC_PER_SEC)
    return;

  if (frame_clock->longterm_max_update_duration_us >
      frame_clock->shortterm_max_update_duration_us)
    {
      /* Exponential drop-off toward the short-term max */
      frame_clock->longterm_max_update_duration_us -=
        (frame_clock->longterm_max_update_duration_us -
         frame_clock->shortterm_max_update_duration_us) / 2;
    }
  else
    {
      frame_clock->longterm_max_update_duration_us =
        frame_clock->shortterm_max_update_duration_us;
    }

  frame_clock->shortterm_max_update_duration_us = 0;
  frame_clock->longterm_promotion_us = frame_info->presentation_time;
}

void
clutter_frame_clock_notify_presented (ClutterFrameClock *frame_clock,
                                      ClutterFrameInfo  *frame_info)
{
  COGL_TRACE_BEGIN_SCOPED (ClutterFrameClockNotifyPresented,
                           "Frame Clock (presented)");

  if (G_UNLIKELY (CLUTTER_HAS_DEBUG (FRAME_CLOCK)))
    {
      int64_t now_us;

      if (frame_clock->is_next_presentation_time_valid &&
          frame_info->presentation_time != 0)
        {
          int64_t diff_us;
          int n_missed_frames;

          diff_us = llabs (frame_info->presentation_time -
                           frame_clock->next_presentation_time_us);
          n_missed_frames =
            (int) roundf ((float) diff_us /
                          (float) frame_clock->refresh_interval_us);

          frame_clock->n_missed_frames = n_missed_frames;
        }

      now_us = g_get_monotonic_time ();
      if ((now_us - frame_clock->missed_frame_report_time_us) > G_USEC_PER_SEC)
        {
          if (frame_clock->n_missed_frames > 0)
            {
              CLUTTER_NOTE (FRAME_CLOCK, "Missed %d frames the last second",
                            frame_clock->n_missed_frames);
            }
          frame_clock->n_missed_frames = 0;
          frame_clock->missed_frame_report_time_us = now_us;
        }
    }

#ifdef COGL_HAS_TRACING
  if (G_UNLIKELY (cogl_is_tracing_enabled ()))
    {
      int64_t current_time_us;
      g_autoptr (GString) description = NULL;

      current_time_us = g_get_monotonic_time ();
      description = g_string_new (NULL);

      if (frame_info->presentation_time != 0)
        {
          if (frame_info->presentation_time <= current_time_us)
            {
              g_string_append_printf (description,
                                      "presentation was %ld µs earlier",
                                      current_time_us - frame_info->presentation_time);
            }
          else
            {
              g_string_append_printf (description,
                                      "presentation will be %ld µs later",
                                      frame_info->presentation_time - current_time_us);
            }
        }

      if (frame_info->gpu_rendering_duration_ns != 0)
        {
          if (description->len > 0)
            g_string_append (description, ", ");

          g_string_append_printf (description,
                                  "buffer swap to GPU done: %ld µs",
                                  ns2us (frame_info->gpu_rendering_duration_ns));
        }

      COGL_TRACE_DESCRIBE (ClutterFrameClockNotifyPresented, description->str);
    }
#endif

  if (frame_info->presentation_time > 0)
    frame_clock->last_presentation_time_us = frame_info->presentation_time;

  frame_clock->got_measurements_last_frame = FALSE;

  if (frame_info->cpu_time_before_buffer_swap_us != 0)
    {
      int64_t dispatch_to_swap_us, swap_to_rendering_done_us, swap_to_flip_us;

      dispatch_to_swap_us =
        frame_info->cpu_time_before_buffer_swap_us -
        frame_clock->last_dispatch_time_us;
      swap_to_rendering_done_us =
        frame_info->gpu_rendering_duration_ns / 1000;
      swap_to_flip_us =
        frame_clock->last_flip_time_us -
        frame_info->cpu_time_before_buffer_swap_us;

      CLUTTER_NOTE (FRAME_TIMINGS,
                    "update2dispatch %ld µs, dispatch2swap %ld µs, swap2render %ld µs, swap2flip %ld µs",
                    frame_clock->last_dispatch_lateness_us,
                    dispatch_to_swap_us,
                    swap_to_rendering_done_us,
                    swap_to_flip_us);

      frame_clock->shortterm_max_update_duration_us =
        CLAMP (frame_clock->last_dispatch_lateness_us + dispatch_to_swap_us +
               MAX (swap_to_rendering_done_us, swap_to_flip_us),
               frame_clock->shortterm_max_update_duration_us,
               frame_clock->refresh_interval_us);

      maybe_update_longterm_max_duration_us (frame_clock, frame_info);

      frame_clock->got_measurements_last_frame = TRUE;
      frame_clock->ever_got_measurements = TRUE;
    }
  else
    {
      CLUTTER_NOTE (FRAME_TIMINGS, "update2dispatch %ld µs",
                    frame_clock->last_dispatch_lateness_us);
    }

  if (frame_info->refresh_rate > 1.0)
    {
      clutter_frame_clock_set_refresh_rate (frame_clock,
                                            frame_info->refresh_rate);
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
      frame_clock->state = CLUTTER_FRAME_CLOCK_STATE_IDLE;
      maybe_reschedule_update (frame_clock);
      break;
    }
}

void
clutter_frame_clock_notify_ready (ClutterFrameClock *frame_clock)
{
  COGL_TRACE_BEGIN_SCOPED (ClutterFrameClockNotifyReady, "Frame Clock (ready)");

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

static int64_t
clutter_frame_clock_compute_max_render_time_us (ClutterFrameClock *frame_clock)
{
  int64_t refresh_interval_us;
  int64_t max_render_time_us;

  refresh_interval_us = frame_clock->refresh_interval_us;

  if (!frame_clock->ever_got_measurements ||
      G_UNLIKELY (clutter_paint_debug_flags &
                  CLUTTER_DEBUG_DISABLE_DYNAMIC_MAX_RENDER_TIME))
    return refresh_interval_us * SYNC_DELAY_FALLBACK_FRACTION;

  /* Max render time shows how early the frame clock needs to be dispatched
   * to make it to the predicted next presentation time. It is an estimate of
   * the total update duration, which is composed of:
   * - Dispatch start lateness.
   * - The duration from dispatch start to buffer swap.
   * - The maximum of duration from buffer swap to GPU rendering finish and
   *   duration from buffer swap to buffer submission to KMS. This is because
   *   both of these things need to happen before the vblank, and they are done
   *   in parallel.
   * - The duration of vertical blank.
   * - A constant to account for variations in the above estimates.
   */
  max_render_time_us =
    MAX (frame_clock->longterm_max_update_duration_us,
         frame_clock->shortterm_max_update_duration_us) +
    frame_clock->vblank_duration_us +
    clutter_max_render_time_constant_us;

  max_render_time_us = CLAMP (max_render_time_us, 0, refresh_interval_us);

  return max_render_time_us;
}

static void
calculate_next_update_time_us (ClutterFrameClock *frame_clock,
                               int64_t           *out_next_update_time_us,
                               int64_t           *out_next_presentation_time_us,
                               int64_t           *out_min_render_time_allowed_us)
{
  int64_t last_presentation_time_us;
  int64_t now_us;
  int64_t refresh_interval_us;
  int64_t min_render_time_allowed_us;
  int64_t max_render_time_allowed_us;
  int64_t next_presentation_time_us;
  int64_t next_update_time_us;

  now_us = g_get_monotonic_time ();

  refresh_interval_us = frame_clock->refresh_interval_us;

  if (frame_clock->last_presentation_time_us == 0)
    {
      *out_next_update_time_us =
        frame_clock->last_dispatch_time_us ?
        ((frame_clock->last_dispatch_time_us -
          frame_clock->last_dispatch_lateness_us) + refresh_interval_us) :
        now_us;

      *out_next_presentation_time_us = 0;
      *out_min_render_time_allowed_us = 0;
      return;
    }

  min_render_time_allowed_us = refresh_interval_us / 2;
  max_render_time_allowed_us =
    clutter_frame_clock_compute_max_render_time_us (frame_clock);

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
      int64_t current_phase_us;

      /*
       * Let's say we're just past next_presentation_time_us.
       *
       * First, we calculate current_phase_us, corresponding to the time since
       * the last integer multiple of the refresh interval passed after the last
       * presentation time. Subtracting this phase from now_us and adding a
       * refresh interval gets us the next possible presentation time after
       * now_us.
       *
       *     last_presentation_time_us
       *    /       next_presentation_time_us
       *   /       /   now_us
       *  /       /   /    new next_presentation_time_us
       * |-------|---o---|-------|--> possible presentation times
       *          \_/     \_____/
       *          /           \
       * current_phase_us      refresh_interval_us
       */

      current_phase_us = (now_us - last_presentation_time_us) % refresh_interval_us;
      next_presentation_time_us = now_us - current_phase_us + refresh_interval_us;
    }

  if (frame_clock->is_next_presentation_time_valid)
    {
      int64_t last_next_presentation_time_us;
      int64_t time_since_last_next_presentation_time_us;

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
      if (time_since_last_next_presentation_time_us > 0 &&
          time_since_last_next_presentation_time_us < (refresh_interval_us / 2))
        {
          next_presentation_time_us =
            frame_clock->next_presentation_time_us + refresh_interval_us;
        }
    }

  if (next_presentation_time_us != last_presentation_time_us + refresh_interval_us)
    {
      /* There was an idle period since the last presentation, so there seems
       * be no constantly updating actor. In this case it's best to start
       * working on the next update ASAP, this results in lowest average latency
       * for sporadic user input.
       */
      next_update_time_us = now_us;
      min_render_time_allowed_us = 0;
    }
  else
    {
      while (next_presentation_time_us < now_us + min_render_time_allowed_us)
        next_presentation_time_us += refresh_interval_us;

      next_update_time_us = next_presentation_time_us - max_render_time_allowed_us;
      if (next_update_time_us < now_us)
        next_update_time_us = now_us;
    }

  *out_next_update_time_us = next_update_time_us;
  *out_next_presentation_time_us = next_presentation_time_us;
  *out_min_render_time_allowed_us = min_render_time_allowed_us;
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
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED:
      next_update_time_us = g_get_monotonic_time ();
      break;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHING:
    case CLUTTER_FRAME_CLOCK_STATE_PENDING_PRESENTED:
      frame_clock->pending_reschedule = TRUE;
      frame_clock->pending_reschedule_now = TRUE;
      return;
    }

  g_warn_if_fail (next_update_time_us != -1);

  frame_clock->next_update_time_us = next_update_time_us;
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
                                     &frame_clock->next_presentation_time_us,
                                     &frame_clock->min_render_time_allowed_us);
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

  frame_clock->next_update_time_us = next_update_time_us;
  g_source_set_ready_time (frame_clock->source, next_update_time_us);
  frame_clock->state = CLUTTER_FRAME_CLOCK_STATE_SCHEDULED;
}

static void
clutter_frame_clock_dispatch (ClutterFrameClock *frame_clock,
                              int64_t            time_us)
{
  const ClutterFrameListenerIface *iface = frame_clock->listener.iface;
  g_autoptr (ClutterFrame) frame = NULL;
  int64_t frame_count;
  ClutterFrameResult result;
  int64_t ideal_dispatch_time_us, lateness_us;

#ifdef COGL_HAS_TRACING
  int64_t this_dispatch_ready_time_us;
  int64_t this_dispatch_time_us;

  COGL_TRACE_BEGIN_SCOPED (ClutterFrameClockDispatch, "Frame Clock (dispatch)");

  this_dispatch_ready_time_us = g_source_get_ready_time (frame_clock->source);
  this_dispatch_time_us = time_us;
#endif

  ideal_dispatch_time_us = frame_clock->next_update_time_us;

  if (ideal_dispatch_time_us <= 0)
    ideal_dispatch_time_us = (frame_clock->last_dispatch_time_us -
                              frame_clock->last_dispatch_lateness_us) +
                             frame_clock->refresh_interval_us;

  lateness_us = time_us - ideal_dispatch_time_us;
  if (lateness_us < 0 || lateness_us >= frame_clock->refresh_interval_us)
    frame_clock->last_dispatch_lateness_us = 0;
  else
    frame_clock->last_dispatch_lateness_us = lateness_us;

#ifdef CLUTTER_ENABLE_DEBUG
  if (G_UNLIKELY (CLUTTER_HAS_DEBUG (FRAME_TIMINGS)))
    {
      int64_t dispatch_interval_us, jitter_us;

      dispatch_interval_us = time_us - frame_clock->last_dispatch_time_us;
      jitter_us = llabs (dispatch_interval_us -
                         frame_clock->last_dispatch_interval_us) %
                  frame_clock->refresh_interval_us;
      frame_clock->last_dispatch_interval_us = dispatch_interval_us;
      CLUTTER_NOTE (FRAME_TIMINGS, "dispatch jitter %5ldµs (%3ld%%)",
                    jitter_us,
                    jitter_us * 100 / frame_clock->refresh_interval_us);
    }
#endif

  frame_clock->last_dispatch_time_us = time_us;
  g_source_set_ready_time (frame_clock->source, -1);

  frame_clock->state = CLUTTER_FRAME_CLOCK_STATE_DISPATCHING;

  frame_count = frame_clock->frame_count++;

  if (iface->new_frame)
    frame = iface->new_frame (frame_clock, frame_clock->listener.user_data);
  if (!frame)
    frame = clutter_frame_new (ClutterFrame, NULL);

  frame->frame_count = frame_count;
  frame->has_target_presentation_time = frame_clock->is_next_presentation_time_valid;
  frame->target_presentation_time_us = frame_clock->next_presentation_time_us;
  frame->min_render_time_allowed_us = frame_clock->min_render_time_allowed_us;

  COGL_TRACE_BEGIN (ClutterFrameClockEvents, "Frame Clock (before frame)");
  if (iface->before_frame)
    iface->before_frame (frame_clock, frame, frame_clock->listener.user_data);
  COGL_TRACE_END (ClutterFrameClockEvents);

  COGL_TRACE_BEGIN (ClutterFrameClockTimelines, "Frame Clock (timelines)");
  if (frame_clock->is_next_presentation_time_valid)
    time_us = frame_clock->next_presentation_time_us;
  advance_timelines (frame_clock, time_us);
  COGL_TRACE_END (ClutterFrameClockTimelines);

  COGL_TRACE_BEGIN (ClutterFrameClockFrame, "Frame Clock (frame)");
  result = iface->frame (frame_clock, frame, frame_clock->listener.user_data);
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

#ifdef COGL_HAS_TRACING
  if (this_dispatch_ready_time_us != -1 &&
      G_UNLIKELY (cogl_is_tracing_enabled ()))
    {
      g_autofree char *description = NULL;
      description = g_strdup_printf ("dispatched %ld µs late",
                                     this_dispatch_time_us - this_dispatch_ready_time_us);
      COGL_TRACE_DESCRIBE (ClutterFrameClockDispatch, description);
    }
#endif
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

void
clutter_frame_clock_record_flip_time (ClutterFrameClock *frame_clock,
                                      int64_t            flip_time_us)
{
  frame_clock->last_flip_time_us = flip_time_us;
}

GString *
clutter_frame_clock_get_max_render_time_debug_info (ClutterFrameClock *frame_clock)
{
  int64_t max_update_duration_us;
  GString *string;

  string = g_string_new (NULL);
  g_string_append_printf (string, "Max render time: %ld µs",
                          clutter_frame_clock_compute_max_render_time_us (frame_clock));

  if (frame_clock->got_measurements_last_frame)
    g_string_append_printf (string, " =");
  else
    g_string_append_printf (string, " (no measurements last frame)");

  max_update_duration_us =
    MAX (frame_clock->longterm_max_update_duration_us,
         frame_clock->shortterm_max_update_duration_us);

  g_string_append_printf (string, "\nVblank duration: %ld µs +",
                          frame_clock->vblank_duration_us);
  g_string_append_printf (string, "\nUpdate duration: %ld µs +",
                          max_update_duration_us);
  g_string_append_printf (string, "\nConstant: %d µs",
                          clutter_max_render_time_constant_us);

  return string;
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

  name = g_strdup_printf ("[mutter] Clutter frame clock (%p)", frame_clock);
  g_source_set_name (source, name);
  g_source_set_priority (source, CLUTTER_PRIORITY_REDRAW);
  g_source_set_can_recurse (source, FALSE);
  clock_source->frame_clock = frame_clock;

  frame_clock->source = source;
  g_source_attach (source, NULL);
}

ClutterFrameClock *
clutter_frame_clock_new (float                            refresh_rate,
                         int64_t                          vblank_duration_us,
                         const ClutterFrameListenerIface *iface,
                         gpointer                         user_data)
{
  ClutterFrameClock *frame_clock;

  g_assert_cmpfloat (refresh_rate, >, 0.0);

  frame_clock = g_object_new (CLUTTER_TYPE_FRAME_CLOCK, NULL);

  frame_clock->listener.iface = iface;
  frame_clock->listener.user_data = user_data;

  init_frame_clock_source (frame_clock);

  clutter_frame_clock_set_refresh_rate (frame_clock, refresh_rate);
  frame_clock->vblank_duration_us = vblank_duration_us;

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

  g_warn_if_fail (frame_clock->state != CLUTTER_FRAME_CLOCK_STATE_DISPATCHING);

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
