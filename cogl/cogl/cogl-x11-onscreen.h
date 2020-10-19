/*
 * Copyright (C) 2020 Red Hat Inc.
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
 */

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#ifndef COGL_X11_ONSCREEN_H
#define COGL_X11_ONSCREEN_H

#include <glib-object.h>
#include <X11/Xlib.h>

#include "cogl-macros.h"

#define COGL_TYPE_X11_ONSCREEN (cogl_x11_onscreen_get_type ())
COGL_EXPORT
G_DECLARE_INTERFACE (CoglX11Onscreen, cogl_x11_onscreen,
                     COGL, X11_ONSCREEN,
                     GObject)

struct _CoglX11OnscreenInterface
{
  GTypeInterface parent_iface;

  Window (* get_x11_window) (CoglX11Onscreen *x11_onscreen);
};

COGL_EXPORT
Window cogl_x11_onscreen_get_x11_window (CoglX11Onscreen *x11_onscreen);

#endif /* COGL_X11_ONSCREEN_H */
