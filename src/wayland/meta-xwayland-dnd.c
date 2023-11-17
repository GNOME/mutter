/*
 * Copyright © 2012 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* The file is loosely based on xwayland/selection.c from Weston */

#include "config.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <glib-unix.h>
#include <gio/gunixoutputstream.h>
#include <gio/gunixinputstream.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xfixes.h>

#include "core/meta-workspace-manager-private.h"
#include "core/workspace-private.h"
#include "mtk/mtk-x11.h"
#include "wayland/meta-wayland-data-device.h"
#include "wayland/meta-xwayland-private.h"
#include "wayland/meta-xwayland-dnd-private.h"
#include "wayland/meta-xwayland.h"
#include "x11/meta-x11-display-private.h"
#include "x11/window-x11.h"

#define INCR_CHUNK_SIZE (128 * 1024)
#define XDND_VERSION 5

struct _MetaWaylandDataSourceXWayland
{
  MetaWaylandDataSource parent;
  MetaXWaylandDnd *dnd;
  gboolean has_utf8_string_atom;
};

struct _MetaXWaylandDnd
{
  MetaXWaylandManager *manager;

  Window owner;
  Time client_message_timestamp;
  MetaWaylandDataSource *source; /* owned by MetaWaylandDataDevice */
  MetaWaylandSurface *focus_surface;
  Window dnd_window[2]; /* Mutter-internal windows, act as peer on wayland drop sites */
  Window dnd_dest; /* X11 drag dest window */
  guint32 last_motion_time;
  int current_dnd_window;
};

typedef struct _DndCandidateDevice DndCandidateDevice;

struct _DndCandidateDevice
{
  MetaWaylandSeat *seat;
  ClutterInputDevice *device;
  ClutterEventSequence *sequence;
  MetaWaylandSurface *focus;
  graphene_point_t pos;
};

enum
{
  ATOM_DND_SELECTION,
  ATOM_DND_AWARE,
  ATOM_DND_STATUS,
  ATOM_DND_POSITION,
  ATOM_DND_ENTER,
  ATOM_DND_LEAVE,
  ATOM_DND_DROP,
  ATOM_DND_FINISHED,
  ATOM_DND_PROXY,
  ATOM_DND_TYPE_LIST,
  ATOM_DND_ACTION_MOVE,
  ATOM_DND_ACTION_COPY,
  ATOM_DND_ACTION_ASK,
  ATOM_DND_ACTION_PRIVATE,
  N_DND_ATOMS
};

/* Matches order in enum above */
const gchar *atom_names[] = {
  "XdndSelection",
  "XdndAware",
  "XdndStatus",
  "XdndPosition",
  "XdndEnter",
  "XdndLeave",
  "XdndDrop",
  "XdndFinished",
  "XdndProxy",
  "XdndTypeList",
  "XdndActionMove",
  "XdndActionCopy",
  "XdndActionAsk",
  "XdndActionPrivate",
  NULL
};

Atom xdnd_atoms[N_DND_ATOMS];

G_DEFINE_TYPE (MetaWaylandDataSourceXWayland, meta_wayland_data_source_xwayland,
               META_TYPE_WAYLAND_DATA_SOURCE);

static MetaDisplay *
display_from_compositor (MetaWaylandCompositor *compositor)
{
  MetaContext *context = meta_wayland_compositor_get_context (compositor);

  return meta_context_get_display (context);
}

static MetaX11Display *
x11_display_from_dnd (MetaXWaylandDnd *dnd)
{
  MetaWaylandCompositor *compositor = dnd->manager->compositor;
  MetaContext *context = meta_wayland_compositor_get_context (compositor);
  MetaDisplay *display = meta_context_get_display (context);

  return meta_display_get_x11_display (display);
}

/* XDND helpers */
static Atom
action_to_atom (uint32_t action)
{
  if (action & WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY)
    return xdnd_atoms[ATOM_DND_ACTION_COPY];
  else if (action & WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE)
    return xdnd_atoms[ATOM_DND_ACTION_MOVE];
  else if (action & WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK)
    return xdnd_atoms[ATOM_DND_ACTION_ASK];
  else
    return None;
}

static enum wl_data_device_manager_dnd_action
atom_to_action (Atom atom)
{
  if (atom == xdnd_atoms[ATOM_DND_ACTION_COPY] ||
      atom == xdnd_atoms[ATOM_DND_ACTION_PRIVATE])
    return WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
  else if (atom == xdnd_atoms[ATOM_DND_ACTION_MOVE])
    return WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE;
  else if (atom == xdnd_atoms[ATOM_DND_ACTION_ASK])
    return WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK;
  else
    return WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;
}

static Window
current_dnd_window (MetaXWaylandDnd *dnd)
{
  return dnd->dnd_window[dnd->current_dnd_window];
}

static Window
next_dnd_window (MetaXWaylandDnd *dnd)
{
  dnd->current_dnd_window =
    (dnd->current_dnd_window + 1) % G_N_ELEMENTS (dnd->dnd_window);

  return current_dnd_window (dnd);
}

static void
create_dnd_windows (MetaXWaylandDnd *dnd,
                    MetaX11Display  *x11_display)
{
  Display *xdisplay = meta_x11_display_get_xdisplay (x11_display);
  XSetWindowAttributes attributes;
  guint32 version = XDND_VERSION;
  int i;

  attributes.event_mask = PropertyChangeMask | SubstructureNotifyMask;
  attributes.override_redirect = True;

  for (i = 0; i < G_N_ELEMENTS (dnd->dnd_window); i++)
    {
      dnd->dnd_window[i] =
        XCreateWindow (xdisplay,
                       meta_x11_display_get_xroot (x11_display),
                       -1, -1, 1, 1,
                       0, /* border width */
                       0, /* depth */
                       InputOnly, /* class */
                       CopyFromParent, /* visual */
                       CWEventMask | CWOverrideRedirect,
                       &attributes);

      XChangeProperty (xdisplay, dnd->dnd_window[i],
                       xdnd_atoms[ATOM_DND_AWARE],
                       XA_ATOM, 32, PropModeReplace,
                       (guchar*) &version, 1);
    }
}

static void
destroy_dnd_windows (MetaXWaylandDnd *dnd,
                     MetaX11Display  *x11_display)
{
  Display *xdisplay = meta_x11_display_get_xdisplay (x11_display);
  int i;

  for (i = 0; i < G_N_ELEMENTS (dnd->dnd_window); i++)
    {
      XDestroyWindow (xdisplay, dnd->dnd_window[i]);
      dnd->dnd_window[i] = None;
    }
}

static void
hide_dnd_window (MetaXWaylandDnd *dnd,
                 MetaX11Display  *x11_display,
                 int              index)
{
  Display *xdisplay = meta_x11_display_get_xdisplay (x11_display);

  g_assert (index < G_N_ELEMENTS (dnd->dnd_window));

  XMoveResizeWindow (xdisplay, dnd->dnd_window[index], -1, -1, 1, 1);
  XUnmapWindow (xdisplay, dnd->dnd_window[index]);
}

static void
hide_all_dnd_windows (MetaXWaylandDnd *dnd,
                      MetaX11Display  *x11_display)
{
  int i;

  for (i = 0; i < G_N_ELEMENTS (dnd->dnd_window); i++)
    hide_dnd_window (dnd, x11_display, i);
}

static void
xdnd_send_enter (MetaXWaylandDnd *dnd,
                 Window           dest)
{
  MetaWaylandCompositor *compositor = dnd->manager->compositor;
  MetaX11Display *x11_display = x11_display_from_dnd (dnd);
  Display *xdisplay = x11_display->xdisplay;
  MetaWaylandDataSource *data_source;
  XEvent xev = { 0 };
  gchar **p;
  struct wl_array *source_mime_types;

  mtk_x11_error_trap_push (x11_display->xdisplay);

  data_source = compositor->seat->data_device.dnd_data_source;
  xev.xclient.type = ClientMessage;
  xev.xclient.message_type = xdnd_atoms[ATOM_DND_ENTER];
  xev.xclient.format = 32;
  xev.xclient.window = dest;

  xev.xclient.data.l[0] = x11_display->selection.xwindow;
  xev.xclient.data.l[1] = XDND_VERSION << 24; /* version */
  xev.xclient.data.l[2] = xev.xclient.data.l[3] = xev.xclient.data.l[4] = 0;

  source_mime_types = meta_wayland_data_source_get_mime_types (data_source);
  if (source_mime_types->size <= 3)
    {
      /* The mimetype atoms fit in this same message */
      gint i = 2;

      wl_array_for_each (p, source_mime_types)
        {
          xev.xclient.data.l[i++] = XInternAtom (xdisplay, *p, False);
        }
    }
  else
    {
      /* We have more than 3 mimetypes, we must set up
       * the mimetype list as a XdndTypeList property.
       */
      g_autofree Atom *atomlist = NULL;
      gint i = 0;

      xev.xclient.data.l[1] |= 1;
      atomlist = g_new0 (Atom, source_mime_types->size);

      wl_array_for_each (p, source_mime_types)
        {
          atomlist[i++] = XInternAtom (xdisplay, *p, False);
        }

      XChangeProperty (xdisplay, x11_display->selection.xwindow,
                       xdnd_atoms[ATOM_DND_TYPE_LIST],
                       XA_ATOM, 32, PropModeReplace,
                       (guchar *) atomlist, i);
    }

  XSendEvent (xdisplay, dest, False, NoEventMask, &xev);

  if (mtk_x11_error_trap_pop_with_return (x11_display->xdisplay) != Success)
    g_critical ("Error sending XdndEnter");
}

static void
xdnd_send_leave (MetaXWaylandDnd *dnd,
                 Window           dest)
{
  MetaX11Display *x11_display = x11_display_from_dnd (dnd);
  Display *xdisplay = x11_display->xdisplay;
  XEvent xev = { 0 };

  xev.xclient.type = ClientMessage;
  xev.xclient.message_type = xdnd_atoms[ATOM_DND_LEAVE];
  xev.xclient.format = 32;
  xev.xclient.window = dest;
  xev.xclient.data.l[0] = x11_display->selection.xwindow;

  mtk_x11_error_trap_push (x11_display->xdisplay);
  XSendEvent (xdisplay, dest, False, NoEventMask, &xev);
  mtk_x11_error_trap_pop (x11_display->xdisplay);
}

static void
xdnd_send_position (MetaXWaylandDnd *dnd,
                    Window           dest,
                    uint32_t         time,
                    int              x,
                    int              y)
{
  MetaWaylandCompositor *compositor = dnd->manager->compositor;
  MetaWaylandDataSource *source = compositor->seat->data_device.dnd_data_source;
  MetaX11Display *x11_display = x11_display_from_dnd (dnd);
  Display *xdisplay = x11_display->xdisplay;
  uint32_t action = 0, user_action, actions;
  XEvent xev = { 0 };

  user_action = meta_wayland_data_source_get_user_action (source);
  meta_wayland_data_source_get_actions (source, &actions);

  if (user_action & actions)
    action = user_action;
  if (!action)
    action = actions;

  xev.xclient.type = ClientMessage;
  xev.xclient.message_type = xdnd_atoms[ATOM_DND_POSITION];
  xev.xclient.format = 32;
  xev.xclient.window = dest;

  xev.xclient.data.l[0] = x11_display->selection.xwindow;
  xev.xclient.data.l[1] = 0;
  xev.xclient.data.l[2] = (x << 16) | y;
  xev.xclient.data.l[3] = time;
  xev.xclient.data.l[4] = action_to_atom (action);

  mtk_x11_error_trap_push (x11_display->xdisplay);
  XSendEvent (xdisplay, dest, False, NoEventMask, &xev);

  if (mtk_x11_error_trap_pop_with_return (x11_display->xdisplay) != Success)
    g_critical ("Error sending XdndPosition");
}

static void
xdnd_send_drop (MetaXWaylandDnd *dnd,
                Window           dest,
                uint32_t         time)
{
  MetaX11Display *x11_display = x11_display_from_dnd (dnd);
  Display *xdisplay = x11_display->xdisplay;
  XEvent xev = { 0 };

  xev.xclient.type = ClientMessage;
  xev.xclient.message_type = xdnd_atoms[ATOM_DND_DROP];
  xev.xclient.format = 32;
  xev.xclient.window = dest;

  xev.xclient.data.l[0] = x11_display->selection.xwindow;
  xev.xclient.data.l[2] = time;

  mtk_x11_error_trap_push (x11_display->xdisplay);
  XSendEvent (xdisplay, dest, False, NoEventMask, &xev);

  if (mtk_x11_error_trap_pop_with_return (x11_display->xdisplay) != Success)
    g_critical ("Error sending XdndDrop");
}

static void
xdnd_send_finished (MetaXWaylandDnd *dnd,
                    Window           dest,
                    gboolean         accepted)
{
  MetaX11Display *x11_display = x11_display_from_dnd (dnd);
  Display *xdisplay = x11_display->xdisplay;
  MetaWaylandDataSource *source = dnd->source;
  uint32_t action = 0;
  XEvent xev = { 0 };

  xev.xclient.type = ClientMessage;
  xev.xclient.message_type = xdnd_atoms[ATOM_DND_FINISHED];
  xev.xclient.format = 32;
  xev.xclient.window = dest;

  xev.xclient.data.l[0] = current_dnd_window (dnd);

  if (accepted)
    {
      action = meta_wayland_data_source_get_current_action (source);
      xev.xclient.data.l[1] = 1; /* Drop successful */
      xev.xclient.data.l[2] = action_to_atom (action);
    }

  mtk_x11_error_trap_push (x11_display->xdisplay);
  XSendEvent (xdisplay, dest, False, NoEventMask, &xev);

  if (mtk_x11_error_trap_pop_with_return (x11_display->xdisplay) != Success)
    g_critical ("Error sending XdndFinished");
}

static void
xdnd_send_status (MetaXWaylandDnd *dnd,
                  Window           dest,
                  uint32_t         action)
{
  MetaX11Display *x11_display = x11_display_from_dnd (dnd);
  Display *xdisplay = meta_x11_display_get_xdisplay (x11_display);
  XEvent xev = { 0 };

  xev.xclient.type = ClientMessage;
  xev.xclient.message_type = xdnd_atoms[ATOM_DND_STATUS];
  xev.xclient.format = 32;
  xev.xclient.window = dest;

  xev.xclient.data.l[0] = current_dnd_window (dnd);
  xev.xclient.data.l[1] = 1 << 1; /* Bit 2: dest wants XdndPosition messages */
  xev.xclient.data.l[4] = action_to_atom (action);

  if (xev.xclient.data.l[4])
    xev.xclient.data.l[1] |= 1 << 0; /* Bit 1: dest accepts the drop */

  mtk_x11_error_trap_push (x11_display->xdisplay);
  XSendEvent (xdisplay, dest, False, NoEventMask, &xev);

  if (mtk_x11_error_trap_pop_with_return (x11_display->xdisplay) != Success)
    g_critical ("Error sending Xdndstatus");
}

static void
meta_xwayland_end_dnd_grab (MetaWaylandDataDevice *data_device,
                            gboolean               success)
{
  MetaWaylandSeat *seat = meta_wayland_data_device_get_seat (data_device);
  MetaWaylandCompositor *compositor = meta_wayland_seat_get_compositor (seat);
  MetaXWaylandManager *manager = &compositor->xwayland_manager;
  MetaWaylandDragGrab *drag_grab = compositor->seat->data_device.current_grab;
  MetaXWaylandDnd *dnd = manager->dnd;
  MetaX11Display *x11_display = x11_display_from_dnd (dnd);

  if (drag_grab)
    {
      if (!success && dnd->source)
        meta_wayland_data_source_set_current_offer (dnd->source, NULL);

      meta_wayland_data_device_end_drag (data_device);
    }

  hide_all_dnd_windows (dnd, x11_display);
}

static void
transfer_cb (MetaSelection *selection,
             GAsyncResult  *res,
             GOutputStream *stream)
{
  GError *error = NULL;

  if (!meta_selection_transfer_finish (selection, res, &error))
    {
      g_warning ("Could not transfer DnD selection: %s", error->message);
      g_error_free (error);
    }

  g_output_stream_close (stream, NULL, NULL);
  g_object_unref (stream);
}

static void
meta_x11_source_send (MetaWaylandDataSource *source,
                      const gchar           *mime_type,
                      gint                   fd)
{
  MetaWaylandCompositor *compositor =
    meta_wayland_data_source_get_compositor (source);
  MetaContext *context = meta_wayland_compositor_get_context (compositor);
  MetaDisplay *display = meta_context_get_display (context);
  GOutputStream *stream;

  stream = g_unix_output_stream_new (fd, TRUE);
  meta_selection_transfer_async (meta_display_get_selection (display),
                                 META_SELECTION_DND,
                                 mime_type,
                                 -1,
                                 stream,
                                 NULL,
                                 (GAsyncReadyCallback) transfer_cb,
                                 stream);
}

static void
meta_x11_source_target (MetaWaylandDataSource *source,
                        const gchar           *mime_type)
{
  MetaWaylandDataSourceXWayland *source_xwayland =
    META_WAYLAND_DATA_SOURCE_XWAYLAND (source);
  MetaXWaylandDnd *dnd = source_xwayland->dnd;
  uint32_t action = 0;

  if (mime_type)
    action = meta_wayland_data_source_get_current_action (source);

  xdnd_send_status (dnd, dnd->owner, action);
}

static void
meta_x11_source_cancel (MetaWaylandDataSource *source)
{
  MetaWaylandDataSourceXWayland *source_xwayland =
    META_WAYLAND_DATA_SOURCE_XWAYLAND (source);
  MetaXWaylandDnd *dnd = source_xwayland->dnd;

  xdnd_send_finished (dnd, dnd->owner, FALSE);
}

static void
meta_x11_source_action (MetaWaylandDataSource *source,
                        uint32_t               action)
{
  MetaWaylandDataSourceXWayland *source_xwayland =
    META_WAYLAND_DATA_SOURCE_XWAYLAND (source);
  MetaXWaylandDnd *dnd = source_xwayland->dnd;

  if (!meta_wayland_data_source_has_target (source))
    action = 0;

  xdnd_send_status (dnd, dnd->owner, action);
}

static void
meta_x11_source_drop_performed (MetaWaylandDataSource *source)
{
}

static void
meta_x11_source_drag_finished (MetaWaylandDataSource *source)
{
  MetaWaylandDataSourceXWayland *source_xwayland =
    META_WAYLAND_DATA_SOURCE_XWAYLAND (source);
  MetaXWaylandDnd *dnd = source_xwayland->dnd;
  MetaX11Display *x11_display = x11_display_from_dnd (dnd);
  uint32_t action = meta_wayland_data_source_get_current_action (source);

  if (action == WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE)
    {
      Display *xdisplay = meta_x11_display_get_xdisplay (x11_display);

      /* Request data deletion on the drag source */
      XConvertSelection (xdisplay,
                         xdnd_atoms[ATOM_DND_SELECTION],
                         XInternAtom (xdisplay, "DELETE", False),
                         XInternAtom (xdisplay, "_META_SELECTION", False),
                         x11_display->selection.xwindow,
                         META_CURRENT_TIME);
    }

  xdnd_send_finished (dnd, dnd->owner, TRUE);
}

static void
meta_wayland_data_source_xwayland_init (MetaWaylandDataSourceXWayland *source_xwayland)
{
}

static void
meta_wayland_data_source_xwayland_class_init (MetaWaylandDataSourceXWaylandClass *klass)
{
  MetaWaylandDataSourceClass *data_source_class =
    META_WAYLAND_DATA_SOURCE_CLASS (klass);

  data_source_class->send = meta_x11_source_send;
  data_source_class->target = meta_x11_source_target;
  data_source_class->cancel = meta_x11_source_cancel;
  data_source_class->action = meta_x11_source_action;
  data_source_class->drop_performed = meta_x11_source_drop_performed;
  data_source_class->drag_finished = meta_x11_source_drag_finished;
}

static MetaWaylandDataSource *
meta_wayland_data_source_xwayland_new (MetaXWaylandDnd       *dnd,
                                       MetaWaylandCompositor *compositor)
{
  MetaWaylandDataSourceXWayland *source_xwayland;

  source_xwayland = g_object_new (META_TYPE_WAYLAND_DATA_SOURCE_XWAYLAND,
                                  "compositor", compositor,
                                  NULL);
  source_xwayland->dnd = dnd;

  return META_WAYLAND_DATA_SOURCE (source_xwayland);
}

static void
meta_x11_drag_dest_focus_in (MetaWaylandDataDevice *data_device,
                             MetaWaylandSurface    *surface,
                             MetaWaylandDataOffer  *offer)
{
  MetaWaylandSeat *seat = meta_wayland_data_device_get_seat (data_device);
  MetaWaylandCompositor *compositor = meta_wayland_seat_get_compositor (seat);
  MetaXWaylandDnd *dnd = compositor->xwayland_manager.dnd;
  MetaWindow *window = meta_wayland_surface_get_window (surface);

  dnd->dnd_dest = meta_window_x11_get_xwindow (window);
  xdnd_send_enter (dnd, dnd->dnd_dest);
}

static void
meta_x11_drag_dest_focus_out (MetaWaylandDataDevice *data_device,
                              MetaWaylandSurface    *surface)
{
  MetaWaylandSeat *seat = meta_wayland_data_device_get_seat (data_device);
  MetaWaylandCompositor *compositor = meta_wayland_seat_get_compositor (seat);
  MetaXWaylandDnd *dnd = compositor->xwayland_manager.dnd;

  xdnd_send_leave (dnd, dnd->dnd_dest);
  dnd->dnd_dest = None;
}

static void
meta_x11_drag_dest_motion (MetaWaylandDataDevice *data_device,
                           MetaWaylandSurface    *surface,
                           float                  x,
                           float                  y,
                           uint32_t               time_ms)
{
  MetaWaylandSeat *seat = meta_wayland_data_device_get_seat (data_device);
  MetaWaylandCompositor *compositor = meta_wayland_seat_get_compositor (seat);
  MetaXWaylandDnd *dnd = compositor->xwayland_manager.dnd;

  xdnd_send_position (dnd, dnd->dnd_dest, time_ms, x, y);
}

static void
meta_x11_drag_dest_drop (MetaWaylandDataDevice *data_device,
                         MetaWaylandSurface    *surface)
{
  MetaWaylandSeat *seat = meta_wayland_data_device_get_seat (data_device);
  MetaWaylandCompositor *compositor = meta_wayland_seat_get_compositor (seat);
  MetaXWaylandDnd *dnd = compositor->xwayland_manager.dnd;
  MetaDisplay *display = display_from_compositor (compositor);

  xdnd_send_drop (dnd, dnd->dnd_dest,
                  meta_display_get_current_time_roundtrip (display));
}

static void
meta_x11_drag_dest_update (MetaWaylandDataDevice *data_device,
                           MetaWaylandSurface    *surface)
{
  MetaWaylandSeat *seat = meta_wayland_data_device_get_seat (data_device);
  MetaWaylandCompositor *compositor = meta_wayland_seat_get_compositor (seat);
  MetaXWaylandDnd *dnd = compositor->xwayland_manager.dnd;
  MetaWaylandDragGrab *drag_grab = compositor->seat->data_device.current_grab;
  ClutterInputDevice *device;
  ClutterEventSequence *sequence;
  graphene_point_t pos;

  device = meta_wayland_drag_grab_get_device (drag_grab, &sequence);

  clutter_seat_query_state (clutter_input_device_get_seat (device),
                            device, sequence, &pos, NULL);
  xdnd_send_position (dnd, dnd->dnd_dest,
                      clutter_get_current_event_time (),
                      pos.x, pos.y);
}

static const MetaWaylandDragDestFuncs meta_x11_drag_dest_funcs = {
  meta_x11_drag_dest_focus_in,
  meta_x11_drag_dest_focus_out,
  meta_x11_drag_dest_motion,
  meta_x11_drag_dest_drop,
  meta_x11_drag_dest_update
};

const MetaWaylandDragDestFuncs *
meta_xwayland_selection_get_drag_dest_funcs (void)
{
  return &meta_x11_drag_dest_funcs;
}

static gboolean
meta_xwayland_data_source_fetch_mimetype_list (MetaWaylandDataSource *source,
                                               Window                 window,
                                               Atom                   prop)
{
  MetaWaylandDataSourceXWayland *source_xwayland =
    META_WAYLAND_DATA_SOURCE_XWAYLAND (source);
  MetaXWaylandDnd *dnd = source_xwayland->dnd;
  MetaX11Display *x11_display = x11_display_from_dnd (dnd);
  Display *xdisplay = meta_x11_display_get_xdisplay (x11_display);
  gulong nitems_ret, bytes_after_ret, i;
  Atom *atoms, type_ret, utf8_string;
  int format_ret;
  struct wl_array *source_mime_types;

  source_mime_types = meta_wayland_data_source_get_mime_types (source);
  if (source_mime_types->size != 0)
    return TRUE;

  mtk_x11_error_trap_push (x11_display->xdisplay);

  utf8_string = XInternAtom (xdisplay, "UTF8_STRING", False);
  if (XGetWindowProperty (xdisplay, window, prop,
                          0, /* offset */
                          0x1fffffff, /* length */
                          False, /* delete */
                          AnyPropertyType,
                          &type_ret,
                          &format_ret,
                          &nitems_ret,
                          &bytes_after_ret,
                          (guchar **) &atoms) != Success)
    {
      mtk_x11_error_trap_pop (xdisplay);
      return FALSE;
    }

  if (mtk_x11_error_trap_pop_with_return (x11_display->xdisplay) != Success)
    return FALSE;

  if (nitems_ret == 0 || type_ret != XA_ATOM)
    {
      XFree (atoms);
      return FALSE;
    }

  for (i = 0; i < nitems_ret; i++)
    {
      char *mime_type;

      if (atoms[i] == utf8_string)
        {
          meta_wayland_data_source_add_mime_type (source,
                                                  "text/plain;charset=utf-8");
          source_xwayland->has_utf8_string_atom = TRUE;
        }

      mime_type = XGetAtomName (xdisplay, atoms[i]);
      meta_wayland_data_source_add_mime_type (source, mime_type);
      XFree (mime_type);
    }

  XFree (atoms);

  return TRUE;
}

static MetaWaylandSurface *
pick_drop_surface (MetaWaylandCompositor *compositor,
                   const ClutterEvent    *event)
{
  MetaContext *context = meta_wayland_compositor_get_context (compositor);
  MetaDisplay *display = meta_context_get_display (context);
  MetaWorkspaceManager *workspace_manager = display->workspace_manager;
  MetaWorkspace *workspace = workspace_manager->active_workspace;
  MetaWindow *focus_window = NULL;
  graphene_point_t pos;

  clutter_event_get_coords (event, &pos.x, &pos.y);
  focus_window = meta_workspace_get_default_focus_window_at_point (workspace,
                                                                   NULL,
                                                                   pos.x, pos.y);
  return focus_window ? meta_window_get_wayland_surface (focus_window) : NULL;
}

static void
repick_drop_surface (MetaWaylandCompositor *compositor,
                     MetaWaylandDragGrab   *drag_grab,
                     const ClutterEvent    *event)
{
  MetaXWaylandDnd *dnd = compositor->xwayland_manager.dnd;
  MetaX11Display *x11_display = x11_display_from_dnd (dnd);
  Display *xdisplay = meta_x11_display_get_xdisplay (x11_display);
  MetaWaylandSurface *focus = NULL;
  MetaWindow *focus_window;

  focus = pick_drop_surface (compositor, event);
  if (dnd->focus_surface == focus)
    return;

  dnd->focus_surface = focus;

  if (focus)
    focus_window = meta_wayland_surface_get_window (focus);
  else
    focus_window = NULL;

  if (focus_window &&
      focus_window->client_type == META_WINDOW_CLIENT_TYPE_WAYLAND)
    {
      Window dnd_window;

      hide_dnd_window (dnd, x11_display, dnd->current_dnd_window);
      dnd_window = next_dnd_window (dnd);

      XMapRaised (xdisplay, dnd_window);
      XMoveResizeWindow (xdisplay, dnd_window,
                         focus_window->rect.x,
                         focus_window->rect.y,
                         focus_window->rect.width,
                         focus_window->rect.height);
    }
  else
    {
      hide_all_dnd_windows (dnd, x11_display);
    }
}

static MetaWaylandSurface *
drag_xgrab_get_focus_surface (MetaWaylandEventHandler *handler,
                              ClutterInputDevice      *device,
                              ClutterEventSequence    *sequence,
                              gpointer                 user_data)
{
  ClutterSeat *clutter_seat;

  clutter_seat = clutter_input_device_get_seat (device);
  if (sequence ||
      device != clutter_seat_get_pointer (clutter_seat))
    return NULL;

  return meta_wayland_event_handler_chain_up_get_focus_surface (handler,
                                                                device,
                                                                sequence);
}

static void
drag_xgrab_focus (MetaWaylandEventHandler *handler,
                  ClutterInputDevice      *device,
                  ClutterEventSequence    *sequence,
                  MetaWaylandSurface      *surface,
                  gpointer                 user_data)
{
  meta_wayland_event_handler_chain_up_focus (handler, device,
                                             sequence, surface);

  /* Do not update the DnD focus here. First, the surface may perfectly
   * be the X11 source DnD icon window's, so we can only be fooled
   * here. Second, delaying focus handling to XdndEnter/Leave
   * makes us do the negotiation orderly on the X11 side.
   */
}

static gboolean
drag_xgrab_motion (MetaWaylandEventHandler *handler,
                   const ClutterEvent      *event,
                   gpointer                 user_data)
{
  MetaWaylandDragGrab *drag_grab = user_data;
  MetaWaylandSeat *seat = meta_wayland_drag_grab_get_seat (drag_grab);
  MetaWaylandCompositor *compositor = meta_wayland_seat_get_compositor (seat);
  MetaXWaylandDnd *dnd = compositor->xwayland_manager.dnd;

  if (clutter_event_type (event) != CLUTTER_MOTION ||
      clutter_event_get_device_tool (event))
    return CLUTTER_EVENT_STOP;

  repick_drop_surface (compositor, drag_grab, event);

  dnd->last_motion_time = clutter_event_get_time (event);

  return CLUTTER_EVENT_PROPAGATE;
}

static gboolean
drag_xgrab_release (MetaWaylandEventHandler *handler,
                    const ClutterEvent      *event,
                    gpointer                 user_data)
{
  MetaWaylandDragGrab *drag_grab = user_data;
  MetaWaylandSeat *seat = meta_wayland_drag_grab_get_seat (drag_grab);
  MetaWaylandCompositor *compositor = meta_wayland_seat_get_compositor (seat);
  MetaWaylandDataSource *data_source;

  if (clutter_event_type (event) != CLUTTER_BUTTON_RELEASE ||
      clutter_event_get_device_tool (event))
    return CLUTTER_EVENT_STOP;

  data_source = compositor->seat->data_device.dnd_data_source;

  if (__builtin_popcount (clutter_event_get_state (event) &
                          (CLUTTER_BUTTON1_MASK |
                           CLUTTER_BUTTON2_MASK |
                           CLUTTER_BUTTON3_MASK |
                           CLUTTER_BUTTON4_MASK |
                           CLUTTER_BUTTON5_MASK)) <= 1 &&
      (!meta_wayland_drag_grab_get_focus (drag_grab) ||
       meta_wayland_data_source_get_current_action (data_source) ==
       WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE))
    meta_xwayland_end_dnd_grab (&seat->data_device, FALSE);

  return CLUTTER_EVENT_PROPAGATE;
}

static gboolean
drag_xgrab_key (MetaWaylandEventHandler *handler,
                const ClutterEvent      *event,
                gpointer                 user_data)
{
  return CLUTTER_EVENT_PROPAGATE;
}

static gboolean
drag_xgrab_ignore_event (MetaWaylandEventHandler *handler,
                         const ClutterEvent      *event,
                         gpointer                 user_data)
{
  return CLUTTER_EVENT_STOP;
}

static const MetaWaylandEventInterface xdnd_event_interface = {
  drag_xgrab_get_focus_surface,
  drag_xgrab_focus,
  drag_xgrab_motion,
  drag_xgrab_ignore_event, /* press */
  drag_xgrab_release,
  drag_xgrab_key,
  drag_xgrab_ignore_event, /* other */
};

static gboolean
meta_xwayland_dnd_handle_client_message (MetaWaylandCompositor *compositor,
                                         XEvent                *xevent)
{
  XClientMessageEvent *event = (XClientMessageEvent *) xevent;
  MetaXWaylandDnd *dnd = compositor->xwayland_manager.dnd;
  MetaWaylandSeat *seat = compositor->seat;
  MetaX11Display *x11_display = x11_display_from_dnd (dnd);
  Display *xdisplay = meta_x11_display_get_xdisplay (x11_display);

  /* Source side messages */
  if (event->window == x11_display->selection.xwindow)
    {
      MetaWaylandDataSource *data_source;
      uint32_t action = 0;

      data_source = compositor->seat->data_device.dnd_data_source;

      if (!data_source)
        return FALSE;

      if (event->message_type == xdnd_atoms[ATOM_DND_STATUS])
        {
          /* The first bit in data.l[1] is set if the drag was accepted */
          meta_wayland_data_source_set_has_target (data_source,
                                                   (event->data.l[1] & 1) != 0);

          /* data.l[4] contains the action atom */
          if (event->data.l[4])
            action = atom_to_action ((Atom) event->data.l[4]);

          meta_wayland_data_source_set_current_action (data_source, action);
          return TRUE;
        }
      else if (event->message_type == xdnd_atoms[ATOM_DND_FINISHED])
        {
          /* Reject messages mid-grab */
          if (compositor->seat->data_device.current_grab)
            return FALSE;

          meta_wayland_data_source_notify_finish (data_source);
          return TRUE;
        }
    }
  /* Dest side messages */
  else if (dnd->source &&
           compositor->seat->data_device.current_grab &&
           (Window) event->data.l[0] == dnd->owner)
    {
      MetaWaylandDragGrab *drag_grab = compositor->seat->data_device.current_grab;
      MetaWaylandSurface *drag_focus = meta_wayland_drag_grab_get_focus (drag_grab);

      if (!drag_focus &&
          event->message_type != xdnd_atoms[ATOM_DND_ENTER])
        return FALSE;

      if (event->message_type == xdnd_atoms[ATOM_DND_ENTER])
        {
          /* Bit 1 in data.l[1] determines whether there's 3 or less mimetype
           * atoms (and are thus contained in this same message), or whether
           * there's more than 3 and we need to check the XdndTypeList property
           * for the full list.
           */
          if (!(event->data.l[1] & 1))
            {
              /* Mimetypes are contained in this message */
              char *mimetype;
              gint i;
              struct wl_array *source_mime_types;

              /* We only need to fetch once */
              source_mime_types =
                meta_wayland_data_source_get_mime_types (dnd->source);
              if (source_mime_types->size == 0)
                {
                  for (i = 2; i <= 4; i++)
                    {
                      if (event->data.l[i] == None)
                        break;

                      mimetype = XGetAtomName (xdisplay, event->data.l[i]);
                      meta_wayland_data_source_add_mime_type (dnd->source,
                                                              mimetype);
                      XFree (mimetype);
                    }
                }
            }
          else
            {
              /* Fetch mimetypes from type list */
              meta_xwayland_data_source_fetch_mimetype_list (dnd->source,
                                                             event->data.l[0],
                                                             xdnd_atoms[ATOM_DND_TYPE_LIST]);
            }

          meta_wayland_data_source_set_actions (dnd->source,
                                                WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY |
                                                WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE |
                                                WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK);
          meta_wayland_drag_grab_set_focus (drag_grab, dnd->focus_surface);
          return TRUE;
        }
      else if (event->message_type == xdnd_atoms[ATOM_DND_POSITION])
        {
          ClutterInputDevice *device;
          ClutterEventSequence *sequence;
          graphene_point_t pos;
          uint32_t action = 0;

          dnd->client_message_timestamp = event->data.l[3];

          device = meta_wayland_drag_grab_get_device (drag_grab, &sequence);

          clutter_seat_query_state (clutter_input_device_get_seat (device),
                                    device, sequence, &pos, NULL);

          action = atom_to_action ((Atom) event->data.l[4]);
          meta_wayland_data_source_set_user_action (dnd->source, action);

          meta_wayland_surface_drag_dest_motion (drag_focus, pos.x, pos.y,
                                                 dnd->last_motion_time);
          xdnd_send_status (dnd, (Window) event->data.l[0],
                            meta_wayland_data_source_get_current_action (dnd->source));

          return TRUE;
        }
      else if (event->message_type == xdnd_atoms[ATOM_DND_LEAVE])
        {
          meta_wayland_drag_grab_set_focus (drag_grab, NULL);
          return TRUE;
        }
      else if (event->message_type == xdnd_atoms[ATOM_DND_DROP])
        {
          dnd->client_message_timestamp = event->data.l[2];
          meta_wayland_surface_drag_dest_drop (drag_focus);
          meta_xwayland_end_dnd_grab (&seat->data_device, TRUE);
          return TRUE;
        }
    }

  return FALSE;
}

static gboolean
find_dnd_candidate_device (ClutterStage         *stage,
                           ClutterInputDevice   *device,
                           ClutterEventSequence *sequence,
                           gpointer              user_data)
{
  DndCandidateDevice *candidate = user_data;
  graphene_point_t pos;
  ClutterModifierType modifiers;
  MetaWaylandSurface *focus;

  clutter_seat_query_state (clutter_input_device_get_seat (device),
                            device, sequence, &pos, &modifiers);

  if (!sequence)
    {
      if (modifiers &
          (CLUTTER_BUTTON1_MASK | CLUTTER_BUTTON2_MASK |
           CLUTTER_BUTTON3_MASK | CLUTTER_BUTTON4_MASK |
           CLUTTER_BUTTON5_MASK))
        return TRUE;
    }

  focus = meta_wayland_seat_get_current_surface (candidate->seat,
                                                 device, sequence);
  if (!focus || !meta_wayland_surface_is_xwayland (focus))
    return TRUE;

  candidate->device = device;
  candidate->sequence = sequence;
  candidate->pos = pos;
  candidate->focus = focus;

  return FALSE;
}

static gboolean
meta_xwayland_dnd_handle_xfixes_selection_notify (MetaWaylandCompositor *compositor,
                                                  XEvent                *xevent)
{
  XFixesSelectionNotifyEvent *event = (XFixesSelectionNotifyEvent *) xevent;
  MetaXWaylandDnd *dnd = compositor->xwayland_manager.dnd;
  MetaWaylandSeat *seat = compositor->seat;
  MetaWaylandDataDevice *data_device = &seat->data_device;
  MetaX11Display *x11_display = x11_display_from_dnd (dnd);
  MetaContext *context = meta_wayland_compositor_get_context (compositor);
  MetaBackend *backend = meta_context_get_backend (context);
  ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
  DndCandidateDevice candidate = { seat, };

  if (event->selection != xdnd_atoms[ATOM_DND_SELECTION])
    return FALSE;

  dnd->owner = event->owner;

  if (event->owner != None && event->owner != x11_display->selection.xwindow &&
      !clutter_stage_pointing_input_foreach (stage, find_dnd_candidate_device,
                                             &candidate))
    {

      dnd->source = meta_wayland_data_source_xwayland_new (dnd, compositor);
      meta_wayland_data_device_set_dnd_source (&compositor->seat->data_device,
                                               dnd->source);

      meta_wayland_data_device_start_drag (data_device,
                                           wl_resource_get_client (candidate.focus->resource),
                                           &xdnd_event_interface,
                                           candidate.focus, dnd->source,
                                           NULL,
                                           candidate.device,
                                           candidate.sequence,
                                           candidate.pos);
    }
  else if (event->owner == None)
    {
      meta_xwayland_end_dnd_grab (data_device, FALSE);
      g_clear_object (&dnd->source);
    }

  return FALSE;
}

gboolean
meta_xwayland_dnd_handle_xevent (MetaXWaylandManager *manager,
                                 XEvent              *xevent)
{
  MetaWaylandCompositor *compositor = manager->compositor;

  if (!compositor->xwayland_manager.dnd)
    return FALSE;

  switch (xevent->type)
    {
    case ClientMessage:
      return meta_xwayland_dnd_handle_client_message (compositor, xevent);
    default:
      {
        MetaDisplay *display = display_from_compositor (compositor);
        MetaX11Display *x11_display = meta_display_get_x11_display (display);

        if (xevent->type - x11_display->xfixes_event_base == XFixesSelectionNotify)
          return meta_xwayland_dnd_handle_xfixes_selection_notify (compositor, xevent);

        return FALSE;
      }
    }
}

void
meta_xwayland_init_dnd (MetaX11Display *x11_display)
{
  MetaDisplay *display = meta_x11_display_get_display (x11_display);
  Display *xdisplay = meta_x11_display_get_xdisplay (x11_display);
  MetaContext *context = meta_display_get_context (display);
  MetaWaylandCompositor *compositor =
    meta_context_get_wayland_compositor (context);
  MetaXWaylandManager *manager = &compositor->xwayland_manager;
  MetaXWaylandDnd *dnd = manager->dnd;
  guint32 i;

  g_assert (manager->dnd == NULL);

  manager->dnd = dnd = g_new0 (MetaXWaylandDnd, 1);

  for (i = 0; i < N_DND_ATOMS; i++)
    xdnd_atoms[i] = XInternAtom (xdisplay, atom_names[i], False);

  create_dnd_windows (dnd, x11_display);
  dnd->manager = manager;
  dnd->current_dnd_window = 0;
}

void
meta_xwayland_shutdown_dnd (MetaXWaylandManager *manager,
                            MetaX11Display      *x11_display)
{
  MetaXWaylandDnd *dnd = manager->dnd;

  g_assert (dnd != NULL);

  destroy_dnd_windows (dnd, x11_display);

  g_free (dnd);
  manager->dnd = NULL;
}
