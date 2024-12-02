/*
 * Copyright (C) 2024 Red Hat
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#pragma once

#include "backends/x11/meta-sprite-x11.h"

#define META_TYPE_SPRITE_X11_NESTED meta_sprite_x11_nested_get_type ()
G_DECLARE_FINAL_TYPE (MetaSpriteX11Nested,
                      meta_sprite_x11_nested,
                      META, SPRITE_X11_NESTED,
                      MetaSpriteX11)
