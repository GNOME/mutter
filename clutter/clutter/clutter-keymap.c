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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "clutter-build-config.h"

#include "clutter-keymap-private.h"
#include "clutter-private.h"

enum
{
  PROP_0,

  PROP_CAPS_LOCK_STATE,
  PROP_NUM_LOCK_STATE,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _ClutterKeymapPrivate
{
  gboolean caps_lock_state;
  gboolean num_lock_state;
} ClutterKeymapPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (ClutterKeymap, clutter_keymap,
                                     G_TYPE_OBJECT)

enum
{
  STATE_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0, };

static void
clutter_keymap_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  ClutterKeymap *keymap = CLUTTER_KEYMAP (object);
  ClutterKeymapPrivate *priv = clutter_keymap_get_instance_private (keymap);

  switch (prop_id)
    {
    case PROP_CAPS_LOCK_STATE:
      g_value_set_boolean (value, priv->caps_lock_state);
      break;
    case PROP_NUM_LOCK_STATE:
      g_value_set_boolean (value, priv->num_lock_state);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_keymap_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  ClutterKeymap *keymap = CLUTTER_KEYMAP (object);
  ClutterKeymapPrivate *priv = clutter_keymap_get_instance_private (keymap);

  switch (prop_id)
    {
    case PROP_CAPS_LOCK_STATE:
      priv->caps_lock_state = g_value_get_boolean (value);
      break;
    case PROP_NUM_LOCK_STATE:
      priv->num_lock_state = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_keymap_class_init (ClutterKeymapClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = clutter_keymap_get_property;
  object_class->set_property = clutter_keymap_set_property;

  obj_props[PROP_CAPS_LOCK_STATE] =
    g_param_spec_boolean ("caps-lock-state",
                          "Caps lock state",
                          "Caps lock state",
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS);
  obj_props[PROP_NUM_LOCK_STATE] =
    g_param_spec_boolean ("num-lock-state",
                          "Num lock state",
                          "Num lock state",
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);

  signals[STATE_CHANGED] =
    g_signal_new (I_("state-changed"),
		  G_TYPE_FROM_CLASS (klass),
		  G_SIGNAL_RUN_FIRST,
		  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
		  G_TYPE_NONE, 0);
}

static void
clutter_keymap_init (ClutterKeymap *keymap)
{
}

gboolean
clutter_keymap_get_num_lock_state (ClutterKeymap *keymap)
{
  ClutterKeymapPrivate *priv = clutter_keymap_get_instance_private (keymap);

  return priv->num_lock_state;
}

gboolean
clutter_keymap_get_caps_lock_state (ClutterKeymap *keymap)
{
  ClutterKeymapPrivate *priv = clutter_keymap_get_instance_private (keymap);

  return priv->caps_lock_state;
}

PangoDirection
clutter_keymap_get_direction (ClutterKeymap *keymap)
{
  return CLUTTER_KEYMAP_GET_CLASS (keymap)->get_direction (keymap);
}

void
clutter_keymap_set_lock_modifier_state (ClutterKeymap *keymap,
                                        gboolean       caps_lock_state,
                                        gboolean       num_lock_state)
{
  ClutterKeymapPrivate *priv = clutter_keymap_get_instance_private (keymap);

  if (priv->caps_lock_state == caps_lock_state &&
      priv->num_lock_state == num_lock_state)
    return;

  if (priv->caps_lock_state != caps_lock_state)
    {
      priv->caps_lock_state = caps_lock_state;
      g_object_notify_by_pspec (G_OBJECT (keymap),
                                obj_props[PROP_CAPS_LOCK_STATE]);
    }

  if (priv->num_lock_state != num_lock_state)
    {
      priv->num_lock_state = num_lock_state;
      g_object_notify_by_pspec (G_OBJECT (keymap),
                                obj_props[PROP_NUM_LOCK_STATE]);
    }

  g_debug ("Locks state changed - Num: %s, Caps: %s",
           priv->num_lock_state ? "set" : "unset",
           priv->caps_lock_state ? "set" : "unset");

  g_signal_emit (keymap, signals[STATE_CHANGED], 0);
}
