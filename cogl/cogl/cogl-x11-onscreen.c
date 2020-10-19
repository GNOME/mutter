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

#include "cogl-config.h"

#include "cogl-x11-onscreen.h"

G_DEFINE_INTERFACE (CoglX11Onscreen, cogl_x11_onscreen,
                    G_TYPE_OBJECT)

Window
cogl_x11_onscreen_get_x11_window (CoglX11Onscreen *x11_onscreen)
{
  CoglX11OnscreenInterface *iface =
    COGL_X11_ONSCREEN_GET_IFACE (x11_onscreen);

  return iface->get_x11_window (x11_onscreen);
}

static void
cogl_x11_onscreen_default_init (CoglX11OnscreenInterface *iface)
{
}
