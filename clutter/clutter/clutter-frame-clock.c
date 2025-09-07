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

#include "config.h"

#include "clutter/clutter-frame-clock.h"

#include <glib/gstdio.h>

#ifdef HAVE_TIMERFD
#include <sys/timerfd.h>
#include <time.h>
#endif

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

#define SYNC_DELAY_FALLBACK_FRACTION 0.875f

#define MINIMUM_REFRESH_RATE 30.f

G_DEFINE_ABSTRACT_TYPE (ClutterFrameClockDriver, clutter_frame_clock_driver,
                        G_TYPE_OBJECT)

typedef struct _ClutterFrameListener
{
  const ClutterFrameListenerIface *iface;
  gpointer user_data;
} ClutterFrameListener;

typedef struct _DeferredTime
{
  int64_t target_time_us;
} DeferredTime;

typedef struct _ClutterClockSource
{
  GSource source;

  ClutterFrameClock *frame_clock;

#ifdef HAVE_TIMERFD
  int tfd;
  struct itimerspec tfd_spec;
#endif
} ClutterClockSource;

typedef enum _ClutterFrameClockState
{
  CLUTTER_FRAME_CLOCK_STATE_INIT,
  CLUTTER_FRAME_CLOCK_STATE_IDLE,
  CLUTTER_FRAME_CLOCK_STATE_SCHEDULED,
  CLUTTER_FRAME_CLOCK_STATE_SCHEDULED_NOW,
  CLUTTER_FRAME_CLOCK_STATE_SCHEDULED_LATER,
  CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE,
  CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED,
  CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED_NOW,
  CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED_LATER,
  CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_TWO,
} ClutterFrameClockState;

typedef struct _Frame
{
  int use_count;
  int64_t dispatch_time_us;
  int64_t dispatch_lateness_us;
  int64_t presentation_time_us;
  int64_t target_presentation_time_us;
  int64_t flip_time_us;
  int64_t dispatch_interval_us;
  ClutterFrameInfoFlag presentation_flags;
  gboolean got_measurements;
} Frame;

struct _ClutterFrameClock
{
  GObject parent;

  gboolean destroy_emitted;

  float refresh_rate;
  int64_t refresh_interval_us;
  int64_t maximum_refresh_interval_us;

  ClutterFrameListener listener;

  ClutterFrameClockDriver *driver;

  GSource *source;

  int64_t frame_count;

  ClutterFrameClockState state;
  ClutterFrameClockMode mode;

  int64_t next_update_time_us;

  Frame frame_pool[3];
  Frame *prev_dispatch;
  Frame *next_presentation;
  Frame *next_next_presentation;
  Frame *prev_presentation;

  gboolean is_next_presentation_time_valid;
  int64_t next_presentation_time_us;

  gboolean has_next_frame_deadline;
  int64_t next_frame_deadline_us;

  /* Buffer must be submitted to KMS and GPU rendering must be finished
   * this amount of time before the next presentation time.
   */
  int64_t vblank_duration_us;

  /* Last time we promoted short-term maximum to long-term one */
  int64_t longterm_promotion_us;
  /* Long-term maximum update duration */
  int64_t longterm_max_update_duration_us;
  /* Short-term maximum update duration */
  int64_t shortterm_max_update_duration_us;

  gboolean ever_got_measurements;

  gboolean pending_reschedule;
  gboolean pending_reschedule_now;

  int inhibit_count;

  GList *timelines;

  int n_missed_frames;
  int64_t missed_frame_report_time_us;

  int64_t deadline_evasion_us;
  int64_t frame_sync_update_time_us;

  char *output_name;

  GQueue *deferred_times;
};

G_DEFINE_TYPE (ClutterFrameClock, clutter_frame_clock,
               G_TYPE_OBJECT)

static void
clutter_frame_clock_schedule_update_later (ClutterFrameClock *frame_clock,
                                           int64_t            target_us);

#ifdef CLUTTER_ENABLE_DEBUG
static const char *
clutter_frame_clock_state_to_string (ClutterFrameClockState state)
{
  switch (state)
    {
    case CLUTTER_FRAME_CLOCK_STATE_INIT:
      return "init";
    case CLUTTER_FRAME_CLOCK_STATE_IDLE:
      return "idle";
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED:
      return "scheduled";
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED_NOW:
      return "scheduled-now";
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED_LATER:
      return "scheduled-later";
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE:
      return "dispatched-one";
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED:
      return "dispatched-one-and-scheduled";
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED_NOW:
      return "dispatched-one-and-scheduled-now";
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED_LATER:
      return "dispatched-one-and-scheduled-later";
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_TWO:
      return "dispatched-two";
    }
  g_assert_not_reached ();
}
#endif

static void
clutter_frame_clock_set_state (ClutterFrameClock      *frame_clock,
                               ClutterFrameClockState  state)
{
  CLUTTER_NOTE (FRAME_CLOCK, "Frame clock %s state transition: %s => %s",
                frame_clock->output_name,
                clutter_frame_clock_state_to_string (frame_clock->state),
                clutter_frame_clock_state_to_string (state));
  frame_clock->state = state;
}

static void
clutter_frame_clock_driver_class_init (ClutterFrameClockDriverClass *klass)
{
}

static void
clutter_frame_clock_driver_init (ClutterFrameClockDriver *driver)
{
}

static void
clutter_frame_clock_driver_schedule_update (ClutterFrameClockDriver *driver)
{
  CLUTTER_FRAME_CLOCK_DRIVER_GET_CLASS (driver)->schedule_update (driver);
}

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

static Frame *
clutter_frame_clock_new_frame (ClutterFrameClock *frame_clock)
{
  for (int i = 0; i < G_N_ELEMENTS (frame_clock->frame_pool); i++)
    {
      Frame *frame = &frame_clock->frame_pool[i];

      if (frame->use_count == 0)
        {
          memset (frame, 0, sizeof (*frame));
          frame->use_count = 1;
          return frame;
        }
    }

  g_assert_not_reached ();
  return NULL;
}

static Frame *
ref_frame (Frame *frame)
{
  frame->use_count++;
  return frame;
}

static void
unref_frame (Frame *frame)
{
  g_return_if_fail (frame->use_count > 0);
  frame->use_count--;
}

static void
clear_frame (Frame **frame)
{
  if (frame && *frame)
    {
      unref_frame (*frame);
      *frame = NULL;
    }
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

static gboolean
clean_deferred_times (ClutterFrameClock *frame_clock)
{
  DeferredTime *head;
  gboolean cleaned_times = FALSE;
  int64_t current_time_us;

  if (frame_clock->is_next_presentation_time_valid)
    current_time_us = frame_clock->next_presentation_time_us;
  else
    current_time_us = g_get_monotonic_time ();

  while ((head = g_queue_peek_head (frame_clock->deferred_times)))
    {
      if (current_time_us < head->target_time_us)
         break;

      g_free (g_queue_pop_head (frame_clock->deferred_times));
      cleaned_times = TRUE;
    }
  return cleaned_times;
}

static void
maybe_reschedule_update (ClutterFrameClock *frame_clock)
{
  DeferredTime *head;
  gboolean cleaned;

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
      return;
    }

  cleaned = clean_deferred_times (frame_clock);
  head = g_queue_peek_head (frame_clock->deferred_times);

  if (head)
    {
      clutter_frame_clock_schedule_update_later (frame_clock,
                                                 head->target_time_us);
    }
  else if (cleaned)
    {
      clutter_frame_clock_schedule_update (frame_clock);
    }
}

static void
maybe_update_longterm_max_duration_us (ClutterFrameClock *frame_clock,
                                       ClutterFrameInfo  *frame_info)
{
  if ((frame_info->presentation_time - frame_clock->longterm_promotion_us) <
      G_USEC_PER_SEC)
    return;

  if (frame_clock->longterm_max_update_duration_us >
      frame_clock->shortterm_max_update_duration_us)
    {
#ifdef CLUTTER_ENABLE_DEBUG
      int64_t old_duration_us;

      old_duration_us = frame_clock->longterm_max_update_duration_us;
#endif
      /* Exponential drop-off toward the short-term max */
      frame_clock->longterm_max_update_duration_us -=
        (frame_clock->longterm_max_update_duration_us -
         frame_clock->shortterm_max_update_duration_us) / 2;

      CLUTTER_NOTE (FRAME_TIMINGS,
                    "Maximum update duration estimate updated: %ldµs → %ldµs",
                    old_duration_us,
                    frame_clock->longterm_max_update_duration_us);
    }
  else
    {
      frame_clock->longterm_max_update_duration_us =
        frame_clock->shortterm_max_update_duration_us;
    }

  frame_clock->shortterm_max_update_duration_us = 0;
  frame_clock->longterm_promotion_us = frame_info->presentation_time;
}

static int64_t
get_max_update_duration_us (ClutterFrameClock *frame_clock)
{
  return MAX (frame_clock->longterm_max_update_duration_us,
              frame_clock->shortterm_max_update_duration_us);
}

void
clutter_frame_clock_notify_presented (ClutterFrameClock *frame_clock,
                                      ClutterFrameInfo  *frame_info)
{
  Frame *presented_frame;
#ifdef CLUTTER_ENABLE_DEBUG
  const char *debug_state =
    frame_clock->state == CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_TWO ?
    "Triple buffering" : "Double buffering";
#endif

  COGL_TRACE_BEGIN_SCOPED (ClutterFrameClockNotifyPresented,
                           "Clutter::FrameClock::presented()");
  COGL_TRACE_DESCRIBE (ClutterFrameClockNotifyPresented,
                       frame_clock->output_name);

  CLUTTER_NOTE (FRAME_CLOCK, "Frame %ld for %s presented",
                frame_info->view_frame_counter,
                frame_clock->output_name);

  g_return_if_fail (frame_clock->next_presentation);
  clear_frame (&frame_clock->prev_presentation);
  presented_frame = frame_clock->prev_presentation =
    g_steal_pointer (&frame_clock->next_presentation);
  frame_clock->next_presentation =
    g_steal_pointer (&frame_clock->next_next_presentation);

  presented_frame->target_presentation_time_us =
    frame_info->target_presentation_time;

  if (G_UNLIKELY (CLUTTER_HAS_DEBUG (FRAME_CLOCK)))
    {
      int64_t now_us;

      if (frame_info->presentation_time > 0 &&
          frame_info->target_presentation_time > 0 &&
          frame_info->presentation_time != frame_info->target_presentation_time)
        {
          int64_t diff_us;

          diff_us = llabs (frame_info->presentation_time -
                           frame_info->target_presentation_time);
          frame_clock->n_missed_frames +=
            (int) roundf ((float) diff_us /
                          (float) frame_clock->refresh_interval_us);
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

#ifdef HAVE_PROFILER
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
    {
      presented_frame->presentation_time_us = frame_info->presentation_time;
      presented_frame->presentation_flags = frame_info->flags;
    }

  presented_frame->got_measurements = FALSE;

  if ((frame_info->cpu_time_before_buffer_swap_us != 0 &&
       frame_info->has_valid_gpu_rendering_duration) ||
      frame_clock->ever_got_measurements)
    {
      int64_t dispatch_to_swap_us, swap_to_rendering_done_us, swap_to_flip_us;
      int64_t dispatch_time_us = presented_frame->dispatch_time_us;
      int64_t flip_time_us = presented_frame->flip_time_us;
      int64_t max_duration_us;

      if (frame_info->cpu_time_before_buffer_swap_us == 0)
        {
          /* User thread cursor-only updates with no "swap": we do know
           * the combined time from dispatch to flip at least.
           */
          dispatch_to_swap_us = 0;
          swap_to_flip_us = flip_time_us - dispatch_time_us;
        }
      else
        {
          dispatch_to_swap_us = frame_info->cpu_time_before_buffer_swap_us -
                                dispatch_time_us;
          swap_to_flip_us = flip_time_us -
                            frame_info->cpu_time_before_buffer_swap_us;
        }
      swap_to_rendering_done_us =
        frame_info->gpu_rendering_duration_ns / 1000;

      CLUTTER_NOTE (FRAME_TIMINGS,
                    "%s: update2dispatch %ld µs, dispatch2swap %ld µs, swap2render %ld µs, swap2flip %ld µs",
                    debug_state,
                    presented_frame->dispatch_lateness_us,
                    dispatch_to_swap_us,
                    swap_to_rendering_done_us,
                    swap_to_flip_us);

      max_duration_us = get_max_update_duration_us (frame_clock);

      frame_clock->shortterm_max_update_duration_us =
        CLAMP (presented_frame->dispatch_lateness_us + dispatch_to_swap_us +
               MAX (swap_to_rendering_done_us, swap_to_flip_us) +
               frame_clock->deadline_evasion_us,
               frame_clock->shortterm_max_update_duration_us,
               2 * frame_clock->refresh_interval_us);

      if (frame_clock->shortterm_max_update_duration_us > max_duration_us)
        {
          CLUTTER_NOTE (FRAME_TIMINGS,
                        "Maximum update duration estimate updated: %ldµs → %ldµs",
                        max_duration_us,
                        frame_clock->shortterm_max_update_duration_us);
        }

      maybe_update_longterm_max_duration_us (frame_clock, frame_info);

      presented_frame->got_measurements = TRUE;
      frame_clock->ever_got_measurements = TRUE;
    }
  else
    {
      CLUTTER_NOTE (FRAME_TIMINGS, "%s: update2dispatch %ld µs",
                    debug_state,
                    presented_frame->dispatch_lateness_us);
    }

  if (G_UNLIKELY (CLUTTER_HAS_DEBUG (FRAME_TIMINGS)) &&
      frame_info->target_presentation_time > 0 &&
      frame_info->presentation_time > 0)
    {
      int64_t diff_us;
      int n_missed_cycles;

      diff_us =
        frame_info->presentation_time - frame_info->target_presentation_time;
      n_missed_cycles = (int) roundf ((float) llabs (diff_us) /
                                      (float) frame_clock->refresh_interval_us);

      if (n_missed_cycles)
        {
          CLUTTER_NOTE (FRAME_TIMINGS,
                        "Frame presented %" G_GINT64_FORMAT "µs "
                        "(%d refresh cycle%s) %s",
                        (int64_t)llabs (diff_us), n_missed_cycles,
                        n_missed_cycles > 1 ? "s" : "",
                        diff_us > 0 ? "late" : "early");
        }
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
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED_NOW:
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED_LATER:
      g_warn_if_reached ();
      break;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE:
      clutter_frame_clock_set_state (frame_clock,
                                     CLUTTER_FRAME_CLOCK_STATE_IDLE);
      maybe_reschedule_update (frame_clock);
      break;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED:
      clutter_frame_clock_set_state (frame_clock,
                                     CLUTTER_FRAME_CLOCK_STATE_SCHEDULED);
      maybe_reschedule_update (frame_clock);
      break;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED_NOW:
      clutter_frame_clock_set_state (frame_clock,
                                     CLUTTER_FRAME_CLOCK_STATE_SCHEDULED_NOW);
      maybe_reschedule_update (frame_clock);
      break;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED_LATER:
      clutter_frame_clock_set_state (frame_clock,
                                     CLUTTER_FRAME_CLOCK_STATE_SCHEDULED_LATER);
      maybe_reschedule_update (frame_clock);
      break;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_TWO:
      clutter_frame_clock_set_state (frame_clock,
                                     CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE);
      maybe_reschedule_update (frame_clock);
      break;
    }
}

void
clutter_frame_clock_notify_ready (ClutterFrameClock *frame_clock)
{
  COGL_TRACE_BEGIN_SCOPED (ClutterFrameClockNotifyReady, "Clutter::FrameClock::ready()");
  COGL_TRACE_DESCRIBE (ClutterFrameClockNotifyReady, frame_clock->output_name);

  CLUTTER_NOTE (FRAME_CLOCK, "Frame for %s ready",
                frame_clock->output_name);

  if (frame_clock->next_next_presentation)
    clear_frame (&frame_clock->next_next_presentation);
  else
    clear_frame (&frame_clock->next_presentation);

  switch (frame_clock->state)
    {
    case CLUTTER_FRAME_CLOCK_STATE_INIT:
    case CLUTTER_FRAME_CLOCK_STATE_IDLE:
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED:
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED_NOW:
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED_LATER:
      g_warn_if_reached ();
      break;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE:
      clutter_frame_clock_set_state (frame_clock,
                                     CLUTTER_FRAME_CLOCK_STATE_IDLE);
      maybe_reschedule_update (frame_clock);
      break;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED:
      clutter_frame_clock_set_state (frame_clock,
                                     CLUTTER_FRAME_CLOCK_STATE_SCHEDULED);
      maybe_reschedule_update (frame_clock);
      break;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED_NOW:
      clutter_frame_clock_set_state (frame_clock,
                                     CLUTTER_FRAME_CLOCK_STATE_SCHEDULED_NOW);
      maybe_reschedule_update (frame_clock);
      break;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED_LATER:
      clutter_frame_clock_set_state (frame_clock,
                                     CLUTTER_FRAME_CLOCK_STATE_SCHEDULED_LATER);
      maybe_reschedule_update (frame_clock);
      break;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_TWO:
      clutter_frame_clock_set_state (frame_clock,
                                     CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE);
      maybe_reschedule_update (frame_clock);
      break;
    }
}

static gboolean
clutter_frame_clock_estimate_max_update_time_us (ClutterFrameClock *frame_clock,
                                                 int64_t           *max_update_time_estimate_us)
{
  int64_t maximum_us;

  if (G_UNLIKELY (clutter_paint_debug_flags &
                  CLUTTER_DEBUG_DISABLE_TRIPLE_BUFFERING))
    maximum_us = frame_clock->refresh_interval_us;
  else
    maximum_us = 2 * frame_clock->refresh_interval_us;

  if (!frame_clock->ever_got_measurements ||
      G_UNLIKELY (clutter_paint_debug_flags &
                  CLUTTER_DEBUG_DISABLE_DYNAMIC_MAX_RENDER_TIME))
    return FALSE;

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
  *max_update_time_estimate_us =
    get_max_update_duration_us (frame_clock) +
    frame_clock->vblank_duration_us +
    clutter_max_render_time_constant_us;

  *max_update_time_estimate_us = CLAMP (*max_update_time_estimate_us, 0,
                                        maximum_us);

  return TRUE;
}

static void
calculate_next_update_time_us (ClutterFrameClock *frame_clock,
                               int64_t           *out_next_update_time_us,
                               int64_t           *out_next_presentation_time_us,
                               int64_t           *out_next_frame_deadline_us)
{
  const Frame *last_presentation = frame_clock->prev_presentation;
  int64_t last_presentation_time_us;
  int64_t now_us;
  int64_t refresh_interval_us;
  int64_t min_render_time_allowed_us;
  int64_t max_update_time_estimate_us;
  int64_t next_presentation_time_us;
  int64_t next_smooth_presentation_time_us = 0;
  int64_t next_update_time_us;
  gboolean have_max_update_time_estimate;

  now_us = g_get_monotonic_time ();

  refresh_interval_us = frame_clock->refresh_interval_us;

  have_max_update_time_estimate =
    clutter_frame_clock_estimate_max_update_time_us (frame_clock,
                                                     &max_update_time_estimate_us);

  if (!last_presentation ||
      !have_max_update_time_estimate ||
      last_presentation->presentation_time_us == 0)
    {
      const Frame *last_dispatch = frame_clock->prev_dispatch;

      *out_next_update_time_us =
        last_dispatch && last_dispatch->dispatch_time_us ?
        ((last_dispatch->dispatch_time_us -
          last_dispatch->dispatch_lateness_us) + refresh_interval_us) :
        now_us;

      *out_next_presentation_time_us = 0;
      *out_next_frame_deadline_us = 0;
      return;
    }

  min_render_time_allowed_us = refresh_interval_us / 2;

  if (min_render_time_allowed_us > max_update_time_estimate_us)
    min_render_time_allowed_us = max_update_time_estimate_us;

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
  last_presentation_time_us = last_presentation->presentation_time_us;
  switch (frame_clock->state)
    {
    case CLUTTER_FRAME_CLOCK_STATE_INIT:
    case CLUTTER_FRAME_CLOCK_STATE_IDLE:
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED:
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED_NOW:
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED_LATER:
      next_smooth_presentation_time_us = last_presentation_time_us +
                                         refresh_interval_us;
      break;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE:
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED:
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED_NOW:
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED_LATER:
      next_smooth_presentation_time_us = last_presentation_time_us +
                                         2 * refresh_interval_us;
      break;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_TWO:
      g_warn_if_reached ();  /* quad buffering would be a bug */
      next_smooth_presentation_time_us = last_presentation_time_us +
                                         3 * refresh_interval_us;
      break;
    }

  /*
   * The last presentation could have happened more than a frame ago.
   * For example, due to idling (nothing on screen changed, so no need to
   * redraw) or due to frames missing deadlines (GPU busy with heavy rendering).
   * The following code adjusts next_presentation_time_us to be in the future,
   * but still aligned to display presentation times. Instead of
   * next presentation = last presentation + 1/2/3 * refresh interval, it will be
   * next presentation = last presentation + N * refresh interval.
   */
  next_presentation_time_us =
    mtk_extrapolate_next_interval_boundary (next_smooth_presentation_time_us,
                                            refresh_interval_us);

  if (last_presentation->target_presentation_time_us > 0)
    {
      int64_t time_since_last_target_presentation_time_us;

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
       *             time_since_last_target_presentation_time_us
       *
       */
      time_since_last_target_presentation_time_us =
        next_presentation_time_us - last_presentation->target_presentation_time_us;
      if (time_since_last_target_presentation_time_us > 0 &&
          time_since_last_target_presentation_time_us < (refresh_interval_us / 2))
        {
          next_presentation_time_us =
            frame_clock->next_presentation_time_us + refresh_interval_us;
        }
    }

  if (last_presentation->presentation_flags & CLUTTER_FRAME_INFO_FLAG_VSYNC &&
      next_presentation_time_us != next_smooth_presentation_time_us)
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
      while (next_presentation_time_us - min_render_time_allowed_us < now_us)
        next_presentation_time_us += refresh_interval_us;

      next_update_time_us = next_presentation_time_us - max_update_time_estimate_us;
      if (next_update_time_us < now_us)
        next_update_time_us = now_us;
    }

  *out_next_update_time_us = next_update_time_us;
  *out_next_presentation_time_us = next_presentation_time_us;
  *out_next_frame_deadline_us = next_presentation_time_us - frame_clock->vblank_duration_us;
}

static void
calculate_next_variable_update_time_us (ClutterFrameClock *frame_clock,
                                        int64_t           *out_next_update_time_us,
                                        int64_t           *out_next_presentation_time_us,
                                        int64_t           *out_next_frame_deadline_us)
{
  const Frame *last_presentation = frame_clock->prev_presentation;
  int64_t last_presentation_time_us;
  int64_t now_us;
  int64_t refresh_interval_us;
  int64_t max_update_time_estimate_us;
  int64_t next_presentation_time_us;
  int64_t next_update_time_us;
  int64_t next_frame_deadline_us;

  now_us = g_get_monotonic_time ();

  refresh_interval_us = frame_clock->refresh_interval_us;

  if (!last_presentation ||
      last_presentation->presentation_time_us == 0 ||
      !clutter_frame_clock_estimate_max_update_time_us (frame_clock,
                                                        &max_update_time_estimate_us))
    {
      const Frame *last_dispatch = frame_clock->prev_dispatch;

      *out_next_update_time_us =
        last_dispatch && last_dispatch->dispatch_time_us ?
        ((last_dispatch->dispatch_time_us -
          last_dispatch->dispatch_lateness_us) + refresh_interval_us) :
        now_us;

      *out_next_presentation_time_us = 0;
      *out_next_frame_deadline_us = 0;
      return;
    }

  last_presentation_time_us = last_presentation->presentation_time_us;
  next_presentation_time_us = last_presentation_time_us + refresh_interval_us;

  next_update_time_us = next_presentation_time_us - max_update_time_estimate_us;
  if (next_update_time_us < now_us)
    next_update_time_us = now_us;

  if (next_presentation_time_us < next_update_time_us)
    next_presentation_time_us = 0;

  next_frame_deadline_us = next_update_time_us;
  if (next_frame_deadline_us == now_us)
    next_frame_deadline_us += refresh_interval_us;

  *out_next_update_time_us = next_update_time_us;
  *out_next_presentation_time_us = next_presentation_time_us;
  *out_next_frame_deadline_us = next_frame_deadline_us;
}

static void
calculate_next_variable_update_timeout_us (ClutterFrameClock *frame_clock,
                                           int64_t           *out_next_update_time_us)
{
  const Frame *last_presentation = frame_clock->prev_presentation;
  int64_t now_us;
  int64_t next_presentation_time_us;
  int64_t timeout_interval_us;

  now_us = g_get_monotonic_time ();

  if (now_us - frame_clock->frame_sync_update_time_us >=
      frame_clock->maximum_refresh_interval_us)
    timeout_interval_us = frame_clock->refresh_interval_us;
  else
    timeout_interval_us = frame_clock->maximum_refresh_interval_us;

  if (!last_presentation || last_presentation->presentation_time_us == 0)
    {
      const Frame *last_dispatch = frame_clock->prev_dispatch;

      *out_next_update_time_us =
        last_dispatch && last_dispatch->dispatch_time_us ?
        ((last_dispatch->dispatch_time_us -
          last_dispatch->dispatch_lateness_us) + timeout_interval_us) :
        now_us;
      return;
    }

  next_presentation_time_us = last_presentation->presentation_time_us +
                              timeout_interval_us;

  while (next_presentation_time_us < now_us)
    next_presentation_time_us += timeout_interval_us;

  *out_next_update_time_us = next_presentation_time_us;
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
        case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED_LATER:
          frame_clock->pending_reschedule = TRUE;
          clutter_frame_clock_set_state (frame_clock,
                                         CLUTTER_FRAME_CLOCK_STATE_IDLE);
          break;
        case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED_NOW:
          frame_clock->pending_reschedule = TRUE;
          frame_clock->pending_reschedule_now = TRUE;
          clutter_frame_clock_set_state (frame_clock,
                                         CLUTTER_FRAME_CLOCK_STATE_IDLE);
          break;
        case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED:
          frame_clock->pending_reschedule = TRUE;
          clutter_frame_clock_set_state (frame_clock,
                                         CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE);
          break;
        case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED_NOW:
          frame_clock->pending_reschedule = TRUE;
          frame_clock->pending_reschedule_now = TRUE;
          clutter_frame_clock_set_state (frame_clock,
                                         CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE);
          break;
        case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED_LATER:
          frame_clock->pending_reschedule = TRUE;
          clutter_frame_clock_set_state (frame_clock,
                                         CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE);
          break;
        case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE:
        case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_TWO:
          break;
        }

      if (frame_clock->source)
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

static gboolean
want_triple_buffering (ClutterFrameClock *frame_clock)
{
  int64_t max_update_time_estimate_us;

  if (G_UNLIKELY (clutter_paint_debug_flags &
                  CLUTTER_DEBUG_DISABLE_TRIPLE_BUFFERING))
    return FALSE;

  switch (frame_clock->mode)
    {
    case CLUTTER_FRAME_CLOCK_MODE_FIXED:
    case CLUTTER_FRAME_CLOCK_MODE_VARIABLE:
      break;
    case CLUTTER_FRAME_CLOCK_MODE_PASSIVE:
      return FALSE;
    };

  if (clutter_frame_clock_estimate_max_update_time_us (frame_clock,
                                                       &max_update_time_estimate_us) &&
      max_update_time_estimate_us < frame_clock->refresh_interval_us)
    return FALSE;

  return TRUE;
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
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED_LATER:
      clutter_frame_clock_set_state (frame_clock,
                                     CLUTTER_FRAME_CLOCK_STATE_SCHEDULED_NOW);
      break;
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED_NOW:
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED_NOW:
      return;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED:
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED_LATER:
      clutter_frame_clock_set_state (frame_clock,
                                     CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED_NOW);
      break;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE:
      if (want_triple_buffering (frame_clock))
        {
          clutter_frame_clock_set_state (frame_clock,
                                         CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED_NOW);
          break;
        }
      G_GNUC_FALLTHROUGH;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_TWO:
      frame_clock->pending_reschedule = TRUE;
      frame_clock->pending_reschedule_now = TRUE;
      return;
    }

  switch (frame_clock->mode)
    {
    case CLUTTER_FRAME_CLOCK_MODE_FIXED:
      next_update_time_us = g_get_monotonic_time ();
      frame_clock->is_next_presentation_time_valid = FALSE;
      frame_clock->has_next_frame_deadline = FALSE;
      break;
    case CLUTTER_FRAME_CLOCK_MODE_VARIABLE:
      calculate_next_variable_update_time_us (frame_clock,
                                              &next_update_time_us,
                                              &frame_clock->next_presentation_time_us,
                                              &frame_clock->next_frame_deadline_us);
      frame_clock->is_next_presentation_time_valid =
        (frame_clock->next_presentation_time_us != 0);
      frame_clock->has_next_frame_deadline =
        (frame_clock->next_frame_deadline_us != 0);
      break;
    case CLUTTER_FRAME_CLOCK_MODE_PASSIVE:
      clutter_frame_clock_driver_schedule_update (frame_clock->driver);
      break;
    }

  if (next_update_time_us != -1)
    {
      frame_clock->next_update_time_us = next_update_time_us;
      g_source_set_ready_time (frame_clock->source, next_update_time_us);
    }
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

  switch (frame_clock->mode)
    {
    case CLUTTER_FRAME_CLOCK_MODE_FIXED:
    case CLUTTER_FRAME_CLOCK_MODE_VARIABLE:
      break;
    case CLUTTER_FRAME_CLOCK_MODE_PASSIVE:
      clutter_frame_clock_driver_schedule_update (frame_clock->driver);
      return;
    };

  switch (frame_clock->state)
    {
    case CLUTTER_FRAME_CLOCK_STATE_INIT:
      next_update_time_us = g_get_monotonic_time ();
      g_source_set_ready_time (frame_clock->source, next_update_time_us);
      clutter_frame_clock_set_state (frame_clock,
                                     CLUTTER_FRAME_CLOCK_STATE_SCHEDULED);
      return;
    case CLUTTER_FRAME_CLOCK_STATE_IDLE:
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED_LATER:
      clutter_frame_clock_set_state (frame_clock,
                                     CLUTTER_FRAME_CLOCK_STATE_SCHEDULED);
      break;
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED:
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED_NOW:
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED:
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED_NOW:
      return;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE:
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED_LATER:
      if (want_triple_buffering (frame_clock))
        {
          clutter_frame_clock_set_state (frame_clock,
                                         CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED);
          break;
        }
      G_GNUC_FALLTHROUGH;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_TWO:
      frame_clock->pending_reschedule = TRUE;
      return;
    }

  switch (frame_clock->mode)
    {
    case CLUTTER_FRAME_CLOCK_MODE_FIXED:
      calculate_next_update_time_us (frame_clock,
                                     &next_update_time_us,
                                     &frame_clock->next_presentation_time_us,
                                     &frame_clock->next_frame_deadline_us);
      frame_clock->is_next_presentation_time_valid =
        (frame_clock->next_presentation_time_us != 0);
      frame_clock->has_next_frame_deadline =
        (frame_clock->next_frame_deadline_us != 0);
      break;
    case CLUTTER_FRAME_CLOCK_MODE_VARIABLE:
      calculate_next_variable_update_timeout_us (frame_clock,
                                                 &next_update_time_us);
      frame_clock->is_next_presentation_time_valid = FALSE;
      frame_clock->has_next_frame_deadline = FALSE;
      break;
    case CLUTTER_FRAME_CLOCK_MODE_PASSIVE:
      g_assert_not_reached ();
      break;
    }

  g_warn_if_fail (next_update_time_us != -1);

  frame_clock->next_update_time_us = next_update_time_us;
  g_source_set_ready_time (frame_clock->source, next_update_time_us);
}

static void
clutter_frame_clock_schedule_update_later (ClutterFrameClock *frame_clock,
                                           int64_t            target_us)
{
  int64_t next_update_time_us = -1;
  int64_t next_presentation_time_us = 0;
  int64_t next_frame_deadline_us;
  int64_t ready_time_us = 0, extrapolated_presentation_time_us;
  int64_t max_update_time_estimate_us;
  int64_t cycles;
  ClutterFrameClockState next_state = frame_clock->state;

  if (frame_clock->inhibit_count > 0)
    {
      frame_clock->pending_reschedule = TRUE;
      return;
    }

  switch (frame_clock->mode)
    {
    case CLUTTER_FRAME_CLOCK_MODE_FIXED:
    case CLUTTER_FRAME_CLOCK_MODE_VARIABLE:
      break;
    case CLUTTER_FRAME_CLOCK_MODE_PASSIVE:
      clutter_frame_clock_driver_schedule_update (frame_clock->driver);
      return;
    };

  switch (frame_clock->state)
    {
    case CLUTTER_FRAME_CLOCK_STATE_INIT:
    case CLUTTER_FRAME_CLOCK_STATE_IDLE:
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED_LATER:
      next_state = CLUTTER_FRAME_CLOCK_STATE_SCHEDULED_LATER;
      break;
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED_NOW:
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED:
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED:
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED_NOW:
      return;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED_LATER:
      break;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE:
      if (want_triple_buffering (frame_clock))
        {
          next_state =
            CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED_LATER;
          break;
        }
      G_GNUC_FALLTHROUGH;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_TWO:
      frame_clock->pending_reschedule = TRUE;
      frame_clock->pending_reschedule_now = TRUE;
      return;
    }

  switch (frame_clock->mode)
    {
    case CLUTTER_FRAME_CLOCK_MODE_FIXED:
      calculate_next_update_time_us (frame_clock,
                                     &next_update_time_us,
                                     &next_presentation_time_us,
                                     &next_frame_deadline_us);
      break;
    case CLUTTER_FRAME_CLOCK_MODE_VARIABLE:
      calculate_next_variable_update_time_us (frame_clock,
                                              &next_update_time_us,
                                              &next_presentation_time_us,
                                              &next_frame_deadline_us);
      break;
    case CLUTTER_FRAME_CLOCK_MODE_PASSIVE:
      g_assert_not_reached ();
      break;
    }

  g_warn_if_fail (next_presentation_time_us != -1);

  if (next_presentation_time_us >= target_us)
    {
      clutter_frame_clock_schedule_update (frame_clock);
      return;
    }

  switch (frame_clock->mode)
    {
    case CLUTTER_FRAME_CLOCK_MODE_FIXED:
      cycles =
        (target_us - next_presentation_time_us +
         frame_clock->refresh_interval_us - 1) /
        frame_clock->refresh_interval_us;
      extrapolated_presentation_time_us =
        next_presentation_time_us + frame_clock->refresh_interval_us * cycles;
      max_update_time_estimate_us = next_presentation_time_us - next_update_time_us;
      ready_time_us = extrapolated_presentation_time_us - max_update_time_estimate_us;
      break;
    case CLUTTER_FRAME_CLOCK_MODE_VARIABLE:
      if (!clutter_frame_clock_estimate_max_update_time_us (frame_clock,
                                                            &max_update_time_estimate_us))
        {
          max_update_time_estimate_us = (int64_t) (frame_clock->refresh_interval_us *
                                                   SYNC_DELAY_FALLBACK_FRACTION);
        }
      ready_time_us = target_us - max_update_time_estimate_us;
      break;
    case CLUTTER_FRAME_CLOCK_MODE_PASSIVE:
      g_assert_not_reached ();
      break;
    }

  g_source_set_ready_time (frame_clock->source, ready_time_us);
  frame_clock->pending_reschedule = TRUE;
  clutter_frame_clock_set_state (frame_clock, next_state);
}

void
clutter_frame_clock_set_frame_sync_update_time (ClutterFrameClock *frame_clock,
                                                int64_t            update_time_us)
{
  frame_clock->frame_sync_update_time_us = update_time_us;
}

static int
compare_times (const DeferredTime *a,
               const DeferredTime *b,
               void               *data)
{
  if (a->target_time_us > b->target_time_us)
    return 1;

  if (a->target_time_us < b->target_time_us)
    return -1;

  return 0;
}

void
clutter_frame_clock_add_future_time (ClutterFrameClock *frame_clock,
                                     int64_t            when_us)
{
  DeferredTime *time = g_new (DeferredTime, 1);
  time->target_time_us = when_us;
  g_queue_insert_sorted (frame_clock->deferred_times, time,
                         (GCompareDataFunc)compare_times, NULL);

  maybe_reschedule_update (frame_clock);
}

void
clutter_frame_clock_set_mode (ClutterFrameClock     *frame_clock,
                              ClutterFrameClockMode  mode)
{
  if (frame_clock->mode == mode)
    return;

  g_assert (frame_clock->mode != CLUTTER_FRAME_CLOCK_MODE_PASSIVE);

  frame_clock->mode = mode;

  switch (frame_clock->state)
    {
    case CLUTTER_FRAME_CLOCK_STATE_INIT:
    case CLUTTER_FRAME_CLOCK_STATE_IDLE:
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE:
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_TWO:
      break;
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED:
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED_LATER:
      frame_clock->pending_reschedule = TRUE;
      clutter_frame_clock_set_state (frame_clock,
                                     CLUTTER_FRAME_CLOCK_STATE_IDLE);
      break;
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED_NOW:
      frame_clock->pending_reschedule = TRUE;
      frame_clock->pending_reschedule_now = TRUE;
      clutter_frame_clock_set_state (frame_clock,
                                     CLUTTER_FRAME_CLOCK_STATE_IDLE);
      break;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED:
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED_LATER:
      frame_clock->pending_reschedule = TRUE;
      clutter_frame_clock_set_state (frame_clock,
                                     CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE);
      break;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED_NOW:
      frame_clock->pending_reschedule = TRUE;
      frame_clock->pending_reschedule_now = TRUE;
      clutter_frame_clock_set_state (frame_clock,
                                     CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE);
      break;
    }

  maybe_reschedule_update (frame_clock);
}

ClutterFrameResult
clutter_frame_clock_dispatch (ClutterFrameClock *frame_clock,
                              int64_t            time_us)
{
  const ClutterFrameListenerIface *iface = frame_clock->listener.iface;
  g_autoptr (ClutterFrame) frame = NULL;
  int64_t frame_count;
  ClutterFrameResult result;
  int64_t ideal_dispatch_time_us, lateness_us;
  Frame *this_dispatch;
  int64_t prev_dispatch_time_us = 0;
#ifdef CLUTTER_ENABLE_DEBUG
  int64_t prev_dispatch_interval_us = 0;
#endif
  int64_t prev_dispatch_lateness_us = 0;

#ifdef HAVE_PROFILER
  int64_t this_dispatch_ready_time_us;
  int64_t this_dispatch_time_us;

  COGL_TRACE_BEGIN_SCOPED (ClutterFrameClockDispatch, "Clutter::FrameClock::dispatch()");
  COGL_TRACE_DESCRIBE (ClutterFrameClockDispatch, frame_clock->output_name);

  if (frame_clock->source)
    this_dispatch_ready_time_us = g_source_get_ready_time (frame_clock->source);
  else
    this_dispatch_ready_time_us = time_us;
  this_dispatch_time_us = time_us;
#endif

  switch (frame_clock->state)
    {
    case CLUTTER_FRAME_CLOCK_STATE_INIT:
    case CLUTTER_FRAME_CLOCK_STATE_IDLE:
      g_warn_if_fail (frame_clock->mode == CLUTTER_FRAME_CLOCK_MODE_PASSIVE);
      frame_clock->state = CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE;
      break;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE:
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_TWO:
      g_warning ("Frame clock dispatched in an unscheduled state %d",
                 frame_clock->state);
      return CLUTTER_FRAME_RESULT_PENDING_PRESENTED;
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED:
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED_NOW:
    case CLUTTER_FRAME_CLOCK_STATE_SCHEDULED_LATER:
      clutter_frame_clock_set_state (frame_clock,
                                     CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE);
      break;
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED:
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED_NOW:
    case CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_ONE_AND_SCHEDULED_LATER:
      clutter_frame_clock_set_state (frame_clock,
                                     CLUTTER_FRAME_CLOCK_STATE_DISPATCHED_TWO);
      break;
    }

  /* Discarding the old prev_dispatch early here allows us to keep the
   * frame_pool size equal to nbuffers instead of nbuffers+1.
   */
  if (frame_clock->prev_dispatch)
    {
      prev_dispatch_time_us = frame_clock->prev_dispatch->dispatch_time_us;
#ifdef CLUTTER_ENABLE_DEBUG
      prev_dispatch_interval_us = frame_clock->prev_dispatch->dispatch_interval_us;
#endif
      prev_dispatch_lateness_us = frame_clock->prev_dispatch->dispatch_lateness_us;
    }

  clear_frame (&frame_clock->prev_dispatch);
  this_dispatch = frame_clock->prev_dispatch =
    clutter_frame_clock_new_frame (frame_clock);

  if (frame_clock->next_presentation == NULL)
    {
      frame_clock->next_presentation = ref_frame (this_dispatch);
    }
  else
    {
      g_warn_if_fail (frame_clock->next_next_presentation == NULL);
      frame_clock->next_next_presentation =
        ref_frame (this_dispatch);
    }

  ideal_dispatch_time_us = frame_clock->next_update_time_us;

  if (ideal_dispatch_time_us <= 0)
    ideal_dispatch_time_us = (prev_dispatch_time_us -
                              prev_dispatch_lateness_us) +
                             frame_clock->refresh_interval_us;

  lateness_us = time_us - ideal_dispatch_time_us;
  if (lateness_us < 0 || lateness_us >= frame_clock->refresh_interval_us / 4)
    this_dispatch->dispatch_lateness_us = 0;
  else
    this_dispatch->dispatch_lateness_us = lateness_us;

#ifdef CLUTTER_ENABLE_DEBUG
  if (G_UNLIKELY (CLUTTER_HAS_DEBUG (FRAME_CLOCK)))
    {
      int64_t dispatch_interval_us, jitter_us;

      dispatch_interval_us = time_us - prev_dispatch_time_us;
      jitter_us = llabs (dispatch_interval_us -
                         prev_dispatch_interval_us) %
                  frame_clock->refresh_interval_us;
      this_dispatch->dispatch_interval_us = dispatch_interval_us;
      CLUTTER_NOTE (FRAME_CLOCK, "dispatch jitter %5ldµs (%3ld%%)",
                    jitter_us,
                    jitter_us * 100 / frame_clock->refresh_interval_us);
    }
#endif

  this_dispatch->dispatch_time_us = time_us;

  if (frame_clock->source)
    g_source_set_ready_time (frame_clock->source, -1);

  frame_count = frame_clock->frame_count++;

  if (iface->new_frame)
    frame = iface->new_frame (frame_clock, frame_clock->listener.user_data);
  if (!frame)
    frame = clutter_frame_new (ClutterFrame, NULL);

  frame->frame_count = frame_count;
  frame->has_target_presentation_time = frame_clock->is_next_presentation_time_valid;
  frame->target_presentation_time_us = frame_clock->next_presentation_time_us;

  frame->has_frame_deadline = frame_clock->has_next_frame_deadline;
  frame->frame_deadline_us = frame_clock->next_frame_deadline_us;

  CLUTTER_NOTE (FRAME_CLOCK, "Dispatching frame %ld for %s",
                frame->frame_count,
                frame_clock->output_name);

  COGL_TRACE_BEGIN_SCOPED (ClutterFrameClockEvents, "Clutter::FrameListener::before_frame()");
  if (iface->before_frame)
    iface->before_frame (frame_clock, frame, frame_clock->listener.user_data);
  COGL_TRACE_END (ClutterFrameClockEvents);

  COGL_TRACE_BEGIN_SCOPED (ClutterFrameClockTimelines, "Clutter::FrameClock::advance_timelines()");
  if (frame_clock->is_next_presentation_time_valid)
    time_us = frame_clock->next_presentation_time_us;
  advance_timelines (frame_clock, time_us);
  COGL_TRACE_END (ClutterFrameClockTimelines);

  COGL_TRACE_BEGIN_SCOPED (ClutterFrameClockFrame, "Clutter::FrameListener::frame()");
  result = iface->frame (frame_clock, frame, frame_clock->listener.user_data);
  COGL_TRACE_END (ClutterFrameClockFrame);

  switch (result)
    {
    case CLUTTER_FRAME_RESULT_PENDING_PRESENTED:
      break;
    case CLUTTER_FRAME_RESULT_IDLE:
      /* The frame was aborted; nothing to paint/present */
      clutter_frame_clock_notify_ready (frame_clock);
      break;
    case CLUTTER_FRAME_RESULT_IGNORED:
      frame_clock->state = CLUTTER_FRAME_CLOCK_STATE_IDLE;
      clear_frame (&frame_clock->next_presentation);
      maybe_reschedule_update (frame_clock);
      break;
    }

#ifdef HAVE_PROFILER
  if (this_dispatch_ready_time_us != -1 &&
      G_UNLIKELY (cogl_is_tracing_enabled ()))
    {
      g_autofree char *description = NULL;
      description = g_strdup_printf ("dispatched %ld µs late",
                                     this_dispatch_time_us - this_dispatch_ready_time_us);
      COGL_TRACE_DESCRIBE (ClutterFrameClockDispatch, description);
    }
#endif

  return result;
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
  Frame *new_frame = frame_clock->prev_dispatch;

  new_frame->flip_time_us = flip_time_us;
}

GString *
clutter_frame_clock_get_max_render_time_debug_info (ClutterFrameClock *frame_clock)
{
  const Frame *last_presentation = frame_clock->prev_presentation;
  int64_t max_update_time_estimate_us;
  GString *string;

  string = g_string_new ("Max update time estimate: ");
  if (!clutter_frame_clock_estimate_max_update_time_us (frame_clock,
                                                        &max_update_time_estimate_us))
    {
      g_string_append (string, "unknown");
      return string;
    }

  g_string_append_printf (string, "%ld µs", max_update_time_estimate_us);

  if (last_presentation && last_presentation->got_measurements)
    g_string_append_printf (string, " =");
  else
    g_string_append_printf (string, " (no measurements last frame)");

  g_string_append_printf (string, "\nVblank duration: %ld µs +",
                          frame_clock->vblank_duration_us);
  g_string_append_printf (string, "\nUpdate duration: %ld µs +",
                          get_max_update_duration_us (frame_clock));
  g_string_append_printf (string, "\nConstant: %d µs",
                          clutter_max_render_time_constant_us);

  return string;
}

static gboolean
frame_clock_source_prepare (GSource *source,
                            int     *timeout)
{
  G_GNUC_UNUSED ClutterClockSource *clock_source = (ClutterClockSource *)source;

  *timeout = -1;

#ifdef HAVE_TIMERFD
  /* The cycle for GMainContext is:
   *
   *   - prepare():  where we update our timerfd deadline
   *   - poll():     internal to GMainContext/GPollFunc
   *   - check():    where GLib will check POLLIN and make ready
   *   - dispatch(): where we actually process the pending work
   *
   * If we have a ready_time >= 0 then we need to set our deadline
   * in nanoseconds for the timerfd. The timerfd will receive POLLIN
   * after that point and poll() will return.
   *
   * If we have a ready_time of -1, then we need to disable our
   * timerfd by setting tv_sec and tv_nsec to 0.
   *
   * In both cases, the POLLIN bit will be reset.
   */
  if (clock_source->tfd > -1)
    {
      int64_t ready_time = g_source_get_ready_time (source);
      struct itimerspec tfd_spec;

      tfd_spec.it_interval.tv_sec = 0;
      tfd_spec.it_interval.tv_nsec = 0;

      if (ready_time > -1)
        {
          tfd_spec.it_value.tv_sec = ready_time / G_USEC_PER_SEC;
          tfd_spec.it_value.tv_nsec = (ready_time % G_USEC_PER_SEC) * 1000L;
        }
      else
        {
          tfd_spec.it_value.tv_sec = 0;
          tfd_spec.it_value.tv_nsec = 0;
        }

      /* Avoid extraneous calls timerfd_settime() */
      if (memcmp (&tfd_spec, &clock_source->tfd_spec, sizeof tfd_spec) != 0)
        {
          clock_source->tfd_spec = tfd_spec;

          timerfd_settime (clock_source->tfd,
                           TFD_TIMER_ABSTIME,
                           &clock_source->tfd_spec,
                           NULL);
        }
    }
#endif

  return FALSE;
}

static void
frame_clock_source_finalize (GSource *source)
{
#ifdef HAVE_TIMERFD
  ClutterClockSource *clock_source = (ClutterClockSource *)source;

  g_clear_fd (&clock_source->tfd, NULL);
#endif
}

static GSourceFuncs frame_clock_source_funcs = {
  frame_clock_source_prepare,
  NULL,
  frame_clock_source_dispatch,
  frame_clock_source_finalize,
};

static void
init_frame_clock_source (ClutterFrameClock *frame_clock)
{
  GSource *source;
  ClutterClockSource *clock_source;
  g_autofree char *name = NULL;

  source = g_source_new (&frame_clock_source_funcs, sizeof (ClutterClockSource));
  clock_source = (ClutterClockSource *) source;

#ifdef HAVE_TIMERFD
  clock_source->tfd = timerfd_create (CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);

  if (clock_source->tfd > -1)
    g_source_add_unix_fd (source, clock_source->tfd, G_IO_IN);
#endif

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
                         const char                      *output_name,
                         const ClutterFrameListenerIface *iface,
                         gpointer                         user_data)
{
  ClutterFrameClock *frame_clock;

  g_assert (refresh_rate >= 0.0f);

  frame_clock = g_object_new (CLUTTER_TYPE_FRAME_CLOCK, NULL);

  frame_clock->listener.iface = iface;
  frame_clock->listener.user_data = user_data;

  init_frame_clock_source (frame_clock);

  clutter_frame_clock_set_refresh_rate (frame_clock, refresh_rate);

  frame_clock->maximum_refresh_interval_us =
    (int64_t) (0.5 + G_USEC_PER_SEC / MINIMUM_REFRESH_RATE);

  frame_clock->vblank_duration_us = vblank_duration_us;

  frame_clock->output_name = g_strdup (output_name);

  frame_clock->deferred_times = g_queue_new ();

  return frame_clock;
}

void
clutter_frame_clock_destroy (ClutterFrameClock *frame_clock)
{
  g_object_run_dispose (G_OBJECT (frame_clock));
  g_object_unref (frame_clock);
}

int
clutter_frame_clock_get_priority (ClutterFrameClock *frame_clock)
{
  return (int) roundf (frame_clock->refresh_rate * 1000.0f);
}

static void
clear_source (ClutterFrameClock *frame_clock)
{
  if (frame_clock->source)
    {
      g_source_destroy (frame_clock->source);
      g_clear_pointer (&frame_clock->source, g_source_unref);
    }
}

static void
clutter_frame_clock_dispose (GObject *object)
{
  ClutterFrameClock *frame_clock = CLUTTER_FRAME_CLOCK (object);

  if (!frame_clock->destroy_emitted)
    {
      g_signal_emit (frame_clock, signals[DESTROY], 0);
      frame_clock->destroy_emitted = TRUE;
    }

  clear_source (frame_clock);

  g_clear_pointer (&frame_clock->output_name, g_free);

  if (frame_clock->deferred_times)
    g_queue_free_full (g_steal_pointer (&frame_clock->deferred_times), g_free);
  frame_clock->deferred_times = NULL;

  g_clear_object (&frame_clock->driver);

  G_OBJECT_CLASS (clutter_frame_clock_parent_class)->dispose (object);
}

static void
clutter_frame_clock_init (ClutterFrameClock *frame_clock)
{
  frame_clock->state = CLUTTER_FRAME_CLOCK_STATE_INIT;
  frame_clock->mode = CLUTTER_FRAME_CLOCK_MODE_FIXED;
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

void
clutter_frame_clock_set_deadline_evasion (ClutterFrameClock *frame_clock,
                                          int64_t            deadline_evasion_us)
{
  frame_clock->deadline_evasion_us = deadline_evasion_us;
}

void
clutter_frame_clock_set_passive (ClutterFrameClock       *frame_clock,
                                 ClutterFrameClockDriver *driver)
{
  CLUTTER_NOTE (FRAME_CLOCK, "Making frame clock for %s passive",
                frame_clock->output_name);
  frame_clock->mode = CLUTTER_FRAME_CLOCK_MODE_PASSIVE;
  frame_clock->is_next_presentation_time_valid = FALSE;
  if (frame_clock->prev_presentation)
    frame_clock->prev_presentation->target_presentation_time_us = 0;
  frame_clock->has_next_frame_deadline = FALSE;

  g_set_object (&frame_clock->driver, driver);

  clear_source (frame_clock);
}
