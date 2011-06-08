/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * \file event.h  Utility functions for handling events
 *
 * Handling events.
 * This file contains helper methods to handle events, specially
 * input events, which can be either core or XInput2.
 */

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

#ifndef META_EVENT_H
#define META_EVENT_H

#include <config.h>
#include <X11/Xlib.h>
#include "display-private.h"


gboolean meta_input_event_get_type          (MetaDisplay *display,
                                             XEvent      *ev,
                                             guint       *ev_type);
gboolean meta_input_event_is_type           (MetaDisplay *display,
                                             XEvent      *ev,
                                             guint        ev_type);

Window   meta_input_event_get_window        (MetaDisplay *display,
                                             XEvent      *ev);
Window   meta_input_event_get_root_window   (MetaDisplay *display,
                                             XEvent      *ev);

Time     meta_input_event_get_time          (MetaDisplay *display,
                                             XEvent      *ev);

gboolean meta_input_event_get_coordinates   (MetaDisplay *display,
                                             XEvent      *ev,
                                             gdouble     *x_ret,
                                             gdouble     *y_ret,
                                             gdouble     *x_root_ret,
                                             gdouble     *y_root_ret);

gboolean meta_input_event_get_state         (MetaDisplay *display,
                                             XEvent      *ev,
                                             guint       *state);
gboolean meta_input_event_get_keycode       (MetaDisplay *display,
                                             XEvent      *ev,
                                             guint       *keycode);
gboolean meta_input_event_get_button        (MetaDisplay *display,
                                             XEvent      *event,
                                             guint       *button);
gboolean meta_input_event_get_crossing_details (MetaDisplay *display,
                                                XEvent      *ev,
                                                guint       *mode_out,
                                                guint       *detail_out);


#endif /* META_EVENT_H */
