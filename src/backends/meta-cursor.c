/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright 2013 Red Hat, Inc.
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
 * Author: Giovanni Campagna <gcampagn@redhat.com>
 */

#include "config.h"

#include "backends/meta-cursor.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-cursor-tracker-private.h"
#include "cogl/cogl.h"
#include "meta/common.h"

enum
{
  PROP_0,

  PROP_CURSOR_TRACKER,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MetaCursorSpritePrivate
{
  MetaCursorTracker *cursor_tracker;
} MetaCursorSpritePrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (MetaCursorSprite,
                                     meta_cursor_sprite,
                                     CLUTTER_TYPE_CURSOR)

static void
meta_cursor_sprite_init (MetaCursorSprite *sprite)
{
}

static void
meta_cursor_sprite_constructed (GObject *object)
{
  MetaCursorSprite *sprite = META_CURSOR_SPRITE (object);
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  g_assert (priv->cursor_tracker);

  G_OBJECT_CLASS (meta_cursor_sprite_parent_class)->constructed (object);
}

static void
meta_cursor_sprite_finalize (GObject *object)
{
  MetaCursorSprite *sprite = META_CURSOR_SPRITE (object);
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  g_clear_object (&priv->cursor_tracker);

  G_OBJECT_CLASS (meta_cursor_sprite_parent_class)->finalize (object);
}

static void
meta_cursor_sprite_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  MetaCursorSprite *sprite = META_CURSOR_SPRITE (object);
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  switch (prop_id)
    {
    case PROP_CURSOR_TRACKER:
      g_set_object (&priv->cursor_tracker, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_cursor_sprite_class_init (MetaCursorSpriteClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_cursor_sprite_constructed;
  object_class->finalize = meta_cursor_sprite_finalize;
  object_class->set_property = meta_cursor_sprite_set_property;

  obj_props[PROP_CURSOR_TRACKER] =
    g_param_spec_object ("cursor-tracker", NULL, NULL,
                         META_TYPE_CURSOR_TRACKER,
                         G_PARAM_WRITABLE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

MetaCursorTracker *
meta_cursor_sprite_get_cursor_tracker (MetaCursorSprite *sprite)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  return priv->cursor_tracker;
}
