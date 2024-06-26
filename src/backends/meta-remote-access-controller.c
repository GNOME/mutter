/*
 * Copyright (C) 2018 Red Hat Inc.
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

#include "backends/meta-remote-access-controller-private.h"

#include "backends/meta-dbus-session-manager.h"

#ifdef HAVE_REMOTE_DESKTOP
#include "backends/meta-remote-desktop.h"
#include "backends/meta-screen-cast.h"
#endif

enum
{
  HANDLE_STOPPED,

  N_HANDLE_SIGNALS
};

static int handle_signals[N_HANDLE_SIGNALS];

enum
{
  PROP_0,

  PROP_IS_RECORDING,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

enum
{
  CONTROLLER_NEW_HANDLE,

  N_CONTROLLER_SIGNALS
};

static int controller_signals[N_CONTROLLER_SIGNALS];

typedef struct _MetaRemoteAccessHandlePrivate
{
  gboolean has_stopped;

  gboolean disable_animations;

  gboolean is_recording;
} MetaRemoteAccessHandlePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaRemoteAccessHandle,
                            meta_remote_access_handle,
                            G_TYPE_OBJECT)

struct _MetaRemoteAccessController
{
  GObject parent;

  GList *session_managers;
};

G_DEFINE_TYPE (MetaRemoteAccessController,
               meta_remote_access_controller,
               G_TYPE_OBJECT)

/**
 * meta_remote_access_handle_stop:
 * @handle: A #MetaRemoteAccessHandle
 *
 * Stop the associated remote access session.
 */
void
meta_remote_access_handle_stop (MetaRemoteAccessHandle *handle)
{
  MetaRemoteAccessHandlePrivate *priv =
    meta_remote_access_handle_get_instance_private (handle);

  if (priv->has_stopped)
    return;

  META_REMOTE_ACCESS_HANDLE_GET_CLASS (handle)->stop (handle);
}

/**
 * meta_remote_access_get_disable_animations:
 * @handle: A #MetaRemoteAccessHandle
 *
 * Returns: %TRUE if the remote access requested that animations should be
 * disabled.
 */
gboolean
meta_remote_access_handle_get_disable_animations (MetaRemoteAccessHandle *handle)
{
  MetaRemoteAccessHandlePrivate *priv =
    meta_remote_access_handle_get_instance_private (handle);

  return priv->disable_animations;
}

void
meta_remote_access_handle_set_disable_animations (MetaRemoteAccessHandle *handle,
                                                  gboolean                disable_animations)
{
  MetaRemoteAccessHandlePrivate *priv =
    meta_remote_access_handle_get_instance_private (handle);

  priv->disable_animations = disable_animations;
}

void
meta_remote_access_handle_notify_stopped (MetaRemoteAccessHandle *handle)
{
  MetaRemoteAccessHandlePrivate *priv =
    meta_remote_access_handle_get_instance_private (handle);

  priv->has_stopped = TRUE;
  g_signal_emit (handle, handle_signals[HANDLE_STOPPED], 0);
}

void
meta_remote_access_controller_notify_new_handle (MetaRemoteAccessController *controller,
                                                 MetaRemoteAccessHandle     *handle)
{
  g_signal_emit (controller, controller_signals[CONTROLLER_NEW_HANDLE], 0,
                 handle);
}

/**
 * meta_remote_access_controller_inhibit_remote_access:
 * @controller: a #MetaRemoteAccessController
 *
 * Inhibits remote access sessions from being created and running. Any active
 * remote access session will be terminated.
 */
void
meta_remote_access_controller_inhibit_remote_access (MetaRemoteAccessController *controller)
{
  GList *l;

  for (l = controller->session_managers; l; l = l->next)
    {
      MetaDbusSessionManager *session_manager = l->data;

      meta_dbus_session_manager_inhibit (session_manager);
    }
}

/**
 * meta_remote_access_controller_uninhibit_remote_access:
 * @controller: a #MetaRemoteAccessController
 *
 * Uninhibits remote access sessions from being created and running. If this was
 * the last inhibition that was inhibited, new remote access sessions can now
 * be created.
 */
void
meta_remote_access_controller_uninhibit_remote_access (MetaRemoteAccessController *controller)
{
  GList *l;

  for (l = controller->session_managers; l; l = l->next)
    {
      MetaDbusSessionManager *session_manager = l->data;

      meta_dbus_session_manager_uninhibit (session_manager);
    }
}

MetaRemoteAccessController *
meta_remote_access_controller_new (void)
{
  return g_object_new (META_TYPE_REMOTE_ACCESS_CONTROLLER, NULL);
}

void
meta_remote_access_controller_add (MetaRemoteAccessController *controller,
                                   MetaDbusSessionManager     *session_manager)
{
  controller->session_managers = g_list_append (controller->session_managers,
                                                g_object_ref (session_manager));
}

static void
meta_remote_access_handle_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  MetaRemoteAccessHandle *handle = META_REMOTE_ACCESS_HANDLE (object);
  MetaRemoteAccessHandlePrivate *priv =
    meta_remote_access_handle_get_instance_private (handle);

  switch (prop_id)
    {
    case PROP_IS_RECORDING:
      g_value_set_boolean (value, priv->is_recording);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_remote_access_handle_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  MetaRemoteAccessHandle *handle = META_REMOTE_ACCESS_HANDLE (object);
  MetaRemoteAccessHandlePrivate *priv =
    meta_remote_access_handle_get_instance_private (handle);

  switch (prop_id)
    {
    case PROP_IS_RECORDING:
      priv->is_recording = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_remote_access_handle_init (MetaRemoteAccessHandle *handle)
{
}

static void
meta_remote_access_handle_class_init (MetaRemoteAccessHandleClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = meta_remote_access_handle_get_property;
  object_class->set_property = meta_remote_access_handle_set_property;

  handle_signals[HANDLE_STOPPED] =
    g_signal_new ("stopped",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  obj_props[PROP_IS_RECORDING] =
    g_param_spec_boolean ("is-recording", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
meta_remote_access_controller_init (MetaRemoteAccessController *controller)
{
}

static void
meta_remote_access_handle_finalize (GObject *object)
{
  MetaRemoteAccessController *controller = META_REMOTE_ACCESS_CONTROLLER (object);

  g_clear_list (&controller->session_managers, g_object_unref);

  G_OBJECT_CLASS (meta_remote_access_controller_parent_class)->finalize (object);
}

static void
meta_remote_access_controller_class_init (MetaRemoteAccessControllerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_remote_access_handle_finalize;

  controller_signals[CONTROLLER_NEW_HANDLE] =
    g_signal_new ("new-handle",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  META_TYPE_REMOTE_ACCESS_HANDLE);
}
