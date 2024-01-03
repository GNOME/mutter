/*
 * Copyright (C) 2022 Red Hat Inc
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

#include "config.h"

#include "compositor/compositor-private.h"
#include "core/window-private.h"
#include "mtk/mtk-x11.h"
#include "x11/meta-sync-counter.h"
#include "x11/meta-x11-display-private.h"
#include "x11/window-x11-private.h"

/* Each time the application updates the sync request counter to a new even value
 * value, we queue a frame into the windows list of frames. Once we're painting
 * an update "in response" to the window, we fill in frame_counter with the
 * Cogl counter for that frame, and send _NET_WM_FRAME_DRAWN at the end of the
 * frame. _NET_WM_FRAME_TIMINGS is sent when we get a frame_complete callback.
 *
 * As an exception, if a window is completely obscured, we try to throttle drawning
 * to a slower frame rate. In this case, frame_counter stays -1 until
 * send_frame_message_timeout() runs, at which point we send both the
 * _NET_WM_FRAME_DRAWN and _NET_WM_FRAME_TIMINGS messages.
 */
typedef struct
{
  uint64_t sync_request_serial;
  int64_t frame_counter;
  int64_t frame_drawn_time;
} FrameData;

void
meta_sync_counter_init (MetaSyncCounter *sync_counter,
                        MetaWindow      *window,
                        Window           xwindow)
{
  sync_counter->window = window;
  sync_counter->xwindow = xwindow;
}

void
meta_sync_counter_clear (MetaSyncCounter *sync_counter)
{
  g_clear_handle_id (&sync_counter->sync_request_timeout_id, g_source_remove);
  meta_sync_counter_destroy_sync_alarm (sync_counter);
  g_clear_list (&sync_counter->frames, g_free);
  sync_counter->window = NULL;
  sync_counter->xwindow = None;
}

void
meta_sync_counter_set_counter (MetaSyncCounter *sync_counter,
                               XSyncCounter     counter,
                               gboolean         extended)
{
  meta_sync_counter_destroy_sync_alarm (sync_counter);
  sync_counter->sync_request_counter = None;

  sync_counter->sync_request_counter = counter;
  sync_counter->extended_sync_request_counter = extended;

  if (sync_counter->sync_request_counter != None)
    {
      meta_verbose ("Window has _NET_WM_SYNC_REQUEST_COUNTER 0x%lx (extended=%s)",
                    sync_counter->sync_request_counter,
                    sync_counter->extended_sync_request_counter ? "true" : "false");
    }

  if (sync_counter->extended_sync_request_counter)
    meta_sync_counter_create_sync_alarm (sync_counter);
}

void
meta_sync_counter_create_sync_alarm (MetaSyncCounter *sync_counter)
{
  MetaWindow *window = sync_counter->window;
  MetaX11Display *x11_display = window->display->x11_display;
  XSyncAlarmAttributes values;
  XSyncValue init;

  if (sync_counter->sync_request_counter == None ||
      sync_counter->sync_request_alarm != None)
    return;

  mtk_x11_error_trap_push (x11_display->xdisplay);

  /* In the new (extended style), the counter value is initialized by
   * the client before mapping the window. In the old style, we're
   * responsible for setting the initial value of the counter.
   */
  if (sync_counter->extended_sync_request_counter)
    {
      if (!XSyncQueryCounter (x11_display->xdisplay,
                              sync_counter->sync_request_counter,
                              &init))
        {
          mtk_x11_error_trap_pop_with_return (x11_display->xdisplay);
          sync_counter->sync_request_counter = None;
          return;
        }

      sync_counter->sync_request_serial =
        XSyncValueLow32 (init) + ((gint64) XSyncValueHigh32 (init) << 32);
    }
  else
    {
      XSyncIntToValue (&init, 0);
      XSyncSetCounter (x11_display->xdisplay,
                       sync_counter->sync_request_counter, init);
      sync_counter->sync_request_serial = 0;
    }

  values.trigger.counter = sync_counter->sync_request_counter;
  values.trigger.test_type = XSyncPositiveComparison;

  /* Initialize to one greater than the current value */
  values.trigger.value_type = XSyncRelative;
  XSyncIntToValue (&values.trigger.wait_value, 1);

  /* After triggering, increment test_value by this until
   * until the test condition is false */
  XSyncIntToValue (&values.delta, 1);

  /* we want events (on by default anyway) */
  values.events = True;

  sync_counter->sync_request_alarm = XSyncCreateAlarm (x11_display->xdisplay,
                                                       XSyncCACounter |
                                                       XSyncCAValueType |
                                                       XSyncCAValue |
                                                       XSyncCATestType |
                                                       XSyncCADelta |
                                                       XSyncCAEvents,
                                                       &values);

  if (mtk_x11_error_trap_pop_with_return (x11_display->xdisplay) == Success)
    {
      meta_x11_display_register_sync_alarm (x11_display,
                                            &sync_counter->sync_request_alarm,
                                            sync_counter);
    }
  else
    {
      sync_counter->sync_request_alarm = None;
      sync_counter->sync_request_counter = None;
    }
}

void
meta_sync_counter_destroy_sync_alarm (MetaSyncCounter *sync_counter)
{
  MetaWindow *window = sync_counter->window;
  MetaX11Display *x11_display = window->display->x11_display;

  if (sync_counter->sync_request_alarm == None)
    return;

  /* Has to be unregistered _before_ clearing the structure field */
  meta_x11_display_unregister_sync_alarm (x11_display,
                                          sync_counter->sync_request_alarm);
  XSyncDestroyAlarm (x11_display->xdisplay,
                     sync_counter->sync_request_alarm);
  sync_counter->sync_request_alarm = None;
}

gboolean
meta_sync_counter_has_sync_alarm (MetaSyncCounter *sync_counter)
{
  return (!sync_counter->disabled &&
          sync_counter->sync_request_alarm != None);
}

static gboolean
sync_request_timeout (gpointer data)
{
  MetaSyncCounter *sync_counter = data;
  MetaWindow *window = sync_counter->window;
  MetaWindowDrag *window_drag;

  sync_counter->sync_request_timeout_id = 0;

  /* We have now waited for more than a second for the
   * application to respond to the sync request
   */
  sync_counter->disabled = TRUE;

  /* Reset the wait serial, so we don't continue freezing
   * window updates
   */
  sync_counter->sync_request_wait_serial = 0;
  meta_compositor_sync_updates_frozen (window->display->compositor, window);

  window_drag =
    meta_compositor_get_current_window_drag (window->display->compositor);

  if (window_drag &&
      window == meta_window_drag_get_window (window_drag) &&
      meta_grab_op_is_resizing (meta_window_drag_get_grab_op (window_drag)))
    meta_window_x11_check_update_resize (window);

  return G_SOURCE_REMOVE;
}

void
meta_sync_counter_send_request (MetaSyncCounter *sync_counter)
{
  MetaWindow *window = sync_counter->window;
  MetaX11Display *x11_display = window->display->x11_display;
  XClientMessageEvent ev = { 0, };
  gint64 wait_serial;

  if (sync_counter->sync_request_counter == None ||
      sync_counter->sync_request_alarm == None ||
      sync_counter->sync_request_timeout_id != 0 ||
      sync_counter->disabled)
    return;

  /* For the old style of _NET_WM_SYNC_REQUEST_COUNTER, we just have to
   * increase the value, but for the new "extended" style we need to
   * pick an even (unfrozen) value sufficiently ahead of the last serial
   * that we received from the client; the same code still works
   * for the old style. The increment of 240 is specified by the EWMH
   * and is (1 second) * (60fps) * (an increment of 4 per frame).
   */
  wait_serial = sync_counter->sync_request_serial + 240;

  sync_counter->sync_request_wait_serial = wait_serial;

  ev.type = ClientMessage;
  ev.window = sync_counter->xwindow;
  ev.message_type = x11_display->atom_WM_PROTOCOLS;
  ev.format = 32;
  ev.data.l[0] = x11_display->atom__NET_WM_SYNC_REQUEST;
  /* FIXME: meta_display_get_current_time() is bad, but since calls
   * come from meta_window_move_resize_internal (which in turn come
   * from all over), I'm not sure what we can do to fix it.  Do we
   * want to use _roundtrip, though?
   */
  ev.data.l[1] = meta_display_get_current_time (window->display);
  ev.data.l[2] = wait_serial & G_GUINT64_CONSTANT (0xffffffff);
  ev.data.l[3] = wait_serial >> 32;
  ev.data.l[4] = sync_counter->extended_sync_request_counter ? 1 : 0;

  /* We don't need to trap errors here as we are already
   * inside an error_trap_push()/pop() pair.
   */
  XSendEvent (x11_display->xdisplay,
	      sync_counter->xwindow, False, 0, (XEvent*) &ev);

  /* We give the window 1 sec to respond to _NET_WM_SYNC_REQUEST;
   * if this time expires, we consider the window unresponsive
   * and resize it unsynchonized.
   */
  sync_counter->sync_request_timeout_id = g_timeout_add (1000,
                                                         sync_request_timeout,
                                                         sync_counter);
  g_source_set_name_by_id (sync_counter->sync_request_timeout_id,
                           "[mutter] sync_request_timeout");

  meta_compositor_sync_updates_frozen (window->display->compositor, window);
}

void
meta_sync_counter_update (MetaSyncCounter *sync_counter,
                          int64_t          new_counter_value)
{
  MetaWindow *window = sync_counter->window;
  gboolean needs_frame_drawn = FALSE;
  gboolean no_delay_frame = FALSE;

  COGL_TRACE_BEGIN_SCOPED (MetaWindowSyncRequestCounter, "Meta::SyncCounter::update()");

  if (sync_counter->extended_sync_request_counter && new_counter_value % 2 == 0)
    {
      needs_frame_drawn = TRUE;
      no_delay_frame = new_counter_value == sync_counter->sync_request_serial + 1;
    }

  sync_counter->sync_request_serial = new_counter_value;
  meta_compositor_sync_updates_frozen (window->display->compositor, window);

  if (new_counter_value >= sync_counter->sync_request_wait_serial &&
      sync_counter->sync_request_timeout_id &&
      (!sync_counter->extended_sync_request_counter ||
       new_counter_value % 2 == 0))
    {
      g_clear_handle_id (&sync_counter->sync_request_timeout_id,
                         g_source_remove);
    }

  /* If sync was previously disabled, turn it back on and hope
   * the application has come to its senses (maybe it was just
   * busy with a pagefault or a long computation).
   */
  sync_counter->disabled = FALSE;

  if (needs_frame_drawn)
    {
      meta_sync_counter_queue_frame_drawn (sync_counter);
      meta_compositor_queue_frame_drawn (window->display->compositor, window,
                                         no_delay_frame);
    }

#ifdef HAVE_PROFILER
  if (G_UNLIKELY (cogl_is_tracing_enabled ()))
    {
      g_autofree char *description = NULL;

      description =
        g_strdup_printf ("sync request serial: %" G_GINT64_FORMAT ", "
                         "needs frame drawn: %s",
                         new_counter_value,
                         needs_frame_drawn ? "yes" : "no");
      COGL_TRACE_DESCRIBE (MetaWindowSyncRequestCounter, description);
    }
#endif
}

gboolean
meta_sync_counter_is_waiting (MetaSyncCounter *sync_counter)
{
  if (sync_counter->extended_sync_request_counter &&
      sync_counter->sync_request_serial % 2 == 1)
    return TRUE;

  if (sync_counter->sync_request_serial < sync_counter->sync_request_wait_serial)
    return TRUE;

  return FALSE;
}

gboolean
meta_sync_counter_is_waiting_response (MetaSyncCounter *sync_counter)
{
  return sync_counter->sync_request_timeout_id != 0;
}

static void
do_send_frame_drawn (MetaSyncCounter *sync_counter,
                     FrameData       *frame)
{
  MetaWindow *window = sync_counter->window;
  MetaDisplay *display = meta_window_get_display (window);
  Display *xdisplay = meta_x11_display_get_xdisplay (display->x11_display);
  int64_t now_us;
  XClientMessageEvent ev = { 0, };

  COGL_TRACE_BEGIN_SCOPED (MetaWindowActorX11FrameDrawn,
                           "Meta::SyncCounter::do_send_frame_drawn()");

  now_us = g_get_monotonic_time ();
  frame->frame_drawn_time =
    meta_compositor_monotonic_to_high_res_xserver_time (display->compositor,
                                                        now_us);
  sync_counter->frame_drawn_time = frame->frame_drawn_time;

  ev.type = ClientMessage;
  ev.window = sync_counter->xwindow;
  ev.message_type = display->x11_display->atom__NET_WM_FRAME_DRAWN;
  ev.format = 32;
  ev.data.l[0] = frame->sync_request_serial & G_GUINT64_CONSTANT (0xffffffff);
  ev.data.l[1] = frame->sync_request_serial >> 32;
  ev.data.l[2] = frame->frame_drawn_time & G_GUINT64_CONSTANT (0xffffffff);
  ev.data.l[3] = frame->frame_drawn_time >> 32;

  mtk_x11_error_trap_push (xdisplay);
  XSendEvent (xdisplay, ev.window, False, 0, (XEvent *) &ev);
  XFlush (xdisplay);
  mtk_x11_error_trap_pop (xdisplay);

#ifdef HAVE_PROFILER
  if (G_UNLIKELY (cogl_is_tracing_enabled ()))
    {
      g_autofree char *description = NULL;

      description = g_strdup_printf ("frame drawn time: %" G_GINT64_FORMAT ", "
                                     "sync request serial: %" G_GINT64_FORMAT,
                                     frame->frame_drawn_time,
                                     frame->sync_request_serial);
      COGL_TRACE_DESCRIBE (MetaWindowActorX11FrameDrawn,
                           description);
    }
#endif
}

static void
do_send_frame_timings (MetaSyncCounter *sync_counter,
                       FrameData       *frame,
                       int              refresh_interval,
                       int64_t          presentation_time)
{
  MetaWindow *window = sync_counter->window;
  MetaDisplay *display = meta_window_get_display (window);
  Display *xdisplay = meta_x11_display_get_xdisplay (display->x11_display);
  XClientMessageEvent ev = { 0, };

  COGL_TRACE_BEGIN_SCOPED (MetaWindowActorX11FrameTimings,
                           "Meta::SyncCounter::do_send_frame_timings()");

  ev.type = ClientMessage;
  ev.window = sync_counter->xwindow;
  ev.message_type = display->x11_display->atom__NET_WM_FRAME_TIMINGS;
  ev.format = 32;
  ev.data.l[0] = frame->sync_request_serial & G_GUINT64_CONSTANT (0xffffffff);
  ev.data.l[1] = frame->sync_request_serial >> 32;

  if (presentation_time != 0)
    {
      MetaCompositor *compositor = display->compositor;
      int64_t presentation_time_server, presentation_time_offset;

      presentation_time_server =
        meta_compositor_monotonic_to_high_res_xserver_time (compositor,
                                                            presentation_time);
      presentation_time_offset = presentation_time_server - frame->frame_drawn_time;
      if (presentation_time_offset == 0)
        presentation_time_offset = 1;

      if ((int32_t) presentation_time_offset == presentation_time_offset)
        ev.data.l[2] = presentation_time_offset;
    }

  ev.data.l[3] = refresh_interval;
  ev.data.l[4] = 1000 * META_SYNC_DELAY;

  mtk_x11_error_trap_push (xdisplay);
  XSendEvent (xdisplay, ev.window, False, 0, (XEvent *) &ev);
  XFlush (xdisplay);
  mtk_x11_error_trap_pop (xdisplay);

#ifdef HAVE_PROFILER
  if (G_UNLIKELY (cogl_is_tracing_enabled ()))
    {
      g_autofree char *description = NULL;

      description =
        g_strdup_printf ("refresh interval: %d, "
                         "presentation time: %" G_GINT64_FORMAT ", "
                         "sync request serial: %" G_GINT64_FORMAT,
                         refresh_interval,
                         frame->sync_request_serial,
                         presentation_time);
      COGL_TRACE_DESCRIBE (MetaWindowActorX11FrameTimings, description);
    }
#endif
}

static void
send_frame_timings (MetaSyncCounter  *sync_counter,
                    FrameData        *frame,
                    ClutterFrameInfo *frame_info,
                    int64_t           presentation_time)
{
  float refresh_rate;
  int refresh_interval;

  refresh_rate = frame_info->refresh_rate;
  /* 0.0 is a flag for not known, but sanity-check against other odd numbers */
  if (refresh_rate >= 1.0)
    refresh_interval = (int) (0.5 + 1000000 / refresh_rate);
  else
    refresh_interval = 0;

  do_send_frame_timings (sync_counter, frame,
                         refresh_interval, presentation_time);
}

void
meta_sync_counter_queue_frame_drawn (MetaSyncCounter *sync_counter)
{
  FrameData *frame;

  frame = g_new0 (FrameData, 1);
  frame->frame_counter = -1;
  frame->sync_request_serial = sync_counter->sync_request_serial;

  sync_counter->frames = g_list_prepend (sync_counter->frames, frame);

  sync_counter->needs_frame_drawn = TRUE;
}

void
meta_sync_counter_assign_counter_to_frames (MetaSyncCounter *sync_counter,
                                            int64_t          counter)
{
  GList *l;

  for (l = sync_counter->frames; l; l = l->next)
    {
      FrameData *frame = l->data;

      if (frame->frame_counter == -1)
        frame->frame_counter = counter;
    }
}

void
meta_sync_counter_complete_frame (MetaSyncCounter  *sync_counter,
                                  ClutterFrameInfo *frame_info,
                                  int64_t           presentation_time)
{
  GList *l;

  for (l = sync_counter->frames; l;)
    {
      GList *l_next = l->next;
      FrameData *frame = l->data;
      int64_t frame_counter = frame_info->frame_counter;

      if (frame->frame_counter != -1 && frame->frame_counter <= frame_counter)
        {
          MetaWindow *window = sync_counter->window;

          if (G_UNLIKELY (frame->frame_drawn_time == 0))
            g_warning ("%s: Frame has assigned frame counter but no frame drawn time",
                       window->desc);
          if (G_UNLIKELY (frame->frame_counter < frame_counter))
            g_debug ("%s: frame_complete callback never occurred for frame %" G_GINT64_FORMAT,
                     window->desc, frame->frame_counter);

          sync_counter->frames = g_list_delete_link (sync_counter->frames, l);
          send_frame_timings (sync_counter, frame, frame_info, presentation_time);
          g_free (frame);
        }

      l = l_next;
    }
}

void
meta_sync_counter_finish_incomplete (MetaSyncCounter *sync_counter)
{
  GList *l;

  for (l = sync_counter->frames; l;)
    {
      GList *l_next = l->next;
      FrameData *frame = l->data;

      if (frame->frame_counter == -1)
        {
          do_send_frame_drawn (sync_counter, frame);
          do_send_frame_timings (sync_counter, frame, 0, 0);

          sync_counter->frames = g_list_delete_link (sync_counter->frames, l);
          g_free (frame);
        }

      l = l_next;
    }

  sync_counter->needs_frame_drawn = FALSE;
}

void
meta_sync_counter_send_frame_drawn (MetaSyncCounter *sync_counter)
{
  GList *l;

  if (!sync_counter->needs_frame_drawn)
    return;

  for (l = sync_counter->frames; l; l = l->next)
    {
      FrameData *frame = l->data;

      if (frame->frame_drawn_time == 0)
        do_send_frame_drawn (sync_counter, frame);
    }

  sync_counter->needs_frame_drawn = FALSE;
}
