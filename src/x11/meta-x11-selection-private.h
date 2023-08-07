/*
 * Copyright (C) 2018 Red Hat
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
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#pragma once

#include "meta/meta-selection.h"
#include "x11/meta-x11-display-private.h"

gboolean meta_x11_selection_handle_event (MetaX11Display *display,
                                          XEvent         *event);

void     meta_x11_selection_init     (MetaX11Display *x11_display);
void     meta_x11_selection_shutdown (MetaX11Display *x11_display);
