/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat
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
 */

#pragma once

#include "backends/x11/meta-renderer-x11.h"

#define META_TYPE_RENDERER_X11_CM (meta_renderer_x11_cm_get_type ())
G_DECLARE_FINAL_TYPE (MetaRendererX11Cm, meta_renderer_x11_cm,
                      META, RENDERER_X11_CM,
                      MetaRendererX11)

void meta_renderer_x11_cm_init_screen_view (MetaRendererX11Cm *renderer_x11_cm,
                                            CoglOnscreen      *onscreen,
                                            int                width,
                                            int                height);

void meta_renderer_x11_cm_resize (MetaRendererX11Cm *renderer_x11_cm,
                                  int                width,
                                  int                height);
