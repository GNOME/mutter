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

#include "backends/meta-a11y-manager.h"
#include "meta/meta-backend.h"
#include "meta/meta-context.h"
#include "meta/util.h"

#include "meta-dbus-a11y.h"

enum
{
  A11Y_MODIFIERS_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

enum
{
  PROP_0,
  PROP_BACKEND,
  N_PROPS,
};

static GParamSpec *props[N_PROPS];

typedef struct _MetaA11yKeystroke
{
  uint32_t keysym;
  ClutterModifierType modifiers;
} MetaA11yKeystroke;

typedef struct _MetaA11yKeyGrabber
{
  MetaA11yManager *manager;
  GDBusConnection *connection;
  char *bus_name;
  guint bus_name_watcher_id;
  gboolean grab_all;
  GArray *modifiers;
  GArray *keystrokes;
} MetaA11yKeyGrabber;

typedef struct _MetaA11yManager
{
  GObject parent;
  MetaBackend *backend;
  guint dbus_name_id;
  MetaDBusKeyboardMonitor *keyboard_monitor_skeleton;

  GList *key_grabbers;
  GHashTable *grabbed_keypresses;
  GHashTable *all_grabbed_modifiers;
} MetaA11yManager;

G_DEFINE_TYPE (MetaA11yManager, meta_a11y_manager, G_TYPE_OBJECT)

static void
key_grabber_free (MetaA11yKeyGrabber *grabber)
{
  if (grabber->bus_name_watcher_id)
    {
      g_bus_unwatch_name (grabber->bus_name_watcher_id);
      grabber->bus_name_watcher_id = 0;
    }

  g_clear_pointer (&grabber->keystrokes, g_array_unref);
  g_clear_pointer (&grabber->modifiers, g_array_unref);
  g_clear_object (&grabber->connection);
  g_clear_pointer (&grabber->bus_name, g_free);

  g_free (grabber);
}

static void
rebuild_all_grabbed_modifiers (MetaA11yManager    *a11y_manager,
                               MetaA11yKeyGrabber *ignored_grabber)
{
  GList *l;
  int i;

  g_hash_table_remove_all (a11y_manager->all_grabbed_modifiers);

  for (l = a11y_manager->key_grabbers; l; l = l->next)
    {
      MetaA11yKeyGrabber *grabber = l->data;

      if (grabber == ignored_grabber)
        continue;

      for (i = 0; i < grabber->modifiers->len; i++)
        {
          uint32_t modifier_keysym = g_array_index (grabber->modifiers, uint32_t, i);
          g_hash_table_add (a11y_manager->all_grabbed_modifiers,
                            GUINT_TO_POINTER (modifier_keysym));
        }
    }
}

static void
key_grabber_bus_name_vanished_callback (GDBusConnection *connection,
                                        const char      *name,
                                        gpointer         user_data)
{
  MetaA11yKeyGrabber *grabber = user_data;
  MetaA11yManager *a11y_manager = grabber->manager;

  grabber->manager->key_grabbers =
    g_list_remove (grabber->manager->key_grabbers, grabber);

  if (grabber->modifiers)
    {
      rebuild_all_grabbed_modifiers (a11y_manager, grabber);
      g_signal_emit (grabber->manager, signals[A11Y_MODIFIERS_CHANGED], 0);
    }

  key_grabber_free (grabber);
}

static MetaA11yKeyGrabber *
ensure_key_grabber (MetaA11yManager       *a11y_manager,
                    GDBusMethodInvocation *invocation)
{
  GDBusConnection *connection =
    g_dbus_method_invocation_get_connection (invocation);
  const char *sender = g_dbus_method_invocation_get_sender (invocation);
  MetaA11yKeyGrabber *grabber;
  GList *l;

  for (l = a11y_manager->key_grabbers; l; l = l->next)
    {
      grabber = l->data;

      if (g_strcmp0 (grabber->bus_name, sender) == 0)
        return grabber;
    }

  grabber = g_new0 (MetaA11yKeyGrabber, 1);
  grabber->manager = a11y_manager;
  grabber->bus_name = g_strdup (sender);
  grabber->connection = g_object_ref (connection);

  grabber->bus_name_watcher_id =
    g_bus_watch_name_on_connection (connection,
                                    grabber->bus_name,
                                    G_BUS_NAME_WATCHER_FLAGS_NONE,
                                    NULL,
                                    key_grabber_bus_name_vanished_callback,
                                    grabber,
                                    NULL);

  a11y_manager->key_grabbers =
    g_list_prepend (a11y_manager->key_grabbers, grabber);

  return grabber;
}

static gboolean
handle_grab_keyboard (MetaDBusKeyboardMonitor *skeleton,
                      GDBusMethodInvocation   *invocation,
                      MetaA11yManager         *a11y_manager)
{
  MetaA11yKeyGrabber *grabber;

  grabber = ensure_key_grabber (a11y_manager, invocation);
  grabber->grab_all = TRUE;
  meta_dbus_keyboard_monitor_complete_grab_keyboard (skeleton, invocation);

  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
handle_ungrab_keyboard (MetaDBusKeyboardMonitor *skeleton,
                        GDBusMethodInvocation   *invocation,
                        MetaA11yManager         *a11y_manager)
{
  MetaA11yKeyGrabber *grabber;

  grabber = ensure_key_grabber (a11y_manager, invocation);
  grabber->grab_all = FALSE;
  meta_dbus_keyboard_monitor_complete_ungrab_keyboard (skeleton, invocation);

  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
handle_set_key_grabs (MetaDBusKeyboardMonitor *skeleton,
                      GDBusMethodInvocation   *invocation,
                      GVariant                *modifiers,
                      GVariant                *keystrokes,
                      MetaA11yManager         *a11y_manager)
{
  MetaA11yKeyGrabber *grabber;
  GVariantIter iter;
  uint32_t modifier_keysym;
  MetaA11yKeystroke keystroke;

  grabber = ensure_key_grabber (a11y_manager, invocation);

  g_clear_pointer (&grabber->modifiers, g_array_unref);
  g_clear_pointer (&grabber->keystrokes, g_array_unref);
  grabber->modifiers = g_array_new (FALSE, FALSE, sizeof (uint32_t));
  grabber->keystrokes = g_array_new (FALSE, FALSE, sizeof (MetaA11yKeystroke));

  g_variant_iter_init (&iter, modifiers);
  while (g_variant_iter_next (&iter, "u", &modifier_keysym))
    g_array_append_val (grabber->modifiers, modifier_keysym);

  g_variant_iter_init (&iter, keystrokes);
  while (g_variant_iter_next (&iter, "(uu)", &keystroke.keysym,
                              &keystroke.modifiers))
    g_array_append_val (grabber->keystrokes, keystroke);

  rebuild_all_grabbed_modifiers (a11y_manager, NULL);
  g_signal_emit (a11y_manager, signals[A11Y_MODIFIERS_CHANGED], 0);
  meta_dbus_keyboard_monitor_complete_set_key_grabs (skeleton, invocation);

  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const char      *name,
                 gpointer         user_data)
{
  MetaA11yManager *manager = user_data;

  manager->keyboard_monitor_skeleton = meta_dbus_keyboard_monitor_skeleton_new ();

  g_signal_connect (manager->keyboard_monitor_skeleton, "handle-grab-keyboard",
                    G_CALLBACK (handle_grab_keyboard), manager);
  g_signal_connect (manager->keyboard_monitor_skeleton, "handle-ungrab-keyboard",
                    G_CALLBACK (handle_ungrab_keyboard), manager);
  g_signal_connect (manager->keyboard_monitor_skeleton, "handle-set-key-grabs",
                    G_CALLBACK (handle_set_key_grabs), manager);

  g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (manager->keyboard_monitor_skeleton),
                                    connection,
                                    "/org/freedesktop/a11y/Manager",
                                    NULL);
}

static void
on_name_acquired (GDBusConnection *connection,
                  const char      *name,
                  gpointer         user_data)
{
  g_debug ("Acquired name %s", name);
}

static void
on_name_lost (GDBusConnection *connection,
              const char      *name,
              gpointer         user_data)
{
  g_debug ("Lost or failed to acquire name %s", name);
}

static void
meta_a11y_manager_finalize (GObject *object)
{
  MetaA11yManager *a11y_manager = META_A11Y_MANAGER (object);

  g_list_free_full (a11y_manager->key_grabbers,
                    (GDestroyNotify) key_grabber_free);
  g_clear_object (&a11y_manager->keyboard_monitor_skeleton);
  g_clear_pointer (&a11y_manager->grabbed_keypresses, g_hash_table_destroy);
  g_clear_pointer (&a11y_manager->all_grabbed_modifiers, g_hash_table_destroy);
  g_bus_unown_name (a11y_manager->dbus_name_id);

  G_OBJECT_CLASS (meta_a11y_manager_parent_class)->finalize (object);
}

static void
meta_a11y_manager_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  MetaA11yManager *a11y_manager = META_A11Y_MANAGER (object);

  switch (prop_id)
    {
    case PROP_BACKEND:
      a11y_manager->backend = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_a11y_manager_constructed (GObject *object)
{
  MetaA11yManager *a11y_manager = META_A11Y_MANAGER (object);
  MetaContext *context;

  g_assert (a11y_manager->backend);
  context = meta_backend_get_context (a11y_manager->backend);

  a11y_manager->grabbed_keypresses = g_hash_table_new (NULL, NULL);
  a11y_manager->all_grabbed_modifiers = g_hash_table_new (NULL, NULL);

  a11y_manager->dbus_name_id =
    g_bus_own_name (G_BUS_TYPE_SESSION,
                    "org.freedesktop.a11y.Manager",
                    G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                    (meta_context_is_replacing (context) ?
                     G_BUS_NAME_OWNER_FLAGS_REPLACE :
                     G_BUS_NAME_OWNER_FLAGS_NONE),
                    on_bus_acquired,
                    on_name_acquired,
                    on_name_lost,
                    a11y_manager,
                    NULL);
}

static void
meta_a11y_manager_class_init (MetaA11yManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_a11y_manager_finalize;
  object_class->set_property = meta_a11y_manager_set_property;
  object_class->constructed = meta_a11y_manager_constructed;

  signals[A11Y_MODIFIERS_CHANGED] =
    g_signal_new ("a11y-modifiers-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  props[PROP_BACKEND] =
    g_param_spec_object ("backend", NULL, NULL,
                         META_TYPE_BACKEND,
                         G_PARAM_WRITABLE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, props);
}

static void
meta_a11y_manager_init (MetaA11yManager *a11y_manager)
{
}

MetaA11yManager *
meta_a11y_manager_new (MetaBackend *backend)
{
  return g_object_new (META_TYPE_A11Y_MANAGER,
                       "backend", backend,
                       NULL);
}
