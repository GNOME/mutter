/*
 * Copyright (C) 2018 Endless, Inc.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by:
 *     Georges Basile Stavracas Neto <gbsneto@gnome.org>
 */

#include "config.h"

#include "compositor/meta-window-actor-x11.h"

#include "backends/meta-logical-monitor.h"
#include "compositor/compositor-private.h"
#include "compositor/meta-surface-actor.h"
#include "core/window-private.h"
#include "meta/compositor.h"
#include "meta/meta-window-actor.h"
#include "meta/meta-x11-errors.h"
#include "meta/window.h"
#include "x11/meta-x11-display-private.h"

struct _MetaWindowActorX11
{
  MetaWindowActor parent;

  /* List of FrameData for recent frames */
  GList *frames;

  guint send_frame_messages_timer;
  int64_t frame_drawn_time;

  guint repaint_scheduled_id;

  /* If set, the client needs to be sent a _NET_WM_FRAME_DRAWN
   * client message for one or more messages in ->frames */
  gboolean needs_frame_drawn;
  gboolean repaint_scheduled;
};

G_DEFINE_TYPE (MetaWindowActorX11, meta_window_actor_x11, META_TYPE_WINDOW_ACTOR)

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

static void
frame_data_free (FrameData *frame)
{
  g_slice_free (FrameData, frame);
}

static void
surface_repaint_scheduled (MetaSurfaceActor *actor,
                           gpointer          user_data)
{
  MetaWindowActorX11 *actor_x11 = META_WINDOW_ACTOR_X11 (user_data);

  actor_x11->repaint_scheduled = TRUE;
}

static void
remove_frame_messages_timer (MetaWindowActorX11 *actor_x11)
{
  g_assert (actor_x11->send_frame_messages_timer != 0);

  g_source_remove (actor_x11->send_frame_messages_timer);
  actor_x11->send_frame_messages_timer = 0;
}

static void
do_send_frame_drawn (MetaWindowActorX11 *actor_x11,
                     FrameData          *frame)
{
  MetaWindow *window =
    meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_x11));
  MetaDisplay *display = meta_window_get_display (window);
  Display *xdisplay = meta_x11_display_get_xdisplay (display->x11_display);

  XClientMessageEvent ev = { 0, };

  frame->frame_drawn_time =
    meta_compositor_monotonic_time_to_server_time (display,
                                                   g_get_monotonic_time ());
  actor_x11->frame_drawn_time = frame->frame_drawn_time;

  ev.type = ClientMessage;
  ev.window = meta_window_get_xwindow (window);
  ev.message_type = display->x11_display->atom__NET_WM_FRAME_DRAWN;
  ev.format = 32;
  ev.data.l[0] = frame->sync_request_serial & G_GUINT64_CONSTANT (0xffffffff);
  ev.data.l[1] = frame->sync_request_serial >> 32;
  ev.data.l[2] = frame->frame_drawn_time & G_GUINT64_CONSTANT (0xffffffff);
  ev.data.l[3] = frame->frame_drawn_time >> 32;

  meta_x11_error_trap_push (display->x11_display);
  XSendEvent (xdisplay, ev.window, False, 0, (XEvent *) &ev);
  XFlush (xdisplay);
  meta_x11_error_trap_pop (display->x11_display);
}

static void
do_send_frame_timings (MetaWindowActorX11 *actor_x11,
                       FrameData          *frame,
                       int                 refresh_interval,
                       int64_t             presentation_time)
{
  MetaWindow *window =
    meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_x11));
  MetaDisplay *display = meta_window_get_display (window);
  Display *xdisplay = meta_x11_display_get_xdisplay (display->x11_display);

  XClientMessageEvent ev = { 0, };

  ev.type = ClientMessage;
  ev.window = meta_window_get_xwindow (window);
  ev.message_type = display->x11_display->atom__NET_WM_FRAME_TIMINGS;
  ev.format = 32;
  ev.data.l[0] = frame->sync_request_serial & G_GUINT64_CONSTANT (0xffffffff);
  ev.data.l[1] = frame->sync_request_serial >> 32;

  if (presentation_time != 0)
    {
      int64_t presentation_time_server =
        meta_compositor_monotonic_time_to_server_time (display,
                                                       presentation_time);
      int64_t presentation_time_offset = presentation_time_server - frame->frame_drawn_time;
      if (presentation_time_offset == 0)
        presentation_time_offset = 1;

      if ((int32_t)presentation_time_offset == presentation_time_offset)
        ev.data.l[2] = presentation_time_offset;
    }

  ev.data.l[3] = refresh_interval;
  ev.data.l[4] = 1000 * META_SYNC_DELAY;

  meta_x11_error_trap_push (display->x11_display);
  XSendEvent (xdisplay, ev.window, False, 0, (XEvent *) &ev);
  XFlush (xdisplay);
  meta_x11_error_trap_pop (display->x11_display);
}

static void
send_frame_timings (MetaWindowActorX11 *actor_x11,
                    FrameData          *frame,
                    ClutterFrameInfo   *frame_info,
                    int64_t             presentation_time)
{
  float refresh_rate;
  int refresh_interval;

  refresh_rate = frame_info->refresh_rate;
  /* 0.0 is a flag for not known, but sanity-check against other odd numbers */
  if (refresh_rate >= 1.0)
    refresh_interval = (int) (0.5 + 1000000 / refresh_rate);
  else
    refresh_interval = 0;

  do_send_frame_timings (actor_x11, frame, refresh_interval, presentation_time);
}

static gboolean
send_frame_messages_timeout (gpointer data)
{
  MetaWindowActorX11 *actor_x11 = META_WINDOW_ACTOR_X11 (data);
  GList *l;

  for (l = actor_x11->frames; l;)
    {
      GList *l_next = l->next;
      FrameData *frame = l->data;

      if (frame->frame_counter == -1)
        {
          do_send_frame_drawn (actor_x11, frame);
          do_send_frame_timings (actor_x11, frame, 0, 0);

          actor_x11->frames = g_list_delete_link (actor_x11->frames, l);
          frame_data_free (frame);
        }

      l = l_next;
    }

  actor_x11->needs_frame_drawn = FALSE;
  actor_x11->send_frame_messages_timer = 0;

  return G_SOURCE_REMOVE;
}

static void
queue_send_frame_messages_timeout (MetaWindowActorX11 *actor_x11)
{
  MetaWindow *window =
    meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_x11));
  MetaDisplay *display = meta_window_get_display (window);
  MetaLogicalMonitor *logical_monitor;
  int64_t current_time;
  float refresh_rate;
  int interval, offset;

  if (actor_x11->send_frame_messages_timer != 0)
    return;

  logical_monitor = meta_window_get_main_logical_monitor (window);
  if (logical_monitor)
    {
      GList *monitors = meta_logical_monitor_get_monitors (logical_monitor);
      MetaMonitor *monitor;
      MetaMonitorMode *mode;

      monitor = g_list_first (monitors)->data;
      mode = meta_monitor_get_current_mode (monitor);

      refresh_rate = meta_monitor_mode_get_refresh_rate (mode);
    }
  else
    {
      refresh_rate = 60.0f;
    }

  current_time =
    meta_compositor_monotonic_time_to_server_time (display,
                                                   g_get_monotonic_time ());
  interval = (int) (1000000 / refresh_rate) * 6;
  offset = MAX (0, actor_x11->frame_drawn_time + interval - current_time) / 1000;

 /* The clutter master clock source has already been added with META_PRIORITY_REDRAW,
  * so the timer will run *after* the clutter frame handling, if a frame is ready
  * to be drawn when the timer expires.
  */
  actor_x11->send_frame_messages_timer =
    g_timeout_add_full (META_PRIORITY_REDRAW, offset,
                        send_frame_messages_timeout,
                        actor_x11, NULL);
  g_source_set_name_by_id (actor_x11->send_frame_messages_timer,
                           "[mutter] send_frame_messages_timeout");
}

static void
assign_frame_counter_to_frames (MetaWindowActorX11 *actor_x11)
{
  MetaWindow *window =
    meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_x11));
  MetaCompositor *compositor = window->display->compositor;
  ClutterStage *stage = CLUTTER_STAGE (compositor->stage);
  GList *l;

  /* If the window is obscured, then we're expecting to deal with sending
   * frame messages in a timeout, rather than in this paint cycle.
   */
  if (actor_x11->send_frame_messages_timer != 0)
    return;

  for (l = actor_x11->frames; l; l = l->next)
    {
      FrameData *frame = l->data;

      if (frame->frame_counter == -1)
        frame->frame_counter = clutter_stage_get_frame_counter (stage);
    }
}

static void
meta_window_actor_x11_frame_complete (MetaWindowActor  *actor,
                                      ClutterFrameInfo *frame_info,
                                      int64_t           presentation_time)
{
  MetaWindowActorX11 *actor_x11 = META_WINDOW_ACTOR_X11 (actor);
  GList *l;

  if (meta_window_actor_is_destroyed (actor))
    return;

  for (l = actor_x11->frames; l;)
    {
      GList *l_next = l->next;
      FrameData *frame = l->data;
      int64_t frame_counter = frame_info->frame_counter;

      if (frame->frame_counter != -1 && frame->frame_counter <= frame_counter)
        {
          MetaWindow *window =
            meta_window_actor_get_meta_window (actor);

          if (G_UNLIKELY (frame->frame_drawn_time == 0))
            g_warning ("%s: Frame has assigned frame counter but no frame drawn time",
                       window->desc);
          if (G_UNLIKELY (frame->frame_counter < frame_counter))
            g_warning ("%s: frame_complete callback never occurred for frame %" G_GINT64_FORMAT,
                       window->desc, frame->frame_counter);

          actor_x11->frames = g_list_delete_link (actor_x11->frames, l);
          send_frame_timings (actor_x11, frame, frame_info, presentation_time);
          frame_data_free (frame);
        }

      l = l_next;
    }
}

static void
meta_window_actor_x11_assign_surface_actor (MetaWindowActor  *actor,
                                            MetaSurfaceActor *surface_actor)
{
  MetaWindowActorClass *parent_class =
    META_WINDOW_ACTOR_CLASS (meta_window_actor_x11_parent_class);
  MetaWindowActorX11 *actor_x11 = META_WINDOW_ACTOR_X11 (actor);

  parent_class->assign_surface_actor (actor, surface_actor);

  actor_x11->repaint_scheduled_id =
    g_signal_connect (surface_actor, "repaint-scheduled",
                      G_CALLBACK (surface_repaint_scheduled),
                      actor_x11);
}

static void
meta_window_actor_x11_queue_frame_drawn (MetaWindowActor *actor,
                                         gboolean         skip_sync_delay)
{
  MetaWindowActorX11 *actor_x11 = META_WINDOW_ACTOR_X11 (actor);
  MetaWindow *window =
    meta_window_actor_get_meta_window (actor);
  FrameData *frame;

  if (meta_window_actor_is_destroyed (actor))
    return;

  frame = g_slice_new0 (FrameData);
  frame->frame_counter = -1;
  frame->sync_request_serial = window->sync_request_serial;

  actor_x11->frames = g_list_prepend (actor_x11->frames, frame);

  actor_x11->needs_frame_drawn = TRUE;

  if (skip_sync_delay)
    {
      ClutterActor *stage = clutter_actor_get_stage (CLUTTER_ACTOR (actor_x11));
      clutter_stage_skip_sync_delay (CLUTTER_STAGE (stage));
    }

  if (!actor_x11->repaint_scheduled)
    {
      MetaSurfaceActor *surface;
      gboolean is_obscured;

      surface = meta_window_actor_get_surface (META_WINDOW_ACTOR (actor_x11));

      if (surface)
        is_obscured = meta_surface_actor_is_obscured (surface);
      else
        is_obscured = FALSE;

      /* A frame was marked by the client without actually doing any
       * damage or any unobscured, or while we had the window frozen
       * (e.g. during an interactive resize.) We need to make sure that the
       * pre_paint/post_paint functions get called, enabling us to
       * send a _NET_WM_FRAME_DRAWN. We do a 1-pixel redraw to get
       * consistent timing with non-empty frames. If the window
       * is completely obscured we fire off the send_frame_messages timeout.
       */
      if (is_obscured)
        {
          queue_send_frame_messages_timeout (actor_x11);
        }
      else if (surface)
        {
          const cairo_rectangle_int_t clip = { 0, 0, 1, 1 };
          clutter_actor_queue_redraw_with_clip (CLUTTER_ACTOR (surface), &clip);
          actor_x11->repaint_scheduled = TRUE;
        }
    }
}

static void
meta_window_actor_x11_pre_paint (MetaWindowActor *actor)
{
  MetaWindowActorX11 *actor_x11 = META_WINDOW_ACTOR_X11 (actor);

  assign_frame_counter_to_frames (actor_x11);
}

static void
meta_window_actor_x11_post_paint (MetaWindowActor *actor)
{
  MetaWindowActorX11 *actor_x11 = META_WINDOW_ACTOR_X11 (actor);

  actor_x11->repaint_scheduled = FALSE;

  if (meta_window_actor_is_destroyed (actor))
    return;

  /* If the window had damage, but wasn't actually redrawn because
   * it is obscured, we should wait until timer expiration before
   * sending _NET_WM_FRAME_* messages.
   */
  if (actor_x11->send_frame_messages_timer == 0 &&
      actor_x11->needs_frame_drawn)
    {
      GList *l;

      for (l = actor_x11->frames; l; l = l->next)
        {
          FrameData *frame = l->data;

          if (frame->frame_drawn_time == 0)
            do_send_frame_drawn (actor_x11, frame);
        }

      actor_x11->needs_frame_drawn = FALSE;
    }
}

static void
meta_window_actor_x11_queue_destroy (MetaWindowActor *actor)
{
  MetaWindowActorX11 *actor_x11 = META_WINDOW_ACTOR_X11 (actor);

  if (actor_x11->send_frame_messages_timer != 0)
    remove_frame_messages_timer (actor_x11);
}

static void
meta_window_actor_x11_paint (ClutterActor *actor)
{
  MetaWindowActorX11 *actor_x11 = META_WINDOW_ACTOR_X11 (actor);

 /* This window got damage when obscured; we set up a timer
  * to send frame completion events, but since we're drawing
  * the window now (for some other reason) cancel the timer
  * and send the completion events normally */
  if (actor_x11->send_frame_messages_timer != 0)
    {
      remove_frame_messages_timer (actor_x11);
      assign_frame_counter_to_frames (actor_x11);
    }

  CLUTTER_ACTOR_CLASS (meta_window_actor_x11_parent_class)->paint (actor);
}

static void
meta_window_actor_x11_constructed (GObject *object)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (object);
  MetaWindow *window =
    meta_window_actor_get_meta_window (window_actor);

  G_OBJECT_CLASS (meta_window_actor_x11_parent_class)->constructed (object);

  /* If a window doesn't start off with updates frozen, we should
   * we should send a _NET_WM_FRAME_DRAWN immediately after the first drawn.
   */
  if (window->extended_sync_request_counter &&
      !meta_window_updates_are_frozen (window))
    meta_window_actor_queue_frame_drawn (window_actor, FALSE);
}

static void
meta_window_actor_x11_dispose (GObject *object)
{
  MetaWindowActorX11 *actor_x11 = META_WINDOW_ACTOR_X11 (object);
  MetaSurfaceActor *surface_actor;

  if (actor_x11->send_frame_messages_timer != 0)
    remove_frame_messages_timer (actor_x11);

  surface_actor = meta_window_actor_get_surface (META_WINDOW_ACTOR (actor_x11));
  if (surface_actor)
    {
      g_signal_handler_disconnect (surface_actor,
                                   actor_x11->repaint_scheduled_id);
      actor_x11->repaint_scheduled_id = 0;
    }

  G_OBJECT_CLASS (meta_window_actor_x11_parent_class)->dispose (object);
}

static void
meta_window_actor_x11_finalize (GObject *object)
{
  MetaWindowActorX11 *actor_x11 = META_WINDOW_ACTOR_X11 (object);

  g_list_free_full (actor_x11->frames, (GDestroyNotify) frame_data_free);

  G_OBJECT_CLASS (meta_window_actor_x11_parent_class)->finalize (object);
}

static void
meta_window_actor_x11_class_init (MetaWindowActorX11Class *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  MetaWindowActorClass *window_actor_class = META_WINDOW_ACTOR_CLASS (klass);

  window_actor_class->frame_complete = meta_window_actor_x11_frame_complete;
  window_actor_class->assign_surface_actor = meta_window_actor_x11_assign_surface_actor;
  window_actor_class->queue_frame_drawn = meta_window_actor_x11_queue_frame_drawn;
  window_actor_class->pre_paint = meta_window_actor_x11_pre_paint;
  window_actor_class->post_paint = meta_window_actor_x11_post_paint;
  window_actor_class->queue_destroy = meta_window_actor_x11_queue_destroy;

  actor_class->paint = meta_window_actor_x11_paint;

  object_class->constructed = meta_window_actor_x11_constructed;
  object_class->dispose = meta_window_actor_x11_dispose;
  object_class->finalize = meta_window_actor_x11_finalize;
}

static void
meta_window_actor_x11_init (MetaWindowActorX11 *self)
{
}
