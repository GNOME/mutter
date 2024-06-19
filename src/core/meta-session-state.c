/*
 * Copyright (C) 2024 Red Hat Inc.
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

#include "core/meta-session-state.h"
#include "meta/util.h"

enum
{
  PROP_0,
  PROP_NAME,
  N_PROPS,
};

static GParamSpec *props[N_PROPS] = { 0, };

typedef struct _MetaSessionStatePrivate MetaSessionStatePrivate;

struct _MetaSessionStatePrivate
{
  char *name;
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (MetaSessionState, meta_session_state, G_TYPE_OBJECT)

const char *
meta_session_state_get_name (MetaSessionState *state)
{
  MetaSessionStatePrivate *priv =
    meta_session_state_get_instance_private (state);

  return priv->name;
}

gboolean
meta_session_state_serialize (MetaSessionState *state,
                              GHashTable       *gvdb_data)
{
  meta_topic (META_DEBUG_SESSION_MANAGEMENT, "Serializing state");

  return META_SESSION_STATE_GET_CLASS (state)->serialize (state, gvdb_data);
}

gboolean
meta_session_state_parse (MetaSessionState  *state,
                          GvdbTable         *data,
                          GError           **error)
{
  meta_topic (META_DEBUG_SESSION_MANAGEMENT, "Parsing state");

  return META_SESSION_STATE_GET_CLASS (state)->parse (state, data, error);
}

void
meta_session_state_save_window (MetaSessionState *state,
                                const char       *name,
                                MetaWindow       *window)
{
  meta_topic (META_DEBUG_SESSION_MANAGEMENT, "Saving window %s", name);

  META_SESSION_STATE_GET_CLASS (state)->save_window (state, name, window);
}

gboolean
meta_session_state_restore_window (MetaSessionState *state,
                                   const char       *name,
                                   MetaWindow       *window)
{
  meta_topic (META_DEBUG_SESSION_MANAGEMENT, "Restoring window %s", name);

  return META_SESSION_STATE_GET_CLASS (state)->restore_window (state,
                                                               name,
                                                               window);
}

void
meta_session_state_remove_window (MetaSessionState *state,
                                  const char       *name)
{
  meta_topic (META_DEBUG_SESSION_MANAGEMENT, "Removing window %s", name);

  META_SESSION_STATE_GET_CLASS (state)->remove_window (state, name);
}

static void
meta_session_state_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  MetaSessionState *session_state = META_SESSION_STATE (object);
  MetaSessionStatePrivate *priv =
    meta_session_state_get_instance_private (session_state);

  switch (prop_id)
    {
    case PROP_NAME:
      priv->name = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_session_state_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  MetaSessionState *session_state = META_SESSION_STATE (object);
  MetaSessionStatePrivate *priv =
    meta_session_state_get_instance_private (session_state);

  switch (prop_id)
    {
    case PROP_NAME:
      g_value_set_string (value, priv->name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_session_state_finalize (GObject *object)
{
  MetaSessionState *session_state = META_SESSION_STATE (object);
  MetaSessionStatePrivate *priv =
    meta_session_state_get_instance_private (session_state);

  g_clear_pointer (&priv->name, g_free);

  G_OBJECT_CLASS (meta_session_state_parent_class)->finalize (object);
}

static void
meta_session_state_class_init (MetaSessionStateClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = meta_session_state_set_property;
  object_class->get_property = meta_session_state_get_property;
  object_class->finalize = meta_session_state_finalize;

  props[PROP_NAME] =
    g_param_spec_string ("name", NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                         G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, N_PROPS, props);
}

static void
meta_session_state_init (MetaSessionState *state)
{
}
