/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2005 Elijah Newren
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "meta/types.h"

/**
 * MetaLaterType:
 * @META_LATER_RESIZE: call in a resize processing phase that is done
 *   before GTK+ repainting (including window borders) is done.
 * @META_LATER_CALC_SHOWING: used by Mutter to compute which windows should be mapped
 * @META_LATER_CHECK_FULLSCREEN: used by Mutter to see if there's a fullscreen window
 * @META_LATER_SYNC_STACK: used by Mutter to send it's idea of the stacking order to the server
 * @META_LATER_BEFORE_REDRAW: call before the stage is redrawn
 * @META_LATER_IDLE: call at a very low priority (can be blocked
 *    by running animations or redrawing applications)
 **/
typedef enum
{
  META_LATER_RESIZE,
  META_LATER_CALC_SHOWING,
  META_LATER_CHECK_FULLSCREEN,
  META_LATER_SYNC_STACK,
  META_LATER_BEFORE_REDRAW,
  META_LATER_IDLE
} MetaLaterType;

#define META_TYPE_LATERS (meta_laters_get_type ())
META_EXPORT
G_DECLARE_FINAL_TYPE (MetaLaters, meta_laters, META, LATERS, GObject)

META_EXPORT
unsigned int meta_laters_add (MetaLaters     *laters,
                              MetaLaterType   when,
                              GSourceFunc     func,
                              gpointer        user_data,
                              GDestroyNotify  notify);

META_EXPORT
void meta_laters_remove (MetaLaters   *laters,
                         unsigned int  later_id);
