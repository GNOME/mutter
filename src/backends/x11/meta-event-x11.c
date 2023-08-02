/* Copyright (C) 2006, 2007, 2008  OpenedHand Ltd
 * Copyright (C) 2009, 2010  Intel Corp.
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
 *
 *
 *
 * Authored by:
 *      Matthew Allum <mallum@openedhand.com>
 *      Emmanuele Bassi <ebassi@linux.intel.com>
 */

#include "config.h"

#include <glib.h>
#include <string.h>

#include "backends/x11/meta-backend-x11.h"
#include "backends/x11/meta-event-x11.h"
#include "backends/x11/meta-seat-x11.h"
#include "backends/x11/meta-stage-x11.h"
#include "clutter/clutter-mutter.h"
#include "cogl/cogl-xlib.h"

/**
 * meta_x11_handle_event:
 * @backend: backend
 * @xevent: pointer to XEvent structure
 *
 * This function processes a single X event; it can be used to hook
 * into external X11 event processing (for example, a GDK filter
 * function).
 *
 * Return value: #MetaX11FilterReturn. %META_X11_FILTER_REMOVE
 *  indicates that Clutter has internally handled the event and the
 *  caller should do no further processing. %META_X11_FILTER_CONTINUE
 *  indicates that Clutter is either not interested in the event,
 *  or has used the event to update internal state without taking
 *  any exclusive action. %META_X11_FILTER_TRANSLATE will not
 *  occur.
 */
MetaX11FilterReturn
meta_x11_handle_event (MetaBackend *backend,
                       XEvent      *xevent)
{
  MetaX11FilterReturn result;
  ClutterBackend *clutter_backend;
  ClutterEvent *event;
  MetaSeatX11 *seat_x11;
  MetaStageX11 *stage_x11;
  gint spin = 1;
  Display *xdisplay;
  gboolean allocated_event;

  /* The return values here are someone approximate; we return
   * META_X11_FILTER_REMOVE if a clutter event is
   * generated for the event. This mostly, but not entirely,
   * corresponds to whether other event processing should be
   * excluded. As long as the stage window is not shared with another
   * toolkit it should be safe, and never return
   * %META_X11_FILTER_REMOVE when more processing is needed.
   */

  result = META_X11_FILTER_CONTINUE;

  clutter_backend = meta_backend_get_clutter_backend (backend);

  xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));

  allocated_event = XGetEventData (xdisplay, &xevent->xcookie);

  if (cogl_xlib_renderer_handle_event (clutter_backend->cogl_renderer,
                                       xevent) == COGL_FILTER_REMOVE)
    goto out;

  stage_x11 =
    META_STAGE_X11 (clutter_backend_get_stage_window (clutter_backend));
  meta_stage_x11_handle_event (stage_x11, xevent);

  event = clutter_event_new (CLUTTER_NOTHING);
  seat_x11 = META_SEAT_X11 (meta_backend_get_default_seat (backend));
  if (meta_seat_x11_translate_event (seat_x11, xevent, event))
    {
      _clutter_event_push (event, FALSE);

      result = META_X11_FILTER_REMOVE;
    }
  else
    {
      clutter_event_free (event);
      goto out;
    }

  /*
   * Motion events can generate synthetic enter and leave events, so if we
   * are processing a motion event, we need to spin the event loop at least
   * two extra times to pump the enter/leave events through (otherwise they
   * just get pushed down the queue and never processed).
   */
  if (event->type == CLUTTER_MOTION)
    spin += 2;

  while (spin > 0 && (event = clutter_event_get ()))
    {
      /* forward the event into clutter for emission etc. */
      clutter_stage_handle_event (CLUTTER_STAGE (meta_backend_get_stage (backend)),
                                  event);
      meta_backend_update_from_event (backend, event);
      clutter_event_free (event);
      --spin;
    }

out:
  if (allocated_event)
    XFreeEventData (xdisplay, &xevent->xcookie);

  return result;
}
