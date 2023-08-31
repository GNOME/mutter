/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2001 Havoc Pennington, error trapping inspired by GDK
 * code copyrighted by the GTK team.
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

/**
 * Errors:
 *
 * Mutter X error handling
 */

#include "config.h"

#include "meta-x11-display-private.h"
#include "meta/meta-x11-errors.h"
#include "mtk/mtk-x11.h"

void
meta_x11_display_init_error_traps (MetaX11Display *x11_display)
{
  mtk_x11_errors_init ();
}

void
meta_x11_display_destroy_error_traps (MetaX11Display *x11_display)
{
  mtk_x11_errors_deinit ();
}

void
meta_x11_error_trap_push (MetaX11Display *x11_display)
{
  mtk_x11_error_trap_push (x11_display->xdisplay);
}

void
meta_x11_error_trap_pop (MetaX11Display *x11_display)
{
  mtk_x11_error_trap_pop (x11_display->xdisplay);
}

int
meta_x11_error_trap_pop_with_return (MetaX11Display *x11_display)
{
  return mtk_x11_error_trap_pop_with_return (x11_display->xdisplay);
}
