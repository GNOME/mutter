/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2008 Iain Holmes
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

#include <glib.h>

#include "meta/types.h"
#include "meta/boxes.h"
#include "meta/window.h"
#include "meta/workspace.h"

#define META_TYPE_COMPOSITOR (meta_compositor_get_type ())
META_EXPORT
G_DECLARE_DERIVABLE_TYPE (MetaCompositor, meta_compositor,
                          META, COMPOSITOR, GObject)

/**
 * MetaCompEffect:
 * @META_COMP_EFFECT_CREATE: The window is newly created
 *   (also used for a window that was previously on a different
 *   workspace and is changed to become visible on the active
 *   workspace.)
 * @META_COMP_EFFECT_UNMINIMIZE: The window should be shown
 *   as unminimizing from its icon geometry.
 * @META_COMP_EFFECT_DESTROY: The window is being destroyed
 * @META_COMP_EFFECT_MINIMIZE: The window should be shown
 *   as minimizing to its icon geometry.
 * @META_COMP_EFFECT_NONE: No effect, the window should be
 *   shown or hidden immediately.
 *
 * Indicates the appropriate effect to show the user for
 * meta_compositor_show_window() and meta_compositor_hide_window()
 */
typedef enum
{
  META_COMP_EFFECT_CREATE,
  META_COMP_EFFECT_UNMINIMIZE,
  META_COMP_EFFECT_DESTROY,
  META_COMP_EFFECT_MINIMIZE,
  META_COMP_EFFECT_NONE
} MetaCompEffect;

typedef enum
{
  META_SIZE_CHANGE_MAXIMIZE,
  META_SIZE_CHANGE_UNMAXIMIZE,
  META_SIZE_CHANGE_FULLSCREEN,
  META_SIZE_CHANGE_UNFULLSCREEN,
  META_SIZE_CHANGE_MONITOR_MOVE,
} MetaSizeChange;

META_EXPORT
MetaLaters * meta_compositor_get_laters (MetaCompositor *compositor);

META_EXPORT
ClutterActor * meta_compositor_get_feedback_group (MetaCompositor *compositor);
