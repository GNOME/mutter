/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* XEvent utility methods */

/*
 * Copyright (C) 2011 Carlos Garnacho
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
 */

#include <config.h>
#include "input-events.h"
#include <X11/Xlib.h>

#ifdef HAVE_XINPUT2
#include <X11/extensions/XInput2.h>
#endif

/* Quite a hack: normalizes XI2 events to their
 * core event equivalent, so most code is shared
 * for both implementations, code handling input
 * events should use the helper functions so
 * the actual event is treated correctly.
 */
gboolean
meta_input_event_get_type (MetaDisplay *display,
                           XEvent      *ev,
                           guint       *ev_type)
{
  guint type = 0; /* Silence gcc */
  gboolean retval = TRUE;

#ifdef HAVE_XINPUT2
  if (display->have_xinput2 &&
      ev->type == GenericEvent &&
      ev->xcookie.extension == display->xinput2_opcode)
    {
      XIEvent *xev;

      /* NB: GDK event filters already have generic events
       * allocated, so no need to do XGetEventData() on our own
       */
      xev = (XIEvent *) ev->xcookie.data;

      switch (xev->evtype)
        {
        case XI_Motion:
          type = MotionNotify;
          break;
        case XI_ButtonPress:
          type = ButtonPress;
          break;
        case XI_ButtonRelease:
          type = ButtonRelease;
          break;
        case XI_KeyPress:
          type = KeyPress;
          break;
        case XI_KeyRelease:
          type = KeyRelease;
          break;
        case XI_FocusIn:
          type = FocusIn;
          break;
        case XI_FocusOut:
          type = FocusOut;
          break;
        case XI_Enter:
          type = EnterNotify;
          break;
        case XI_Leave:
          type = LeaveNotify;
          break;
        default:
          retval = FALSE;
          break;
        }
    }
  else
#endif /* HAVE_XINPUT2 */
    {
      switch (ev->type)
        {
        case MotionNotify:
        case ButtonPress:
        case ButtonRelease:
        case KeyPress:
        case KeyRelease:
        case FocusIn:
        case FocusOut:
        case EnterNotify:
        case LeaveNotify:
          type = ev->type;
          break;
        default:
          retval = FALSE;
          break;
        }
    }

  if (retval)
    {
      if (ev_type)
        *ev_type = type;

      return TRUE;
    }
  else
    return FALSE;
}


Window
meta_input_event_get_window (MetaDisplay *display,
                             XEvent      *ev)
{
#ifdef HAVE_XINPUT2
  if (ev->type == GenericEvent &&
      ev->xcookie.extension == display->xinput2_opcode)
    {
      XIEvent *xev;

      g_assert (display->have_xinput2 == TRUE);

      /* GDK event filters already have generic events allocated */
      xev = (XIEvent *) ev->xcookie.data;

      switch (xev->evtype)
        {
        case XI_Motion:
        case XI_ButtonPress:
        case XI_ButtonRelease:
        case XI_KeyPress:
        case XI_KeyRelease:
          return ((XIDeviceEvent *) xev)->event;
        case XI_FocusIn:
        case XI_FocusOut:
        case XI_Enter:
        case XI_Leave:
          return ((XIEnterEvent *) xev)->event;
        default:
          return None;
        }
    }
  else
#endif /* HAVE_XINPUT2 */
    return ev->xany.window;
}

Window
meta_input_event_get_root_window (MetaDisplay *display,
                                  XEvent      *ev)
{
#ifdef HAVE_XINPUT2
  if (ev->type == GenericEvent &&
      ev->xcookie.extension == display->xinput2_opcode)
    {
      XIEvent *xev;

      g_assert (display->have_xinput2 == TRUE);

      xev = (XIEvent *) ev->xcookie.data;

      switch (xev->evtype)
        {
        case XI_Motion:
        case XI_ButtonPress:
        case XI_ButtonRelease:
        case XI_KeyPress:
        case XI_KeyRelease:
          return ((XIDeviceEvent *) xev)->root;
        case XI_FocusIn:
        case XI_FocusOut:
        case XI_Enter:
        case XI_Leave:
          return ((XIEnterEvent *) xev)->root;
        default:
          break;
        }
    }
  else
#endif /* HAVE_XINPUT2 */
    {
      switch (ev->type)
        {
        case KeyPress:
        case KeyRelease:
          return ev->xkey.root;
        case ButtonPress:
        case ButtonRelease:
          return ev->xbutton.root;
        case EnterNotify:
        case LeaveNotify:
          return ev->xcrossing.root;
        case MotionNotify:
          return ev->xbutton.root;
        default:
          break;
        }
    }

  return None;
}

Time
meta_input_event_get_time (MetaDisplay *display,
                           XEvent      *ev)
{
#ifdef HAVE_XINPUT2
  if (ev->type == GenericEvent &&
      ev->xcookie.extension == display->xinput2_opcode)
    {
      XIEvent *xev;

      g_assert (display->have_xinput2 == TRUE);

      xev = (XIEvent *) ev->xcookie.data;

      switch (xev->evtype)
        {
        case XI_Motion:
        case XI_ButtonPress:
        case XI_ButtonRelease:
        case XI_KeyPress:
        case XI_KeyRelease:
          return ((XIDeviceEvent *) xev)->time;
        case XI_FocusIn:
        case XI_FocusOut:
        case XI_Enter:
        case XI_Leave:
          return ((XIEnterEvent *) xev)->time;
        default:
          break;
        }
    }
  else
#endif /* HAVE_XINPUT2 */
    {
      switch (ev->type)
        {
        case KeyPress:
        case KeyRelease:
          return ev->xkey.time;
        case ButtonPress:
        case ButtonRelease:
          return ev->xbutton.time;
        case EnterNotify:
        case LeaveNotify:
          return ev->xcrossing.time;
        case MotionNotify:
          return ev->xmotion.time;
        default:
          break;
        }
    }

  return CurrentTime;
}

gboolean
meta_input_event_get_coordinates (MetaDisplay *display,
                                  XEvent      *ev,
                                  gdouble     *x_ret,
                                  gdouble     *y_ret,
                                  gdouble     *x_root_ret,
                                  gdouble     *y_root_ret)
{
  gdouble x, y, x_root, y_root;
  gboolean retval = TRUE;

#ifdef HAVE_XINPUT2
  if (ev->type == GenericEvent &&
      ev->xcookie.extension == display->xinput2_opcode)
    {
      XIEvent *xev;

      g_assert (display->have_xinput2 == TRUE);

      xev = (XIEvent *) ev->xcookie.data;

      switch (xev->evtype)
        {
        case XI_Motion:
        case XI_ButtonPress:
        case XI_ButtonRelease:
        case XI_KeyPress:
        case XI_KeyRelease:
          {
            XIDeviceEvent *event = (XIDeviceEvent *) xev;

            x = event->event_x;
            y = event->event_y;
            x_root = event->root_x;
            y_root = event->root_y;
          }

          break;
        case XI_FocusIn:
        case XI_FocusOut:
        case XI_Enter:
        case XI_Leave:
          {
            XIEnterEvent *event = (XIEnterEvent *) xev;

            x = event->event_x;
            y = event->event_y;
            x_root = event->root_x;
            y_root = event->root_y;
          }

          break;
        default:
          retval = FALSE;
          break;
        }
    }
  else
#endif /* HAVE_XINPUT2 */
    {
      switch (ev->type)
        {
        case KeyPress:
        case KeyRelease:
          x = ev->xkey.x;
          y = ev->xkey.y;
          x_root = ev->xkey.x_root;
          y_root = ev->xkey.y_root;
          break;
        case ButtonPress:
        case ButtonRelease:
          x = ev->xbutton.x;
          y = ev->xbutton.y;
          x_root = ev->xbutton.x_root;
          y_root = ev->xbutton.y_root;
          break;
        case EnterNotify:
        case LeaveNotify:
          x = ev->xcrossing.x;
          y = ev->xcrossing.y;
          x_root = ev->xcrossing.x_root;
          y_root = ev->xcrossing.y_root;
          break;
        case MotionNotify:
          x = ev->xmotion.x;
          y = ev->xmotion.y;
          x_root = ev->xmotion.x_root;
          y_root = ev->xmotion.y_root;
          break;
        default:
          retval = FALSE;
          break;
        }
    }

  if (retval)
    {
      if (x_ret)
        *x_ret = x;

      if (y_ret)
        *y_ret = y;

      if (x_root_ret)
        *x_root_ret = x_root;

      if (y_root_ret)
        *y_root_ret = y_root;
    }

  return retval;
}

gboolean
meta_input_event_get_state (MetaDisplay *display,
                            XEvent      *ev,
                            guint       *state)
{
  gboolean retval = TRUE;
  guint s;

#ifdef HAVE_XINPUT2
  if (ev->type == GenericEvent &&
      ev->xcookie.extension == display->xinput2_opcode)
    {
      XIEvent *xev;

      g_assert (display->have_xinput2 == TRUE);

      xev = (XIEvent *) ev->xcookie.data;

      switch (xev->evtype)
        {
        case XI_Motion:
        case XI_ButtonPress:
        case XI_ButtonRelease:
        case XI_KeyPress:
        case XI_KeyRelease:
          s = ((XIDeviceEvent *) xev)->mods.effective;
          break;
        case XI_FocusIn:
        case XI_FocusOut:
        case XI_Enter:
        case XI_Leave:
          s = ((XIDeviceEvent *) xev)->mods.effective;
          break;
        default:
          retval = FALSE;
          break;
        }
    }
  else
#endif /* HAVE_XINPUT2 */
    {
      switch (ev->type)
        {
        case KeyPress:
        case KeyRelease:
          s = ev->xkey.state;
          break;
        case ButtonPress:
        case ButtonRelease:
          s = ev->xbutton.state;
          break;
        case EnterNotify:
        case LeaveNotify:
          s = ev->xcrossing.state;
          break;
        case MotionNotify:
          s = ev->xmotion.state;
          break;
        default:
          retval = FALSE;
          break;
        }
    }

  if (retval && state)
    *state = s;

  return retval;
}

gboolean
meta_input_event_get_keycode (MetaDisplay *display,
                              XEvent      *ev,
                              guint       *keycode)
{
#ifdef HAVE_XINPUT2
  if (ev->type == GenericEvent &&
      ev->xcookie.extension == display->xinput2_opcode)
    {
      XIEvent *xev;

      g_assert (display->have_xinput2 == TRUE);

      xev = (XIEvent *) ev->xcookie.data;

      if (xev->evtype == XI_KeyPress ||
          xev->evtype == XI_KeyRelease)
        {
          if (keycode)
            {
              /* The detail field contains keycode for key events */
              *keycode = ((XIDeviceEvent *) xev)->detail;
            }

          return TRUE;
        }
    }
  else
#endif /* HAVE_XINPUT2 */
    {
      if (ev->type == KeyPress ||
          ev->type == KeyRelease)
        {
          if (keycode)
            *keycode = ev->xkey.keycode;

          return TRUE;
        }
    }

  return FALSE;
}

gboolean
meta_input_event_get_button (MetaDisplay *display,
                             XEvent      *ev,
                             guint       *button)
{
#ifdef HAVE_XINPUT2
  if (ev->type == GenericEvent &&
      ev->xcookie.extension == display->xinput2_opcode)
    {
      XIEvent *xev;

      g_assert (display->have_xinput2 == TRUE);

      xev = (XIEvent *) ev->xcookie.data;

      if (xev->evtype == XI_ButtonPress ||
          xev->evtype == XI_ButtonRelease)
        {
          if (button)
            {
              /* The detail field contains
               * button number for button events
               */
              *button = ((XIDeviceEvent *) xev)->detail;
            }

          return TRUE;
        }
    }
  else
#endif /* HAVE_XINPUT2 */
    {
      if (ev->type == ButtonPress ||
          ev->type == ButtonRelease)
        {
          if (button)
            *button = ev->xbutton.button;

          return TRUE;
        }
    }

  return FALSE;
}
