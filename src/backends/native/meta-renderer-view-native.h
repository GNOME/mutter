/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2020-2022 Dor Askayo
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
 *     Dor Askayo <dor.askayo@gmail.com>
 */

#pragma once

#include "backends/meta-renderer-view.h"

#define META_TYPE_RENDERER_VIEW_NATIVE (meta_renderer_view_native_get_type ())
G_DECLARE_FINAL_TYPE (MetaRendererViewNative, meta_renderer_view_native,
                      META, RENDERER_VIEW_NATIVE, MetaRendererView)
