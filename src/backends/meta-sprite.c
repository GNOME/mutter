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

#include "backends/meta-sprite.h"
#include "backends/meta-backend-private.h"

#include "clutter/clutter-sprite-private.h"

enum
{
  PROP_0,
  PROP_BACKEND,
  N_PROPS,
};

static GParamSpec *props[N_PROPS] = { 0, };

typedef struct _MetaSpritePrivate MetaSpritePrivate;

struct _MetaSpritePrivate
{
  MetaBackend *backend;
};

G_DEFINE_TYPE_WITH_PRIVATE (MetaSprite, meta_sprite, CLUTTER_TYPE_SPRITE)

static void
meta_sprite_init (MetaSprite *sprite)
{
}

static void
meta_sprite_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  MetaSprite *sprite = META_SPRITE (object);
  MetaSpritePrivate *priv = meta_sprite_get_instance_private (sprite);

  switch (prop_id)
    {
    case PROP_BACKEND:
      priv->backend = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_sprite_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  MetaSprite *sprite = META_SPRITE (object);
  MetaSpritePrivate *priv = meta_sprite_get_instance_private (sprite);

  switch (prop_id)
    {
    case PROP_BACKEND:
      g_value_set_object (value, priv->backend);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_sprite_class_init (MetaSpriteClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = meta_sprite_set_property;
  object_class->get_property = meta_sprite_get_property;

  props[PROP_BACKEND] =
    g_param_spec_object ("backend", NULL, NULL,
                         META_TYPE_BACKEND,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, props);
}

MetaBackend *
meta_sprite_get_backend (MetaSprite *sprite)
{
  MetaSpritePrivate *priv = meta_sprite_get_instance_private (sprite);

  return priv->backend;
}
