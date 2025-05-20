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
#include "backends/native/meta-backend-native-types.h"
#include "meta/meta-backend.h"

#define META_TYPE_CURSOR_RENDERER_NATIVE (meta_cursor_renderer_native_get_type ())
G_DECLARE_FINAL_TYPE (MetaCursorRendererNative, meta_cursor_renderer_native,
                      META, CURSOR_RENDERER_NATIVE,
                      MetaCursorRenderer)

void meta_cursor_renderer_native_prepare_frame (MetaCursorRendererNative *cursor_renderer_native,
                                                MetaRendererView         *view,
                                                ClutterFrame             *frame);

MetaCursorRendererNative * meta_cursor_renderer_native_new (MetaBackend   *backend,
                                                            ClutterSprite *sprite);
