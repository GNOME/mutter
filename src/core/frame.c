/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Mutter X window decorations */

/* 
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2003, 2004 Red Hat, Inc.
 * Copyright (C) 2005 Elijah Newren
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

#include <config.h>
#include "frame.h"
#include "bell.h"
#include <meta/errors.h>
#include "keybindings-private.h"

#include <X11/extensions/Xrender.h>

#define EVENT_MASK (SubstructureRedirectMask |                     \
                    StructureNotifyMask | SubstructureNotifyMask | \
                    ExposureMask |                                 \
                    ButtonPressMask | ButtonReleaseMask |          \
                    PointerMotionMask | PointerMotionHintMask |    \
                    EnterWindowMask | LeaveWindowMask |            \
                    FocusChangeMask |                              \
                    ColormapChangeMask)

void
meta_window_ensure_frame (MetaWindow *window)
{
  MetaFrame *frame;
  XSetWindowAttributes attrs;
  gulong create_serial;
  
  if (window->frame)
    return;
  
  frame = g_new (MetaFrame, 1);

  frame->window = window;
  frame->xwindow = None;

  frame->rect = window->rect;
  frame->child_x = 0;
  frame->child_y = 0;
  frame->bottom_height = 0;
  frame->right_width = 0;
  frame->current_cursor = 0;

  frame->is_flashing = FALSE;

  meta_verbose ("Frame geometry %d,%d  %dx%d\n",
                frame->rect.x, frame->rect.y,
                frame->rect.width, frame->rect.height);

  attrs.event_mask = EVENT_MASK;
  XChangeWindowAttributes (window->display->xdisplay,
			   frame->xwindow, CWEventMask, &attrs);

  create_serial = XNextRequest (window->display->xdisplay);

  frame->xwindow = XCreateWindow (window->display->xdisplay,
                                  DefaultRootWindow (window->display->xdisplay),
                                  frame->rect.x, frame->rect.y,
                                  frame->rect.width, frame->rect.height,
                                  0,
                                  CopyFromParent,
                                  InputOnly,
                                  CopyFromParent,
                                  CWEventMask,
                                  &attrs);

  meta_stack_tracker_record_add (window->screen->stack_tracker,
                                 frame->xwindow,
                                 create_serial);

  meta_verbose ("Frame for %s is 0x%lx\n", frame->window->desc, frame->xwindow);

  meta_display_register_x_window (window->display, &frame->xwindow, window);

  meta_error_trap_push (window->display);
  if (window->mapped)
    {
      window->mapped = FALSE; /* the reparent will unmap the window,
                               * we don't want to take that as a withdraw
                               */
      meta_topic (META_DEBUG_WINDOW_STATE,
                  "Incrementing unmaps_pending on %s for reparent\n", window->desc);
      window->unmaps_pending += 1;
    }
  /* window was reparented to this position */
  window->rect.x = 0;
  window->rect.y = 0;

  meta_stack_tracker_record_remove (window->screen->stack_tracker,
                                    window->xwindow,
                                    XNextRequest (window->display->xdisplay));
  /* FIXME handle this error */
  meta_error_trap_pop (window->display);
  
  /* stick frame to the window */
  window->frame = frame;

  /* Move keybindings to frame instead of window */
  meta_window_grab_keys (window);

  meta_ui_map_frame (frame->window->screen->ui, frame->xwindow);
}

void
meta_window_destroy_frame (MetaWindow *window)
{
  MetaFrame *frame;
  MetaFrameBorders borders;
  
  if (window->frame == NULL)
    return;

  meta_verbose ("Unframing window %s\n", window->desc);
  
  frame = window->frame;

  meta_frame_calc_borders (frame, &borders);
  
  meta_bell_notify_frame_destroy (frame);
  
  /* Unparent the client window; it may be destroyed,
   * thus the error trap.
   */
  meta_error_trap_push (window->display);
  if (window->mapped)
    {
      window->mapped = FALSE; /* Keep track of unmapping it, so we
                               * can identify a withdraw initiated
                               * by the client.
                               */
      meta_topic (META_DEBUG_WINDOW_STATE,
                  "Incrementing unmaps_pending on %s for reparent back to root\n", window->desc);
      window->unmaps_pending += 1;
    }
  meta_stack_tracker_record_add (window->screen->stack_tracker,
                                 window->xwindow,
                                 XNextRequest (window->display->xdisplay));
  meta_error_trap_pop (window->display);

  XDestroyWindow (window->display->xdisplay, frame->xwindow);

  meta_display_unregister_x_window (window->display,
                                    frame->xwindow);
  
  window->frame = NULL;
  if (window->frame_bounds)
    {
      cairo_region_destroy (window->frame_bounds);
      window->frame_bounds = NULL;
    }

  /* Move keybindings to window instead of frame */
  meta_window_grab_keys (window);
  
  g_free (frame);
  
  /* Put our state back where it should be */
  meta_window_queue (window, META_QUEUE_CALC_SHOWING);
  meta_window_queue (window, META_QUEUE_MOVE_RESIZE);
}


MetaFrameFlags
meta_frame_get_flags (MetaFrame *frame)
{
  MetaFrameFlags flags;

  flags = 0;

  if (frame->window->border_only)
    {
      ; /* FIXME this may disable the _function_ as well as decor
         * in some cases, which is sort of wrong.
         */
    }
  else
    {
      flags |= META_FRAME_ALLOWS_MENU;
      
      if (frame->window->has_close_func)
        flags |= META_FRAME_ALLOWS_DELETE;
      
      if (frame->window->has_maximize_func)
        flags |= META_FRAME_ALLOWS_MAXIMIZE;
      
      if (frame->window->has_minimize_func)
        flags |= META_FRAME_ALLOWS_MINIMIZE;
      
      if (frame->window->has_shade_func)
        flags |= META_FRAME_ALLOWS_SHADE;
    }  
  
  if (META_WINDOW_ALLOWS_MOVE (frame->window))
    flags |= META_FRAME_ALLOWS_MOVE;

  if (META_WINDOW_ALLOWS_HORIZONTAL_RESIZE (frame->window))
    flags |= META_FRAME_ALLOWS_HORIZONTAL_RESIZE;

  if (META_WINDOW_ALLOWS_VERTICAL_RESIZE (frame->window))
    flags |= META_FRAME_ALLOWS_VERTICAL_RESIZE;
  
  if (meta_window_appears_focused (frame->window))
    flags |= META_FRAME_HAS_FOCUS;

  if (frame->window->shaded)
    flags |= META_FRAME_SHADED;

  if (frame->window->on_all_workspaces_requested)
    flags |= META_FRAME_STUCK;

  /* FIXME: Should we have some kind of UI for windows that are just vertically
   * maximized or just horizontally maximized?
   */
  if (META_WINDOW_MAXIMIZED (frame->window))
    flags |= META_FRAME_MAXIMIZED;

  if (META_WINDOW_TILED_LEFT (frame->window))
    flags |= META_FRAME_TILED_LEFT;

  if (META_WINDOW_TILED_RIGHT (frame->window))
    flags |= META_FRAME_TILED_RIGHT;

  if (frame->window->fullscreen)
    flags |= META_FRAME_FULLSCREEN;

  if (frame->is_flashing)
    flags |= META_FRAME_IS_FLASHING;

  if (frame->window->wm_state_above)
    flags |= META_FRAME_ABOVE;
  
  return flags;
}

void
meta_frame_borders_clear (MetaFrameBorders *self)
{
  self->visible.top    = self->invisible.top    = self->total.top    = 0;
  self->visible.bottom = self->invisible.bottom = self->total.bottom = 0;
  self->visible.left   = self->invisible.left   = self->total.left   = 0;
  self->visible.right  = self->invisible.right  = self->total.right  = 0;
}

void
meta_frame_calc_borders (MetaFrame        *frame,
                         MetaFrameBorders *borders)
{
  meta_frame_borders_clear (borders);
}

void
meta_frame_clear_cached_borders (MetaFrame *frame)
{
  frame->borders_cached = FALSE;
}

gboolean
meta_frame_sync_to_window (MetaFrame *frame,
                           int        resize_gravity,
                           gboolean   need_move,
                           gboolean   need_resize)
{
  meta_topic (META_DEBUG_GEOMETRY,
              "Syncing frame geometry %d,%d %dx%d (SE: %d,%d)\n",
              frame->rect.x, frame->rect.y,
              frame->rect.width, frame->rect.height,
              frame->rect.x + frame->rect.width,
              frame->rect.y + frame->rect.height);

  XMoveResizeWindow (frame->window->display->xdisplay,
                     frame->xwindow,
                     frame->rect.x,
                     frame->rect.y,
                     frame->rect.width,
                     frame->rect.height);

  return need_resize;
}

cairo_region_t *
meta_frame_get_frame_bounds (MetaFrame *frame)
{
  cairo_rectangle_int_t rect;

  rect.x = frame->window->rect.x;
  rect.y = frame->window->rect.y;
  rect.width = frame->window->rect.width;
  rect.height = frame->window->rect.height;

  return cairo_region_create_rectangles (&rect, 1);
}

void
meta_frame_queue_draw (MetaFrame *frame)
{
}

void
meta_frame_set_screen_cursor (MetaFrame	*frame,
			      MetaCursor cursor)
{
  Cursor xcursor;
  if (cursor == frame->current_cursor)
    return;
  frame->current_cursor = cursor;
  if (cursor == META_CURSOR_DEFAULT)
    XUndefineCursor (frame->window->display->xdisplay, frame->xwindow);
  else
    { 
      xcursor = meta_display_create_x_cursor (frame->window->display, cursor);
      XDefineCursor (frame->window->display->xdisplay, frame->xwindow, xcursor);
      XFlush (frame->window->display->xdisplay);
      XFreeCursor (frame->window->display->xdisplay, xcursor);
    }
}

Window
meta_frame_get_xwindow (MetaFrame *frame)
{
  return frame->xwindow;
}
