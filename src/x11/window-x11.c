/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001 Havoc Pennington, Anders Carlsson
 * Copyright (C) 2002, 2003 Red Hat, Inc.
 * Copyright (C) 2003 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
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
 */

#include "config.h"

#include "x11/window-x11.h"
#include "x11/window-x11-private.h"

#include <string.h>
#include <X11/Xatom.h>
#include <X11/Xlibint.h>
#include <X11/Xlib-xcb.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xcomposite.h>
#include <xcb/res.h>

#include "backends/meta-logical-monitor-private.h"
#include "backends/x11/meta-backend-x11.h"
#include "compositor/compositor-private.h"
#include "compositor/meta-window-actor-private.h"
#include "core/boxes-private.h"
#include "core/meta-window-config-private.h"
#include "core/meta-workspace-manager-private.h"
#include "core/window-private.h"
#include "core/workspace-private.h"
#include "meta/common.h"
#include "meta/meta-cursor-tracker.h"
#include "meta/meta-later.h"
#include "meta/prefs.h"
#include "mtk/mtk-x11.h"

#ifdef HAVE_XWAYLAND
#include "wayland/meta-window-xwayland.h"
#endif

#include "x11/meta-sync-counter.h"
#include "x11/meta-x11-display-private.h"
#include "x11/meta-x11-frame.h"
#include "x11/meta-x11-group-private.h"
#include "x11/window-props.h"
#include "x11/xprops.h"

#define TAKE_FOCUS_FALLBACK_DELAY_MS 150

enum _MetaGtkEdgeConstraints
{
  META_GTK_EDGE_CONSTRAINT_TOP_TILED = 1 << 0,
  META_GTK_EDGE_CONSTRAINT_TOP_RESIZABLE = 1 << 1,
  META_GTK_EDGE_CONSTRAINT_RIGHT_TILED = 1 << 2,
  META_GTK_EDGE_CONSTRAINT_RIGHT_RESIZABLE = 1 << 3,
  META_GTK_EDGE_CONSTRAINT_BOTTOM_TILED = 1 << 4,
  META_GTK_EDGE_CONSTRAINT_BOTTOM_RESIZABLE = 1 << 5,
  META_GTK_EDGE_CONSTRAINT_LEFT_TILED = 1 << 6,
  META_GTK_EDGE_CONSTRAINT_LEFT_RESIZABLE = 1 << 7
} MetaGtkEdgeConstraints;

G_DEFINE_TYPE_WITH_PRIVATE (MetaWindowX11, meta_window_x11, META_TYPE_WINDOW)

enum
{
  PROP_0,

  PROP_ATTRIBUTES,
  PROP_XWINDOW,

  PROP_LAST,
};

static GParamSpec *obj_props[PROP_LAST];

static void
meta_window_x11_maybe_focus_delayed (MetaWindow *window,
                                     GQueue     *other_focus_candidates,
                                     guint32     timestamp);

static void
meta_window_x11_impl_process_property_notify (MetaWindow     *window,
                                              XPropertyEvent *event);

static void
meta_window_x11_compute_group (MetaWindow *window);

static void
meta_window_x11_init (MetaWindowX11 *window_x11)
{
}

MetaWindowX11Private *
meta_window_x11_get_private (MetaWindowX11 *window_x11)
{
  return meta_window_x11_get_instance_private (window_x11);
}

static void
meta_window_x11_stage_to_protocol (MetaWindow          *window,
                                   int                  stage_x,
                                   int                  stage_y,
                                   int                 *protocol_x,
                                   int                 *protocol_y,
                                   MtkRoundingStrategy  rounding_strategy)
{
  if (protocol_x)
    *protocol_x = stage_x;
  if (protocol_y)
    *protocol_y = stage_y;
}

static void
meta_window_x11_protocol_to_stage (MetaWindow          *window,
                                   int                  protocol_x,
                                   int                  protocol_y,
                                   int                 *stage_x,
                                   int                 *stage_y,
                                   MtkRoundingStrategy  rounding_strategy)
{
  if (stage_x)
    *stage_x = protocol_x;
  if (stage_y)
    *stage_y = protocol_y;
}

static MtkRectangle *
protocol_rects_to_stage_rects (MetaWindow *window,
                               size_t      n_rects,
                               XRectangle *protocol_rects)
{
  MtkRectangle *rects;
  size_t i;

  rects = g_new0 (MtkRectangle, n_rects);
  for (i = 0; i < n_rects; i++)
    {
      MtkRectangle protocol_rect =
        MTK_RECTANGLE_INIT (protocol_rects[i].x,
                            protocol_rects[i].y,
                            protocol_rects[i].width,
                            protocol_rects[i].height);

      meta_window_protocol_to_stage_rect (window, &protocol_rect, &rects[i]);
    }

  return rects;
}

static void
send_icccm_message (MetaWindow *window,
                    Atom        atom,
                    guint32     timestamp)
{
  /* This comment and code are from twm, copyright
   * Open Group, Evans & Sutherland, etc.
   */

  /*
   * ICCCM Client Messages - Section 4.2.8 of the ICCCM dictates that all
   * client messages will have the following form:
   *
   *     event type	ClientMessage
   *     message type	_XA_WM_PROTOCOLS
   *     window		tmp->w
   *     format		32
   *     data[0]		message atom
   *     data[1]		time stamp
   */

  XClientMessageEvent ev = { 0 };
  MetaX11Display *x11_display = window->display->x11_display;

  ev.type = ClientMessage;
  ev.window = meta_window_x11_get_xwindow (window);
  ev.message_type = x11_display->atom_WM_PROTOCOLS;
  ev.format = 32;
  ev.data.l[0] = atom;
  ev.data.l[1] = timestamp;

  mtk_x11_error_trap_push (x11_display->xdisplay);
  XSendEvent (x11_display->xdisplay,
              meta_window_x11_get_xwindow (window), False, 0, (XEvent*) &ev);
  mtk_x11_error_trap_pop (x11_display->xdisplay);
}

static Window
read_client_leader (MetaDisplay *display,
                    Window       xwindow)
{
  Window retval = None;

  meta_prop_get_window (display->x11_display, xwindow,
                        display->x11_display->atom_WM_CLIENT_LEADER,
                        &retval);

  return retval;
}

typedef struct
{
  Window leader;
} ClientLeaderData;

static gboolean
find_client_leader_func (MetaWindow *ancestor,
                         void       *data)
{
  ClientLeaderData *d;

  d = data;

  d->leader = read_client_leader (ancestor->display,
                                  meta_window_x11_get_xwindow (ancestor));

  /* keep going if no client leader found */
  return d->leader == None;
}

static void
update_sm_hints (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_private (window_x11);
  Window leader;

  priv->xclient_leader = None;
  priv->sm_client_id = NULL;

  /* If not on the current window, we can get the client
   * leader from transient parents. If we find a client
   * leader, we read the SM_CLIENT_ID from it.
   */
  leader = read_client_leader (window->display,
                               priv->xwindow);
  if (leader == None)
    {
      ClientLeaderData d;
      d.leader = None;
      meta_window_foreach_ancestor (window, find_client_leader_func,
                                    &d);
      leader = d.leader;
    }

  if (leader != None)
    {
      priv->xclient_leader = leader;

      meta_prop_get_latin1_string (window->display->x11_display, leader,
                                   window->display->x11_display->atom_SM_CLIENT_ID,
                                   &priv->sm_client_id);
    }
  else
    {
      meta_topic (META_DEBUG_X11,
                  "Didn't find a client leader for %s", window->desc);

      if (!meta_prefs_get_disable_workarounds ())
        {
          /* Some broken apps (kdelibs fault?) set SM_CLIENT_ID on the app
           * instead of the client leader
           */
          meta_prop_get_latin1_string (window->display->x11_display,
                                       priv->xwindow,
                                       window->display->x11_display->atom_SM_CLIENT_ID,
                                       &priv->sm_client_id);

          if (priv->sm_client_id)
            {
              meta_topic (META_DEBUG_X11,
                          "Window %s sets SM_CLIENT_ID on itself, "
                          "instead of on the WM_CLIENT_LEADER window "
                          "as specified in the ICCCM.",
                          window->desc);
            }
        }
    }

  meta_topic (META_DEBUG_X11,
              "Window %s client leader: 0x%lx SM_CLIENT_ID: '%s'",
              window->desc, priv->xclient_leader,
              priv->sm_client_id ? priv->sm_client_id : "none");
}

static void
send_configure_notify (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);
  XEvent event = { 0 };

  g_assert (!window->override_redirect);

  /* from twm */

  event.type = ConfigureNotify;
  event.xconfigure.display = x11_display->xdisplay;
  event.xconfigure.event = priv->xwindow;
  event.xconfigure.window = priv->xwindow;
  meta_window_stage_to_protocol_point (window,
                                       priv->client_rect.x - priv->border_width,
                                       priv->client_rect.y - priv->border_width,
                                       &event.xconfigure.x,
                                       &event.xconfigure.y);
  if (priv->frame)
    {
      if (window->withdrawn)
        {
          MetaFrameBorders borders;
          /* We reparent the client window and put it to the position
           * where the visible top-left of the frame window currently is.
           */

          meta_frame_calc_borders (priv->frame, &borders);

          meta_window_stage_to_protocol_point (window,
                                               (priv->frame->rect.x +
                                                borders.invisible.left),
                                               (priv->frame->rect.y +
                                                borders.invisible.top),
                                               &event.xconfigure.x,
                                               &event.xconfigure.y);
        }
      else
        {
          int dx, dy;

          /* Need to be in root window coordinates */
          meta_window_stage_to_protocol_point (window,
                                               priv->frame->rect.x,
                                               priv->frame->rect.y,
                                               &dx,
                                               &dy);
          event.xconfigure.x += dx;
          event.xconfigure.y += dy;
        }
    }
  meta_window_stage_to_protocol_point (window,
                                       priv->client_rect.width,
                                       priv->client_rect.height,
                                       &event.xconfigure.width,
                                       &event.xconfigure.height);
  meta_window_stage_to_protocol_point (window,
                                       priv->border_width, 0,
                                       &event.xconfigure.border_width, NULL);
  event.xconfigure.above = None; /* FIXME */
  event.xconfigure.override_redirect = False;

  meta_topic (META_DEBUG_GEOMETRY,
              "Sending synthetic configure notify to %s with x: %d y: %d w: %d h: %d",
              window->desc,
              event.xconfigure.x, event.xconfigure.y,
              event.xconfigure.width, event.xconfigure.height);

  mtk_x11_error_trap_push (x11_display->xdisplay);
  XSendEvent (x11_display->xdisplay,
              priv->xwindow,
              False, StructureNotifyMask, &event);
  mtk_x11_error_trap_pop (x11_display->xdisplay);
}

static void
adjust_for_gravity (MetaWindow   *window,
                    gboolean      coords_assume_border,
                    MetaGravity   gravity,
                    MtkRectangle *rect)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);
  int ref_x, ref_y;
  int bw;
  int child_x, child_y;
  int frame_width, frame_height;
  MetaFrameBorders borders;

  /* We're computing position to pass to window_move, which is
   * the position of the client window (META_GRAVITY_STATIC basically)
   *
   * (see WM spec description of gravity computation, but note that
   * their formulas assume we're honoring the border width, rather
   * than compensating for having turned it off)
   */

  if (gravity == META_GRAVITY_STATIC)
    return;

  if (coords_assume_border)
    bw = priv->border_width;
  else
    bw = 0;

  meta_frame_calc_borders (priv->frame, &borders);

  child_x = borders.visible.left;
  child_y = borders.visible.top;
  frame_width = child_x + rect->width + borders.visible.right;
  frame_height = child_y + rect->height + borders.visible.bottom;

  /* Calculate the the reference point, which is the corner of the
   * outer window specified by the gravity. So, META_GRAVITY_NORTH_EAST
   * would have the reference point as the top-right corner of the
   * outer window. */
  ref_x = rect->x;
  ref_y = rect->y;

  switch (gravity)
    {
    case META_GRAVITY_NORTH:
    case META_GRAVITY_CENTER:
    case META_GRAVITY_SOUTH:
      ref_x += rect->width / 2 + bw;
      break;
    case META_GRAVITY_NORTH_EAST:
    case META_GRAVITY_EAST:
    case META_GRAVITY_SOUTH_EAST:
      ref_x += rect->width + bw * 2;
      break;
    default:
      break;
    }

  switch (gravity)
    {
    case META_GRAVITY_WEST:
    case META_GRAVITY_CENTER:
    case META_GRAVITY_EAST:
      ref_y += rect->height / 2 + bw;
      break;
    case META_GRAVITY_SOUTH_WEST:
    case META_GRAVITY_SOUTH:
    case META_GRAVITY_SOUTH_EAST:
      ref_y += rect->height + bw * 2;
      break;
    default:
      break;
    }

  /* Find the top-left corner of the outer window from
   * the reference point. */

  rect->x = ref_x;
  rect->y = ref_y;

  switch (gravity)
    {
    case META_GRAVITY_NORTH:
    case META_GRAVITY_CENTER:
    case META_GRAVITY_SOUTH:
      rect->x -= frame_width / 2;
      break;
    case META_GRAVITY_NORTH_EAST:
    case META_GRAVITY_EAST:
    case META_GRAVITY_SOUTH_EAST:
      rect->x -= frame_width;
      break;
    default:
      break;
    }

  switch (gravity)
    {
    case META_GRAVITY_WEST:
    case META_GRAVITY_CENTER:
    case META_GRAVITY_EAST:
      rect->y -= frame_height / 2;
      break;
    case META_GRAVITY_SOUTH_WEST:
    case META_GRAVITY_SOUTH:
    case META_GRAVITY_SOUTH_EAST:
      rect->y -= frame_height;
      break;
    default:
      break;
    }

  /* Adjust to get the top-left corner of the inner window. */
  rect->x += child_x;
  rect->y += child_y;
}

static void
meta_window_x11_manage (MetaWindow *window)
{
  MetaDisplay *display = window->display;
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  meta_sync_counter_init (&priv->sync_counter, window, priv->xwindow);

  meta_x11_display_register_x_window (display->x11_display,
                                      &priv->xwindow,
                                      window);

  /* assign the window to its group, or create a new group if needed */
  priv->group = NULL;
  priv->xgroup_leader = None;
  meta_window_x11_compute_group (window);

  meta_window_load_initial_properties (window);

  if (!window->override_redirect)
    update_sm_hints (window); /* must come after transient_for */

  if (window->decorated)
    meta_window_ensure_frame (window);
  else
    meta_window_x11_initialize_state (window);
}

void
meta_window_x11_initialize_state (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  /* For override-redirect windows, save the client rect
   * directly. window->config->rect was assigned from the XWindowAttributes
   * in the main meta_window_shared_new.
   *
   * For normal windows, do a full ConfigureRequest based on the
   * window hints, as that's what the ICCCM says to do.
   */
  priv->client_rect = meta_window_config_get_rect (window->config);
  window->buffer_rect = meta_window_config_get_rect (window->config);

  if (!window->override_redirect)
    {
      MtkRectangle rect;
      MetaMoveResizeFlags flags;
      MetaSizeHintsFlags size_hints_flags;
      MetaGravity gravity = window->size_hints.win_gravity;
      MetaPlaceFlag place_flags = META_PLACE_FLAG_NONE;

      rect.x = window->size_hints.x;
      rect.y = window->size_hints.y;
      rect.width = window->size_hints.width;
      rect.height = window->size_hints.height;

      flags = (META_MOVE_RESIZE_CONFIGURE_REQUEST |
               META_MOVE_RESIZE_MOVE_ACTION |
               META_MOVE_RESIZE_RESIZE_ACTION |
               META_MOVE_RESIZE_CONSTRAIN);

      size_hints_flags = window->size_hints.flags;
      if (!(size_hints_flags & META_SIZE_HINTS_USER_POSITION) &&
          !meta_window_config_get_is_fullscreen (window->config))
        flags |= META_MOVE_RESIZE_RECT_INVALID;

      adjust_for_gravity (window, TRUE, gravity, &rect);
      meta_window_client_rect_to_frame_rect (window, &rect, &rect);

      meta_window_move_resize_internal (window, flags, place_flags, rect, NULL);
    }

  meta_window_x11_update_shape_region (window);
  meta_window_x11_update_input_region (window);
}

static void
meta_window_x11_unmanage (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  mtk_x11_error_trap_push (x11_display->xdisplay);

  meta_window_x11_destroy_sync_request_alarm (window);

  if (window->withdrawn)
    {
      /* We need to clean off the window's state so it
       * won't be restored if the app maps it again.
       */
      meta_topic (META_DEBUG_X11,
                  "Cleaning state from window %s", window->desc);
      XDeleteProperty (x11_display->xdisplay,
                       priv->xwindow,
                       x11_display->atom__NET_WM_DESKTOP);
      XDeleteProperty (x11_display->xdisplay,
                       priv->xwindow,
                       x11_display->atom__NET_WM_STATE);
      XDeleteProperty (x11_display->xdisplay,
                       priv->xwindow,
                       x11_display->atom__NET_WM_FULLSCREEN_MONITORS);
      meta_window_x11_set_wm_state (window);
    }
  else
    {
      /* We need to put WM_STATE so that others will understand it on
       * restart.
       */
      if (!window->minimized)
        meta_window_x11_set_wm_state (window);

      /* If we're unmanaging a window that is not withdrawn, then
       * either (a) mutter is exiting, in which case we need to map
       * the window so the next WM will know that it's not Withdrawn,
       * or (b) we want to create a new MetaWindow to replace the
       * current one, which will happen automatically if we re-map
       * the X Window.
       */
      XMapWindow (x11_display->xdisplay,
                  priv->xwindow);
    }

  meta_x11_display_unregister_x_window (x11_display, priv->xwindow);

  /* Put back anything we messed up */
  if (priv->border_width != 0)
    XSetWindowBorderWidth (x11_display->xdisplay,
                           priv->xwindow,
                           priv->border_width);

  /* No save set */
  XRemoveFromSaveSet (x11_display->xdisplay,
                      priv->xwindow);

  /* Even though the window is now unmanaged, we can't unselect events. This
   * window might be a window from this process, like a GdkMenu, in
   * which case it will have pointer events and so forth selected
   * for it by GDK. There's no way to disentangle those events from the events
   * we've selected. Even for a window from a different X client,
   * GDK could also have selected events for it for IPC purposes, so we
   * can't unselect in that case either.
   *
   * Similarly, we can't unselected for events on window->user_time_window.
   * It might be our own GDK focus window, or it might be a window that a
   * different client is using for multiple different things:
   * _NET_WM_USER_TIME_WINDOW and IPC, perhaps.
   */

  if (priv->user_time_window != None)
    {
      meta_x11_display_unregister_x_window (x11_display,
                                            priv->user_time_window);
      priv->user_time_window = None;
    }

  if (META_X11_DISPLAY_HAS_SHAPE (x11_display))
    XShapeSelectInput (x11_display->xdisplay, priv->xwindow, NoEventMask);

  mtk_x11_error_trap_pop (x11_display->xdisplay);

  if (priv->frame)
    {
      /* The XReparentWindow call in meta_window_destroy_frame() moves the
       * window so we need to send a configure notify; see bug 399552.  (We
       * also do this just in case a window got unmaximized.)
       */
      send_configure_notify (window);

      meta_window_destroy_frame (window);
    }

  meta_sync_counter_clear (&priv->sync_counter);
}

void
meta_window_x11_set_wm_ping (MetaWindow *window,
                             gboolean    ping)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv =
    meta_window_x11_get_instance_private (window_x11);

  priv->wm_ping = ping;
}

static gboolean
meta_window_x11_can_ping (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv =
    meta_window_x11_get_instance_private (window_x11);

  return priv->wm_ping;
}

static void
meta_window_x11_ping (MetaWindow *window,
                      guint32     serial)
{
  MetaDisplay *display = window->display;

  send_icccm_message (window, display->x11_display->atom__NET_WM_PING, serial);
}

void
meta_window_x11_set_wm_delete_window (MetaWindow *window,
                                      gboolean    delete_window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv =
    meta_window_x11_get_instance_private (window_x11);

  priv->wm_delete_window = delete_window;
}

static void
meta_window_x11_delete (MetaWindow *window,
                        guint32     timestamp)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv =
    meta_window_x11_get_instance_private (window_x11);
  MetaX11Display *x11_display = window->display->x11_display;

  mtk_x11_error_trap_push (x11_display->xdisplay);
  if (priv->wm_delete_window)
    {
      meta_topic (META_DEBUG_WINDOW_OPS,
                  "Deleting %s with delete_window request",
                  window->desc);
      send_icccm_message (window, x11_display->atom_WM_DELETE_WINDOW, timestamp);
    }
  else
    {
      meta_topic (META_DEBUG_WINDOW_OPS,
                  "Deleting %s with explicit kill",
                  window->desc);
      XKillClient (x11_display->xdisplay, priv->xwindow);
    }
  mtk_x11_error_trap_pop (x11_display->xdisplay);
}

static void
meta_window_x11_kill (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;

  meta_topic (META_DEBUG_WINDOW_OPS,
              "Disconnecting %s with XKillClient()",
              window->desc);

  mtk_x11_error_trap_push (x11_display->xdisplay);
  XKillClient (x11_display->xdisplay,
               meta_window_x11_get_xwindow (window));
  mtk_x11_error_trap_pop (x11_display->xdisplay);
}

static void
request_take_focus (MetaWindow *window,
                    guint32     timestamp)
{
  MetaDisplay *display = window->display;

  meta_topic (META_DEBUG_FOCUS, "WM_TAKE_FOCUS(%s, %u)",
              window->desc, timestamp);

  send_icccm_message (window, display->x11_display->atom_WM_TAKE_FOCUS, timestamp);
}

typedef struct
{
  MetaWindow *window;
  GQueue *pending_focus_candidates;
  guint32 timestamp;
  guint timeout_id;
  gulong unmanaged_id;
  gulong focused_changed_id;
} MetaWindowX11DelayedFocusData;

static void
disconnect_pending_focus_window_signals (MetaWindow *window,
                                         GQueue     *focus_candidates)
{
  g_signal_handlers_disconnect_by_func (window, g_queue_remove,
                                        focus_candidates);
}

static void
meta_window_x11_delayed_focus_data_free (MetaWindowX11DelayedFocusData *data)
{
  g_clear_signal_handler (&data->unmanaged_id, data->window);
  g_clear_signal_handler (&data->focused_changed_id, data->window->display);

  if (data->pending_focus_candidates)
    {
      g_queue_foreach (data->pending_focus_candidates,
                       (GFunc) disconnect_pending_focus_window_signals,
                       data->pending_focus_candidates);
      g_queue_free (data->pending_focus_candidates);
    }

  g_clear_handle_id (&data->timeout_id, g_source_remove);
  g_free (data);
}

static void
focus_candidates_maybe_take_and_focus_next (GQueue  **focus_candidates_ptr,
                                            guint32   timestamp)
{
  MetaWindow *focus_window;
  GQueue *focus_candidates;

  g_assert (*focus_candidates_ptr);

  if (g_queue_is_empty (*focus_candidates_ptr))
    return;

  focus_candidates = g_steal_pointer (focus_candidates_ptr);
  focus_window = g_queue_pop_head (focus_candidates);

  disconnect_pending_focus_window_signals (focus_window, focus_candidates);
  meta_window_x11_maybe_focus_delayed (focus_window, focus_candidates, timestamp);
}

static void
focus_window_delayed_unmanaged (gpointer user_data)
{
  MetaWindowX11DelayedFocusData *data = user_data;
  uint32_t timestamp = data->timestamp;

  focus_candidates_maybe_take_and_focus_next (&data->pending_focus_candidates,
                                              timestamp);

  meta_window_x11_delayed_focus_data_free (data);
}

static void
focus_window_delayed_timeout (gpointer user_data)
{
  MetaWindowX11DelayedFocusData *data = user_data;
  MetaWindow *window = data->window;
  guint32 timestamp = data->timestamp;

  focus_candidates_maybe_take_and_focus_next (&data->pending_focus_candidates,
                                              timestamp);

  data->timeout_id = 0;
  meta_window_x11_delayed_focus_data_free (data);

  meta_window_focus (window, timestamp);
}

static void
meta_window_x11_maybe_focus_delayed (MetaWindow *window,
                                     GQueue     *other_focus_candidates,
                                     guint32     timestamp)
{
  MetaWindowX11DelayedFocusData *data;

  data = g_new0 (MetaWindowX11DelayedFocusData, 1);
  data->window = window;
  data->timestamp = timestamp;
  data->pending_focus_candidates = other_focus_candidates;

  meta_topic (META_DEBUG_FOCUS,
              "Requesting delayed focus to %s", window->desc);

  data->unmanaged_id =
    g_signal_connect_swapped (window, "unmanaged",
                              G_CALLBACK (focus_window_delayed_unmanaged),
                              data);

  data->focused_changed_id =
    g_signal_connect_swapped (window->display, "notify::focus-window",
                              G_CALLBACK (meta_window_x11_delayed_focus_data_free),
                              data);

  data->timeout_id = g_timeout_add_once (TAKE_FOCUS_FALLBACK_DELAY_MS,
                                         focus_window_delayed_timeout, data);
}

static void
maybe_focus_default_window (MetaDisplay *display,
                            MetaWindow  *not_this_one,
                            guint32      timestamp)
{
  MetaWorkspace *workspace;
  g_autoptr (GList) focusable_windows = NULL;
  g_autoptr (GQueue) focus_candidates = NULL;
  GList *l;

  if (not_this_one && not_this_one->workspace)
    workspace = not_this_one->workspace;
  else
    workspace = display->workspace_manager->active_workspace;

   /* Go through all the focusable windows and try to focus them
    * in order, waiting for a delay. The first one that replies to
    * the request (in case of take focus windows) changing the display
    * focused window, will stop the chained requests.
    */
  focusable_windows =
    meta_workspace_get_default_focus_candidates (workspace);
  focus_candidates = g_queue_new ();

  for (l = g_list_last (focusable_windows); l; l = l->prev)
    {
      MetaWindow *focus_window = l->data;

      if (focus_window == not_this_one)
        continue;

      g_queue_push_tail (focus_candidates, focus_window);
      g_signal_connect_swapped (focus_window, "unmanaged",
                                G_CALLBACK (g_queue_remove),
                                focus_candidates);

      if (!META_IS_WINDOW_X11 (focus_window))
        break;

      if (focus_window->input)
        break;
    }

  focus_candidates_maybe_take_and_focus_next (&focus_candidates, timestamp);
}

static void
meta_window_x11_focus (MetaWindow *window,
                       guint32     timestamp)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv =
    meta_window_x11_get_instance_private (window_x11);
  gboolean is_output_only_with_frame;

  is_output_only_with_frame =
    priv->frame && !meta_window_is_focusable (window);

  if (window->input || is_output_only_with_frame)
    {
      if (window->input)
        {
          meta_topic (META_DEBUG_FOCUS,
                      "Setting input focus on %s since input = true",
                      window->desc);
        }
      else if (is_output_only_with_frame)
        {
          meta_topic (META_DEBUG_FOCUS,
                      "Focusing frame of %s", window->desc);
        }

      meta_display_set_input_focus (window->display,
                                    window,
                                    timestamp);
    }

  if (priv->wm_take_focus)
    {
      meta_topic (META_DEBUG_FOCUS,
                  "Sending WM_TAKE_FOCUS to %s since take_focus = true",
                  window->desc);

      if (!window->input)
        {
          /* The "Globally Active Input" window case, where the window
           * doesn't want us to call XSetInputFocus on it, but does
           * want us to send a WM_TAKE_FOCUS.
           *
           * Normally, we want to just leave the focus undisturbed until
           * the window responds to WM_TAKE_FOCUS, but if we're unmanaging
           * the current focus window we *need* to move the focus away, so
           * we focus the no focus window before sending WM_TAKE_FOCUS,
           * and eventually the default focus window excluding this one,
           * if meanwhile we don't get any focus request.
           */
          if (window->display->focus_window != NULL &&
              window->display->focus_window->unmanaging)
            {
              meta_display_unset_input_focus (window->display, timestamp);
              maybe_focus_default_window (window->display, window,
                                          timestamp);
            }
        }

      request_take_focus (window, timestamp);
    }
}

static void
meta_window_get_client_root_coords (MetaWindow   *window,
                                    MtkRectangle *rect)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  *rect = priv->client_rect;

  if (priv->frame)
    {
      rect->x += priv->frame->rect.x;
      rect->y += priv->frame->rect.y;
    }
}

static void
meta_window_refresh_resize_popup (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  if (priv->showing_resize_popup)
    {
      MtkRectangle rect;
      int display_w, display_h;

      meta_window_get_client_root_coords (window, &rect);

      display_w = (rect.width - window->size_hints.base_width);
      if (window->size_hints.width_inc > 0)
        display_w /= window->size_hints.width_inc;

      display_h = (rect.height - window->size_hints.base_height);
      if (window->size_hints.height_inc > 0)
        display_h /= window->size_hints.height_inc;

      meta_display_show_resize_popup (window->display, TRUE, &rect, display_w, display_h);
    }
  else
    {
      meta_display_show_resize_popup (window->display, FALSE, NULL, 0, 0);
    }
}

static void
meta_window_x11_grab_op_began (MetaWindow *window,
                               MetaGrabOp  op)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  if (meta_grab_op_is_resizing (op))
    {
      meta_window_x11_create_sync_request_alarm (window);

      if (window->size_hints.width_inc > 2 || window->size_hints.height_inc > 2)
        {
          priv->showing_resize_popup = TRUE;
          meta_window_refresh_resize_popup (window);
        }
    }

  META_WINDOW_CLASS (meta_window_x11_parent_class)->grab_op_began (window, op);
}

static void
meta_window_x11_grab_op_ended (MetaWindow *window,
                               MetaGrabOp  op)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  if (priv->showing_resize_popup)
    {
      priv->showing_resize_popup = FALSE;
      meta_window_refresh_resize_popup (window);
    }

  META_WINDOW_CLASS (meta_window_x11_parent_class)->grab_op_ended (window, op);
}

static void
update_net_frame_extents (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);
  MetaX11Display *x11_display = window->display->x11_display;
  int left, right, top, bottom;
  unsigned long data[4];
  MetaFrameBorders borders;
  Window xwindow = meta_window_x11_get_xwindow (window);

  meta_frame_calc_borders (priv->frame, &borders);
  meta_window_stage_to_protocol_point (window,
                                       borders.visible.left,
                                       borders.visible.right,
                                       &left,
                                       &right);
  meta_window_stage_to_protocol_point (window,
                                       borders.visible.top,
                                       borders.visible.bottom,
                                       &top,
                                       &bottom);

  data[0] = left;
  data[1] = right;
  data[2] = top;
  data[3] = bottom;

  meta_topic (META_DEBUG_GEOMETRY,
              "Setting _NET_FRAME_EXTENTS on managed window 0x%lx "
              "to left = %lu, right = %lu, top = %lu, bottom = %lu",
              xwindow, data[0], data[1], data[2], data[3]);

  mtk_x11_error_trap_push (x11_display->xdisplay);
  XChangeProperty (x11_display->xdisplay, xwindow,
                   x11_display->atom__NET_FRAME_EXTENTS,
                   XA_CARDINAL,
                   32, PropModeReplace, (guchar*) data, 4);
  mtk_x11_error_trap_pop (x11_display->xdisplay);
}

static gboolean
is_edge_constraint_resizable (MetaEdgeConstraint constraint)
{
  switch (constraint)
    {
    case META_EDGE_CONSTRAINT_NONE:
    case META_EDGE_CONSTRAINT_WINDOW:
      return TRUE;
    case META_EDGE_CONSTRAINT_MONITOR:
      return FALSE;
    }

  g_assert_not_reached ();
  return FALSE;
}

static gboolean
is_edge_constraint_tiled (MetaEdgeConstraint constraint)
{
  switch (constraint)
    {
    case META_EDGE_CONSTRAINT_NONE:
      return FALSE;
    case META_EDGE_CONSTRAINT_WINDOW:
    case META_EDGE_CONSTRAINT_MONITOR:
      return TRUE;
    }

  g_assert_not_reached ();
  return FALSE;
}

static unsigned long
edge_constraints_to_gtk_edge_constraints (MetaWindow *window)
{
  unsigned long gtk_edge_constraints = 0;

  if (is_edge_constraint_tiled (window->edge_constraints.top))
    gtk_edge_constraints |= META_GTK_EDGE_CONSTRAINT_TOP_TILED;
  if (is_edge_constraint_resizable (window->edge_constraints.top))
    gtk_edge_constraints |= META_GTK_EDGE_CONSTRAINT_TOP_RESIZABLE;

  if (is_edge_constraint_tiled (window->edge_constraints.right))
    gtk_edge_constraints |= META_GTK_EDGE_CONSTRAINT_RIGHT_TILED;
  if (is_edge_constraint_resizable (window->edge_constraints.right))
    gtk_edge_constraints |= META_GTK_EDGE_CONSTRAINT_RIGHT_RESIZABLE;

  if (is_edge_constraint_tiled (window->edge_constraints.bottom))
    gtk_edge_constraints |= META_GTK_EDGE_CONSTRAINT_BOTTOM_TILED;
  if (is_edge_constraint_resizable (window->edge_constraints.bottom))
    gtk_edge_constraints |= META_GTK_EDGE_CONSTRAINT_BOTTOM_RESIZABLE;

  if (is_edge_constraint_tiled (window->edge_constraints.left))
    gtk_edge_constraints |= META_GTK_EDGE_CONSTRAINT_LEFT_TILED;
  if (is_edge_constraint_resizable (window->edge_constraints.left))
    gtk_edge_constraints |= META_GTK_EDGE_CONSTRAINT_LEFT_RESIZABLE;

  return gtk_edge_constraints;
}

static void
update_gtk_edge_constraints (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);
  MetaX11Display *x11_display = window->display->x11_display;
  unsigned long data[1];

  data[0] = edge_constraints_to_gtk_edge_constraints (window);

  meta_topic (META_DEBUG_X11,
              "Setting _GTK_EDGE_CONSTRAINTS to %lu", data[0]);

  mtk_x11_error_trap_push (x11_display->xdisplay);
  XChangeProperty (x11_display->xdisplay,
                   priv->frame ? priv->frame->xwindow : meta_window_x11_get_xwindow (window),
                   x11_display->atom__GTK_EDGE_CONSTRAINTS,
                   XA_CARDINAL, 32, PropModeReplace,
                   (guchar*) data, 1);
  mtk_x11_error_trap_pop (x11_display->xdisplay);
}

static unsigned long
meta_window_get_net_wm_desktop (MetaWindow *window)
{
  if (window->on_all_workspaces)
    return 0xFFFFFFFF;
  else
    return meta_workspace_index (window->workspace);
}

static void
meta_window_x11_current_workspace_changed (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;
  /* FIXME if on more than one workspace, we claim to be "sticky",
   * the WM spec doesn't say what to do here.
   */
  unsigned long data[1];

  if (window->unmanaging)
    return;

  data[0] = meta_window_get_net_wm_desktop (window);

  meta_topic (META_DEBUG_X11,
              "Setting _NET_WM_DESKTOP of %s to %lu",
              window->desc, data[0]);

  mtk_x11_error_trap_push (x11_display->xdisplay);
  XChangeProperty (x11_display->xdisplay,
                   meta_window_x11_get_xwindow (window),
                   x11_display->atom__NET_WM_DESKTOP,
                   XA_CARDINAL,
                   32, PropModeReplace, (guchar*) data, 1);
  mtk_x11_error_trap_pop (x11_display->xdisplay);
}

static gboolean
meta_window_x11_can_freeze_commits (MetaWindow *window)
{
  MetaWindowActor *window_actor;

  window_actor = meta_window_actor_from_window (window);
  if (window_actor == NULL)
    return FALSE;

  return meta_window_actor_can_freeze_commits (window_actor);
}

static void
meta_window_x11_move_resize_internal (MetaWindow                *window,
                                      MtkRectangle               unconstrained_rect,
                                      MtkRectangle               constrained_rect,
                                      MtkRectangle               intermediate_rect,
                                      int                        rel_x,
                                      int                        rel_y,
                                      MetaMoveResizeFlags        flags,
                                      MetaMoveResizeResultFlags *result)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);
  MetaFrameBorders borders;
  MtkRectangle client_rect;
  MtkRectangle values_rect;
  int size_dx, size_dy;
  XWindowChanges values;
  unsigned int mask;
  gboolean need_configure_notify;
  gboolean need_move_client = FALSE;
  gboolean need_move_frame = FALSE;
  gboolean need_resize_client = FALSE;
  gboolean need_resize_frame = FALSE;
  gboolean configure_frame_first;
  gboolean is_configure_request;
  MetaWindowDrag *window_drag;
  MtkRectangle frame_rect;

  is_configure_request = (flags & META_MOVE_RESIZE_CONFIGURE_REQUEST) != 0;

  meta_frame_calc_borders (priv->frame, &borders);

  frame_rect = meta_window_config_get_rect (window->config);
  size_dx = constrained_rect.width - frame_rect.width;
  size_dy = constrained_rect.height - frame_rect.height;

  meta_window_config_set_rect (window->config, constrained_rect);
  frame_rect = meta_window_config_get_rect (window->config);

  if (priv->frame)
    {
      int new_w, new_h;
      int new_x, new_y;

      /* Compute new frame size */
      new_w = frame_rect.width + borders.invisible.left + borders.invisible.right;
      new_h = frame_rect.height + borders.invisible.top + borders.invisible.bottom;

      if (new_w != priv->frame->rect.width ||
          new_h != priv->frame->rect.height)
        {
          need_resize_frame = TRUE;
          priv->frame->rect.width = new_w;
          priv->frame->rect.height = new_h;
        }

      /* Compute new frame coords */
      new_x = frame_rect.x - borders.invisible.left;
      new_y = frame_rect.y - borders.invisible.top;

      if (new_x != priv->frame->rect.x ||
          new_y != priv->frame->rect.y)
        {
          need_move_frame = TRUE;
          priv->frame->rect.x = new_x;
          priv->frame->rect.y = new_y;
        }
    }

  /* Calculate the new client rect */
  meta_window_frame_rect_to_client_rect (window, &constrained_rect, &client_rect);

  /* The above client_rect is in root window coordinates. The
   * values we need to pass to XConfigureWindow are in parent
   * coordinates, so if the window is in a frame, we need to
   * correct the x/y positions here. */
  if (priv->frame)
    {
      client_rect.x = borders.total.left;
      client_rect.y = borders.total.top;
    }

  if (client_rect.x != priv->client_rect.x ||
      client_rect.y != priv->client_rect.y)
    {
      need_move_client = TRUE;
      priv->client_rect.x = client_rect.x;
      priv->client_rect.y = client_rect.y;
    }

  if (client_rect.width != priv->client_rect.width ||
      client_rect.height != priv->client_rect.height)
    {
      need_resize_client = TRUE;
      priv->client_rect.width = client_rect.width;
      priv->client_rect.height = client_rect.height;
    }

  /* If frame extents have changed, fill in other frame fields and
     change frame's extents property. */
  if (priv->frame &&
      (priv->frame->child_x != borders.total.left ||
       priv->frame->child_y != borders.total.top ||
       priv->frame->right_width != borders.total.right ||
       priv->frame->bottom_height != borders.total.bottom))
    {
      priv->frame->child_x = borders.total.left;
      priv->frame->child_y = borders.total.top;
      priv->frame->right_width = borders.total.right;
      priv->frame->bottom_height = borders.total.bottom;

      update_net_frame_extents (window);
    }

  /* See ICCCM 4.1.5 for when to send ConfigureNotify */

  need_configure_notify = FALSE;

  /* If this is a configure request and we change nothing, then we
   * must send configure notify.
   */
  if  (is_configure_request &&
       !(need_move_client || need_move_frame ||
         need_resize_client || need_resize_frame ||
         priv->border_width != 0))
    need_configure_notify = TRUE;

  /* We must send configure notify if we move but don't resize, since
   * the client window may not get a real event
   */
  if ((need_move_client || need_move_frame) &&
      !(need_resize_client || need_resize_frame))
    need_configure_notify = TRUE;

  /* MapRequest events with a PROGRAM_POSITION or USER_POSITION hint with a frame
   * are moved by mutter without resizing; send a configure notify
   * in such cases.  See #322840.  (Note that window->constructing is
   * only true iff this call is due to a MapRequest, and when
   * PROGRAM_POSITION/USER_POSITION hints aren't set, mutter seems to send a
   * ConfigureNotify anyway due to the above code.)
   */
  if (window->constructing && priv->frame &&
      ((window->size_hints.flags & META_SIZE_HINTS_PROGRAM_POSITION) ||
       (window->size_hints.flags & META_SIZE_HINTS_USER_POSITION)))
    need_configure_notify = TRUE;

  /* If resizing, freeze commits - This is for Xwayland, and a no-op on Xorg */
  if (need_resize_client || need_resize_frame)
    {
      if (meta_window_x11_can_freeze_commits (window) &&
          !meta_window_x11_should_thaw_after_paint (window))
        {
          meta_window_x11_set_thaw_after_paint (window, TRUE);
          meta_window_x11_freeze_commits (window);
        }
    }

  /* The rest of this function syncs our new size/pos with X as
   * efficiently as possible
   */

  /* For nice effect, when growing the window we want to move/resize
   * the frame first, when shrinking the window we want to move/resize
   * the client first. If we grow one way and shrink the other,
   * see which way we're moving "more"
   *
   * Mail from Owen subject "Suggestion: Gravity and resizing from the left"
   * http://mail.gnome.org/archives/wm-spec-list/1999-November/msg00088.html
   *
   * An annoying fact you need to know in this code is that META_GRAVITY_STATIC
   * does nothing if you _only_ resize or _only_ move the frame;
   * it must move _and_ resize, otherwise you get META_GRAVITY_NORTH_WEST
   * behavior. The move and resize must actually occur, it is not
   * enough to set CWX | CWWidth but pass in the current size/pos.
   */

  /* Normally, we configure the frame first depending on whether
   * we grow the frame more than we shrink. The idea is to avoid
   * messing up the window contents by having a temporary situation
   * where the frame is smaller than the window. However, if we're
   * cooperating with the client to create an atomic frame update,
   * and the window is redirected, then we should always update
   * the frame first, since updating the frame will force a new
   * backing pixmap to be allocated, and the old backing pixmap
   * will be left undisturbed for us to paint to the screen until
   * the client finishes redrawing.
   */
  if (priv->sync_counter.extended_sync_request_counter)
    configure_frame_first = TRUE;
  else
    configure_frame_first = size_dx + size_dy >= 0;

  meta_window_stage_to_protocol_rect (window, &client_rect, &values_rect);
  values.border_width = 0;
  values.x = values_rect.x;
  values.y = values_rect.y;
  values.width = values_rect.width;
  values.height = values_rect.height;

  mask = 0;
  if (is_configure_request && priv->border_width != 0)
    mask |= CWBorderWidth; /* must force to 0 */
  if (need_move_client)
    mask |= (CWX | CWY);
  if (need_resize_client)
    mask |= (CWWidth | CWHeight);

  mtk_x11_error_trap_push (window->display->x11_display->xdisplay);

  window_drag =
    meta_compositor_get_current_window_drag (window->display->compositor);

  if (mask != 0 &&
      window_drag &&
      window == meta_window_drag_get_window (window_drag) &&
      meta_grab_op_is_resizing (meta_window_drag_get_grab_op (window_drag)))
    {
      meta_sync_counter_send_request (&priv->sync_counter);
      if (priv->frame)
        meta_sync_counter_send_request (meta_frame_get_sync_counter (priv->frame));
    }

  if (configure_frame_first && priv->frame)
    meta_frame_sync_to_window (priv->frame, need_resize_frame);

  if (mask != 0)
    {
      XConfigureWindow (window->display->x11_display->xdisplay,
                        priv->xwindow,
                        mask,
                        &values);
    }

  if (!configure_frame_first && priv->frame)
    meta_frame_sync_to_window (priv->frame, need_resize_frame);

  mtk_x11_error_trap_pop (window->display->x11_display->xdisplay);

  if (priv->frame)
    window->buffer_rect = priv->frame->rect;
  else
    window->buffer_rect = client_rect;

  if (need_configure_notify)
    send_configure_notify (window);

  if (priv->showing_resize_popup)
    meta_window_refresh_resize_popup (window);

  if (need_move_client || need_move_frame)
    *result |= META_MOVE_RESIZE_RESULT_MOVED;
  if (need_resize_client || need_resize_frame)
    *result |= META_MOVE_RESIZE_RESULT_RESIZED;
  if (flags & META_MOVE_RESIZE_STATE_CHANGED)
    *result |= META_MOVE_RESIZE_RESULT_STATE_CHANGED;
  *result |= META_MOVE_RESIZE_RESULT_UPDATE_UNCONSTRAINED;

  update_gtk_edge_constraints (window);
}

static gboolean
meta_window_x11_update_struts (MetaWindow *window)
{
  g_autoslist (MetaStrut) old_struts = NULL;
  GSList *new_struts;
  GSList *old_iter, *new_iter;
  uint32_t *struts = NULL;
  int nitems;
  gboolean changed;

  g_return_val_if_fail (!window->override_redirect, FALSE);

  meta_topic (META_DEBUG_X11, "Updating struts for %s", window->desc);

  Window xwindow = meta_window_x11_get_xwindow (window);
  old_struts = g_steal_pointer (&window->struts);
  new_struts = NULL;

  if (meta_prop_get_cardinal_list (window->display->x11_display,
                                   xwindow,
                                   window->display->x11_display->atom__NET_WM_STRUT_PARTIAL,
                                   &struts, &nitems))
    {
      if (nitems != 12)
        {
          meta_topic (META_DEBUG_X11,
                      "_NET_WM_STRUT_PARTIAL on %s has %d values instead of 12.",
                      window->desc, nitems);
        }
      else
        {
          /* Pull out the strut info for each side in the hint */
          int i;
          for (i=0; i<4; i++)
            {
              MetaStrut *temp;
              int thickness, strut_begin, strut_end;

              thickness = struts[i];
              if (thickness == 0)
                continue;
              strut_begin = struts[4+(i*2)];
              strut_end   = struts[4+(i*2)+1];

              meta_window_protocol_to_stage_point (window,
                                                   strut_begin, 0,
                                                   &strut_begin, NULL,
                                                   MTK_ROUNDING_STRATEGY_SHRINK);
              meta_window_protocol_to_stage_point (window,
                                                   strut_end,
                                                   thickness,
                                                   &strut_end,
                                                   &thickness,
                                                   MTK_ROUNDING_STRATEGY_GROW);

              temp = g_new0 (MetaStrut, 1);
              temp->side = 1 << i; /* See MetaSide def.  Matches nicely, eh? */
              meta_display_get_size (window->display,
                                     &temp->rect.width, &temp->rect.height);
              switch (temp->side)
                {
                case META_SIDE_RIGHT:
                  temp->rect.x = BOX_RIGHT(temp->rect) - thickness;
                  G_GNUC_FALLTHROUGH;
                case META_SIDE_LEFT:
                  temp->rect.width  = thickness;
                  temp->rect.y      = strut_begin;
                  temp->rect.height = strut_end - strut_begin + 1;
                  break;
                case META_SIDE_BOTTOM:
                  temp->rect.y = BOX_BOTTOM(temp->rect) - thickness;
                  G_GNUC_FALLTHROUGH;
                case META_SIDE_TOP:
                  temp->rect.height = thickness;
                  temp->rect.x      = strut_begin;
                  temp->rect.width  = strut_end - strut_begin + 1;
                  break;
                default:
                  g_assert_not_reached ();
                }

              new_struts = g_slist_prepend (new_struts, temp);
            }

          meta_topic (META_DEBUG_X11,
                      "_NET_WM_STRUT_PARTIAL struts %u %u %u %u for "
                      "window %s",
                      struts[0], struts[1], struts[2], struts[3],
                      window->desc);
        }
      g_free (struts);
    }
  else
    {
      meta_topic (META_DEBUG_X11,
                  "No _NET_WM_STRUT property for %s",
                  window->desc);
    }

  if (!new_struts &&
      meta_prop_get_cardinal_list (window->display->x11_display,
                                   xwindow,
                                   window->display->x11_display->atom__NET_WM_STRUT,
                                   &struts, &nitems))
    {
      if (nitems != 4)
        meta_topic (META_DEBUG_X11,
                    "_NET_WM_STRUT on %s has %d values instead of 4",
                    window->desc, nitems);
      else
        {
          /* Pull out the strut info for each side in the hint */
          int i;
          for (i=0; i<4; i++)
            {
              MetaStrut *temp;
              int thickness;

              thickness = struts[i];
              if (thickness == 0)
                continue;

              meta_window_protocol_to_stage_point (window,
                                                   thickness, 0,
                                                   &thickness, NULL,
                                                   MTK_ROUNDING_STRATEGY_GROW);

              temp = g_new0 (MetaStrut, 1);
              temp->side = 1 << i;
              meta_display_get_size (window->display,
                                     &temp->rect.width, &temp->rect.height);
              switch (temp->side)
                {
                case META_SIDE_RIGHT:
                  temp->rect.x = BOX_RIGHT(temp->rect) - thickness;
                  G_GNUC_FALLTHROUGH;
                case META_SIDE_LEFT:
                  temp->rect.width  = thickness;
                  break;
                case META_SIDE_BOTTOM:
                  temp->rect.y = BOX_BOTTOM(temp->rect) - thickness;
                  G_GNUC_FALLTHROUGH;
                case META_SIDE_TOP:
                  temp->rect.height = thickness;
                  break;
                default:
                  g_assert_not_reached ();
                }

              new_struts = g_slist_prepend (new_struts, temp);
            }

          meta_topic (META_DEBUG_X11,
                      "_NET_WM_STRUT struts %u %u %u %u for window %s",
                      struts[0], struts[1], struts[2], struts[3],
                      window->desc);
        }
      g_free (struts);
    }
  else if (!new_struts)
    {
      meta_topic (META_DEBUG_X11, "No _NET_WM_STRUT property for %s",
                  window->desc);
    }

  /* Determine whether old_struts and new_struts are the same */
  old_iter = old_struts;
  new_iter = new_struts;
  while (old_iter && new_iter)
    {
      MetaStrut *old_strut = (MetaStrut*) old_iter->data;
      MetaStrut *new_strut = (MetaStrut*) new_iter->data;

      if (old_strut->side != new_strut->side ||
          !mtk_rectangle_equal (&old_strut->rect, &new_strut->rect))
        break;

      old_iter = old_iter->next;
      new_iter = new_iter->next;
    }
  changed = (old_iter != NULL || new_iter != NULL);

  /* Update appropriately */
  window->struts = new_struts;
  return changed;
}

static void
meta_window_x11_get_default_skip_hints (MetaWindow *window,
                                        gboolean   *skip_taskbar_out,
                                        gboolean   *skip_pager_out)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  *skip_taskbar_out = priv->wm_state_skip_taskbar;
  *skip_pager_out = priv->wm_state_skip_pager;
}

static void
meta_window_x11_update_main_monitor (MetaWindow                   *window,
                                     MetaWindowUpdateMonitorFlags  flags)
{
  g_set_object (&window->monitor,
                meta_window_find_monitor_from_frame_rect (window));
}

static void
meta_window_x11_main_monitor_changed (MetaWindow               *window,
                                      const MetaLogicalMonitor *old)
{
}

static pid_t
meta_window_x11_get_client_pid (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;
  xcb_connection_t *xcb = XGetXCBConnection (x11_display->xdisplay);
  xcb_res_client_id_spec_t spec = { 0 };
  xcb_res_query_client_ids_cookie_t cookie;
  xcb_res_query_client_ids_reply_t *reply = NULL;

  spec.client = meta_window_x11_get_xwindow (window);
  spec.mask = XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID;

  cookie = xcb_res_query_client_ids (xcb, 1, &spec);
  reply = xcb_res_query_client_ids_reply (xcb, cookie, NULL);

  if (reply == NULL)
    return 0;

  uint32_t pid = 0, *value;
  xcb_res_client_id_value_iterator_t it;
  for (it = xcb_res_query_client_ids_ids_iterator (reply);
       it.rem;
       xcb_res_client_id_value_next (&it))
    {
      spec = it.data->spec;
      if (spec.mask & XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID)
        {
          value = xcb_res_client_id_value_value (it.data);
          pid = *value;
          break;
        }
    }

  free (reply);
  return (pid_t) pid;
}

static void
meta_window_x11_force_restore_shortcuts (MetaWindow         *window,
                                         ClutterInputDevice *source)
{
  /*
   * Not needed on X11 because clients can use a keyboard grab
   * to bypass the compositor shortcuts.
   */
}

static gboolean
meta_window_x11_shortcuts_inhibited (MetaWindow         *window,
                                     ClutterInputDevice *source)
{
  /*
   * On X11, we don't use a shortcuts inhibitor, clients just grab
   * the keyboard.
   */
  return FALSE;
}

void
meta_window_x11_set_wm_take_focus (MetaWindow *window,
                                   gboolean    take_focus)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv =
    meta_window_x11_get_instance_private (window_x11);

  priv->wm_take_focus = take_focus;
}

static gboolean
meta_window_x11_is_focusable (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv =
    meta_window_x11_get_instance_private (window_x11);

  return window->input || priv->wm_take_focus;
}

static gboolean
meta_window_x11_is_stackable (MetaWindow *window)
{
  return !window->override_redirect;
}

static gboolean
meta_window_x11_are_updates_frozen (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  if (priv->frame &&
      meta_sync_counter_is_waiting (meta_frame_get_sync_counter (priv->frame)))
    return TRUE;

  return meta_sync_counter_is_waiting (&priv->sync_counter);
}

/* Get layer ignoring any transient or group relationships */
static MetaStackLayer
get_standalone_layer (MetaWindow *window)
{
  MetaStackLayer layer;

  switch (window->type)
    {
    case META_WINDOW_DROPDOWN_MENU:
    case META_WINDOW_POPUP_MENU:
    case META_WINDOW_TOOLTIP:
    case META_WINDOW_NOTIFICATION:
    case META_WINDOW_COMBO:
    case META_WINDOW_OVERRIDE_OTHER:
      layer = META_LAYER_OVERRIDE_REDIRECT;
      break;

    default:
      layer = meta_window_get_default_layer (window);
      break;
    }

  return layer;
}

/* Note that this function can never use window->layer only
 * get_standalone_layer, or we'd have issues.
 */
static MetaStackLayer
get_maximum_layer_in_group (MetaWindow *window)
{
  GSList *members;
  MetaGroup *group;
  GSList *tmp;
  MetaStackLayer max;
  MetaStackLayer layer;

  max = META_LAYER_DESKTOP;

  group = meta_window_x11_get_group (window);

  if (group != NULL)
    members = meta_group_list_windows (group);
  else
    members = NULL;

  tmp = members;
  while (tmp != NULL)
    {
      MetaWindow *w = tmp->data;

      if (!w->override_redirect)
        {
          layer = get_standalone_layer (w);
          if (layer > max)
            max = layer;
        }

      tmp = tmp->next;
    }

  g_slist_free (members);

  return max;
}

static MetaStackLayer
meta_window_x11_calculate_layer (MetaWindow *window)
{
  MetaStackLayer layer = get_standalone_layer (window);

  /* We can only do promotion-due-to-group for dialogs and other
   * transients, or weird stuff happens like the desktop window and
   * nautilus windows getting in the same layer, or all gnome-terminal
   * windows getting in fullscreen layer if any terminal is
   * fullscreen.
   */
  if (layer != META_LAYER_DESKTOP &&
      meta_window_has_transient_type (window) &&
      window->transient_for == NULL)
    {
      /* We only do the group thing if the dialog is NOT transient for
       * a particular window. Imagine a group with a normal window, a dock,
       * and a dialog transient for the normal window; you don't want the dialog
       * above the dock if it wouldn't normally be.
       */

      MetaStackLayer group_max;

      group_max = get_maximum_layer_in_group (window);

      if (group_max > layer)
        {
          meta_topic (META_DEBUG_STACK,
                      "Promoting window %s from layer %u to %u due to group membership",
                      window->desc, layer, group_max);
          layer = group_max;
        }
    }

  meta_topic (META_DEBUG_STACK,
              "Window %s on layer %u type = %u has_focus = %d",
              window->desc, layer,
              window->type, window->has_focus);
  return layer;
}

static void
meta_window_x11_impl_freeze_commits (MetaWindow *window)
{
}

static void
meta_window_x11_impl_thaw_commits (MetaWindow *window)
{
}

static void
on_mapped_changed (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;

  if (window->mapped)
    {
      mtk_x11_error_trap_push (x11_display->xdisplay);
      XMapWindow (x11_display->xdisplay,
                  meta_window_x11_get_xwindow (window));
      mtk_x11_error_trap_pop (x11_display->xdisplay);
    }
  else
    {
      mtk_x11_error_trap_push (x11_display->xdisplay);
      XUnmapWindow (x11_display->xdisplay,
                    meta_window_x11_get_xwindow (window));
      mtk_x11_error_trap_pop (x11_display->xdisplay);
      window->unmaps_pending ++;
    }
}

static gboolean
meta_window_x11_impl_always_update_shape (MetaWindow *window)
{
  return FALSE;
}

static gboolean
meta_window_x11_is_focus_async (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv =
    meta_window_x11_get_instance_private (window_x11);

  return !window->input && priv->wm_take_focus;
}

static gboolean
meta_window_x11_set_transient_for (MetaWindow *window,
                                   MetaWindow *parent)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv =
    meta_window_x11_get_instance_private (window_x11);
  Window xtransient_for;

  meta_window_x11_recalc_window_type (window);
  if (!window->constructing)
    {
      /* If the window attaches, detaches, or changes attached
        * parents, we need to destroy the MetaWindow and let a new one
        * be created (which happens as a side effect of
        * meta_window_unmanage()). The condition below is correct
        * because we know window->transient_for has changed.
        */
      if (window->attached || meta_window_should_attach_to_parent (window))
        {
          guint32 timestamp;

          timestamp =
            meta_display_get_current_time_roundtrip (window->display);
          meta_window_unmanage (window, timestamp);
          return FALSE;
        }
    }

  /* possibly change its group. We treat being a window's transient as
   * equivalent to making it your group leader, to work around shortcomings
   * in programs such as xmms-- see #328211.
   */
  xtransient_for = meta_window_x11_get_xtransient_for (window);
  if (xtransient_for != None &&
      priv->xgroup_leader != None &&
      xtransient_for != priv->xgroup_leader)
    meta_window_x11_group_leader_changed (window);

  return TRUE;
}

static MetaGravity
meta_window_x11_get_gravity (MetaWindow *window)
{
  MetaGravity gravity;

  gravity = META_WINDOW_CLASS (meta_window_x11_parent_class)->get_gravity (window);
  if (gravity == META_GRAVITY_NONE)
    gravity = window->size_hints.win_gravity;

  return gravity;
}

static void
meta_window_x11_save_rect (MetaWindow *window)
{
  MtkRectangle rect;
  MetaWindowDrag *window_drag;

  if (!meta_window_config_is_floating (window->config))
    return;

  window_drag =
    meta_compositor_get_current_window_drag (window->display->compositor);
  if (window_drag &&
      meta_window_drag_get_window (window_drag) == window)
    return;

  rect = meta_window_config_get_rect (window->config);

  if (!meta_window_config_is_maximized_horizontally (window->config))
    {
      window->saved_rect.x = rect.x;
      window->saved_rect.width = rect.width;
    }
  if (!meta_window_config_is_maximized_vertically (window->config))
    {
      window->saved_rect.y = rect.y;
      window->saved_rect.height = rect.height;
    }
}

gboolean
meta_window_x11_is_ssd (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv =
    meta_window_x11_get_instance_private (window_x11);

  return priv->frame != NULL;
}

static void
meta_window_x11_constructed (GObject *object)
{
  MetaWindow *window = META_WINDOW (object);
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (object);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);
  XWindowAttributes attrs = priv->attributes;
  MtkRectangle rect;

  meta_topic (META_DEBUG_X11,
              "attrs->map_state = %d (%s)",
              attrs.map_state,
              (attrs.map_state == IsUnmapped) ?
              "IsUnmapped" :
              (attrs.map_state == IsViewable) ?
              "IsViewable" :
              (attrs.map_state == IsUnviewable) ?
              "IsUnviewable" :
              "(unknown)");

  window->client_type = META_WINDOW_CLIENT_TYPE_X11;
  window->override_redirect = attrs.override_redirect;

  rect = MTK_RECTANGLE_INIT (attrs.x, attrs.y, attrs.width, attrs.height);
  meta_window_protocol_to_stage_rect (window, &rect, &rect);

  window->config = meta_window_config_new ();
  meta_window_config_set_rect (window->config, rect);

  /* size_hints are the "request" */
  window->size_hints.x = rect.x;
  window->size_hints.y = rect.y;
  window->size_hints.width = rect.width;
  window->size_hints.height = rect.height;

  window->depth = attrs.depth;
  priv->xvisual = attrs.visual;
  window->mapped = attrs.map_state != IsUnmapped;

  priv->user_time_window = None;

  priv->frame = NULL;
  window->decorated = TRUE;
  window->hidden = FALSE;
  priv->xclient_leader = None;

  meta_window_protocol_to_stage_point (window,
                                       attrs.border_width, 0,
                                       &priv->border_width, NULL,
                                       MTK_ROUNDING_STRATEGY_GROW);

  g_signal_connect (window, "notify::decorated",
                    G_CALLBACK (meta_window_x11_update_input_region),
                    NULL);

  g_signal_connect (window, "notify::mapped",
                    G_CALLBACK (on_mapped_changed),
                    NULL);

  G_OBJECT_CLASS (meta_window_x11_parent_class)->constructed (object);
}

static void
meta_window_x11_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  MetaWindowX11 *win = META_WINDOW_X11 (object);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (win);

  switch (prop_id)
    {
    case PROP_ATTRIBUTES:
      g_value_set_pointer (value, &priv->attributes);
      break;
    case PROP_XWINDOW:
      g_value_set_ulong (value, priv->xwindow);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_window_x11_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  MetaWindowX11 *win = META_WINDOW_X11 (object);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (win);

  switch (prop_id)
    {
    case PROP_ATTRIBUTES:
      priv->attributes = *((XWindowAttributes *) g_value_get_pointer (value));
      break;
    case PROP_XWINDOW:
      priv->xwindow = g_value_get_ulong (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_window_x11_finalize (GObject *object)
{
  MetaWindowX11 *win = META_WINDOW_X11 (object);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (win);

  g_clear_pointer (&priv->shape_region, mtk_region_unref);
  g_clear_pointer (&priv->input_region, mtk_region_unref);
  g_clear_pointer (&priv->opaque_region, mtk_region_unref);
  g_clear_pointer (&priv->wm_client_machine, g_free);
  g_clear_pointer (&priv->sm_client_id, g_free);

  G_OBJECT_CLASS (meta_window_x11_parent_class)->finalize (object);
}

static void
meta_window_x11_class_init (MetaWindowX11Class *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaWindowClass *window_class = META_WINDOW_CLASS (klass);

  object_class->get_property = meta_window_x11_get_property;
  object_class->set_property = meta_window_x11_set_property;
  object_class->constructed = meta_window_x11_constructed;
  object_class->finalize = meta_window_x11_finalize;

  window_class->manage = meta_window_x11_manage;
  window_class->unmanage = meta_window_x11_unmanage;
  window_class->ping = meta_window_x11_ping;
  window_class->delete = meta_window_x11_delete;
  window_class->kill = meta_window_x11_kill;
  window_class->focus = meta_window_x11_focus;
  window_class->grab_op_began = meta_window_x11_grab_op_began;
  window_class->grab_op_ended = meta_window_x11_grab_op_ended;
  window_class->current_workspace_changed = meta_window_x11_current_workspace_changed;
  window_class->move_resize_internal = meta_window_x11_move_resize_internal;
  window_class->update_struts = meta_window_x11_update_struts;
  window_class->get_default_skip_hints = meta_window_x11_get_default_skip_hints;
  window_class->update_main_monitor = meta_window_x11_update_main_monitor;
  window_class->main_monitor_changed = meta_window_x11_main_monitor_changed;
  window_class->get_client_pid = meta_window_x11_get_client_pid;
  window_class->force_restore_shortcuts = meta_window_x11_force_restore_shortcuts;
  window_class->shortcuts_inhibited = meta_window_x11_shortcuts_inhibited;
  window_class->is_focusable = meta_window_x11_is_focusable;
  window_class->is_stackable = meta_window_x11_is_stackable;
  window_class->can_ping = meta_window_x11_can_ping;
  window_class->are_updates_frozen = meta_window_x11_are_updates_frozen;
  window_class->calculate_layer = meta_window_x11_calculate_layer;
  window_class->is_focus_async = meta_window_x11_is_focus_async;
  window_class->set_transient_for = meta_window_x11_set_transient_for;
  window_class->stage_to_protocol = meta_window_x11_stage_to_protocol;
  window_class->protocol_to_stage = meta_window_x11_protocol_to_stage;
  window_class->get_gravity = meta_window_x11_get_gravity;
  window_class->save_rect = meta_window_x11_save_rect;

  klass->freeze_commits = meta_window_x11_impl_freeze_commits;
  klass->thaw_commits = meta_window_x11_impl_thaw_commits;
  klass->always_update_shape = meta_window_x11_impl_always_update_shape;
  klass->process_property_notify = meta_window_x11_impl_process_property_notify;

  obj_props[PROP_ATTRIBUTES] =
    g_param_spec_pointer ("attributes", NULL, NULL,
                          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

  obj_props[PROP_XWINDOW] =
    g_param_spec_ulong ("xwindow", NULL, NULL,
                        0, G_MAXULONG, 0,
                        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

  g_object_class_install_properties (object_class, PROP_LAST, obj_props);
}

void
meta_window_x11_set_net_wm_state (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);
  int i;
  unsigned long data[13];

  i = 0;
  if (priv->wm_state_modal)
    {
      data[i] = x11_display->atom__NET_WM_STATE_MODAL;
      ++i;
    }
  if (window->skip_pager)
    {
      data[i] = x11_display->atom__NET_WM_STATE_SKIP_PAGER;
      ++i;
    }
  if (window->skip_taskbar)
    {
      data[i] = x11_display->atom__NET_WM_STATE_SKIP_TASKBAR;
      ++i;
    }
  if (meta_window_config_is_maximized_horizontally (window->config))
    {
      data[i] = x11_display->atom__NET_WM_STATE_MAXIMIZED_HORZ;
      ++i;
    }
  if (meta_window_config_is_maximized_vertically (window->config))
    {
      data[i] = x11_display->atom__NET_WM_STATE_MAXIMIZED_VERT;
      ++i;
    }
  if (meta_window_is_fullscreen (window))
    {
      data[i] = x11_display->atom__NET_WM_STATE_FULLSCREEN;
      ++i;
    }
  if (!meta_window_showing_on_its_workspace (window))
    {
      data[i] = x11_display->atom__NET_WM_STATE_HIDDEN;
      ++i;
    }
  if (window->wm_state_above)
    {
      data[i] = x11_display->atom__NET_WM_STATE_ABOVE;
      ++i;
    }
  if (window->wm_state_below)
    {
      data[i] = x11_display->atom__NET_WM_STATE_BELOW;
      ++i;
    }
  if (window->wm_state_demands_attention)
    {
      data[i] = x11_display->atom__NET_WM_STATE_DEMANDS_ATTENTION;
      ++i;
    }
  if (window->on_all_workspaces_requested)
    {
      data[i] = x11_display->atom__NET_WM_STATE_STICKY;
      ++i;
    }
  if (meta_window_appears_focused (window))
    {
      data[i] = x11_display->atom__NET_WM_STATE_FOCUSED;
      ++i;
    }

  meta_topic (META_DEBUG_X11, "Setting _NET_WM_STATE with %d atoms", i);

  mtk_x11_error_trap_push (x11_display->xdisplay);
  XChangeProperty (x11_display->xdisplay, priv->xwindow,
                   x11_display->atom__NET_WM_STATE,
                   XA_ATOM,
                   32, PropModeReplace, (guchar*) data, i);

  if (priv->frame)
    {
      XChangeProperty (x11_display->xdisplay,
                       priv->frame->xwindow,
                       x11_display->atom__NET_WM_STATE,
                       XA_ATOM,
                       32, PropModeReplace, (guchar*) data, i);
    }

  mtk_x11_error_trap_pop (x11_display->xdisplay);

  if (meta_window_is_fullscreen (window))
    {
      if (meta_window_has_fullscreen_monitors (window))
        {
          data[0] =
            meta_x11_display_logical_monitor_to_xinerama_index (window->display->x11_display,
                                                                window->fullscreen_monitors.top);
          data[1] =
            meta_x11_display_logical_monitor_to_xinerama_index (window->display->x11_display,
                                                                window->fullscreen_monitors.bottom);
          data[2] =
            meta_x11_display_logical_monitor_to_xinerama_index (window->display->x11_display,
                                                                window->fullscreen_monitors.left);
          data[3] =
            meta_x11_display_logical_monitor_to_xinerama_index (window->display->x11_display,
                                                                window->fullscreen_monitors.right);

          meta_topic (META_DEBUG_X11, "Setting _NET_WM_FULLSCREEN_MONITORS");
          mtk_x11_error_trap_push (x11_display->xdisplay);
          XChangeProperty (x11_display->xdisplay,
                           priv->xwindow,
                           x11_display->atom__NET_WM_FULLSCREEN_MONITORS,
                           XA_CARDINAL, 32, PropModeReplace,
                           (guchar*) data, 4);
          mtk_x11_error_trap_pop (x11_display->xdisplay);
        }
      else
        {
          meta_topic (META_DEBUG_X11, "Clearing _NET_WM_FULLSCREEN_MONITORS");
          mtk_x11_error_trap_push (x11_display->xdisplay);
          XDeleteProperty (x11_display->xdisplay,
                           priv->xwindow,
                           x11_display->atom__NET_WM_FULLSCREEN_MONITORS);
          mtk_x11_error_trap_pop (x11_display->xdisplay);
        }
    }

  /* Edge constraints */
  update_gtk_edge_constraints (window);
}

static void
meta_window_set_input_region (MetaWindow *window,
                              MtkRegion  *region)
{
  MetaWindowX11Private *priv =
    meta_window_x11_get_private (META_WINDOW_X11 (window));

  if (mtk_region_equal (priv->input_region, region))
    return;

  g_clear_pointer (&priv->input_region, mtk_region_unref);

  if (region != NULL)
    priv->input_region = mtk_region_ref (region);

  meta_compositor_window_shape_changed (window->display->compositor, window);
}

#if 0
/* Print out a region; useful for debugging */
static void
print_region (MtkRegion *region)
{
  int n_rects;
  int i;

  n_rects = mtk_region_num_rectangles (region);
  g_print ("[");
  for (i = 0; i < n_rects; i++)
    {
      MtkRectangle rect;
      rect = mtk_region_get_rectangle (region, i);
      g_print ("+%d+%dx%dx%d ",
               rect.x, rect.y, rect.width, rect.height);
    }
  g_print ("]\n");
}
#endif

void
meta_window_x11_update_input_region (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;
  g_autoptr (MtkRegion) region = NULL;
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);
  MtkRectangle bounding_rect = { 0 };
  Window xwindow;

  if (window->decorated)
    {
      if (!meta_window_x11_is_ssd (window))
        {
          if (priv->input_region)
            meta_window_set_input_region (window, NULL);
          return;
        }
      xwindow = priv->frame->xwindow;
      bounding_rect.width = window->buffer_rect.width;
      bounding_rect.height = window->buffer_rect.height;
    }
  else
    {
      xwindow = priv->xwindow;
      bounding_rect.width = priv->client_rect.width;
      bounding_rect.height = priv->client_rect.height;
    }

  if (META_X11_DISPLAY_HAS_SHAPE (x11_display))
    {
      /* Translate the set of XShape rectangles that we
       * get from the X server to a MtkRegion. */
      XRectangle *protocol_rects = NULL;
      g_autoptr (MtkRectangle) rects = NULL;
      int n_rects = -1, ordering;

      mtk_x11_error_trap_push (x11_display->xdisplay);
      protocol_rects = XShapeGetRectangles (x11_display->xdisplay,
                                            xwindow,
                                            ShapeInput,
                                            &n_rects,
                                            &ordering);
      mtk_x11_error_trap_pop (x11_display->xdisplay);

      /* XXX: The X Shape specification is quite unfortunately specified.
       *
       * By default, the window has a shape the same as its bounding region,
       * which we consider "NULL".
       *
       * If the window sets an empty region, then we'll get n_rects as 0
       * and rects as NULL, which we need to transform back into an empty
       * region.
       *
       * It would be great to have a less-broken extension for this, but
       * hey, it's X11!
       */

      if (n_rects >= 1)
        rects = protocol_rects_to_stage_rects (window, n_rects, protocol_rects);

      if (n_rects == -1)
        {
          /* We had an error. */
          region = NULL;
        }
      else if (n_rects == 0)
        {
          /* Client set an empty region. */
          region = mtk_region_create ();
        }
      else if (n_rects == 1 &&
               (rects[0].x == 0 &&
                rects[0].y == 0 &&
                rects[0].width == bounding_rect.width &&
                rects[0].height == bounding_rect.height))
        {
          /* This is the bounding region case. Keep the
           * region as NULL. */
          region = NULL;
        }
      else
        {
          /* Window has a custom shape. */
          region = mtk_region_create_rectangles (rects, n_rects);
        }

      meta_XFree (protocol_rects);
    }

  if (region != NULL)
    {
      /* The shape we get back from the client may have coordinates
       * outside of the frame. The X SHAPE Extension requires that
       * the overall shape the client provides never exceeds the
       * "bounding rectangle" of the window -- the shape that the
       * window would have gotten if it was unshaped.
       */
      mtk_region_intersect_rectangle (region, &bounding_rect);
    }

  meta_window_set_input_region (window, region);
}

static void
meta_window_set_shape_region (MetaWindow *window,
                              MtkRegion  *region)
{
  MetaWindowX11Private *priv =
    meta_window_x11_get_instance_private (META_WINDOW_X11 (window));

  if (mtk_region_equal (priv->shape_region, region))
    return;

  g_clear_pointer (&priv->shape_region, mtk_region_unref);

  if (region != NULL)
    priv->shape_region = mtk_region_ref (region);

  meta_compositor_window_shape_changed (window->display->compositor, window);
}

void
meta_window_x11_update_shape_region (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);
  g_autoptr (MtkRegion) region = NULL;

  if (META_X11_DISPLAY_HAS_SHAPE (x11_display))
    {
      /* Translate the set of XShape rectangles that we
       * get from the X server to a MtkRegion. */
      XRectangle *protocol_rects = NULL;
      int n_rects, ordering;

      int x_bounding, y_bounding, x_clip, y_clip;
      unsigned w_bounding, h_bounding, w_clip, h_clip;
      int bounding_shaped, clip_shaped;

      mtk_x11_error_trap_push (x11_display->xdisplay);
      XShapeQueryExtents (x11_display->xdisplay, priv->xwindow,
                          &bounding_shaped, &x_bounding, &y_bounding,
                          &w_bounding, &h_bounding,
                          &clip_shaped, &x_clip, &y_clip,
                          &w_clip, &h_clip);

      if (bounding_shaped)
        {
          protocol_rects = XShapeGetRectangles (x11_display->xdisplay,
                                                priv->xwindow,
                                                ShapeBounding,
                                                &n_rects,
                                                &ordering);
        }
      mtk_x11_error_trap_pop (x11_display->xdisplay);

      if (protocol_rects)
        {
          g_autoptr (MtkRectangle) rects = NULL;

          rects = protocol_rects_to_stage_rects (window, n_rects, protocol_rects);
          region = mtk_region_create_rectangles (rects, n_rects);

          XFree (protocol_rects);
        }
    }

  if (region != NULL)
    {
      MtkRectangle client_area;

      client_area.x = 0;
      client_area.y = 0;
      client_area.width = priv->client_rect.width;
      client_area.height = priv->client_rect.height;

      /* The shape we get back from the client may have coordinates
       * outside of the frame. The X SHAPE Extension requires that
       * the overall shape the client provides never exceeds the
       * "bounding rectangle" of the window -- the shape that the
       * window would have gotten if it was unshaped. In our case,
       * this is simply the client area.
       */
      mtk_region_intersect_rectangle (region, &client_area);
      /* Some applications might explicitly set their bounding region
       * to the client area. Detect these cases, and throw out the
       * bounding region in this case for decorated windows. */
      if (window->decorated &&
          mtk_region_contains_rectangle (region, &client_area) == MTK_REGION_OVERLAP_IN)
        g_clear_pointer (&region, mtk_region_unref);
    }

  meta_window_set_shape_region (window, region);
}

/* Generally meta_window_x11_same_application() is a better idea
 * of "sameness", since it handles the case where multiple apps
 * want to look like the same app or the same app wants to look
 * like multiple apps, but in the case of workarounds for legacy
 * applications (which likely aren't setting the group properly
 * anyways), it may be desirable to check this as well.
 */
static gboolean
meta_window_same_client (MetaWindow *window,
                         MetaWindow *other_window)
{
  int resource_mask = window->display->x11_display->xdisplay->resource_mask;

  return ((meta_window_x11_get_xwindow (window) & ~resource_mask) ==
          (meta_window_x11_get_xwindow (other_window) & ~resource_mask));
}


/* gets position we need to set to stay in current position,
 * assuming position will be gravity-compensated. i.e.
 * this is the position a client would send in a configure
 * request.
 */
static void
meta_window_x11_get_gravity_position (MetaWindow  *window,
                                      MetaGravity  gravity,
                                      int         *root_x,
                                      int         *root_y)
{
  MetaWindowX11Private *priv =
    meta_window_x11_get_private (META_WINDOW_X11 (window));
  MtkRectangle frame_extents;
  int w, h;
  int x, y;

  meta_window_config_get_size (window->config, &w, &h);

  if (gravity == META_GRAVITY_STATIC)
    {
      frame_extents = meta_window_config_get_rect (window->config);
      if (priv->frame)
        {
          frame_extents.x = priv->frame->rect.x + priv->frame->child_x;
          frame_extents.y = priv->frame->rect.y + priv->frame->child_y;
        }
    }
  else
    {
      if (priv->frame == NULL)
        frame_extents = meta_window_config_get_rect (window->config);
      else
        frame_extents = priv->frame->rect;
    }

  x = frame_extents.x;
  y = frame_extents.y;

  switch (gravity)
    {
    case META_GRAVITY_NORTH:
    case META_GRAVITY_CENTER:
    case META_GRAVITY_SOUTH:
      /* Find center of frame. */
      x += frame_extents.width / 2;
      /* Center client window on that point. */
      x -= w / 2;
      break;

    case META_GRAVITY_SOUTH_EAST:
    case META_GRAVITY_EAST:
    case META_GRAVITY_NORTH_EAST:
      /* Find right edge of frame */
      x += frame_extents.width;
      /* Align left edge of client at that point. */
      x -= w;
      break;
    default:
      break;
    }

  switch (gravity)
    {
    case META_GRAVITY_WEST:
    case META_GRAVITY_CENTER:
    case META_GRAVITY_EAST:
      /* Find center of frame. */
      y += frame_extents.height / 2;
      /* Center client window there. */
      y -= h / 2;
      break;
    case META_GRAVITY_SOUTH_WEST:
    case META_GRAVITY_SOUTH:
    case META_GRAVITY_SOUTH_EAST:
      /* Find south edge of frame */
      y += frame_extents.height;
      /* Place bottom edge of client there */
      y -= h;
      break;
    default:
      break;
    }

  if (root_x)
    *root_x = x;
  if (root_y)
    *root_y = y;
}

/* Get geometry for saving in the session; x/y are gravity
 * position, and w/h are in resize inc above the base size.
 */
void
meta_window_x11_get_session_geometry (MetaWindow  *window,
                                      int         *x,
                                      int         *y,
                                      int         *width,
                                      int         *height)
{
  meta_window_x11_get_gravity_position (window,
                                        window->size_hints.win_gravity,
                                        x, y);

  meta_window_config_get_position (window->config, width, height);
  *width -= window->size_hints.base_width;
  *width /= window->size_hints.width_inc;
  *height -= window->size_hints.base_height;
  *height /= window->size_hints.height_inc;
}

static void
meta_window_move_resize_request (MetaWindow  *window,
                                 guint        value_mask,
                                 MetaGravity  gravity,
                                 int          new_x,
                                 int          new_y,
                                 int          new_width,
                                 int          new_height)
{
  MetaWindowX11Private *priv =
    meta_window_x11_get_private (META_WINDOW_X11 (window));
  int x, y, width, height;
  gboolean allow_position_change;
  gboolean in_grab_op;
  MetaMoveResizeFlags flags;
  MtkRectangle buffer_rect;
  MetaWindowDrag *window_drag;

  /* We ignore configure requests while the user is moving/resizing
   * the window, since these represent the app sucking and fighting
   * the user, most likely due to a bug in the app (e.g. pfaedit
   * seemed to do this)
   *
   * Still have to do the ConfigureNotify and all, but pretend the
   * app asked for the current size/position instead of the new one.
   */
  window_drag =
    meta_compositor_get_current_window_drag (window->display->compositor);
  in_grab_op = (window_drag &&
                meta_window_drag_get_window (window_drag) == window &&
                meta_grab_op_is_mouse (meta_window_drag_get_grab_op (window_drag)));

  /* it's essential to use only the explicitly-set fields,
   * and otherwise use our current up-to-date position.
   *
   * Otherwise you get spurious position changes when the app changes
   * size, for example, if window->config->rect is not in sync with the
   * server-side position in effect when the configure request was
   * generated.
   */
  meta_window_x11_get_gravity_position (window,
                                        gravity,
                                        &x, &y);

  allow_position_change = FALSE;

  if (meta_prefs_get_disable_workarounds ())
    {
      if (window->type == META_WINDOW_DIALOG ||
          window->type == META_WINDOW_MODAL_DIALOG ||
          window->type == META_WINDOW_SPLASHSCREEN)
        ; /* No position change for these */
      else if ((window->size_hints.flags & META_SIZE_HINTS_PROGRAM_POSITION) ||
               /* USER_POSITION is just stale if window is placed;
                * no --geometry involved here.
                */
               ((window->size_hints.flags & META_SIZE_HINTS_USER_POSITION) &&
                !window->placed))
        allow_position_change = TRUE;
    }
  else
    {
      allow_position_change = TRUE;
    }

  if (in_grab_op)
    allow_position_change = FALSE;

  if (allow_position_change)
    {
      if (value_mask & CWX)
        x = new_x;
      if (value_mask & CWY)
        y = new_y;
      if (value_mask & (CWX | CWY))
        {
          /* Once manually positioned, windows shouldn't be placed
           * by the window manager.
           */
          window->placed = TRUE;
        }
    }
  else
    {
      meta_topic (META_DEBUG_GEOMETRY,
		  "Not allowing position change for window %s PROGRAM_POSITION 0x%lx USER_POSITION 0x%lx type %u",
		  window->desc, window->size_hints.flags & META_SIZE_HINTS_PROGRAM_POSITION,
		  window->size_hints.flags & META_SIZE_HINTS_USER_POSITION,
		  window->type);
    }

  if (window->decorated && !meta_window_x11_is_ssd (window))
    {
      width = new_width;
      height = new_height;
    }
  else
    {
      meta_window_get_buffer_rect (window, &buffer_rect);
      width = buffer_rect.width;
      height = buffer_rect.height;

      if (!in_grab_op || !window_drag ||
          !meta_grab_op_is_resizing (meta_window_drag_get_grab_op (window_drag)))
        {
          if (value_mask & CWWidth)
            width = new_width;

          if (value_mask & CWHeight)
            height = new_height;
        }
    }

  /* ICCCM 4.1.5 */

  /* We're ignoring the value_mask here, since sizes
   * not in the mask will be the current window geometry.
   */
  window->size_hints.x = x;
  window->size_hints.y = y;
  window->size_hints.width = width;
  window->size_hints.height = height;

  /* NOTE: We consider ConfigureRequests to be "user" actions in one
   * way, but not in another.  Explanation of the two cases are in the
   * next two big comments.
   */

  /* The constraints code allows user actions to move windows
   * offscreen, etc., and configure request actions would often send
   * windows offscreen when users don't want it if not constrained
   * (e.g. hitting a dropdown triangle in a fileselector to show more
   * options, which makes the window bigger).  Thus we do not set
   * META_MOVE_RESIZE_USER_ACTION in flags to the
   * meta_window_move_resize() call.
   */
  flags = META_MOVE_RESIZE_CONFIGURE_REQUEST;
  if (value_mask & (CWX | CWY))
    flags |= META_MOVE_RESIZE_MOVE_ACTION | META_MOVE_RESIZE_CONSTRAIN;
  if (value_mask & (CWWidth | CWHeight))
    flags |= META_MOVE_RESIZE_RESIZE_ACTION | META_MOVE_RESIZE_CONSTRAIN;

  if (flags & (META_MOVE_RESIZE_MOVE_ACTION | META_MOVE_RESIZE_RESIZE_ACTION))
    {
      MtkRectangle rect;

      rect.x = x;
      rect.y = y;
      rect.width = width;
      rect.height = height;

      if (window->monitor)
        {
          MtkRectangle monitor_rect;

          meta_display_get_monitor_geometry (window->display,
                                             window->monitor->number,
                                             &monitor_rect);

          /* Workaround braindead legacy apps that don't know how to
           * fullscreen themselves properly - don't get fooled by
           * windows which hide their titlebar when maximized or which are
           * client decorated; that's not the same as fullscreen, even
           * if there are no struts making the workarea smaller than
           * the monitor.
           */
          if (meta_prefs_get_force_fullscreen() &&
              (window->decorated || !priv->has_custom_frame_extents) &&
              mtk_rectangle_equal (&rect, &monitor_rect) &&
              window->has_fullscreen_func &&
              !meta_window_is_fullscreen (window))
            {
              meta_topic (META_DEBUG_GEOMETRY,
                          "Treating resize request of legacy application %s as a "
                          "fullscreen request",
                          window->desc);
              meta_window_make_fullscreen_internal (window);
            }
        }

      adjust_for_gravity (window, TRUE, gravity, &rect);
      meta_window_client_rect_to_frame_rect (window, &rect, &rect);
      meta_window_move_resize (window, flags, rect);
    }
}

static void
restack_window (MetaWindow *window,
                MetaWindow *sibling,
                int         direction)
{
 switch (direction)
   {
   case Above:
     if (sibling)
       meta_window_stack_just_above (window, sibling);
     else
       meta_window_raise (window);
     break;
   case Below:
     if (sibling)
       meta_window_stack_just_below (window, sibling);
     else
       meta_window_lower (window);
     break;
   case TopIf:
   case BottomIf:
   case Opposite:
     break;
   }
}

gboolean
meta_window_x11_configure_request (MetaWindow *window,
                                   XEvent     *event)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);
  MtkRectangle rect;

  /* Note that x, y is the corner of the window border,
   * and width, height is the size of the window inside
   * its border, but that we always deny border requests
   * and give windows a border of 0. But we save the
   * requested border here.
   */
  if (event->xconfigurerequest.value_mask & CWBorderWidth)
    {
      meta_window_protocol_to_stage_point (window,
                                           event->xconfigurerequest.border_width, 0,
                                           &priv->border_width, NULL,
                                           MTK_ROUNDING_STRATEGY_GROW);
    }

  rect = MTK_RECTANGLE_INIT (event->xconfigurerequest.x, event->xconfigurerequest.y,
                             event->xconfigurerequest.width, event->xconfigurerequest.height);
  meta_window_protocol_to_stage_rect (window, &rect, &rect);

  meta_window_move_resize_request (window,
                                   event->xconfigurerequest.value_mask,
                                   window->size_hints.win_gravity,
                                   rect.x,
                                   rect.y,
                                   rect.width,
                                   rect.height);

  /* Handle stacking. We only handle raises/lowers, mostly because
   * stack.c really can't deal with anything else.  I guess we'll fix
   * that if a client turns up that really requires it. Only a very
   * few clients even require the raise/lower (and in fact all client
   * attempts to deal with stacking order are essentially broken,
   * since they have no idea what other clients are involved or how
   * the stack looks).
   *
   * I'm pretty sure no interesting client uses TopIf, BottomIf, or
   * Opposite anyway.
   */
  if (event->xconfigurerequest.value_mask & CWStackMode)
    {
      MetaWindow *active_window;
      active_window = window->display->focus_window;
      if (meta_prefs_get_disable_workarounds ())
        {
          meta_topic (META_DEBUG_STACK,
                      "%s sent an xconfigure stacking request; this is "
                      "broken behavior and the request is being ignored.",
                      window->desc);
        }
      else if (active_window &&
               (active_window->client_type != window->client_type ||
                (!meta_window_x11_same_application (window, active_window) &&
                 !meta_window_same_client (window, active_window))) &&
               XSERVER_TIME_IS_BEFORE (window->net_wm_user_time,
                                       active_window->net_wm_user_time))
        {
          meta_topic (META_DEBUG_STACK,
                      "Ignoring xconfigure stacking request from %s (with "
                      "user_time %u); currently active application is %s (with "
                      "user_time %u).",
                      window->desc,
                      window->net_wm_user_time,
                      active_window->desc,
                      active_window->net_wm_user_time);
          if (event->xconfigurerequest.detail == Above)
            meta_window_set_demands_attention(window);
        }
      else
        {
          MetaWindow *sibling = NULL;
          /* Handle Above/Below with a sibling set */
          if (event->xconfigurerequest.above != None)
            {
              MetaDisplay *display;

              display = meta_window_get_display (window);
              sibling = meta_x11_display_lookup_x_window (display->x11_display,
                                                          event->xconfigurerequest.above);
              if (sibling == NULL)
                return TRUE;

              meta_topic (META_DEBUG_STACK,
                          "xconfigure stacking request from window %s "
                          "sibling %s stackmode %d",
                          window->desc, sibling->desc, event->xconfigurerequest.detail);
            }
          restack_window (window, sibling, event->xconfigurerequest.detail);
        }
    }

  return TRUE;
}

static void
meta_window_x11_impl_process_property_notify (MetaWindow     *window,
                                              XPropertyEvent *event)
{
  Window xid = meta_window_x11_get_xwindow (window);
  Window user_time_window = meta_window_x11_get_user_time_window (window);

  if (meta_is_verbose ()) /* avoid looking up the name if we don't have to */
    {
      char *property_name = XGetAtomName (window->display->x11_display->xdisplay,
                                          event->atom);

      meta_topic (META_DEBUG_X11, "Property notify on %s for %s",
                  window->desc, property_name);
      XFree (property_name);
    }

  if (event->atom == window->display->x11_display->atom__NET_WM_USER_TIME &&
      user_time_window)
    {
      xid = user_time_window;
    }

  meta_window_reload_property_from_xwindow (window, xid, event->atom, FALSE);
}

void
meta_window_x11_property_notify (MetaWindow *window,
                                 XEvent     *event)
{
  MetaWindowX11Class *klass = META_WINDOW_X11_GET_CLASS (window);

  klass->process_property_notify (window, &event->xproperty);
}

#define _NET_WM_MOVERESIZE_SIZE_TOPLEFT      0
#define _NET_WM_MOVERESIZE_SIZE_TOP          1
#define _NET_WM_MOVERESIZE_SIZE_TOPRIGHT     2
#define _NET_WM_MOVERESIZE_SIZE_RIGHT        3
#define _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT  4
#define _NET_WM_MOVERESIZE_SIZE_BOTTOM       5
#define _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT   6
#define _NET_WM_MOVERESIZE_SIZE_LEFT         7
#define _NET_WM_MOVERESIZE_MOVE              8
#define _NET_WM_MOVERESIZE_SIZE_KEYBOARD     9
#define _NET_WM_MOVERESIZE_MOVE_KEYBOARD    10
#define _NET_WM_MOVERESIZE_CANCEL           11

static int
query_pressed_buttons (MetaWindow *window)
{
  MetaContext *context = meta_display_get_context (window->display);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaCursorTracker *tracker = meta_backend_get_cursor_tracker (backend);
  ClutterModifierType mods;
  int button = 0;

  meta_cursor_tracker_get_pointer (tracker, NULL, &mods);

  if (mods & CLUTTER_BUTTON1_MASK)
    button |= 1 << 1;
  if (mods & CLUTTER_BUTTON2_MASK)
    button |= 1 << 2;
  if (mods & CLUTTER_BUTTON3_MASK)
    button |= 1 << 3;

  return button;
}

static void
handle_net_restack_window (MetaDisplay *display,
                           XEvent      *event)
{
  MetaWindow *window, *sibling = NULL;

  /* Ignore if this does not come from a pager, see the WM spec
   */
  if (event->xclient.data.l[0] != 2)
    return;

  window = meta_x11_display_lookup_x_window (display->x11_display,
                                             event->xclient.window);

  if (window)
    {
      if (event->xclient.data.l[1])
        sibling = meta_x11_display_lookup_x_window (display->x11_display,
                                                    event->xclient.data.l[1]);

      restack_window (window, sibling, event->xclient.data.l[2]);
    }
}

#ifdef HAVE_XWAYLAND
typedef struct {
  ClutterSprite *sprite;
  graphene_point_t device_point;
  graphene_point_t coords;
  int button;
} NearestDeviceData;

static gboolean
nearest_device_func (ClutterStage  *stage,
                     ClutterSprite *sprite,
                     gpointer       user_data)
{
  ClutterContext *context = clutter_actor_get_context (CLUTTER_ACTOR (stage));
  ClutterBackend *clutter_backend = clutter_context_get_backend (context);
  ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);
  NearestDeviceData *data = user_data;
  graphene_point_t point;
  ClutterModifierType mods;
  const int nearest_threshold = 64;

  clutter_seat_query_state (seat, sprite, &point, &mods);

  if (!clutter_sprite_get_sequence (sprite))
    {
      ClutterModifierType accepted_buttons = 0;
      ClutterModifierType mask =
        (CLUTTER_BUTTON1_MASK | CLUTTER_BUTTON2_MASK |
         CLUTTER_BUTTON3_MASK | CLUTTER_BUTTON4_MASK |
         CLUTTER_BUTTON5_MASK);

      if (data->button != 0)
        accepted_buttons = (CLUTTER_BUTTON1_MASK << (data->button - 1)) & mask;
      else
        accepted_buttons = mask;

      /* Check that pointers have any of the relevant buttons pressed */
      if (!(mods & accepted_buttons))
        return TRUE;
    }

  if (ABS (point.x - data->coords.x) < nearest_threshold &&
      ABS (point.y - data->coords.y) < nearest_threshold &&
      ABS (point.x - data->coords.x) < ABS (data->device_point.x - data->coords.x) &&
      ABS (point.y - data->coords.y) < ABS (data->device_point.y - data->coords.y))
    {
      data->sprite = sprite;
      data->device_point = point;
    }

  return TRUE;
}

static gboolean
guess_nearest_device (MetaWindow            *window,
                      int                    root_x,
                      int                    root_y,
                      int                    button,
                      ClutterSprite        **sprite)
{
  MetaDisplay *display = meta_window_get_display (window);
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
  NearestDeviceData data = { 0, };

  data.button = button;
  graphene_point_init (&data.coords, root_x, root_y);
  clutter_stage_foreach_sprite (stage, nearest_device_func, &data);

  if (sprite && data.sprite)
    *sprite = data.sprite;

  return data.sprite != NULL;
}
#endif /* HAVE_XWAYLAND */

gboolean
meta_window_x11_client_message (MetaWindow *window,
                                XEvent     *event)
{
  MetaX11Display *x11_display = window->display->x11_display;
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);
  MetaDisplay *display;

  display = window->display;

  if (window->override_redirect)
    {
      /* Don't warn here: we could warn on any of the messages below,
       * but we might also receive other client messages that are
       * part of protocols we don't know anything about. So, silently
       * ignoring is simplest.
       */
      return FALSE;
    }

  if (event->xclient.message_type ==
      x11_display->atom__NET_CLOSE_WINDOW)
    {
      guint32 timestamp;

      if (event->xclient.data.l[0] != 0)
	timestamp = event->xclient.data.l[0];
      else
        {
          meta_topic (META_DEBUG_X11,
                      "Receiving a NET_CLOSE_WINDOW message for %s without "
                      "an expected timestamp.",
                      window->desc);
          timestamp = meta_display_get_current_time (window->display);
        }

      meta_window_delete (window, timestamp);

      return TRUE;
    }
  else if (event->xclient.message_type ==
           x11_display->atom__NET_WM_DESKTOP)
    {
      int space;
      MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;
      MetaWorkspace *workspace;

      space = event->xclient.data.l[0];

      meta_topic (META_DEBUG_X11, "Request to move %s to workspace %d",
                  window->desc, space);

      workspace =
        meta_workspace_manager_get_workspace_by_index (workspace_manager,
                                                       space);

      if (workspace)
        meta_window_change_workspace (window, workspace);
      else if (space == (int) 0xFFFFFFFF)
        meta_window_stick (window);
      else
        meta_topic (META_DEBUG_X11, "No such workspace %d for screen", space);

      meta_topic (META_DEBUG_X11, "Window %s now on_all_workspaces = %d",
                  window->desc, window->on_all_workspaces);

      return TRUE;
    }
  else if (event->xclient.message_type ==
           x11_display->atom__NET_WM_STATE)
    {
      gulong action;
      Atom first;
      Atom second;

      action = event->xclient.data.l[0];
      first = event->xclient.data.l[1];
      second = event->xclient.data.l[2];

      if (meta_is_verbose ())
        {
          char *str1;
          char *str2;

          mtk_x11_error_trap_push (x11_display->xdisplay);
          str1 = XGetAtomName (x11_display->xdisplay, first);
          if (mtk_x11_error_trap_pop_with_return (x11_display->xdisplay) != Success)
            str1 = NULL;

          mtk_x11_error_trap_push (x11_display->xdisplay);
          str2 = XGetAtomName (x11_display->xdisplay, second);
          if (mtk_x11_error_trap_pop_with_return (x11_display->xdisplay) != Success)
            str2 = NULL;

          meta_topic (META_DEBUG_X11, "Request to change _NET_WM_STATE action %lu atom1: %s atom2: %s",
                      action,
                      str1 ? str1 : "(unknown)",
                      str2 ? str2 : "(unknown)");

          meta_XFree (str1);
          meta_XFree (str2);
        }

      if (first == x11_display->atom__NET_WM_STATE_FULLSCREEN ||
          second == x11_display->atom__NET_WM_STATE_FULLSCREEN)
        {
          gboolean make_fullscreen;

          make_fullscreen = (action == _NET_WM_STATE_ADD ||
                             (action == _NET_WM_STATE_TOGGLE &&
                              !meta_window_is_fullscreen (window)));
          if (make_fullscreen && window->has_fullscreen_func)
            meta_window_make_fullscreen (window);
          else
            meta_window_unmake_fullscreen (window);
        }

      if (first == x11_display->atom__NET_WM_STATE_MAXIMIZED_HORZ ||
          second == x11_display->atom__NET_WM_STATE_MAXIMIZED_HORZ ||
          first == x11_display->atom__NET_WM_STATE_MAXIMIZED_VERT ||
          second == x11_display->atom__NET_WM_STATE_MAXIMIZED_VERT)
        {
          gboolean max;
          MetaMaximizeFlags directions = 0;

          max =
            (action == _NET_WM_STATE_ADD ||
             (action == _NET_WM_STATE_TOGGLE &&
              !meta_window_config_is_maximized_horizontally (window->config)));

          if (first == x11_display->atom__NET_WM_STATE_MAXIMIZED_HORZ ||
              second == x11_display->atom__NET_WM_STATE_MAXIMIZED_HORZ)
            directions |= META_MAXIMIZE_HORIZONTAL;

          if (first == x11_display->atom__NET_WM_STATE_MAXIMIZED_VERT ||
              second == x11_display->atom__NET_WM_STATE_MAXIMIZED_VERT)
            directions |= META_MAXIMIZE_VERTICAL;

          if (max && window->has_maximize_func)
            {
              if (meta_prefs_get_raise_on_click ())
                meta_window_raise (window);
              meta_window_set_maximize_flags (window, directions);
            }
          else
            {
              if (meta_prefs_get_raise_on_click ())
                meta_window_raise (window);
              meta_window_set_unmaximize_flags (window, directions);
            }
        }

      if (first == x11_display->atom__NET_WM_STATE_MODAL ||
          second == x11_display->atom__NET_WM_STATE_MODAL)
        {
          priv->wm_state_modal =
            (action == _NET_WM_STATE_ADD) ||
            (action == _NET_WM_STATE_TOGGLE && !priv->wm_state_modal);

          meta_window_x11_recalc_window_type (window);
          meta_window_queue(window, META_QUEUE_MOVE_RESIZE);
        }

      if (first == x11_display->atom__NET_WM_STATE_SKIP_PAGER ||
          second == x11_display->atom__NET_WM_STATE_SKIP_PAGER)
        {
          priv->wm_state_skip_pager =
            (action == _NET_WM_STATE_ADD) ||
            (action == _NET_WM_STATE_TOGGLE && !window->skip_pager);

          meta_window_recalc_features (window);
          meta_window_x11_set_net_wm_state (window);
        }

      if (first == x11_display->atom__NET_WM_STATE_SKIP_TASKBAR ||
          second == x11_display->atom__NET_WM_STATE_SKIP_TASKBAR)
        {
          priv->wm_state_skip_taskbar =
            (action == _NET_WM_STATE_ADD) ||
            (action == _NET_WM_STATE_TOGGLE && !window->skip_taskbar);

          meta_window_recalc_features (window);
          meta_window_x11_set_net_wm_state (window);
        }

      if (first == x11_display->atom__NET_WM_STATE_ABOVE ||
          second == x11_display->atom__NET_WM_STATE_ABOVE)
        {
          if ((action == _NET_WM_STATE_ADD) ||
              (action == _NET_WM_STATE_TOGGLE && !window->wm_state_demands_attention))
            meta_window_make_above (window);
          else
            meta_window_unmake_above (window);
        }

      if (first == x11_display->atom__NET_WM_STATE_BELOW ||
          second == x11_display->atom__NET_WM_STATE_BELOW)
        {
          window->wm_state_below =
            (action == _NET_WM_STATE_ADD) ||
            (action == _NET_WM_STATE_TOGGLE && !window->wm_state_below);

          meta_window_update_layer (window);
          meta_window_x11_set_net_wm_state (window);
        }

      if (first == x11_display->atom__NET_WM_STATE_DEMANDS_ATTENTION ||
          second == x11_display->atom__NET_WM_STATE_DEMANDS_ATTENTION)
        {
          if ((action == _NET_WM_STATE_ADD) ||
              (action == _NET_WM_STATE_TOGGLE && !window->wm_state_demands_attention))
            meta_window_set_demands_attention (window);
          else
            meta_window_unset_demands_attention (window);
        }

       if (first == x11_display->atom__NET_WM_STATE_STICKY ||
          second == x11_display->atom__NET_WM_STATE_STICKY)
        {
          if ((action == _NET_WM_STATE_ADD) ||
              (action == _NET_WM_STATE_TOGGLE && !window->on_all_workspaces_requested))
            meta_window_stick (window);
          else
            meta_window_unstick (window);
        }

      return TRUE;
    }
  else if (event->xclient.message_type ==
           x11_display->atom_WM_CHANGE_STATE)
    {
      meta_topic (META_DEBUG_X11, "WM_CHANGE_STATE client message, state: %ld",
                  event->xclient.data.l[0]);
      if (event->xclient.data.l[0] == IconicState)
        meta_window_minimize (window);

      return TRUE;
    }
  else if (event->xclient.message_type ==
           x11_display->atom__NET_WM_MOVERESIZE)
    {
      int x_root;
      int y_root;
      int action;
      MetaGrabOp op;
      int button;
      guint32 timestamp;
      MetaWindowDrag *window_drag;

      meta_window_protocol_to_stage_point (window,
                                           event->xclient.data.l[0],
                                           event->xclient.data.l[1],
                                           &x_root,
                                           &y_root,
                                           MTK_ROUNDING_STRATEGY_SHRINK);
      action = event->xclient.data.l[2];
      button = event->xclient.data.l[3];

      /* FIXME: What a braindead protocol; no timestamp?!? */
      timestamp = meta_display_get_current_time_roundtrip (display);
      meta_topic (META_DEBUG_WINDOW_OPS,
                  "Received _NET_WM_MOVERESIZE message on %s, %d,%d action = %d, button %d",
                  window->desc,
                  x_root, y_root, action, button);

      op = META_GRAB_OP_NONE;
      switch (action)
        {
        case _NET_WM_MOVERESIZE_SIZE_TOPLEFT:
          op = META_GRAB_OP_RESIZING_NW;
          break;
        case _NET_WM_MOVERESIZE_SIZE_TOP:
          op = META_GRAB_OP_RESIZING_N;
          break;
        case _NET_WM_MOVERESIZE_SIZE_TOPRIGHT:
          op = META_GRAB_OP_RESIZING_NE;
          break;
        case _NET_WM_MOVERESIZE_SIZE_RIGHT:
          op = META_GRAB_OP_RESIZING_E;
          break;
        case _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT:
          op = META_GRAB_OP_RESIZING_SE;
          break;
        case _NET_WM_MOVERESIZE_SIZE_BOTTOM:
          op = META_GRAB_OP_RESIZING_S;
          break;
        case _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT:
          op = META_GRAB_OP_RESIZING_SW;
          break;
        case _NET_WM_MOVERESIZE_SIZE_LEFT:
          op = META_GRAB_OP_RESIZING_W;
          break;
        case _NET_WM_MOVERESIZE_MOVE:
          op = META_GRAB_OP_MOVING;
          break;
        case _NET_WM_MOVERESIZE_SIZE_KEYBOARD:
          op = META_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN;
          break;
        case _NET_WM_MOVERESIZE_MOVE_KEYBOARD:
          op = META_GRAB_OP_KEYBOARD_MOVING;
          break;
        case _NET_WM_MOVERESIZE_CANCEL:
          /* handled below */
          break;
        default:
          break;
        }

      if (action == _NET_WM_MOVERESIZE_CANCEL)
        {
          window_drag =
            meta_compositor_get_current_window_drag (window->display->compositor);
          if (window_drag)
            meta_window_drag_end (window_drag);
        }
      else if (op != META_GRAB_OP_NONE &&
          ((window->has_move_func && op == META_GRAB_OP_KEYBOARD_MOVING) ||
           (window->has_resize_func && op == META_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN)))
        {
          MetaContext *context = meta_display_get_context (display);
          MetaBackend *backend = meta_context_get_backend (context);
          ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
          ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
          ClutterSprite *sprite;

          sprite = clutter_backend_get_pointer_sprite (clutter_backend, stage);
          meta_window_begin_grab_op (window, op,
                                     sprite,
                                     timestamp,
                                     NULL);
        }
      else if (op != META_GRAB_OP_NONE &&
               ((window->has_move_func && op == META_GRAB_OP_MOVING) ||
               (window->has_resize_func &&
                (op != META_GRAB_OP_MOVING &&
                 op != META_GRAB_OP_KEYBOARD_MOVING))))
        {
          MetaContext *context = meta_display_get_context (display);
          MetaBackend *backend = meta_context_get_backend (context);
          ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
          ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
          ClutterSprite *sprite = NULL;
          int button_mask;

#ifdef HAVE_XWAYLAND
          if (meta_is_wayland_compositor ())
            {
              if (!guess_nearest_device (window, x_root, y_root, button, &sprite))
                return FALSE;
            }
          else
#endif
            {
              sprite = clutter_backend_get_pointer_sprite (clutter_backend,
                                                           stage);
            }

          g_assert (sprite);
          meta_topic (META_DEBUG_WINDOW_OPS,
                      "Beginning move/resize with button = %d", button);
          meta_window_begin_grab_op (window, op,
                                     sprite,
                                     timestamp,
                                     &GRAPHENE_POINT_INIT (x_root, y_root));

          window_drag =
            meta_compositor_get_current_window_drag (window->display->compositor);

#ifdef HAVE_XWAYLAND
          if (!meta_is_wayland_compositor ())
#endif
            {
              button_mask = query_pressed_buttons (window);

              if (button == 0)
                {
                  /*
                   * the button SHOULD already be included in the message
                   */
                  if ((button_mask & (1 << 1)) != 0)
                    button = 1;
                  else if ((button_mask & (1 << 2)) != 0)
                    button = 2;
                  else if ((button_mask & (1 << 3)) != 0)
                    button = 3;

                  if (button == 0 && window_drag)
                    meta_window_drag_end (window_drag);
                }
              else
                {
                  /* There is a potential race here. If the user presses and
                   * releases their mouse button very fast, it's possible for
                   * both the ButtonPress and ButtonRelease to be sent to the
                   * client before it can get a chance to send _NET_WM_MOVERESIZE
                   * to us. When that happens, we'll become stuck in a grab
                   * state, as we haven't received a ButtonRelease to cancel the
                   * grab.
                   *
                   * We can solve this by querying after we take the explicit
                   * pointer grab -- if the button isn't pressed, we cancel the
                   * drag immediately.
                   */

                  if (window_drag && (button_mask & (1 << button)) == 0)
                    meta_window_drag_end (window_drag);
                }
            }
        }

      return TRUE;
    }
  else if (event->xclient.message_type ==
           x11_display->atom__NET_MOVERESIZE_WINDOW)
    {
      MetaGravity gravity;
      guint value_mask;
      MtkRectangle rect;

      gravity = (MetaGravity) (event->xclient.data.l[0] & 0xff);
      value_mask = (event->xclient.data.l[0] & 0xf00) >> 8;
      /* source = (event->xclient.data.l[0] & 0xf000) >> 12; */

      if (gravity == 0)
        gravity = window->size_hints.win_gravity;

      rect = MTK_RECTANGLE_INIT (event->xclient.data.l[1],
                                 event->xclient.data.l[2],
                                 event->xclient.data.l[3],
                                 event->xclient.data.l[4]);
      meta_window_protocol_to_stage_rect (window, &rect, &rect);

      meta_window_move_resize_request(window,
                                      value_mask,
                                      gravity,
                                      rect.x,
                                      rect.y,
                                      rect.width,
                                      rect.height);
    }
  else if (event->xclient.message_type ==
           x11_display->atom__NET_ACTIVE_WINDOW &&
           meta_display_windows_are_interactable (window->display))
    {
      MetaClientType source_indication;
      guint32        timestamp;

      meta_topic (META_DEBUG_X11, "_NET_ACTIVE_WINDOW request for window '%s', activating",
                  window->desc);

      source_indication = event->xclient.data.l[0];
      timestamp = event->xclient.data.l[1];

      if (source_indication > META_CLIENT_TYPE_MAX_RECOGNIZED)
        source_indication = META_CLIENT_TYPE_UNKNOWN;

      if (timestamp == 0)
        {
          /* Client using older EWMH _NET_ACTIVE_WINDOW without a timestamp */
          meta_topic (META_DEBUG_X11,
                      "Client sent a _NET_ACTIVE_WINDOW message with an invalid"
                      "timestamp of 0 for %s",
                      window->desc);
          timestamp = meta_display_get_current_time (display);
        }

      meta_window_activate_full (window, timestamp, source_indication, NULL);
      return TRUE;
    }
  else if (event->xclient.message_type ==
           x11_display->atom__NET_WM_FULLSCREEN_MONITORS)
    {
      MetaLogicalMonitor *top, *bottom, *left, *right;

      meta_topic (META_DEBUG_X11,
                  "_NET_WM_FULLSCREEN_MONITORS request for window '%s'",
                  window->desc);

      top =
        meta_x11_display_xinerama_index_to_logical_monitor (window->display->x11_display,
                                                            event->xclient.data.l[0]);
      bottom =
        meta_x11_display_xinerama_index_to_logical_monitor (window->display->x11_display,
                                                            event->xclient.data.l[1]);
      left =
        meta_x11_display_xinerama_index_to_logical_monitor (window->display->x11_display,
                                                            event->xclient.data.l[2]);
      right =
        meta_x11_display_xinerama_index_to_logical_monitor (window->display->x11_display,
                                                            event->xclient.data.l[3]);
      /* source_indication = event->xclient.data.l[4]; */

      meta_window_update_fullscreen_monitors (window, top, bottom, left, right);
    }
  else if (event->xclient.message_type ==
           x11_display->atom__GTK_SHOW_WINDOW_MENU)
    {
      int x, y;

      /* l[0] is device_id, which we don't use */
      meta_window_protocol_to_stage_point (window,
                                           event->xclient.data.l[1],
                                           event->xclient.data.l[2],
                                           &x, &y,
                                           MTK_ROUNDING_STRATEGY_SHRINK);

      meta_window_show_menu (window, META_WINDOW_MENU_WM, x, y);
    }
  else if (event->xclient.message_type ==
           x11_display->atom__NET_RESTACK_WINDOW)
    {
      handle_net_restack_window (display, event);
    }

  return FALSE;
}

static void
set_wm_state_on_xwindow (MetaDisplay *display,
                         Window       xwindow,
                         int          state)
{
  unsigned long data[2];

  /* Mutter doesn't use icon windows, so data[1] should be None
   * according to the ICCCM 2.0 Section 4.1.3.1.
   */
  data[0] = state;
  data[1] = None;

  mtk_x11_error_trap_push (display->x11_display->xdisplay);
  XChangeProperty (display->x11_display->xdisplay, xwindow,
                   display->x11_display->atom_WM_STATE,
                   display->x11_display->atom_WM_STATE,
                   32, PropModeReplace, (guchar*) data, 2);
  mtk_x11_error_trap_pop (display->x11_display->xdisplay);
}

void
meta_window_x11_set_wm_state (MetaWindow *window)
{
  int state;

  if (window->withdrawn)
    state = WithdrawnState;
  else if (window->iconic)
    state = IconicState;
  else
    state = NormalState;

  set_wm_state_on_xwindow (window->display,
                           meta_window_x11_get_xwindow (window),
                           state);
}

/* The MUTTER_WM_CLASS_FILTER environment variable is designed for
 * performance and regression testing environments where we want to do
 * tests with only a limited set of windows and ignore all other windows
 *
 * When it is set to a comma separated list of WM_CLASS class names, all
 * windows not matching the list will be ignored.
 *
 * Returns TRUE if window has been filtered out and should be ignored.
 */
static gboolean
maybe_filter_xwindow (MetaDisplay       *display,
                      Window             xwindow,
                      gboolean           must_be_viewable,
                      XWindowAttributes *attrs)
{
  static char **filter_wm_classes = NULL;
  static gboolean initialized = FALSE;
  XClassHint class_hint;
  gboolean filtered;
  Status success;
  int i;

  if (!initialized)
    {
      const char *filter_string = g_getenv ("MUTTER_WM_CLASS_FILTER");
      if (filter_string)
        filter_wm_classes = g_strsplit (filter_string, ",", -1);
      initialized = TRUE;
    }

  if (!filter_wm_classes || !filter_wm_classes[0])
    return FALSE;

  filtered = TRUE;

  mtk_x11_error_trap_push (display->x11_display->xdisplay);
  success = XGetClassHint (display->x11_display->xdisplay,
                           xwindow, &class_hint);

  if (success)
    {
      for (i = 0; filter_wm_classes[i]; i++)
        {
          if (strcmp (class_hint.res_class, filter_wm_classes[i]) == 0)
            {
              filtered = FALSE;
              break;
            }
        }

      XFree (class_hint.res_name);
      XFree (class_hint.res_class);
    }

  if (filtered)
    {
      /* We want to try and get the window managed by the next WM that come along,
       * so we need to make sure that windows that are requested to be mapped while
       * Mutter is running (!must_be_viewable), or windows already viewable at startup
       * get a non-withdrawn WM_STATE property. Previously unmapped windows are left
       * with whatever WM_STATE property they had.
       */
      if (!must_be_viewable || attrs->map_state == IsViewable)
        {
          uint32_t old_state;

          if (!meta_prop_get_cardinal_with_atom_type (display->x11_display, xwindow,
                                                      display->x11_display->atom_WM_STATE,
                                                      display->x11_display->atom_WM_STATE,
                                                      &old_state))
            old_state = WithdrawnState;

          if (old_state == WithdrawnState)
            set_wm_state_on_xwindow (display, xwindow, NormalState);
        }

      /* Make sure filtered windows are hidden from view */
      XUnmapWindow (display->x11_display->xdisplay, xwindow);
    }

  mtk_x11_error_trap_pop (display->x11_display->xdisplay);

  return filtered;
}

static gboolean
is_our_xwindow (MetaX11Display    *x11_display,
                Window             xwindow,
                XWindowAttributes *attrs)
{
#ifdef HAVE_X11
  MetaDisplay *display;
  MetaContext *context;
  MetaBackend *backend;
#endif

  if (xwindow == x11_display->no_focus_window)
    return TRUE;

  if (xwindow == x11_display->wm_sn_selection_window)
    return TRUE;

  if (xwindow == x11_display->wm_cm_selection_window)
    return TRUE;

  if (xwindow == x11_display->guard_window)
    return TRUE;

  if (xwindow == x11_display->composite_overlay_window)
    return TRUE;

#ifdef HAVE_X11
  display = meta_x11_display_get_display (x11_display);
  context = meta_display_get_context (display);
  backend = meta_context_get_backend (context);

  if (META_IS_BACKEND_X11 (backend) &&
      xwindow == meta_backend_x11_get_xwindow (META_BACKEND_X11 (backend)))
    return TRUE;
#endif

  /* Any windows created via meta_create_offscreen_window */
  if (attrs->override_redirect &&
      attrs->x == -100 &&
      attrs->y == -100 &&
      attrs->width == 1 &&
      attrs->height == 1)
    return TRUE;

  return FALSE;
}

#ifdef WITH_VERBOSE_MODE
static const char*
wm_state_to_string (int state)
{
  switch (state)
    {
    case NormalState:
      return "NormalState";
    case IconicState:
      return "IconicState";
    case WithdrawnState:
      return "WithdrawnState";
    }

  return "Unknown";
}
#endif

MetaWindow *
meta_window_x11_new (MetaDisplay       *display,
                     Window             xwindow,
                     gboolean           must_be_viewable,
                     MetaCompEffect     effect)
{
  MetaX11Display *x11_display = display->x11_display;
  XWindowAttributes attrs;
  gulong existing_wm_state;
  MetaWindow *window = NULL;
  gulong event_mask;

  meta_topic (META_DEBUG_X11, "Attempting to manage 0x%lx", xwindow);

  if (meta_x11_display_xwindow_is_a_no_focus_window (x11_display, xwindow))
    {
      meta_topic (META_DEBUG_X11, "Not managing no_focus_window 0x%lx",
                  xwindow);
      return NULL;
    }

  mtk_x11_error_trap_push (x11_display->xdisplay); /* Push a trap over all of window
                                       * creation, to reduce XSync() calls
                                       */
  /*
   * This function executes without any server grabs held. This means that
   * the window could have already gone away, or could go away at any point,
   * so we must be careful with X error handling.
   */

  if (!XGetWindowAttributes (x11_display->xdisplay, xwindow, &attrs))
    {
      meta_topic (META_DEBUG_X11, "Failed to get attributes for window 0x%lx",
                  xwindow);
      goto error;
    }

  if (attrs.root != x11_display->xroot)
    {
      meta_topic (META_DEBUG_X11, "Not on our screen");
      goto error;
    }

  if (attrs.class == InputOnly)
    {
      meta_topic (META_DEBUG_X11, "Not managing InputOnly windows");
      goto error;
    }

  if (is_our_xwindow (x11_display, xwindow, &attrs))
    {
      meta_topic (META_DEBUG_X11, "Not managing our own windows");
      goto error;
    }

  if (maybe_filter_xwindow (display, xwindow, must_be_viewable, &attrs))
    {
      meta_topic (META_DEBUG_X11, "Not managing filtered window");
      goto error;
    }

  existing_wm_state = WithdrawnState;
  if (must_be_viewable && attrs.map_state != IsViewable)
    {
      /* Only manage if WM_STATE is IconicState or NormalState */
      uint32_t state;

      /* WM_STATE isn't a cardinal, it's type WM_STATE, but is an int */
      if (!(meta_prop_get_cardinal_with_atom_type (x11_display, xwindow,
                                                   x11_display->atom_WM_STATE,
                                                   x11_display->atom_WM_STATE,
                                                   &state) &&
            (state == IconicState || state == NormalState)))
        {
          meta_topic (META_DEBUG_X11,
                      "Deciding not to manage unmapped or unviewable window 0x%lx",
                      xwindow);
          goto error;
        }

      existing_wm_state = state;
      meta_topic (META_DEBUG_X11, "WM_STATE of %lx = %s", xwindow,
                  wm_state_to_string (existing_wm_state));
    }

  /*
   * XAddToSaveSet can only be called on windows created by a different
   * client.  with Mutter we want to be able to create manageable windows
   * from within the process (such as a dummy desktop window). As we do not
   * want this call failing to prevent the window from being managed, we
   * call this before creating the return-checked error trap.
   */
  XAddToSaveSet (x11_display->xdisplay, xwindow);

  mtk_x11_error_trap_push (x11_display->xdisplay);

  event_mask = PropertyChangeMask;
  if (attrs.override_redirect)
    event_mask |= StructureNotifyMask;

  /* If the window is from this client (a menu, say) we need to augment
   * the event mask, not replace it. For windows from other clients,
   * attrs.your_event_mask will be empty at this point.
   */
  XSelectInput (x11_display->xdisplay, xwindow, attrs.your_event_mask | event_mask);

  {
    unsigned char mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
    XIEventMask mask = { XIAllMasterDevices, sizeof (mask_bits), mask_bits };

    XISetMask (mask.mask, XI_Enter);
    XISetMask (mask.mask, XI_Leave);
    XISetMask (mask.mask, XI_FocusIn);
    XISetMask (mask.mask, XI_FocusOut);

    XISelectEvents (x11_display->xdisplay, xwindow, &mask, 1);
  }

  if (META_X11_DISPLAY_HAS_SHAPE (x11_display))
    XShapeSelectInput (x11_display->xdisplay, xwindow, ShapeNotifyMask);

  /* Get rid of any borders */
  if (attrs.border_width != 0)
    XSetWindowBorderWidth (x11_display->xdisplay, xwindow, 0);

  /* Get rid of weird gravities */
  if (attrs.win_gravity != NorthWestGravity)
    {
      XSetWindowAttributes set_attrs;

      set_attrs.win_gravity = NorthWestGravity;

      XChangeWindowAttributes (x11_display->xdisplay,
                               xwindow,
                               CWWinGravity,
                               &set_attrs);
    }

  if (mtk_x11_error_trap_pop_with_return (x11_display->xdisplay) != Success)
    {
      meta_topic (META_DEBUG_X11,
                  "Window 0x%lx disappeared just as we tried to manage it",
                  xwindow);
      goto error;
    }

#ifdef HAVE_XWAYLAND
  if (meta_is_wayland_compositor ())
    {
      window = g_initable_new (META_TYPE_WINDOW_XWAYLAND,
                               NULL, NULL,
                               "display", display,
                               "effect", effect,
                               "attributes", &attrs,
                               "xwindow", xwindow,
                               NULL);
    }
  else
#endif
    {
      window = g_initable_new (META_TYPE_WINDOW_X11,
                               NULL, NULL,
                               "display", display,
                               "effect", effect,
                               "attributes", &attrs,
                               "xwindow", xwindow,
                               NULL);
    }
  if (existing_wm_state == IconicState)
    {
      /* WM_STATE said minimized */
      window->minimized = TRUE;
      meta_topic (META_DEBUG_X11,
                  "Window %s had preexisting WM_STATE = IconicState, minimizing",
                  window->desc);

      /* Assume window was previously placed, though perhaps it's
       * been iconic its whole life, we have no way of knowing.
       */
      window->placed = TRUE;
    }

  mtk_x11_error_trap_pop (x11_display->xdisplay); /* pop the XSync()-reducing trap */

  meta_display_notify_window_created (display, window);

  return window;

error:
  mtk_x11_error_trap_pop (x11_display->xdisplay);
  return NULL;
}

void
meta_window_x11_recalc_window_type (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);
  MetaWindowType type;

  if (priv->type_atom != None)
    {
      if (priv->type_atom  == x11_display->atom__NET_WM_WINDOW_TYPE_DESKTOP)
        type = META_WINDOW_DESKTOP;
      else if (priv->type_atom  == x11_display->atom__NET_WM_WINDOW_TYPE_DOCK)
        type = META_WINDOW_DOCK;
      else if (priv->type_atom  == x11_display->atom__NET_WM_WINDOW_TYPE_TOOLBAR)
        type = META_WINDOW_TOOLBAR;
      else if (priv->type_atom  == x11_display->atom__NET_WM_WINDOW_TYPE_MENU)
        type = META_WINDOW_MENU;
      else if (priv->type_atom  == x11_display->atom__NET_WM_WINDOW_TYPE_UTILITY)
        type = META_WINDOW_UTILITY;
      else if (priv->type_atom  == x11_display->atom__NET_WM_WINDOW_TYPE_SPLASH)
        type = META_WINDOW_SPLASHSCREEN;
      else if (priv->type_atom  == x11_display->atom__NET_WM_WINDOW_TYPE_DIALOG)
        type = META_WINDOW_DIALOG;
      else if (priv->type_atom  == x11_display->atom__NET_WM_WINDOW_TYPE_NORMAL)
        type = META_WINDOW_NORMAL;
      /* The below are *typically* override-redirect windows, but the spec does
       * not disallow using them for managed windows.
       */
      else if (priv->type_atom  == x11_display->atom__NET_WM_WINDOW_TYPE_DROPDOWN_MENU)
        type = META_WINDOW_DROPDOWN_MENU;
      else if (priv->type_atom  == x11_display->atom__NET_WM_WINDOW_TYPE_POPUP_MENU)
        type = META_WINDOW_POPUP_MENU;
      else if (priv->type_atom  == x11_display->atom__NET_WM_WINDOW_TYPE_TOOLTIP)
        type = META_WINDOW_TOOLTIP;
      else if (priv->type_atom  == x11_display->atom__NET_WM_WINDOW_TYPE_NOTIFICATION)
        type = META_WINDOW_NOTIFICATION;
      else if (priv->type_atom  == x11_display->atom__NET_WM_WINDOW_TYPE_COMBO)
        type = META_WINDOW_COMBO;
      else if (priv->type_atom  == x11_display->atom__NET_WM_WINDOW_TYPE_DND)
        type = META_WINDOW_DND;
      else
        {
          char *atom_name;

          /*
           * Fallback on a normal type, and print warning. Don't abort.
           */
          type = META_WINDOW_NORMAL;

          mtk_x11_error_trap_push (x11_display->xdisplay);
          atom_name = XGetAtomName (x11_display->xdisplay,
                                    priv->type_atom);
          mtk_x11_error_trap_pop (x11_display->xdisplay);

          g_warning ("Unrecognized type atom [%s] set for %s ",
                     atom_name ? atom_name : "unknown",
                     window->desc);

          if (atom_name)
            XFree (atom_name);
        }
    }
  else if (window->transient_for != NULL)
    {
      type = META_WINDOW_DIALOG;
    }
  else
    {
      type = META_WINDOW_NORMAL;
    }

  if (type == META_WINDOW_DIALOG && priv->wm_state_modal)
    type = META_WINDOW_MODAL_DIALOG;

  /* We don't want to allow override-redirect windows to have decorated-window
   * types since that's just confusing.
   */
  if (window->override_redirect)
    {
      switch (type)
        {
        /* Decorated types */
        case META_WINDOW_NORMAL:
        case META_WINDOW_DIALOG:
        case META_WINDOW_MODAL_DIALOG:
        case META_WINDOW_MENU:
        case META_WINDOW_UTILITY:
          type = META_WINDOW_OVERRIDE_OTHER;
          break;
        /* Undecorated types, normally not override-redirect */
        case META_WINDOW_DESKTOP:
        case META_WINDOW_DOCK:
        case META_WINDOW_TOOLBAR:
        case META_WINDOW_SPLASHSCREEN:
        /* Undecorated types, normally override-redirect types */
        case META_WINDOW_DROPDOWN_MENU:
        case META_WINDOW_POPUP_MENU:
        case META_WINDOW_TOOLTIP:
        case META_WINDOW_NOTIFICATION:
        case META_WINDOW_COMBO:
        case META_WINDOW_DND:
        /* To complete enum */
        case META_WINDOW_OVERRIDE_OTHER:
          break;
        }
    }

  meta_topic (META_DEBUG_X11, "Calculated type %u for %s, old type %u",
              type, window->desc, type);
  meta_window_set_type (window, type);
}

/**
 * meta_window_x11_configure_notify: (skip)
 * @window: a #MetaWindow
 * @event: a #XConfigureEvent
 *
 * This is used to notify us of an unrequested configuration
 * (only applicable to override redirect windows)
 */
void
meta_window_x11_configure_notify (MetaWindow      *window,
                                  XConfigureEvent *event)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);
  MtkRectangle rect;

  g_assert (window->override_redirect);
  g_assert (priv->frame == NULL);

  meta_window_protocol_to_stage_rect (window,
                                      &MTK_RECTANGLE_INIT (event->x,
                                                           event->y,
                                                           event->width,
                                                           event->height),
                                      &rect);
  meta_window_config_set_rect (window->config, rect);

  priv->client_rect = rect;
  window->buffer_rect = rect;

  meta_window_update_monitor (window, META_WINDOW_UPDATE_MONITOR_FLAGS_NONE);

  /* Whether an override-redirect window is considered fullscreen depends
   * on its geometry.
   */
  if (window->override_redirect)
    meta_display_queue_check_fullscreen (window->display);

  if (!event->override_redirect && !event->send_event)
    {
      meta_topic (META_DEBUG_X11,
                  "Unhandled change of windows override redirect status");
    }

  meta_compositor_sync_window_geometry (window->display->compositor, window, FALSE);
}

void
meta_window_x11_set_allowed_actions_hint (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);
  MetaX11Display *x11_display = window->display->x11_display;
#define MAX_N_ACTIONS 12
  unsigned long data[MAX_N_ACTIONS];
  int i;

  i = 0;
  if (window->has_move_func)
    {
      data[i] = x11_display->atom__NET_WM_ACTION_MOVE;
      ++i;
    }
  if (window->has_resize_func)
    {
      data[i] = x11_display->atom__NET_WM_ACTION_RESIZE;
      ++i;
    }
  if (window->has_fullscreen_func)
    {
      data[i] = x11_display->atom__NET_WM_ACTION_FULLSCREEN;
      ++i;
    }
  if (window->has_minimize_func)
    {
      data[i] = x11_display->atom__NET_WM_ACTION_MINIMIZE;
      ++i;
    }
  /* sticky according to EWMH is different from mutter's sticky;
   * mutter doesn't support EWMH sticky
   */
  if (window->has_maximize_func)
    {
      data[i] = x11_display->atom__NET_WM_ACTION_MAXIMIZE_HORZ;
      ++i;
      data[i] = x11_display->atom__NET_WM_ACTION_MAXIMIZE_VERT;
      ++i;
    }
  /* We always allow this */
  data[i] = x11_display->atom__NET_WM_ACTION_CHANGE_DESKTOP;
  ++i;
  if (window->has_close_func)
    {
      data[i] = x11_display->atom__NET_WM_ACTION_CLOSE;
      ++i;
    }

  /* I guess we always allow above/below operations */
  data[i] = x11_display->atom__NET_WM_ACTION_ABOVE;
  ++i;
  data[i] = x11_display->atom__NET_WM_ACTION_BELOW;
  ++i;

  g_assert (i <= MAX_N_ACTIONS);

  meta_topic (META_DEBUG_X11,
              "Setting _NET_WM_ALLOWED_ACTIONS with %d atoms", i);

  mtk_x11_error_trap_push (x11_display->xdisplay);
  XChangeProperty (x11_display->xdisplay,
                   meta_window_x11_get_xwindow (window),
                   x11_display->atom__NET_WM_ALLOWED_ACTIONS,
                   XA_ATOM,
                   32, PropModeReplace, (guchar*) data, i);

  if (priv->frame)
    {
      XChangeProperty (x11_display->xdisplay,
                       priv->frame->xwindow,
                       x11_display->atom__NET_WM_ALLOWED_ACTIONS,
                       XA_ATOM,
                       32, PropModeReplace, (guchar*) data, i);
    }

  mtk_x11_error_trap_pop (x11_display->xdisplay);
#undef MAX_N_ACTIONS
}

void
meta_window_x11_create_sync_request_alarm (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  if (priv->frame)
    meta_sync_counter_create_sync_alarm (meta_frame_get_sync_counter (priv->frame));

  meta_sync_counter_create_sync_alarm (&priv->sync_counter);
}

void
meta_window_x11_destroy_sync_request_alarm (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  if (priv->frame)
    meta_sync_counter_destroy_sync_alarm (meta_frame_get_sync_counter (priv->frame));

  meta_sync_counter_destroy_sync_alarm (&priv->sync_counter);
}

Window
meta_window_x11_get_toplevel_xwindow (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  return priv->frame ? priv->frame->xwindow : meta_window_x11_get_xwindow (window);
}

void
meta_window_x11_freeze_commits (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  META_WINDOW_X11_GET_CLASS (window_x11)->freeze_commits (window);
}

void
meta_window_x11_thaw_commits (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  META_WINDOW_X11_GET_CLASS (window_x11)->thaw_commits (window);
}

void
meta_window_x11_set_thaw_after_paint (MetaWindow *window,
                                      gboolean    thaw_after_paint)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  priv->thaw_after_paint = thaw_after_paint;
}

gboolean
meta_window_x11_should_thaw_after_paint (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  return priv->thaw_after_paint;
}

gboolean
meta_window_x11_always_update_shape (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);

  return META_WINDOW_X11_GET_CLASS (window_x11)->always_update_shape (window);
}

void
meta_window_x11_surface_rect_to_frame_rect (MetaWindow   *window,
                                            MtkRectangle *surface_rect,
                                            MtkRectangle *frame_rect)

{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);
  MetaFrameBorders borders;

  g_return_if_fail (priv->frame);

  meta_frame_calc_borders (priv->frame, &borders);

  *frame_rect = *surface_rect;
  frame_rect->x += borders.invisible.left;
  frame_rect->y += borders.invisible.top;
  frame_rect->width -= borders.invisible.left + borders.invisible.right;
  frame_rect->height -= borders.invisible.top + borders.invisible.bottom;
}

void
meta_window_x11_surface_rect_to_client_rect (MetaWindow   *window,
                                             MtkRectangle *surface_rect,
                                             MtkRectangle *client_rect)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);
  MetaFrameBorders borders;

  meta_frame_calc_borders (priv->frame, &borders);

  *client_rect = *surface_rect;
  client_rect->x += borders.total.left;
  client_rect->y += borders.total.top;
  client_rect->width -= borders.total.left + borders.total.right;
  client_rect->height -= borders.total.top + borders.total.bottom;
}

MtkRectangle
meta_window_x11_get_client_rect (MetaWindowX11 *window_x11)
{
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  return priv->client_rect;
}

void
meta_window_x11_set_client_rect (MetaWindowX11 *window_x11,
                                 MtkRectangle  *client_rect)
{
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  priv->client_rect = *client_rect;
}

static gboolean
has_requested_dont_bypass_compositor (MetaWindowX11 *window_x11)
{
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  return priv->bypass_compositor == META_BYPASS_COMPOSITOR_HINT_OFF;
}

gboolean
meta_window_x11_can_unredirect (MetaWindowX11 *window_x11)
{
  MetaWindow *window = META_WINDOW (window_x11);
  MetaWindowX11Private *priv =
    meta_window_x11_get_instance_private (window_x11);

  if (has_requested_dont_bypass_compositor (window_x11))
    return FALSE;

  if (window->opacity != 0xFF)
    return FALSE;

  if (priv->shape_region != NULL)
    return FALSE;

  if (!window->monitor)
    return FALSE;

  if (meta_window_is_fullscreen (window))
    return TRUE;

  if (meta_window_is_screen_sized (window))
    return TRUE;

  if (window->override_redirect)
    {
      MtkRectangle window_rect;
      MtkRectangle logical_monitor_layout;
      MetaLogicalMonitor *logical_monitor = window->monitor;

      meta_window_get_frame_rect (window, &window_rect);
      logical_monitor_layout =
        meta_logical_monitor_get_layout (logical_monitor);

      if (mtk_rectangle_equal (&window_rect, &logical_monitor_layout))
        return TRUE;
    }

  return FALSE;
}

MetaSyncCounter *
meta_window_x11_get_sync_counter (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  return &priv->sync_counter;
}

MetaFrame*
meta_window_x11_get_frame (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  return priv->frame;
}

gboolean
meta_window_x11_get_frame_borders (MetaWindow       *window,
                                   MetaFrameBorders *borders)
{
  MetaFrame *frame = meta_window_x11_get_frame (window);

  if (!frame)
    return FALSE;

  meta_frame_calc_borders (frame, borders);
  return TRUE;
}

gboolean
meta_window_x11_is_awaiting_sync_response (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  if (priv->frame &&
      meta_sync_counter_is_waiting_response (meta_frame_get_sync_counter (priv->frame)))
    return TRUE;

  return meta_sync_counter_is_waiting_response (&priv->sync_counter);
}

void
meta_window_x11_check_update_resize (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);
  MetaWindowDrag *window_drag;

  if (priv->frame &&
      meta_sync_counter_is_waiting (meta_frame_get_sync_counter (priv->frame)))
    return;

  if (meta_sync_counter_is_waiting (&priv->sync_counter))
    return;

  window_drag =
    meta_compositor_get_current_window_drag (window->display->compositor);
  meta_window_drag_update_resize (window_drag);
}

gboolean
meta_window_x11_has_alpha_channel (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;
  MetaWindowX11 *windox_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (windox_x11);
  int n_xvisuals;
  gboolean has_alpha;
  XVisualInfo *xvisual_info;
  XVisualInfo template = {
    .visualid = XVisualIDFromVisual (priv->xvisual),
  };

  xvisual_info = XGetVisualInfo (meta_x11_display_get_xdisplay (x11_display),
                                 VisualIDMask,
                                 &template,
                                 &n_xvisuals);
  if (!xvisual_info)
    return FALSE;

  has_alpha = (xvisual_info->depth >
               __builtin_popcount (xvisual_info->red_mask |
                                   xvisual_info->green_mask |
                                   xvisual_info->blue_mask));
  XFree (xvisual_info);

  return has_alpha;
}

/**
 * meta_window_x11_get_xwindow: (skip)
 * @window: a #MetaWindow
 *
 */
Window
meta_window_x11_get_xwindow (MetaWindow *window)
{
  MetaWindowX11 *window_x11;
  MetaWindowX11Private *priv;

  g_return_val_if_fail (META_IS_WINDOW_X11 (window), None);

  window_x11 = META_WINDOW_X11 (window);
  priv = meta_window_x11_get_instance_private (window_x11);

  return priv->xwindow;
}

Window
meta_window_x11_get_xgroup_leader (MetaWindow *window)
{
  MetaWindowX11 *window_x11;
  MetaWindowX11Private *priv;

  g_return_val_if_fail (META_IS_WINDOW_X11 (window), None);

  window_x11 = META_WINDOW_X11 (window);
  priv = meta_window_x11_get_instance_private (window_x11);

  return priv->xgroup_leader;
}

Window
meta_window_x11_get_user_time_window (MetaWindow *window)
{
  MetaWindowX11 *window_x11;
  MetaWindowX11Private *priv;

  g_return_val_if_fail (META_IS_WINDOW_X11 (window), None);

  window_x11 = META_WINDOW_X11 (window);
  priv = meta_window_x11_get_instance_private (window_x11);

  return priv->user_time_window;
}

Window
meta_window_x11_get_xtransient_for (MetaWindow *window)
{
  MetaWindow *transient_for;

  g_return_val_if_fail (META_IS_WINDOW_X11 (window), None);

  transient_for = meta_window_get_transient_for (window);
  if (transient_for)
    return meta_window_x11_get_xwindow (transient_for);

  return None;
}

gboolean
meta_window_x11_has_pointer (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;
  Window root, child;
  double root_x, root_y, x, y;
  XIButtonState buttons;
  XIModifierState mods;
  XIGroupState group;

  mtk_x11_error_trap_push (x11_display->xdisplay);
  XIQueryPointer (x11_display->xdisplay,
                  META_VIRTUAL_CORE_POINTER_ID,
                  x11_display->xroot,
                  &root, &child,
                  &root_x, &root_y, &x, &y,
                  &buttons, &mods, &group);
  mtk_x11_error_trap_pop (x11_display->xdisplay);
  free (buttons.mask);

  return meta_x11_display_lookup_x_window (x11_display, child) == window;
}

gboolean
meta_window_x11_same_application (MetaWindow *window,
                                  MetaWindow *other_window)
{
  MetaGroup *group = meta_window_x11_get_group (window);
  MetaGroup *other_group = meta_window_x11_get_group (other_window);

  return (group != NULL &&
          other_group != NULL &&
          group == other_group);
}

/**
 * meta_window_x11_get_group: (skip)
 * @window: a #MetaWindow
 *
 * Returns: (transfer none) (nullable): the #MetaGroup of the window
 */
MetaGroup*
meta_window_x11_get_group (MetaWindow *window)
{
  MetaWindowX11 *window_x11;
  MetaWindowX11Private *priv;

  g_return_val_if_fail (META_IS_WINDOW_X11 (window), NULL);

  if (window->unmanaging)
    return NULL;

  window_x11 = META_WINDOW_X11 (window);
  priv = meta_window_x11_get_private (window_x11);

  return priv->group;
}

static void
meta_window_x11_compute_group (MetaWindow *window)
{
  MetaGroup *group = NULL;
  MetaX11Display *x11_display = window->display->x11_display;
  MetaWindow *ancestor = meta_window_find_root_ancestor (window);
  Window win_leader = meta_window_x11_get_xgroup_leader (window);
  Window xwindow = meta_window_x11_get_xwindow (window);
  Window ancestor_leader = meta_window_x11_get_xgroup_leader (ancestor);
  MetaWindowX11Private *priv =
    meta_window_x11_get_private (META_WINDOW_X11 (window));

  /* use window->xwindow if no window->xgroup_leader */

  /* Determine the ancestor of the window; its group setting will override the
   * normal grouping rules; see bug 328211.
   */

  if (x11_display->groups_by_leader)
    {
      if (ancestor != window && ancestor_leader != None)
        group = meta_window_x11_get_group (ancestor);

      if (win_leader != None && group == NULL)
        group = g_hash_table_lookup (x11_display->groups_by_leader,
                                     &win_leader);

      if (group == NULL)
        group = g_hash_table_lookup (x11_display->groups_by_leader,
                                     &xwindow);
    }

  if (group != NULL)
    {
      priv->group = group;
      group->refcount += 1;
    }
  else
    {

      if (ancestor != window && ancestor_leader != None)
        group = meta_group_new (x11_display, ancestor_leader);
      else if (win_leader != None)
        group = meta_group_new (x11_display, win_leader);
      else
        group = meta_group_new (x11_display, xwindow);

      priv->group = group;
    }

  if (!priv->group)
    return;

  priv->group->windows = g_slist_prepend (priv->group->windows, window);

  meta_topic (META_DEBUG_X11,
              "Adding %s to group with leader 0x%lx",
              window->desc, group->group_leader);
}

static void
remove_window_from_group (MetaWindow *window)
{
  MetaWindowX11Private *priv =
    meta_window_x11_get_private (META_WINDOW_X11 (window));

  if (priv->group != NULL)
    {
      meta_topic (META_DEBUG_X11,
                  "Removing %s from group with leader 0x%lx",
                  window->desc, priv->group->group_leader);

      priv->group->windows =
        g_slist_remove (priv->group->windows,
                        window);
      meta_group_unref (priv->group);
      priv->group = NULL;
    }
}

void
meta_window_x11_group_leader_changed (MetaWindow *window)
{
  remove_window_from_group (window);
  meta_window_x11_compute_group (window);
}

void
meta_window_x11_shutdown_group (MetaWindow *window)
{
  remove_window_from_group (window);
}

void
meta_window_x11_configure (MetaWindow *window)
{
  g_autoptr (MetaWindowConfig) window_config = NULL;
  MtkRectangle new_rect;

  window_config = meta_window_config_new_from (window->config);
  if (window->showing_for_first_time)
    meta_window_config_set_initial (window_config);
  meta_window_emit_configure (window, window_config);

  new_rect = meta_window_config_get_rect (window_config);

  meta_topic (META_DEBUG_GEOMETRY,
              "Window %s pre-configured at (%i,%i) [%ix%i]",
              window->desc, new_rect.x, new_rect.y, new_rect.width, new_rect.height);

  if (meta_window_config_has_position (window_config))
    {
      window->size_hints.x = new_rect.x;
      window->size_hints.y = new_rect.y;
      window->size_hints.width = new_rect.width;
      window->size_hints.height = new_rect.height;
    }

  meta_window_apply_config (window, window_config,
                            META_WINDOW_APPLY_FLAG_NONE);
}
