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

#include "backends/x11/nested/meta-sprite-x11-nested.h"

typedef struct _MetaSpriteX11Nested MetaSpriteX11Nested;

struct _MetaSpriteX11Nested
{
  MetaSpriteX11 parent_instance;
};

G_DEFINE_TYPE (MetaSpriteX11Nested, meta_sprite_x11_nested, META_TYPE_SPRITE_X11)

static void
meta_sprite_x11_nested_init (MetaSpriteX11Nested *sprite_x11_nested)
{
}

static void
meta_sprite_x11_nested_update_from_event (ClutterFocus       *focus,
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
meta_sprite_x11_nested_class_init (MetaSpriteX11NestedClass *klass)
{
  ClutterFocusClass *focus_class = CLUTTER_FOCUS_CLASS (klass);

  focus_class->update_from_event = meta_sprite_x11_nested_update_from_event;
}
