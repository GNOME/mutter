/*
 * Copyright (C) 2022 Red Hat Inc.
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

#include "clutter/clutter-mutter.h"

#define META_TYPE_STAGE_VIEW (meta_stage_view_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaStageView,
                          meta_stage_view,
                          META, STAGE_VIEW,
                          ClutterStageView)

void meta_stage_view_inhibit_cursor_overlay (MetaStageView *view);

void meta_stage_view_uninhibit_cursor_overlay (MetaStageView *view);

gboolean meta_stage_view_is_cursor_overlay_inhibited (MetaStageView *view);
