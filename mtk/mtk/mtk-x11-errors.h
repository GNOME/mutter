/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Mutter X error handling */

/*
 * Copyright (C) 2001 Havoc Pennington
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

#include "mtk/mtk-macros.h"

#include <X11/Xlib.h>

MTK_EXPORT
void mtk_x11_errors_init (void);

MTK_EXPORT
void mtk_x11_errors_deinit (void);

MTK_EXPORT
void mtk_x11_error_trap_push (Display *xdisplay);

MTK_EXPORT
void mtk_x11_error_trap_pop (Display *xdisplay);

MTK_EXPORT
int mtk_x11_error_trap_pop_with_return (Display *xdisplay);
