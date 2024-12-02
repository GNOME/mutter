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

#include "clutter/clutter-mutter.h"
#include "backends/meta-backend-private.h"

#define META_TYPE_SPRITE meta_sprite_get_type ()
G_DECLARE_DERIVABLE_TYPE (MetaSprite,
                          meta_sprite,
                          META, SPRITE,
                          ClutterSprite)

struct _MetaSpriteClass
{
  ClutterSpriteClass parent_class;
};

MetaBackend * meta_sprite_get_backend (MetaSprite *sprite);
