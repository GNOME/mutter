/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
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
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

/**
 * SECTION:meta-backend
 * @title: MetaBackend
 * @short_description: Handles monitor config, modesetting, cursor sprites, ...
 *
 * MetaBackend is the abstraction that deals with several things like:
 * - Modesetting (depending on the backend, this can be done either by X or KMS)
 * - Initializing the #MetaSettings
 * - Setting up Monitor configuration
 * - Input device configuration (using the #ClutterDeviceManager)
 * - Creating the #MetaRenderer
 * - Setting up the stage of the scene graph (using #MetaStage)
 * - Creating the object that deals wih the cursor (using #MetaCursorTracker)
 *     and its possible pointer constraint (using #MetaPointerConstraint)
 * - Setting the cursor sprite (using #MetaCursorRenderer)
 * - Interacting with logind (using the appropriate D-Bus interface)
 * - Querying UPower (over D-Bus) to know when the lid is closed
 * - Setup Remote Desktop / Screencasting (#MetaRemoteDesktop)
 * - Setup the #MetaEgl object
 *
 * Note that the #MetaBackend is not a subclass of #ClutterBackend. It is
 * responsible for creating the correct one, based on the backend that is
 * used (#MetaBackendNative or #MetaBackendX11).
 */

#include "config.h"

#include "backends/meta-backend-private.h"

#include <stdlib.h>

#include "backends/meta-cursor-tracker-private.h"
#include "backends/meta-idle-monitor-private.h"
#include "backends/meta-input-settings-private.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor-manager-dummy.h"
#include "backends/meta-settings-private.h"
#include "backends/meta-stage-private.h"
#include "backends/x11/meta-backend-x11.h"
#include "clutter/clutter-mutter.h"
#include "meta/main.h"
#include "meta/meta-backend.h"
#include "meta/util.h"

#ifdef HAVE_PROFILER
#include "backends/meta-profiler.h"
#endif

#ifdef HAVE_REMOTE_DESKTOP
#include "backends/meta-dbus-session-watcher.h"
#include "backends/meta-remote-access-controller-private.h"
#include "backends/meta-remote-desktop.h"
#include "backends/meta-screen-cast.h"
#endif

#ifdef HAVE_NATIVE_BACKEND
#include "backends/native/meta-backend-native.h"
#endif

#define META_IDLE_MONITOR_CORE_DEVICE 0

enum
{
  KEYMAP_CHANGED,
  KEYMAP_LAYOUT_GROUP_CHANGED,
  LAST_DEVICE_CHANGED,
  LID_IS_CLOSED_CHANGED,
  GPU_ADDED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

static MetaBackend *_backend;

static gboolean stage_views_disabled = FALSE;

/**
 * meta_get_backend:
 *
 * Accessor for the singleton MetaBackend.
 *
 * Returns: (transfer none): The only #MetaBackend there is.
 */
MetaBackend *
meta_get_backend (void)
{
  return _backend;
}

struct _MetaBackendPrivate
{
  MetaMonitorManager *monitor_manager;
  MetaOrientationManager *orientation_manager;
  MetaCursorTracker *cursor_tracker;
  MetaCursorRenderer *cursor_renderer;
  MetaInputSettings *input_settings;
  MetaRenderer *renderer;
#ifdef HAVE_EGL
  MetaEgl *egl;
#endif
  MetaSettings *settings;
#ifdef HAVE_REMOTE_DESKTOP
  MetaRemoteAccessController *remote_access_controller;
  MetaDbusSessionWatcher *dbus_session_watcher;
  MetaScreenCast *screen_cast;
  MetaRemoteDesktop *remote_desktop;
#endif

#ifdef HAVE_PROFILER
  MetaProfiler *profiler;
#endif

  ClutterBackend *clutter_backend;
  ClutterActor *stage;

  GList *gpus;

  gboolean is_pointer_position_initialized;

  guint device_update_idle_id;
  guint keymap_state_changed_id;

  GHashTable *device_monitors;

  int current_device_id;

  MetaPointerConstraint *client_pointer_constraint;
  MetaDnd *dnd;

  guint upower_watch_id;
  GDBusProxy *upower_proxy;
  gboolean lid_is_closed;

  guint sleep_signal_id;
  GCancellable *cancellable;
  GDBusConnection *system_bus;

  gboolean was_headless;
};
typedef struct _MetaBackendPrivate MetaBackendPrivate;

static void
initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (MetaBackend, meta_backend, G_TYPE_OBJECT,
                                  G_ADD_PRIVATE (MetaBackend)
                                  G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                         initable_iface_init));

static void
meta_backend_finalize (GObject *object)
{
  MetaBackend *backend = META_BACKEND (object);
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  if (priv->keymap_state_changed_id)
    {
      ClutterKeymap *keymap;

      keymap = clutter_backend_get_keymap (priv->clutter_backend);
      g_signal_handler_disconnect (keymap, priv->keymap_state_changed_id);
    }

  g_list_free_full (priv->gpus, g_object_unref);

  g_clear_object (&priv->monitor_manager);
  g_clear_object (&priv->orientation_manager);
  g_clear_object (&priv->input_settings);
#ifdef HAVE_REMOTE_DESKTOP
  g_clear_object (&priv->remote_desktop);
  g_clear_object (&priv->screen_cast);
  g_clear_object (&priv->dbus_session_watcher);
  g_clear_object (&priv->remote_access_controller);
#endif

  if (priv->sleep_signal_id)
    g_dbus_connection_signal_unsubscribe (priv->system_bus, priv->sleep_signal_id);
  if (priv->upower_watch_id)
    g_bus_unwatch_name (priv->upower_watch_id);
  g_cancellable_cancel (priv->cancellable);
  g_clear_object (&priv->cancellable);
  g_clear_object (&priv->system_bus);
  g_clear_object (&priv->upower_proxy);

  if (priv->device_update_idle_id)
    g_source_remove (priv->device_update_idle_id);

  g_hash_table_destroy (priv->device_monitors);

  g_clear_object (&priv->settings);

#ifdef HAVE_PROFILER
  g_clear_object (&priv->profiler);
#endif

  G_OBJECT_CLASS (meta_backend_parent_class)->finalize (object);
}

static void
meta_backend_sync_screen_size (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  int width, height;

  meta_monitor_manager_get_screen_size (priv->monitor_manager, &width, &height);

  META_BACKEND_GET_CLASS (backend)->update_screen_size (backend, width, height);
}

static void
reset_pointer_position (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  MetaMonitorManager *monitor_manager = priv->monitor_manager;
  MetaLogicalMonitor *primary;

  primary =
    meta_monitor_manager_get_primary_logical_monitor (monitor_manager);

  /* Move the pointer out of the way to avoid hovering over reactive
   * elements (e.g. users list at login) causing undesired behaviour. */
  meta_backend_warp_pointer (backend,
                             primary->rect.x + primary->rect.width * 0.9,
                             primary->rect.y + primary->rect.height * 0.9);
}

void
meta_backend_monitors_changed (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  ClutterDeviceManager *manager = clutter_device_manager_get_default ();
  ClutterInputDevice *device = clutter_device_manager_get_core_device (manager, CLUTTER_POINTER_DEVICE);
  ClutterPoint point;

  meta_backend_sync_screen_size (backend);

  if (clutter_input_device_get_coords (device, NULL, &point))
    {
      /* If we're outside all monitors, warp the pointer back inside */
      if ((!meta_monitor_manager_get_logical_monitor_at (monitor_manager,
                                                         point.x, point.y) ||
           !priv->is_pointer_position_initialized) &&
          !meta_monitor_manager_is_headless (monitor_manager))
        {
          reset_pointer_position (backend);
          priv->is_pointer_position_initialized = TRUE;
        }
    }

  meta_cursor_renderer_force_update (priv->cursor_renderer);

  if (meta_monitor_manager_is_headless (priv->monitor_manager) &&
      !priv->was_headless)
    {
      clutter_stage_freeze_updates (CLUTTER_STAGE (priv->stage));
      priv->was_headless = TRUE;
    }
  else if (!meta_monitor_manager_is_headless (priv->monitor_manager) &&
           priv->was_headless)
    {
      clutter_stage_thaw_updates (CLUTTER_STAGE (priv->stage));
      priv->was_headless = FALSE;
    }
}

void
meta_backend_foreach_device_monitor (MetaBackend *backend,
                                     GFunc        func,
                                     gpointer     user_data)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  GHashTableIter iter;
  gpointer value;

  g_hash_table_iter_init (&iter, priv->device_monitors);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      MetaIdleMonitor *device_monitor = META_IDLE_MONITOR (value);

      func (device_monitor, user_data);
    }
}

static MetaIdleMonitor *
meta_backend_create_idle_monitor (MetaBackend *backend,
                                  int          device_id)
{
  return g_object_new (META_TYPE_IDLE_MONITOR,
                       "device-id", device_id,
                       NULL);
}

static void
create_device_monitor (MetaBackend *backend,
                       int          device_id)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  MetaIdleMonitor *idle_monitor;

  g_assert (g_hash_table_lookup (priv->device_monitors, &device_id) == NULL);

  idle_monitor = meta_backend_create_idle_monitor (backend, device_id);
  g_hash_table_insert (priv->device_monitors, &idle_monitor->device_id, idle_monitor);
}

static void
destroy_device_monitor (MetaBackend *backend,
                        int          device_id)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  g_hash_table_remove (priv->device_monitors, &device_id);
}

static void
meta_backend_monitor_device (MetaBackend        *backend,
                             ClutterInputDevice *device)
{
  int device_id;

  device_id = clutter_input_device_get_device_id (device);
  create_device_monitor (backend, device_id);
}

static void
on_device_added (ClutterDeviceManager *device_manager,
                 ClutterInputDevice   *device,
                 gpointer              user_data)
{
  MetaBackend *backend = META_BACKEND (user_data);
  int device_id = clutter_input_device_get_device_id (device);

  create_device_monitor (backend, device_id);
}

static inline gboolean
device_is_slave_touchscreen (ClutterInputDevice *device)
{
  return (clutter_input_device_get_device_mode (device) != CLUTTER_INPUT_MODE_MASTER &&
          clutter_input_device_get_device_type (device) == CLUTTER_TOUCHSCREEN_DEVICE);
}

static inline gboolean
check_has_pointing_device (ClutterDeviceManager *manager)
{
  const GSList *devices;

  devices = clutter_device_manager_peek_devices (manager);

  for (; devices; devices = devices->next)
    {
      ClutterInputDevice *device = devices->data;

      if (clutter_input_device_get_device_mode (device) == CLUTTER_INPUT_MODE_MASTER)
        continue;
      if (clutter_input_device_get_device_type (device) == CLUTTER_TOUCHSCREEN_DEVICE ||
          clutter_input_device_get_device_type (device) == CLUTTER_KEYBOARD_DEVICE)
        continue;

      return TRUE;
    }

  return FALSE;
}

static inline gboolean
check_has_slave_touchscreen (ClutterDeviceManager *manager)
{
  const GSList *devices;

  devices = clutter_device_manager_peek_devices (manager);

  for (; devices; devices = devices->next)
    {
      ClutterInputDevice *device = devices->data;

      if (clutter_input_device_get_device_mode (device) != CLUTTER_INPUT_MODE_MASTER &&
          clutter_input_device_get_device_type (device) == CLUTTER_TOUCHSCREEN_DEVICE)
        return TRUE;
    }

  return FALSE;
}

static void
on_device_removed (ClutterDeviceManager *device_manager,
                   ClutterInputDevice   *device,
                   gpointer              user_data)
{
  MetaBackend *backend = META_BACKEND (user_data);
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  int device_id = clutter_input_device_get_device_id (device);

  destroy_device_monitor (backend, device_id);

  /* If the device the user last interacted goes away, check again pointer
   * visibility.
   */
  if (priv->current_device_id == device_id)
    {
      MetaCursorTracker *cursor_tracker = priv->cursor_tracker;
      gboolean has_touchscreen, has_pointing_device;
      ClutterInputDeviceType device_type;

      priv->current_device_id = 0;

      device_type = clutter_input_device_get_device_type (device);
      has_touchscreen = check_has_slave_touchscreen (device_manager);

      if (device_type == CLUTTER_TOUCHSCREEN_DEVICE && has_touchscreen)
        {
          /* There's more touchscreens left, keep the pointer hidden */
          meta_cursor_tracker_set_pointer_visible (cursor_tracker, FALSE);
        }
      else if (device_type != CLUTTER_KEYBOARD_DEVICE)
        {
          has_pointing_device = check_has_pointing_device (device_manager);
          meta_cursor_tracker_set_pointer_visible (cursor_tracker,
                                                   has_pointing_device &&
                                                   !has_touchscreen);
        }
    }
}

static void
create_device_monitors (MetaBackend          *backend,
                        ClutterDeviceManager *device_manager)
{
  const GSList *devices;
  const GSList *l;

  create_device_monitor (backend, META_IDLE_MONITOR_CORE_DEVICE);

  devices = clutter_device_manager_peek_devices (device_manager);
  for (l = devices; l; l = l->next)
    {
      ClutterInputDevice *device = l->data;

      meta_backend_monitor_device (backend, device);
    }
}

static void
set_initial_pointer_visibility (MetaBackend          *backend,
                                ClutterDeviceManager *device_manager)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  const GSList *devices;
  const GSList *l;
  gboolean has_touchscreen = FALSE;

  devices = clutter_device_manager_peek_devices (device_manager);
  for (l = devices; l; l = l->next)
    {
      ClutterInputDevice *device = l->data;

      has_touchscreen |= device_is_slave_touchscreen (device);
    }

  meta_cursor_tracker_set_pointer_visible (priv->cursor_tracker,
                                           !has_touchscreen);
}

static MetaInputSettings *
meta_backend_create_input_settings (MetaBackend *backend)
{
  return META_BACKEND_GET_CLASS (backend)->create_input_settings (backend);
}

static void
meta_backend_real_post_init (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  ClutterDeviceManager *device_manager = clutter_device_manager_get_default ();
  ClutterKeymap *keymap = clutter_backend_get_keymap (priv->clutter_backend);

  priv->stage = meta_stage_new (backend);
  clutter_actor_realize (priv->stage);
  META_BACKEND_GET_CLASS (backend)->select_stage_events (backend);

  meta_monitor_manager_setup (priv->monitor_manager);

  meta_backend_sync_screen_size (backend);

  priv->cursor_renderer = META_BACKEND_GET_CLASS (backend)->create_cursor_renderer (backend);

  priv->device_monitors =
    g_hash_table_new_full (g_int_hash, g_int_equal,
                           NULL, (GDestroyNotify) g_object_unref);

  create_device_monitors (backend, device_manager);

  g_signal_connect_object (device_manager, "device-added",
                           G_CALLBACK (on_device_added), backend, 0);
  g_signal_connect_object (device_manager, "device-removed",
                           G_CALLBACK (on_device_removed), backend, 0);

  set_initial_pointer_visibility (backend, device_manager);

  priv->input_settings = meta_backend_create_input_settings (backend);

  if (priv->input_settings)
    {
      priv->keymap_state_changed_id =
        g_signal_connect_swapped (keymap, "state-changed",
                                  G_CALLBACK (meta_input_settings_maybe_save_numlock_state),
                                  priv->input_settings);
      meta_input_settings_maybe_restore_numlock_state (priv->input_settings);
    }

#ifdef HAVE_REMOTE_DESKTOP
  priv->remote_access_controller =
    g_object_new (META_TYPE_REMOTE_ACCESS_CONTROLLER, NULL);
  priv->dbus_session_watcher = g_object_new (META_TYPE_DBUS_SESSION_WATCHER, NULL);
  priv->screen_cast = meta_screen_cast_new (backend,
                                            priv->dbus_session_watcher);
  priv->remote_desktop = meta_remote_desktop_new (priv->dbus_session_watcher);
#endif /* HAVE_REMOTE_DESKTOP */

  if (!meta_monitor_manager_is_headless (priv->monitor_manager))
    {
      reset_pointer_position (backend);
      priv->is_pointer_position_initialized = TRUE;
    }
}

static MetaCursorRenderer *
meta_backend_real_create_cursor_renderer (MetaBackend *backend)
{
  return meta_cursor_renderer_new ();
}

static gboolean
meta_backend_real_grab_device (MetaBackend *backend,
                               int          device_id,
                               uint32_t     timestamp)
{
  /* Do nothing */
  return TRUE;
}

static gboolean
meta_backend_real_ungrab_device (MetaBackend *backend,
                                 int          device_id,
                                 uint32_t     timestamp)
{
  /* Do nothing */
  return TRUE;
}

static void
meta_backend_real_select_stage_events (MetaBackend *backend)
{
  /* Do nothing */
}

static gboolean
meta_backend_real_get_relative_motion_deltas (MetaBackend *backend,
                                             const         ClutterEvent *event,
                                             double        *dx,
                                             double        *dy,
                                             double        *dx_unaccel,
                                             double        *dy_unaccel)
{
  return FALSE;
}

static gboolean
meta_backend_real_is_lid_closed (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->lid_is_closed;
}

gboolean
meta_backend_is_lid_closed (MetaBackend *backend)
{
  return META_BACKEND_GET_CLASS (backend)->is_lid_closed (backend);
}

static void
upower_properties_changed (GDBusProxy *proxy,
                           GVariant   *changed_properties,
                           GStrv       invalidated_properties,
                           gpointer    user_data)
{
  MetaBackend *backend = user_data;
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  GVariant *v;
  gboolean lid_is_closed;

  v = g_variant_lookup_value (changed_properties,
                              "LidIsClosed",
                              G_VARIANT_TYPE_BOOLEAN);
  if (!v)
    return;

  lid_is_closed = g_variant_get_boolean (v);
  g_variant_unref (v);

  if (lid_is_closed == priv->lid_is_closed)
    return;

  priv->lid_is_closed = lid_is_closed;
  g_signal_emit (backend, signals[LID_IS_CLOSED_CHANGED], 0,
                 priv->lid_is_closed);

  if (lid_is_closed)
    return;

  meta_idle_monitor_reset_idletime (meta_idle_monitor_get_core ());
}

static void
upower_ready_cb (GObject      *source_object,
                 GAsyncResult *res,
                 gpointer      user_data)
{
  MetaBackend *backend;
  MetaBackendPrivate *priv;
  GDBusProxy *proxy;
  GError *error = NULL;
  GVariant *v;

  proxy = g_dbus_proxy_new_finish (res, &error);
  if (!proxy)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Failed to create UPower proxy: %s", error->message);
      g_error_free (error);
      return;
    }

  backend = META_BACKEND (user_data);
  priv = meta_backend_get_instance_private (backend);

  priv->upower_proxy = proxy;
  g_signal_connect (proxy, "g-properties-changed",
                    G_CALLBACK (upower_properties_changed), backend);

  v = g_dbus_proxy_get_cached_property (proxy, "LidIsClosed");
  if (!v)
    return;
  priv->lid_is_closed = g_variant_get_boolean (v);
  g_variant_unref (v);

  if (priv->lid_is_closed)
    {
      g_signal_emit (backend, signals[LID_IS_CLOSED_CHANGED], 0,
                     priv->lid_is_closed);
    }
}

static void
upower_appeared (GDBusConnection *connection,
                 const gchar     *name,
                 const gchar     *name_owner,
                 gpointer         user_data)
{
  MetaBackend *backend = META_BACKEND (user_data);
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  g_dbus_proxy_new (connection,
                    G_DBUS_PROXY_FLAGS_NONE,
                    NULL,
                    "org.freedesktop.UPower",
                    "/org/freedesktop/UPower",
                    "org.freedesktop.UPower",
                    priv->cancellable,
                    upower_ready_cb,
                    backend);
}

static void
upower_vanished (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
  MetaBackend *backend = META_BACKEND (user_data);
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  g_clear_object (&priv->upower_proxy);
}

static void
meta_backend_constructed (GObject *object)
{
  MetaBackend *backend = META_BACKEND (object);
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  MetaBackendClass *backend_class =
   META_BACKEND_GET_CLASS (backend);

  if (backend_class->is_lid_closed != meta_backend_real_is_lid_closed)
    return;

  priv->upower_watch_id = g_bus_watch_name (G_BUS_TYPE_SYSTEM,
                                            "org.freedesktop.UPower",
                                            G_BUS_NAME_WATCHER_FLAGS_NONE,
                                            upower_appeared,
                                            upower_vanished,
                                            backend,
                                            NULL);
}

static void
meta_backend_class_init (MetaBackendClass *klass)
{
  const gchar *mutter_stage_views;
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_backend_finalize;
  object_class->constructed = meta_backend_constructed;

  klass->post_init = meta_backend_real_post_init;
  klass->create_cursor_renderer = meta_backend_real_create_cursor_renderer;
  klass->grab_device = meta_backend_real_grab_device;
  klass->ungrab_device = meta_backend_real_ungrab_device;
  klass->select_stage_events = meta_backend_real_select_stage_events;
  klass->get_relative_motion_deltas = meta_backend_real_get_relative_motion_deltas;
  klass->is_lid_closed = meta_backend_real_is_lid_closed;

  signals[KEYMAP_CHANGED] =
    g_signal_new ("keymap-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
  signals[KEYMAP_LAYOUT_GROUP_CHANGED] =
    g_signal_new ("keymap-layout-group-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1, G_TYPE_UINT);
  signals[LAST_DEVICE_CHANGED] =
    g_signal_new ("last-device-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1, G_TYPE_INT);
  signals[LID_IS_CLOSED_CHANGED] =
    g_signal_new ("lid-is-closed-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
  /**
   * MetaBackend::gpu-added: (skip)
   * @backend: the #MetaBackend
   * @gpu: the #MetaGpu
   */
  signals[GPU_ADDED] =
    g_signal_new ("gpu-added",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1, META_TYPE_GPU);

  mutter_stage_views = g_getenv ("MUTTER_STAGE_VIEWS");
  stage_views_disabled = g_strcmp0 (mutter_stage_views, "0") == 0;
}

static MetaMonitorManager *
meta_backend_create_monitor_manager (MetaBackend *backend,
                                     GError     **error)
{
  if (g_getenv ("META_DUMMY_MONITORS"))
    return g_object_new (META_TYPE_MONITOR_MANAGER_DUMMY, NULL);

  return META_BACKEND_GET_CLASS (backend)->create_monitor_manager (backend,
                                                                   error);
}

static MetaRenderer *
meta_backend_create_renderer (MetaBackend *backend,
                              GError     **error)
{
  return META_BACKEND_GET_CLASS (backend)->create_renderer (backend, error);
}

static void
prepare_for_sleep_cb (GDBusConnection *connection,
                      const gchar     *sender_name,
                      const gchar     *object_path,
                      const gchar     *interface_name,
                      const gchar     *signal_name,
                      GVariant        *parameters,
                      gpointer         user_data)
{
  gboolean suspending;

  g_variant_get (parameters, "(b)", &suspending);
  if (suspending)
    return;
  meta_idle_monitor_reset_idletime (meta_idle_monitor_get_core ());
}

static void
system_bus_gotten_cb (GObject      *object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  MetaBackendPrivate *priv;
  GDBusConnection *bus;

  bus = g_bus_get_finish (res, NULL);
  if (!bus)
    return;

  priv = meta_backend_get_instance_private (user_data);
  priv->system_bus = bus;
  priv->sleep_signal_id =
    g_dbus_connection_signal_subscribe (priv->system_bus,
                                        "org.freedesktop.login1",
                                        "org.freedesktop.login1.Manager",
                                        "PrepareForSleep",
                                        "/org/freedesktop/login1",
                                        NULL,
                                        G_DBUS_SIGNAL_FLAGS_NONE,
                                        prepare_for_sleep_cb,
                                        NULL,
                                        NULL);
}

static gboolean
meta_backend_initable_init (GInitable     *initable,
                            GCancellable  *cancellable,
                            GError       **error)
{
  MetaBackend *backend = META_BACKEND (initable);
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  priv->settings = meta_settings_new (backend);

#ifdef HAVE_EGL
  priv->egl = g_object_new (META_TYPE_EGL, NULL);
#endif

  priv->orientation_manager = g_object_new (META_TYPE_ORIENTATION_MANAGER, NULL);

  priv->monitor_manager = meta_backend_create_monitor_manager (backend, error);
  if (!priv->monitor_manager)
    return FALSE;

  priv->renderer = meta_backend_create_renderer (backend, error);
  if (!priv->renderer)
    return FALSE;

  priv->cursor_tracker = g_object_new (META_TYPE_CURSOR_TRACKER, NULL);

  priv->dnd = g_object_new (META_TYPE_DND, NULL);

  priv->cancellable = g_cancellable_new ();
  g_bus_get (G_BUS_TYPE_SYSTEM,
             priv->cancellable,
             system_bus_gotten_cb,
             backend);

#ifdef HAVE_PROFILER
  priv->profiler = meta_profiler_new ();
#endif

  return TRUE;
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
  initable_iface->init = meta_backend_initable_init;
}

static void
meta_backend_init (MetaBackend *backend)
{
  _backend = backend;
}

static void
meta_backend_post_init (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  META_BACKEND_GET_CLASS (backend)->post_init (backend);

  meta_settings_post_init (priv->settings);
}

/**
 * meta_backend_get_idle_monitor: (skip)
 */
MetaIdleMonitor *
meta_backend_get_idle_monitor (MetaBackend *backend,
                               int          device_id)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return g_hash_table_lookup (priv->device_monitors, &device_id);
}

/**
 * meta_backend_get_monitor_manager: (skip)
 */
MetaMonitorManager *
meta_backend_get_monitor_manager (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->monitor_manager;
}

/**
 * meta_backend_get_orientation_manager: (skip)
 */
MetaOrientationManager *
meta_backend_get_orientation_manager (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->orientation_manager;
}

MetaCursorTracker *
meta_backend_get_cursor_tracker (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->cursor_tracker;
}

/**
 * meta_backend_get_cursor_renderer: (skip)
 */
MetaCursorRenderer *
meta_backend_get_cursor_renderer (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->cursor_renderer;
}

/**
 * meta_backend_get_renderer: (skip)
 */
MetaRenderer *
meta_backend_get_renderer (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->renderer;
}

#ifdef HAVE_EGL
/**
 * meta_backend_get_egl: (skip)
 */
MetaEgl *
meta_backend_get_egl (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->egl;
}
#endif /* HAVE_EGL */

/**
 * meta_backend_get_settings: (skip)
 */
MetaSettings *
meta_backend_get_settings (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->settings;
}

#ifdef HAVE_REMOTE_DESKTOP
/**
 * meta_backend_get_remote_desktop: (skip)
 */
MetaRemoteDesktop *
meta_backend_get_remote_desktop (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->remote_desktop;
}
#endif /* HAVE_REMOTE_DESKTOP */

/**
 * meta_backend_get_remote_access_controller:
 * @backend: A #MetaBackend
 *
 * Return Value: (transfer none): The #MetaRemoteAccessController
 */
MetaRemoteAccessController *
meta_backend_get_remote_access_controller (MetaBackend *backend)
{
#ifdef HAVE_REMOTE_DESKTOP
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->remote_access_controller;
#else
  return NULL;
#endif
}

/**
 * meta_backend_grab_device: (skip)
 */
gboolean
meta_backend_grab_device (MetaBackend *backend,
                          int          device_id,
                          uint32_t     timestamp)
{
  return META_BACKEND_GET_CLASS (backend)->grab_device (backend, device_id, timestamp);
}

/**
 * meta_backend_ungrab_device: (skip)
 */
gboolean
meta_backend_ungrab_device (MetaBackend *backend,
                            int          device_id,
                            uint32_t     timestamp)
{
  return META_BACKEND_GET_CLASS (backend)->ungrab_device (backend, device_id, timestamp);
}

/**
 * meta_backend_warp_pointer: (skip)
 */
void
meta_backend_warp_pointer (MetaBackend *backend,
                           int          x,
                           int          y)
{
  META_BACKEND_GET_CLASS (backend)->warp_pointer (backend, x, y);
}

MetaLogicalMonitor *
meta_backend_get_current_logical_monitor (MetaBackend *backend)
{
  return META_BACKEND_GET_CLASS (backend)->get_current_logical_monitor (backend);
}

void
meta_backend_set_keymap (MetaBackend *backend,
                         const char  *layouts,
                         const char  *variants,
                         const char  *options)
{
  META_BACKEND_GET_CLASS (backend)->set_keymap (backend, layouts, variants, options);
}

/**
 * meta_backend_get_keymap: (skip)
 */
struct xkb_keymap *
meta_backend_get_keymap (MetaBackend *backend)

{
  return META_BACKEND_GET_CLASS (backend)->get_keymap (backend);
}

xkb_layout_index_t
meta_backend_get_keymap_layout_group (MetaBackend *backend)
{
  return META_BACKEND_GET_CLASS (backend)->get_keymap_layout_group (backend);
}

void
meta_backend_lock_layout_group (MetaBackend *backend,
                                guint idx)
{
  META_BACKEND_GET_CLASS (backend)->lock_layout_group (backend, idx);
}

void
meta_backend_set_numlock (MetaBackend *backend,
                          gboolean     numlock_state)
{
  META_BACKEND_GET_CLASS (backend)->set_numlock (backend, numlock_state);
}


/**
 * meta_backend_get_stage:
 * @backend: A #MetaBackend
 *
 * Gets the global #ClutterStage that's managed by this backend.
 *
 * Returns: (transfer none): the #ClutterStage
 */
ClutterActor *
meta_backend_get_stage (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  return priv->stage;
}

void
meta_backend_freeze_updates (MetaBackend *backend)
{
  ClutterStage *stage;

  stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
  clutter_stage_freeze_updates (stage);
}

void
meta_backend_thaw_updates (MetaBackend *backend)
{
  ClutterStage *stage;

  stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
  clutter_stage_thaw_updates (stage);
}

static gboolean
update_last_device (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  MetaCursorTracker *cursor_tracker = priv->cursor_tracker;
  ClutterInputDeviceType device_type;
  ClutterDeviceManager *manager;
  ClutterInputDevice *device;

  priv->device_update_idle_id = 0;
  manager = clutter_device_manager_get_default ();
  device = clutter_device_manager_get_device (manager,
                                              priv->current_device_id);
  device_type = clutter_input_device_get_device_type (device);

  g_signal_emit (backend, signals[LAST_DEVICE_CHANGED], 0,
                 priv->current_device_id);

  switch (device_type)
    {
    case CLUTTER_KEYBOARD_DEVICE:
      break;
    case CLUTTER_TOUCHSCREEN_DEVICE:
      meta_cursor_tracker_set_pointer_visible (cursor_tracker, FALSE);
      break;
    default:
      meta_cursor_tracker_set_pointer_visible (cursor_tracker, TRUE);
      break;
    }

  return G_SOURCE_REMOVE;
}

void
meta_backend_update_last_device (MetaBackend *backend,
                                 int          device_id)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  ClutterDeviceManager *manager;
  ClutterInputDevice *device;

  if (priv->current_device_id == device_id)
    return;

  manager = clutter_device_manager_get_default ();
  device = clutter_device_manager_get_device (manager, device_id);

  if (!device ||
      clutter_input_device_get_device_mode (device) == CLUTTER_INPUT_MODE_MASTER)
    return;

  priv->current_device_id = device_id;

  if (priv->device_update_idle_id == 0)
    {
      priv->device_update_idle_id =
        g_idle_add ((GSourceFunc) update_last_device, backend);
      g_source_set_name_by_id (priv->device_update_idle_id,
                               "[mutter] update_last_device");
    }
}

gboolean
meta_backend_get_relative_motion_deltas (MetaBackend *backend,
                                         const        ClutterEvent *event,
                                         double       *dx,
                                         double       *dy,
                                         double       *dx_unaccel,
                                         double       *dy_unaccel)
{
  MetaBackendClass *klass = META_BACKEND_GET_CLASS (backend);
  return klass->get_relative_motion_deltas (backend,
                                            event,
                                            dx, dy,
                                            dx_unaccel, dy_unaccel);
}

MetaPointerConstraint *
meta_backend_get_client_pointer_constraint (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->client_pointer_constraint;
}

/**
 * meta_backend_set_client_pointer_constraint:
 * @backend: a #MetaBackend object.
 * @constraint: (nullable): the client constraint to follow.
 *
 * Sets the current pointer constraint and removes (and unrefs) the previous
 * one. If @constrant is %NULL, this means that there is no
 * #MetaPointerConstraint active.
 */
void
meta_backend_set_client_pointer_constraint (MetaBackend           *backend,
                                            MetaPointerConstraint *constraint)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  g_assert (!constraint || !priv->client_pointer_constraint);

  g_clear_object (&priv->client_pointer_constraint);
  if (constraint)
    priv->client_pointer_constraint = g_object_ref (constraint);
}

/* Mutter is responsible for pulling events off the X queue, so Clutter
 * doesn't need (and shouldn't) run its normal event source which polls
 * the X fd, but we do have to deal with dispatching events that accumulate
 * in the clutter queue. This happens, for example, when clutter generate
 * enter/leave events on mouse motion - several events are queued in the
 * clutter queue but only one dispatched. It could also happen because of
 * explicit calls to clutter_event_put(). We add a very simple custom
 * event loop source which is simply responsible for pulling events off
 * of the queue and dispatching them before we block for new events.
 */

static gboolean
event_prepare (GSource    *source,
               gint       *timeout_)
{
  *timeout_ = -1;

  return clutter_events_pending ();
}

static gboolean
event_check (GSource *source)
{
  return clutter_events_pending ();
}

static gboolean
event_dispatch (GSource    *source,
                GSourceFunc callback,
                gpointer    user_data)
{
  ClutterEvent *event = clutter_event_get ();

  if (event)
    {
      clutter_do_event (event);
      clutter_event_free (event);
    }

  return TRUE;
}

static GSourceFuncs event_funcs = {
  event_prepare,
  event_check,
  event_dispatch
};

ClutterBackend *
meta_backend_get_clutter_backend (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  if (!priv->clutter_backend)
    {
      priv->clutter_backend =
        META_BACKEND_GET_CLASS (backend)->create_clutter_backend (backend);
    }

  return priv->clutter_backend;
}

static ClutterBackend *
meta_get_clutter_backend (void)
{
  MetaBackend *backend = meta_get_backend ();

  return meta_backend_get_clutter_backend (backend);
}

void
meta_init_backend (GType backend_gtype)
{
  MetaBackend *backend;
  GError *error = NULL;

  /* meta_backend_init() above install the backend globally so
   * so meta_get_backend() works even during initialization. */
  backend = g_object_new (backend_gtype, NULL);
  if (!g_initable_init (G_INITABLE (backend), NULL, &error))
    {
      g_warning ("Failed to create backend: %s", error->message);
      meta_exit (META_EXIT_ERROR);
    }
}

/**
 * meta_clutter_init: (skip)
 */
void
meta_clutter_init (void)
{
  GSource *source;

  clutter_set_custom_backend_func (meta_get_clutter_backend);

  if (clutter_init (NULL, NULL) != CLUTTER_INIT_SUCCESS)
    {
      g_warning ("Unable to initialize Clutter.\n");
      exit (1);
    }

  source = g_source_new (&event_funcs, sizeof (GSource));
  g_source_attach (source, NULL);
  g_source_unref (source);

  meta_backend_post_init (_backend);
}

/**
 * meta_is_stage_views_enabled:
 *
 * Returns whether the #ClutterStage can be rendered using multiple stage views.
 * In practice, this means we can define a separate framebuffer for each
 * #MetaLogicalMonitor, rather than rendering everything into a single
 * framebuffer. For example: in X11, onle one single framebuffer is allowed.
 */
gboolean
meta_is_stage_views_enabled (void)
{
  if (!meta_is_wayland_compositor ())
    return FALSE;

  return !stage_views_disabled;
}

gboolean
meta_is_stage_views_scaled (void)
{
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaLogicalMonitorLayoutMode layout_mode;

  if (!meta_is_stage_views_enabled ())
    return FALSE;

  layout_mode = monitor_manager->layout_mode;

  return layout_mode == META_LOGICAL_MONITOR_LAYOUT_MODE_LOGICAL;
}

MetaInputSettings *
meta_backend_get_input_settings (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->input_settings;
}

/**
 * meta_backend_get_dnd:
 * @backend: A #MetaDnd
 *
 * Gets the global #MetaDnd that's managed by this backend.
 *
 * Returns: (transfer none): the #MetaDnd
 */
MetaDnd *
meta_backend_get_dnd (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->dnd;
}

void
meta_backend_notify_keymap_changed (MetaBackend *backend)
{
  g_signal_emit (backend, signals[KEYMAP_CHANGED], 0);
}

void
meta_backend_notify_keymap_layout_group_changed (MetaBackend *backend,
                                                 unsigned int locked_group)
{
  g_signal_emit (backend, signals[KEYMAP_LAYOUT_GROUP_CHANGED], 0,
                 locked_group);
}

void
meta_backend_add_gpu (MetaBackend *backend,
                      MetaGpu     *gpu)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  priv->gpus = g_list_append (priv->gpus, gpu);

  g_signal_emit (backend, signals[GPU_ADDED], 0, gpu);
}

GList *
meta_backend_get_gpus (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->gpus;
}
