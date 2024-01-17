/*
 * Copyright (C) 2018 Red Hat
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

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <glib-object.h>

#include "clutter/clutter-enums.h"
#include "clutter/clutter-macros.h"


typedef struct _ClutterKeymap ClutterKeymap;
typedef struct _ClutterKeymapClass ClutterKeymapClass;

struct _ClutterKeymapClass
{
  GObjectClass parent_class;

  ClutterTextDirection (* get_direction) (ClutterKeymap *keymap);
};

#define CLUTTER_TYPE_KEYMAP (clutter_keymap_get_type ())
CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterKeymap, clutter_keymap,
			  CLUTTER, KEYMAP,
			  GObject)

CLUTTER_EXPORT
gboolean clutter_keymap_get_num_lock_state  (ClutterKeymap *keymap);

CLUTTER_EXPORT
gboolean clutter_keymap_get_caps_lock_state (ClutterKeymap *keymap);

ClutterTextDirection clutter_keymap_get_direction (ClutterKeymap *keymap);
