/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
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
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#pragma once

#include "backends/meta-cursor-renderer.h"

#define META_TYPE_CURSOR_RENDERER_X11             (meta_cursor_renderer_x11_get_type ())

typedef struct _MetaCursorRendererX11        MetaCursorRendererX11;

G_DECLARE_FINAL_TYPE (MetaCursorRendererX11,
                      meta_cursor_renderer_x11,
                      META, CURSOR_RENDERER_X11,
                      MetaCursorRenderer)
