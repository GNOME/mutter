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

#include "config.h"

#include "core/frame.h"

#include "backends/x11/meta-backend-x11.h"
#include "compositor/compositor-private.h"
#include "core/bell.h"
#include "core/keybindings-private.h"
#include "mtk/mtk-x11.h"
#include "x11/meta-x11-display-private.h"
#include "x11/window-x11-private.h"
#include "x11/window-props.h"

#include <X11/Xatom.h>
#include <X11/extensions/shape.h>

#define EVENT_MASK (SubstructureRedirectMask |                     \
                    StructureNotifyMask | SubstructureNotifyMask | \
                    PropertyChangeMask | FocusChangeMask)

void
meta_window_ensure_frame (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;
  unsigned long data[1] = { 1 };

  mtk_x11_error_trap_push (x11_display->xdisplay);

  XChangeProperty (x11_display->xdisplay,
                   meta_window_x11_get_xwindow (window),
                   x11_display->atom__MUTTER_NEEDS_FRAME,
                   XA_CARDINAL,
                   32, PropModeReplace, (guchar*) data, 1);

  mtk_x11_error_trap_pop (x11_display->xdisplay);
}

void
meta_window_x11_set_frame_xwindow (MetaWindow *window,
                                   Window      xframe)
{
  MetaX11Display *x11_display = window->display->x11_display;
  XSetWindowAttributes attrs;
  gulong create_serial = 0;
  MetaFrame *frame;

  if (window->frame)
    return;

  frame = g_new0 (MetaFrame, 1);

  frame->window = window;
  frame->xwindow = xframe;

  frame->rect = window->rect;
  frame->child_x = 0;
  frame->child_y = 0;
  frame->bottom_height = 0;
  frame->right_width = 0;

  frame->borders_cached = FALSE;

  meta_sync_counter_init (&frame->sync_counter, window, frame->xwindow);

  window->frame = frame;

  meta_verbose ("Frame geometry %d,%d  %dx%d",
                frame->rect.x, frame->rect.y,
                frame->rect.width, frame->rect.height);

  meta_verbose ("Setting frame 0x%lx for window %s, "
                "frame geometry %d,%d  %dx%d",
                xframe, window->desc,
                frame->rect.x, frame->rect.y,
                frame->rect.width, frame->rect.height);

  meta_stack_tracker_record_add (window->display->stack_tracker,
                                 frame->xwindow,
                                 create_serial);

  meta_verbose ("Frame for %s is 0x%lx", frame->window->desc, frame->xwindow);

  mtk_x11_error_trap_push (x11_display->xdisplay);

  attrs.event_mask = EVENT_MASK;
  XChangeWindowAttributes (x11_display->xdisplay,
			   frame->xwindow, CWEventMask, &attrs);

  if (META_X11_DISPLAY_HAS_SHAPE (x11_display))
    XShapeSelectInput (x11_display->xdisplay, frame->xwindow, ShapeNotifyMask);

  meta_x11_display_register_x_window (x11_display, &frame->xwindow, window);

  if (window->mapped)
    {
      window->mapped = FALSE; /* the reparent will unmap the window,
                               * we don't want to take that as a withdraw
                               */
      meta_topic (META_DEBUG_WINDOW_STATE,
                  "Incrementing unmaps_pending on %s for reparent", window->desc);
      window->unmaps_pending += 1;
    }

  meta_stack_tracker_record_remove (window->display->stack_tracker,
                                    meta_window_x11_get_xwindow (window),
                                    XNextRequest (x11_display->xdisplay));
  XReparentWindow (x11_display->xdisplay,
                   meta_window_x11_get_xwindow (window),
                   frame->xwindow,
                   frame->child_x,
                   frame->child_y);
  window->reparents_pending += 1;
  /* FIXME handle this error */
  mtk_x11_error_trap_pop (x11_display->xdisplay);

  /* Ensure focus is restored after the unmap/map events triggered
   * by XReparentWindow().
   */
  if (meta_window_has_focus (window))
    window->restore_focus_on_map = TRUE;

  /* stick frame to the window */
  window->frame = frame;

  meta_window_reload_property_from_xwindow (window, frame->xwindow,
                                            x11_display->atom__NET_WM_SYNC_REQUEST_COUNTER,
                                            TRUE);
  meta_window_reload_property_from_xwindow (window, frame->xwindow,
                                            x11_display->atom__NET_WM_OPAQUE_REGION,
                                            TRUE);

  mtk_x11_error_trap_push (x11_display->xdisplay);
  XMapWindow (x11_display->xdisplay, frame->xwindow);
  mtk_x11_error_trap_pop (x11_display->xdisplay);

  /* Move keybindings to frame instead of window */
  meta_window_grab_keys (window);

  /* Even though the property was already set, notify
   * on it so other bits of the machinery catch up
   * on the new frame.
   */
  g_object_notify (G_OBJECT (window), "decorated");
}

void
meta_window_destroy_frame (MetaWindow *window)
{
  MetaFrame *frame;
  MetaFrameBorders borders;
  MetaX11Display *x11_display;

  if (window->frame == NULL)
    return;

  x11_display = window->display->x11_display;

  meta_verbose ("Unframing window %s", window->desc);

  frame = window->frame;

  meta_frame_calc_borders (frame, &borders);

  /* Unparent the client window; it may be destroyed,
   * thus the error trap.
   */
  mtk_x11_error_trap_push (x11_display->xdisplay);
  if (window->mapped)
    {
      window->mapped = FALSE; /* Keep track of unmapping it, so we
                               * can identify a withdraw initiated
                               * by the client.
                               */
      meta_topic (META_DEBUG_WINDOW_STATE,
                  "Incrementing unmaps_pending on %s for reparent back to root", window->desc);
      window->unmaps_pending += 1;
    }

  if (!x11_display->closing)
    {
      if (!window->unmanaging)
        {
          meta_stack_tracker_record_add (window->display->stack_tracker,
                                         meta_window_x11_get_xwindow (window),
                                         XNextRequest (x11_display->xdisplay));
        }

      XReparentWindow (x11_display->xdisplay,
                       meta_window_x11_get_xwindow (window),
                       x11_display->xroot,
                       /* Using anything other than client root window coordinates
                        * coordinates here means we'll need to ensure a configure
                        * notify event is sent; see bug 399552.
                        */
                       window->frame->rect.x + borders.invisible.left,
                       window->frame->rect.y + borders.invisible.top);
      window->reparents_pending += 1;
    }

  if (META_X11_DISPLAY_HAS_SHAPE (x11_display))
    XShapeSelectInput (x11_display->xdisplay, frame->xwindow, NoEventMask);

  XDeleteProperty (x11_display->xdisplay,
                   meta_window_x11_get_xwindow (window),
                   x11_display->atom__MUTTER_NEEDS_FRAME);

  mtk_x11_error_trap_pop (x11_display->xdisplay);

  /* Ensure focus is restored after the unmap/map events triggered
   * by XReparentWindow().
   */
  if (meta_window_has_focus (window))
    window->restore_focus_on_map = TRUE;

  meta_x11_display_unregister_x_window (x11_display, frame->xwindow);

  window->frame = NULL;
  g_clear_pointer (&window->frame_bounds, mtk_region_unref);
  g_clear_pointer (&frame->opaque_region, mtk_region_unref);

  /* Move keybindings to window instead of frame */
  meta_window_grab_keys (window);

  meta_sync_counter_clear (&frame->sync_counter);

  g_free (frame);

  /* Put our state back where it should be */
  if (!window->unmanaging)
    meta_compositor_sync_updates_frozen (window->display->compositor, window);

  meta_window_queue (window, META_QUEUE_CALC_SHOWING);
  meta_window_queue (window, META_QUEUE_MOVE_RESIZE);
}

void
meta_frame_borders_clear (MetaFrameBorders *self)
{
  self->visible.top    = self->invisible.top    = self->total.top    = 0;
  self->visible.bottom = self->invisible.bottom = self->total.bottom = 0;
  self->visible.left   = self->invisible.left   = self->total.left   = 0;
  self->visible.right  = self->invisible.right  = self->total.right  = 0;
}

static void
meta_frame_query_borders (MetaFrame        *frame,
                          MetaFrameBorders *borders)
{
  MetaWindow *window = frame->window;
  MetaX11Display *x11_display = window->display->x11_display;
  int format, res;
  Atom type;
  unsigned long nitems, bytes_after;
  unsigned char *data;

  if (!frame->xwindow)
    return;

  mtk_x11_error_trap_push (x11_display->xdisplay);

  res = XGetWindowProperty (x11_display->xdisplay,
                            frame->xwindow,
                            x11_display->atom__GTK_FRAME_EXTENTS,
                            0, 4,
                            False, XA_CARDINAL,
                            &type, &format,
                            &nitems, &bytes_after,
                            (unsigned char **) &data);

  if (mtk_x11_error_trap_pop_with_return (x11_display->xdisplay) != Success)
    return;

  if (res == Success && nitems == 4)
    {
      borders->invisible = (MetaFrameBorder) {
        ((long *) data)[0],
        ((long *) data)[1],
        ((long *) data)[2],
        ((long *) data)[3],
      };
    }

  g_clear_pointer (&data, XFree);

  mtk_x11_error_trap_push (x11_display->xdisplay);

  res = XGetWindowProperty (x11_display->xdisplay,
                            frame->xwindow,
                            x11_display->atom__MUTTER_FRAME_EXTENTS,
                            0, 4,
                            False, XA_CARDINAL,
                            &type, &format,
                            &nitems, &bytes_after,
                            (unsigned char **) &data);

  if (mtk_x11_error_trap_pop_with_return (x11_display->xdisplay) != Success)
    return;

  if (res == Success && nitems == 4)
    {
      borders->visible = (MetaFrameBorder) {
        ((long *) data)[0],
        ((long *) data)[1],
        ((long *) data)[2],
        ((long *) data)[3],
      };
    }

  g_clear_pointer (&data, XFree);

  borders->total = (MetaFrameBorder) {
    borders->invisible.left + frame->cached_borders.visible.left,
    borders->invisible.right + frame->cached_borders.visible.right,
    borders->invisible.top + frame->cached_borders.visible.top,
    borders->invisible.bottom + frame->cached_borders.visible.bottom,
  };
}

void
meta_frame_calc_borders (MetaFrame        *frame,
                         MetaFrameBorders *borders)
{
  /* Save on if statements and potential uninitialized values
   * in callers -- if there's no frame, then zero the borders. */
  if (frame == NULL)
    meta_frame_borders_clear (borders);
  else
    {
      if (!frame->borders_cached)
        {
          meta_frame_query_borders (frame, &frame->cached_borders);
          frame->borders_cached = TRUE;
        }

      *borders = frame->cached_borders;
    }
}

void
meta_frame_clear_cached_borders (MetaFrame *frame)
{
  frame->borders_cached = FALSE;
}

gboolean
meta_frame_sync_to_window (MetaFrame *frame,
                           gboolean   need_resize)
{
  MetaWindow *window = frame->window;
  MetaX11Display *x11_display = window->display->x11_display;

  meta_topic (META_DEBUG_GEOMETRY,
              "Syncing frame geometry %d,%d %dx%d (SE: %d,%d)",
              frame->rect.x, frame->rect.y,
              frame->rect.width, frame->rect.height,
              frame->rect.x + frame->rect.width,
              frame->rect.y + frame->rect.height);

  mtk_x11_error_trap_push (x11_display->xdisplay);

  XMoveResizeWindow (x11_display->xdisplay,
                     frame->xwindow,
                     frame->rect.x,
                     frame->rect.y,
                     frame->rect.width,
                     frame->rect.height);

  mtk_x11_error_trap_pop (x11_display->xdisplay);

  return need_resize;
}

MtkRegion *
meta_frame_get_frame_bounds (MetaFrame *frame)
{
  MetaFrameBorders borders;
  MtkRegion *bounds;

  meta_frame_calc_borders (frame, &borders);
  /* FIXME: currently just the client area, should shape closer to
   * frame border, incl. rounded corners.
   */
  bounds = mtk_region_create_rectangle (&(MtkRectangle) {
    borders.total.left,
    borders.total.top,
    frame->rect.width - borders.total.left - borders.total.right,
    frame->rect.height - borders.total.top - borders.total.bottom,
  });

  return bounds;
}

Window
meta_frame_get_xwindow (MetaFrame *frame)
{
  return frame->xwindow;
}

static void
send_configure_notify (MetaFrame *frame)
{
  MetaX11Display *x11_display = frame->window->display->x11_display;
  XEvent event = { 0 };

  /* We never get told by the frames client, just reassert the
   * current frame size.
   */
  event.type = ConfigureNotify;
  event.xconfigure.display = x11_display->xdisplay;
  event.xconfigure.event = frame->xwindow;
  event.xconfigure.window = frame->xwindow;
  event.xconfigure.x = frame->rect.x;
  event.xconfigure.y = frame->rect.y;
  event.xconfigure.width = frame->rect.width;
  event.xconfigure.height = frame->rect.height;
  event.xconfigure.border_width = 0;
  event.xconfigure.above = None;
  event.xconfigure.override_redirect = False;

  mtk_x11_error_trap_push (x11_display->xdisplay);
  XSendEvent (x11_display->xdisplay,
              frame->xwindow,
              False, StructureNotifyMask, &event);
  mtk_x11_error_trap_pop (x11_display->xdisplay);
}

gboolean
meta_frame_handle_xevent (MetaFrame *frame,
                          XEvent    *xevent)
{
  MetaWindow *window = frame->window;
  MetaX11Display *x11_display = window->display->x11_display;

  if (xevent->xany.type == PropertyNotify &&
      xevent->xproperty.state == PropertyNewValue &&
      (xevent->xproperty.atom == x11_display->atom__GTK_FRAME_EXTENTS ||
       xevent->xproperty.atom == x11_display->atom__MUTTER_FRAME_EXTENTS))
    {
      meta_window_frame_size_changed (window);
      meta_window_queue (window, META_QUEUE_MOVE_RESIZE);
      return TRUE;
    }
  else if (xevent->xany.type == PropertyNotify &&
           xevent->xproperty.state == PropertyNewValue &&
           (xevent->xproperty.atom == x11_display->atom__NET_WM_SYNC_REQUEST_COUNTER ||
            xevent->xproperty.atom == x11_display->atom__NET_WM_OPAQUE_REGION))
    {
      meta_window_reload_property_from_xwindow (window, frame->xwindow,
                                                xevent->xproperty.atom, FALSE);
      return TRUE;
    }
  else if (xevent->xany.type == ConfigureRequest &&
           xevent->xconfigurerequest.window == frame->xwindow)
    {
      send_configure_notify (frame);
      return TRUE;
    }

  return FALSE;
}

GSubprocess *
meta_frame_launch_client (MetaX11Display *x11_display,
                          const char     *display_name)
{
  g_autoptr(GSubprocessLauncher) launcher = NULL;
  g_autoptr (GError) error = NULL;
  GSubprocess *proc;
  const char *args[2];

  args[0] = MUTTER_LIBEXECDIR "/mutter-x11-frames";
  args[1] = NULL;

  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);
  g_subprocess_launcher_setenv (launcher, "DISPLAY", display_name, TRUE);

  proc = g_subprocess_launcher_spawnv (launcher, args, &error);
  if (error)
    {
      if (g_error_matches (error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT))
        {
          /* Fallback case for uninstalled tests, relies on CWD being
           * the builddir, as it is the case during "ninja test".
           */
          g_clear_error (&error);
          args[0] = "./src/frames/mutter-x11-frames";
          proc = g_subprocess_launcher_spawnv (launcher, args, &error);
        }

      if (error)
        {
          g_warning ("Could not launch X11 frames client: %s", error->message);
          return NULL;
        }
    }

  return proc;
}

/**
 * meta_frame_type_to_string:
 * @type: a #MetaFrameType
 *
 * Converts a frame type enum value to the name string that would
 * appear in the theme definition file.
 *
 * Return value: the string value
 */
const char *
meta_frame_type_to_string (MetaFrameType type)
{
  switch (type)
    {
    case META_FRAME_TYPE_NORMAL:
      return "normal";
    case META_FRAME_TYPE_DIALOG:
      return "dialog";
    case META_FRAME_TYPE_MODAL_DIALOG:
      return "modal_dialog";
    case META_FRAME_TYPE_UTILITY:
      return "utility";
    case META_FRAME_TYPE_MENU:
      return "menu";
    case META_FRAME_TYPE_BORDER:
      return "border";
    case META_FRAME_TYPE_ATTACHED:
      return "attached";
    case  META_FRAME_TYPE_LAST:
      break;
    }

  return "<unknown>";
}

MetaSyncCounter *
meta_frame_get_sync_counter (MetaFrame *frame)
{
  return &frame->sync_counter;
}

void
meta_frame_set_opaque_region (MetaFrame *frame,
                              MtkRegion *region)
{
  MetaWindow *window = frame->window;

  if (mtk_region_equal (frame->opaque_region, region))
    return;

  g_clear_pointer (&frame->opaque_region, mtk_region_unref);

  if (region != NULL)
    frame->opaque_region = mtk_region_ref (region);

  meta_compositor_window_shape_changed (window->display->compositor, window);
}
