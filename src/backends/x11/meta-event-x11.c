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
#include "clutter/clutter-mutter.h"

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
 *
 * Since: 0.8
 */
MetaX11FilterReturn
meta_x11_handle_event (MetaBackend *backend,
                       XEvent      *xevent)
{
  MetaX11FilterReturn result;
  ClutterBackend *clutter_backend;
  ClutterEvent *event;
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

  event = clutter_event_new (CLUTTER_NOTHING);

  xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));

  allocated_event = XGetEventData (xdisplay, &xevent->xcookie);

  if (_clutter_backend_translate_event (clutter_backend, xevent, event))
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
      clutter_do_event (event);
      meta_backend_update_from_event (backend, event);
      clutter_event_free (event);
      --spin;
    }

out:
  if (allocated_event)
    XFreeEventData (xdisplay, &xevent->xcookie);

  return result;
}
