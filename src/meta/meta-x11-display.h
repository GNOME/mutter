/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2008 Iain Holmes
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

#pragma once

#include <glib-object.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>

#include "meta/common.h"
#include "meta/prefs.h"
#include "meta/types.h"

typedef void (* MetaX11DisplayEventFunc) (MetaX11Display *x11_display,
                                          XEvent         *xev,
                                          gpointer        user_data);

#define META_TYPE_X11_DISPLAY (meta_x11_display_get_type ())

META_EXPORT
G_DECLARE_FINAL_TYPE (MetaX11Display, meta_x11_display, META, X11_DISPLAY, GObject)

META_EXPORT
Display *meta_x11_display_get_xdisplay      (MetaX11Display *x11_display);

META_EXPORT
Window   meta_x11_display_get_xroot         (MetaX11Display *x11_display);

META_EXPORT
void     meta_x11_display_set_stage_input_region (MetaX11Display *x11_display,
                                                  XserverRegion   region);

META_EXPORT
unsigned int meta_x11_display_add_event_func (MetaX11Display          *x11_display,
                                              MetaX11DisplayEventFunc  event_func,
                                              gpointer                 user_data,
                                              GDestroyNotify           destroy_notify);

META_EXPORT
void meta_x11_display_remove_event_func (MetaX11Display *x11_display,
                                         unsigned int    id);

META_EXPORT
void     meta_x11_display_redirect_windows (MetaX11Display *x11_display,
                                            MetaDisplay    *display);

META_EXPORT
Window meta_x11_display_lookup_xwindow (MetaX11Display *x11_display,
                                        MetaWindow     *window);
