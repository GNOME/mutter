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

#include "config.h"

#include "backends/native/meta-sprite-native.h"

#include "clutter/clutter-sprite-private.h"

typedef struct _MetaSpriteNative MetaSpriteNative;

struct _MetaSpriteNative
{
  MetaSprite parent_instance;
};

G_DEFINE_FINAL_TYPE (MetaSpriteNative, meta_sprite_native, META_TYPE_SPRITE)

static void
meta_sprite_native_init (MetaSpriteNative *sprite_native)
{
}

static void
meta_sprite_native_update_from_event (ClutterFocus       *focus,
                                      const ClutterEvent *event)
{
  ClutterSprite *sprite = CLUTTER_SPRITE (focus);
  MetaBackend *backend = meta_sprite_get_backend (META_SPRITE (focus));
  MetaCursorRenderer *cursor_renderer;

  cursor_renderer =
    meta_backend_get_cursor_renderer_for_sprite (backend, sprite);
  if (cursor_renderer)
    meta_cursor_renderer_update_position (cursor_renderer);
}

static void
meta_sprite_native_class_init (MetaSpriteNativeClass *klass)
{
  ClutterFocusClass *focus_class = CLUTTER_FOCUS_CLASS (klass);

  focus_class->update_from_event = meta_sprite_native_update_from_event;
}
