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

#include "meta-sync-counter.h"

#include "core/window-private.h"
#include "meta/meta-x11-errors.h"
#include "x11/meta-x11-display-private.h"

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

  meta_x11_error_trap_push (x11_display);

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
          meta_x11_error_trap_pop_with_return (x11_display);
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

  if (meta_x11_error_trap_pop_with_return (x11_display) == Success)
    {
      meta_x11_display_register_sync_alarm (x11_display,
                                            &sync_counter->sync_request_alarm,
                                            window);
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

  if (window == window->display->grab_window &&
      meta_grab_op_is_resizing (window->display->grab_op))
    {
      meta_window_update_resize (window,
                                 window->display->grab_last_edge_resistance_flags,
                                 window->display->grab_latest_motion_x,
                                 window->display->grab_latest_motion_y);
    }

  return G_SOURCE_REMOVE;
}

void
meta_sync_counter_send_request (MetaSyncCounter *sync_counter)
{
  MetaWindow *window = sync_counter->window;
  MetaX11Display *x11_display = window->display->x11_display;
  XClientMessageEvent ev;
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

  COGL_TRACE_BEGIN (MetaWindowSyncRequestCounter, "X11: Sync request counter");

  if (sync_counter->extended_sync_request_counter && new_counter_value % 2 == 0)
    {
      needs_frame_drawn = TRUE;
      no_delay_frame = new_counter_value == sync_counter->sync_request_serial + 1;
    }

  sync_counter->sync_request_serial = new_counter_value;
  meta_compositor_sync_updates_frozen (window->display->compositor, window);

  if (new_counter_value >= sync_counter->sync_request_wait_serial &&
      sync_counter->sync_request_timeout_id)
    {
      if (!sync_counter->extended_sync_request_counter ||
          new_counter_value % 2 == 0)
        {
          g_clear_handle_id (&sync_counter->sync_request_timeout_id,
                             g_source_remove);
        }

      if (window == window->display->grab_window &&
          meta_grab_op_is_resizing (window->display->grab_op) &&
          (!sync_counter->extended_sync_request_counter ||
           new_counter_value % 2 == 0))
        {
          meta_topic (META_DEBUG_RESIZING,
                      "Alarm event received last motion x = %d y = %d",
                      window->display->grab_latest_motion_x,
                      window->display->grab_latest_motion_y);

          /* This means we are ready for another configure;
           * no pointer round trip here, to keep in sync */
          meta_window_update_resize (window,
                                     window->display->grab_last_edge_resistance_flags,
                                     window->display->grab_latest_motion_x,
                                     window->display->grab_latest_motion_y);
        }
    }

  /* If sync was previously disabled, turn it back on and hope
   * the application has come to its senses (maybe it was just
   * busy with a pagefault or a long computation).
   */
  sync_counter->disabled = FALSE;

  if (needs_frame_drawn)
    meta_compositor_queue_frame_drawn (window->display->compositor, window,
                                       no_delay_frame);

#ifdef COGL_HAS_TRACING
  if (G_UNLIKELY (cogl_is_tracing_enabled ()))
    {
      g_autofree char *description = NULL;

      description =
        g_strdup_printf ("sync request serial: %" G_GINT64_FORMAT ", "
                         "needs frame drawn: %s",
                         new_counter_value,
                         needs_frame_drawn ? "yes" : "no");
      COGL_TRACE_DESCRIBE (MetaWindowSyncRequestCounter, description);
      COGL_TRACE_END (MetaWindowSyncRequestCounter);
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
