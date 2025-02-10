/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright 2024 GNOME Foundation
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
 */

#include "config.h"

#include "meta-dbus-access-checker.h"

#include "core/meta-context-private.h"

enum
{
  PROP_0,
  PROP_CONNECTION,
  PROP_CONTEXT,
  N_PROPS
};

static GParamSpec *props[N_PROPS];

typedef struct _AllowedSender AllowedSender;

struct _AllowedSender
{
  char *name;
  char *name_owner;
  guint watch_id;
};

struct _MetaDbusAccessChecker
{
  GObject parent_instance;
  GDBusConnection *connection;
  GPtrArray *allowed_senders;
  MetaContext *context;
};

G_DEFINE_TYPE (MetaDbusAccessChecker, meta_dbus_access_checker, G_TYPE_OBJECT)

static void
name_appeared_cb (GDBusConnection *connection,
                  const char      *name,
                  const char      *name_owner,
                  gpointer         user_data)
{
  AllowedSender *allowed_sender = user_data;

  allowed_sender->name_owner = g_strdup (name_owner);
}

static void
name_vanished_cb (GDBusConnection *connection,
                  const char      *name,
                  gpointer         user_data)
{
  AllowedSender *allowed_sender = user_data;

  g_clear_pointer (&allowed_sender->name_owner, g_free);
}

static AllowedSender *
allowed_sender_new (MetaDbusAccessChecker *self,
                    const char            *name)
{
  AllowedSender *allowed_sender;

  allowed_sender = g_new0 (AllowedSender, 1);
  allowed_sender->name = g_strdup (name);
  allowed_sender->watch_id =
    g_bus_watch_name_on_connection (self->connection,
                                    name,
                                    G_BUS_NAME_WATCHER_FLAGS_NONE,
                                    name_appeared_cb,
                                    name_vanished_cb,
                                    allowed_sender,
                                    NULL);

  return allowed_sender;
}

static void
allowed_sender_free (AllowedSender *allowed_sender)
{
  g_clear_pointer (&allowed_sender->name, g_free);
  g_clear_pointer (&allowed_sender->name_owner, g_free);
  g_bus_unwatch_name (allowed_sender->watch_id);
  g_free (allowed_sender);
}

static void
meta_dbus_access_checker_init (MetaDbusAccessChecker *self)
{
  self->allowed_senders =
    g_ptr_array_new_with_free_func ((GDestroyNotify) allowed_sender_free);
}

static void
meta_dbus_access_checker_finalize (GObject *object)
{
  MetaDbusAccessChecker *self =
    META_DBUS_ACCESS_CHECKER (object);

  g_clear_pointer (&self->allowed_senders, g_ptr_array_unref);
  g_clear_object (&self->connection);

  G_OBJECT_CLASS (meta_dbus_access_checker_parent_class)->finalize (object);
}

static void
meta_dbus_access_checker_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  MetaDbusAccessChecker *self = META_DBUS_ACCESS_CHECKER (object);

  switch (prop_id)
    {
    case PROP_CONNECTION:
      self->connection = g_value_dup_object (value);
      break;
    case PROP_CONTEXT:
      self->context = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_dbus_access_checker_class_init (MetaDbusAccessCheckerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_dbus_access_checker_finalize;
  object_class->set_property = meta_dbus_access_checker_set_property;

  props[PROP_CONNECTION] =
    g_param_spec_object ("connection", NULL, NULL,
                         G_TYPE_DBUS_CONNECTION,
                         G_PARAM_WRITABLE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_CONTEXT] =
    g_param_spec_object ("context", NULL, NULL,
                         META_TYPE_CONTEXT,
                         G_PARAM_WRITABLE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, props);
}

MetaDbusAccessChecker *
meta_dbus_access_checker_new (GDBusConnection *connection,
                              MetaContext     *context)
{
  return g_object_new (META_TYPE_DBUS_ACCESS_CHECKER,
                       "connection", connection,
                       "context", context,
                       NULL);
}

void
meta_dbus_access_checker_allow_sender (MetaDbusAccessChecker *self,
                                       const char            *name)
{
  AllowedSender *allowed_sender;

  allowed_sender = allowed_sender_new (self, name);
  g_ptr_array_add (self->allowed_senders, allowed_sender);
}

gboolean
meta_dbus_access_checker_is_sender_allowed (MetaDbusAccessChecker *self,
                                            const char            *sender_name)
{
  int i;

  if (meta_context_get_unsafe_mode (self->context))
    return TRUE;

  for (i = 0; i < self->allowed_senders->len; i++)
    {
      AllowedSender *allowed_sender;

      allowed_sender = g_ptr_array_index (self->allowed_senders, i);

      if (sender_name &&
          g_strcmp0 (allowed_sender->name_owner, sender_name) == 0)
        return TRUE;
    }

  return FALSE;
}
