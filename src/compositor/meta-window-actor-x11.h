/*
 * Copyright (C) 2018 Endless, Inc.
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
 *     Georges Basile Stavracas Neto <gbsneto@gnome.org>
 */

#pragma once

#include <X11/extensions/Xdamage.h>

#include "compositor/meta-window-actor-private.h"

#define META_TYPE_WINDOW_ACTOR_X11 (meta_window_actor_x11_get_type())
G_DECLARE_FINAL_TYPE (MetaWindowActorX11,
                      meta_window_actor_x11,
                      META, WINDOW_ACTOR_X11,
                      MetaWindowActor)

void meta_window_actor_x11_process_x11_damage (MetaWindowActorX11 *actor_x11,
                                               XDamageNotifyEvent *event);

#ifdef HAVE_X11
gboolean meta_window_actor_x11_should_unredirect (MetaWindowActorX11 *actor_x11);

void meta_window_actor_x11_set_unredirected (MetaWindowActorX11 *actor_x11,
                                             gboolean            unredirected);
#endif

void meta_window_actor_x11_update_shape (MetaWindowActorX11 *actor_x11);

void meta_window_actor_x11_process_damage (MetaWindowActorX11 *actor_x11,
                                           XDamageNotifyEvent *event);
