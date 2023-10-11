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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

/**
 * MetaBackend:
 *
 * Handles monitor config, modesetting, cursor sprites, ...
 *
 * MetaBackend is the abstraction that deals with several things like:
 * - Modesetting (depending on the backend, this can be done either by X or KMS)
 * - Initializing the #MetaSettings
 * - Setting up Monitor configuration
 * - Input device configuration (using the #ClutterDeviceManager)
 * - Creating the #MetaRenderer
 * - Setting up the stage of the scene graph (using #MetaStage)
 * - Creating the object that deals with the cursor (using #MetaCursorTracker)
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

#include "backends/meta-barrier-private.h"
#include "backends/meta-cursor-renderer.h"
#include "backends/meta-cursor-tracker-private.h"
#include "backends/meta-dbus-session-watcher.h"
#include "backends/meta-idle-manager.h"
#include "backends/meta-idle-monitor-private.h"
#include "backends/meta-input-capture.h"
#include "backends/meta-input-mapper-private.h"
#include "backends/meta-input-settings-private.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor-manager-dummy.h"
#include "backends/meta-remote-access-controller-private.h"
#include "backends/meta-settings-private.h"
#include "backends/meta-stage-private.h"
#include "backends/x11/meta-backend-x11.h"
#include "clutter/clutter-mutter.h"
#include "clutter/clutter-seat-private.h"
#include "compositor/meta-dnd-private.h"
#include "core/meta-context-private.h"
#include "meta/main.h"
#include "meta/meta-backend.h"
#include "meta/meta-context.h"
#include "meta/meta-enum-types.h"
#include "meta/util.h"

#ifdef HAVE_REMOTE_DESKTOP
#include "backends/meta-dbus-session-watcher.h"
#include "backends/meta-remote-access-controller-private.h"
#include "backends/meta-remote-desktop.h"
#include "backends/meta-screen-cast.h"
#endif

#ifdef HAVE_NATIVE_BACKEND
#include "backends/native/meta-backend-native.h"
#endif

#ifdef HAVE_WAYLAND
#include "wayland/meta-wayland.h"
#endif

enum
{
  PROP_0,

  PROP_CONTEXT,
  PROP_CAPABILITIES,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

enum
{
  KEYMAP_CHANGED,
  KEYMAP_LAYOUT_GROUP_CHANGED,
  LAST_DEVICE_CHANGED,
  LID_IS_CLOSED_CHANGED,
  GPU_ADDED,
  PREPARE_SHUTDOWN,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

#define HIDDEN_POINTER_TIMEOUT 300 /* ms */

#ifndef BTN_LEFT
#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112
#endif

struct _MetaBackendPrivate
{
  MetaContext *context;

  MetaMonitorManager *monitor_manager;
  MetaOrientationManager *orientation_manager;
  MetaCursorTracker *cursor_tracker;
  MetaInputMapper *input_mapper;
  MetaIdleManager *idle_manager;
  MetaRenderer *renderer;
  MetaColorManager *color_manager;
#ifdef HAVE_EGL
  MetaEgl *egl;
#endif
  MetaSettings *settings;
  MetaDbusSessionWatcher *dbus_session_watcher;
  MetaRemoteAccessController *remote_access_controller;
#ifdef HAVE_REMOTE_DESKTOP
  MetaScreenCast *screen_cast;
  MetaRemoteDesktop *remote_desktop;
#endif
  MetaInputCapture *input_capture;

#ifdef HAVE_LIBWACOM
  WacomDeviceDatabase *wacom_db;
#endif
#ifdef HAVE_GNOME_DESKTOP
  GnomePnpIds *pnp_ids;
#endif

  ClutterContext *clutter_context;
  ClutterSeat *default_seat;
  ClutterActor *stage;

  GList *gpus;
  GList *hw_cursor_inhibitors;

  gboolean in_init;

  guint device_update_idle_id;

  ClutterInputDevice *current_device;

  MetaPointerConstraint *client_pointer_constraint;
  MetaDnd *dnd;

  guint upower_watch_id;
  GDBusProxy *upower_proxy;
  gboolean lid_is_closed;
  gboolean on_battery;

  guint sleep_signal_id;
  GCancellable *cancellable;
  GDBusConnection *system_bus;

  uint32_t last_pointer_motion;
};
typedef struct _MetaBackendPrivate MetaBackendPrivate;

typedef struct _MetaBackendSource MetaBackendSource;

struct _MetaBackendSource
{
  GSource parent;
  MetaBackend *backend;
};

static void
initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (MetaBackend, meta_backend, G_TYPE_OBJECT,
                                  G_ADD_PRIVATE (MetaBackend)
                                  G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                         initable_iface_init));

static void
meta_backend_dispose (GObject *object)
{
  MetaBackend *backend = META_BACKEND (object);
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  g_clear_pointer (&priv->cursor_tracker, meta_cursor_tracker_destroy);
  g_clear_object (&priv->current_device);
  g_clear_object (&priv->color_manager);
  g_clear_object (&priv->monitor_manager);
  g_clear_object (&priv->orientation_manager);
#ifdef HAVE_REMOTE_DESKTOP
  g_clear_object (&priv->remote_desktop);
  g_clear_object (&priv->screen_cast);
#endif
  g_clear_object (&priv->input_capture);
  g_clear_object (&priv->dbus_session_watcher);
  g_clear_object (&priv->remote_access_controller);

#ifdef HAVE_LIBWACOM
  g_clear_pointer (&priv->wacom_db, libwacom_database_destroy);
#endif
#ifdef HAVE_GNOME_DESKTOP
  g_clear_object (&priv->pnp_ids);
#endif

  if (priv->sleep_signal_id)
    {
      g_dbus_connection_signal_unsubscribe (priv->system_bus, priv->sleep_signal_id);
      priv->sleep_signal_id = 0;
    }

  if (priv->upower_watch_id)
    {
      g_bus_unwatch_name (priv->upower_watch_id);
      priv->upower_watch_id = 0;
    }

  g_cancellable_cancel (priv->cancellable);
  g_clear_object (&priv->cancellable);
  g_clear_object (&priv->system_bus);
  g_clear_object (&priv->upower_proxy);

  g_clear_handle_id (&priv->device_update_idle_id, g_source_remove);

  g_clear_object (&priv->settings);

  g_clear_pointer (&priv->default_seat, clutter_seat_destroy);
  g_clear_pointer (&priv->stage, clutter_actor_destroy);
  g_clear_pointer (&priv->idle_manager, meta_idle_manager_free);
  g_clear_object (&priv->renderer);
  g_clear_pointer (&priv->clutter_context, clutter_context_free);
  g_clear_list (&priv->gpus, g_object_unref);

  G_OBJECT_CLASS (meta_backend_parent_class)->dispose (object);
}

void
meta_backend_destroy (MetaBackend *backend)
{
  g_object_run_dispose (G_OBJECT (backend));
  g_object_unref (backend);
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
init_pointer_position (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  MetaMonitorManager *monitor_manager = priv->monitor_manager;
  MetaCursorRenderer *cursor_renderer;
  ClutterSeat *seat = priv->default_seat;
  MetaLogicalMonitor *primary;

  primary =
    meta_monitor_manager_get_primary_logical_monitor (monitor_manager);

  /* Move the pointer out of the way to avoid hovering over reactive
   * elements (e.g. users list at login) causing undesired behaviour. */
  clutter_seat_init_pointer_position (seat,
                                      primary->rect.x + primary->rect.width * 0.9,
                                      primary->rect.y + primary->rect.height * 0.9);

  cursor_renderer = meta_backend_get_cursor_renderer (backend);
  meta_cursor_renderer_update_position (cursor_renderer);
}

static gboolean
should_have_cursor_renderer (ClutterInputDevice *device)
{
  switch (clutter_input_device_get_device_type (device))
    {
    case CLUTTER_POINTER_DEVICE:
      if (clutter_input_device_get_device_mode (device) ==
          CLUTTER_INPUT_MODE_LOGICAL)
        return TRUE;

      return FALSE;
    case CLUTTER_TABLET_DEVICE:
      return TRUE;
    default:
      return FALSE;
    }
}

static void
update_cursors (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  ClutterSeat *seat = priv->default_seat;
  MetaCursorRenderer *cursor_renderer;
  ClutterInputDevice *pointer, *device;
  GList *devices, *l;

  pointer = clutter_seat_get_pointer (seat);
  devices = clutter_seat_list_devices (seat);
  devices = g_list_prepend (devices, pointer);

  for (l = devices; l; l = l->next)
    {
      device = l->data;

      if (!should_have_cursor_renderer (device))
        continue;

      cursor_renderer = meta_backend_get_cursor_renderer_for_device (backend,
                                                                     device);
      if (cursor_renderer)
        meta_cursor_renderer_force_update (cursor_renderer);
    }

  g_list_free (devices);
}

void
meta_backend_monitors_changed (MetaBackend *backend)
{
  meta_backend_sync_screen_size (backend);
  update_cursors (backend);
}

static gboolean
update_last_device (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  priv->device_update_idle_id = 0;
  g_signal_emit (backend, signals[LAST_DEVICE_CHANGED], 0,
                 priv->current_device);

  return G_SOURCE_REMOVE;
}

static void
meta_backend_update_last_device (MetaBackend        *backend,
                                 ClutterInputDevice *device)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  if (priv->current_device == device)
    return;

  if (!device ||
      clutter_input_device_get_device_mode (device) == CLUTTER_INPUT_MODE_LOGICAL)
    return;

  g_set_object (&priv->current_device, device);

  if (priv->device_update_idle_id == 0)
    {
      priv->device_update_idle_id =
        g_idle_add ((GSourceFunc) update_last_device, backend);
      g_source_set_name_by_id (priv->device_update_idle_id,
                               "[mutter] update_last_device");
    }
}

static inline gboolean
determine_hotplug_pointer_visibility (ClutterSeat *seat)
{
  g_autoptr (GList) devices = NULL;
  const GList *l;
  gboolean has_touchscreen = FALSE, has_pointer = FALSE, has_tablet = FALSE;

  devices = clutter_seat_list_devices (seat);

  for (l = devices; l; l = l->next)
    {
      ClutterInputDevice *device = l->data;
      ClutterInputDeviceType device_type;

      device_type = clutter_input_device_get_device_type (device);

      if (device_type == CLUTTER_TOUCHSCREEN_DEVICE)
        has_touchscreen = TRUE;
      if (device_type == CLUTTER_POINTER_DEVICE ||
          device_type == CLUTTER_TOUCHPAD_DEVICE)
        has_pointer = TRUE;
      if (device_type == CLUTTER_TABLET_DEVICE ||
          device_type == CLUTTER_PEN_DEVICE ||
          device_type == CLUTTER_ERASER_DEVICE)
        {
          if (meta_is_wayland_compositor ())
            has_tablet = TRUE;
          else
            has_pointer = TRUE;
        }
    }

  return has_pointer && !has_touchscreen && !has_tablet;
}

static void
on_device_added (ClutterSeat        *seat,
                 ClutterInputDevice *device,
                 gpointer            user_data)
{
  MetaBackend *backend = META_BACKEND (user_data);
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  ClutterInputDeviceType device_type;

  if (clutter_input_device_get_device_mode (device) ==
      CLUTTER_INPUT_MODE_LOGICAL)
    return;

  device_type = clutter_input_device_get_device_type (device);

  if (!priv->in_init &&
      (device_type == CLUTTER_TOUCHSCREEN_DEVICE ||
       device_type == CLUTTER_POINTER_DEVICE))
    {
      meta_cursor_tracker_set_pointer_visible (priv->cursor_tracker,
                                               determine_hotplug_pointer_visibility (seat));
    }

  if (device_type == CLUTTER_TOUCHSCREEN_DEVICE ||
      device_type == CLUTTER_TABLET_DEVICE ||
      device_type == CLUTTER_PEN_DEVICE ||
      device_type == CLUTTER_ERASER_DEVICE ||
      device_type == CLUTTER_CURSOR_DEVICE ||
      device_type == CLUTTER_PAD_DEVICE)
    meta_input_mapper_add_device (priv->input_mapper, device);
}

static void
on_device_removed (ClutterSeat        *seat,
                   ClutterInputDevice *device,
                   gpointer            user_data)
{
  MetaBackend *backend = META_BACKEND (user_data);
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  g_warn_if_fail (!priv->in_init);

  if (clutter_input_device_get_device_mode (device) ==
      CLUTTER_INPUT_MODE_LOGICAL)
    return;

  meta_input_mapper_remove_device (priv->input_mapper, device);

  /* If the device the user last interacted goes away, check again pointer
   * visibility.
   */
  if (priv->current_device == device)
    {
      MetaCursorTracker *cursor_tracker = priv->cursor_tracker;

      g_clear_object (&priv->current_device);
      g_clear_handle_id (&priv->device_update_idle_id, g_source_remove);

      meta_cursor_tracker_set_pointer_visible (cursor_tracker,
                                               determine_hotplug_pointer_visibility (seat));
    }

  if (priv->current_device == device)
    meta_backend_update_last_device (backend, NULL);
}

static void
input_mapper_device_mapped_cb (MetaInputMapper    *mapper,
                               ClutterInputDevice *device,
                               float               matrix[6],
                               MetaInputSettings  *input_settings)
{
  meta_input_settings_set_device_matrix (input_settings, device, matrix);
}

static void
input_mapper_device_enabled_cb (MetaInputMapper    *mapper,
                                ClutterInputDevice *device,
                                gboolean            enabled,
                                MetaInputSettings  *input_settings)
{
  meta_input_settings_set_device_enabled (input_settings, device, enabled);
}

static void
input_mapper_device_aspect_ratio_cb (MetaInputMapper    *mapper,
                                     ClutterInputDevice *device,
                                     double              aspect_ratio,
                                     MetaInputSettings  *input_settings)
{
  meta_input_settings_set_device_aspect_ratio (input_settings, device, aspect_ratio);
}

static void
on_prepare_shutdown (MetaContext *context,
                     MetaBackend *backend)
{
  g_signal_emit (backend, signals[PREPARE_SHUTDOWN], 0);
}

static void
on_started (MetaContext *context,
            MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  ClutterSeat *seat = priv->default_seat;

  meta_cursor_tracker_set_pointer_visible (priv->cursor_tracker,
                                           determine_hotplug_pointer_visibility (seat));
}

static void
meta_backend_real_post_init (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  ClutterSeat *seat = priv->default_seat;
  MetaInputSettings *input_settings;

  priv->stage = meta_stage_new (backend);
  clutter_actor_realize (priv->stage);
  META_BACKEND_GET_CLASS (backend)->select_stage_events (backend);

  meta_monitor_manager_setup (priv->monitor_manager);

  meta_backend_sync_screen_size (backend);

  priv->idle_manager = meta_idle_manager_new (backend);

  g_signal_connect_object (seat, "device-added",
                           G_CALLBACK (on_device_added), backend, 0);
  g_signal_connect_object (seat, "device-removed",
                           G_CALLBACK (on_device_removed), backend,
                           G_CONNECT_AFTER);

  priv->input_mapper = meta_input_mapper_new (backend);

  input_settings = meta_backend_get_input_settings (backend);

  if (input_settings)
    {
      g_signal_connect (priv->input_mapper, "device-mapped",
                        G_CALLBACK (input_mapper_device_mapped_cb),
                        input_settings);
      g_signal_connect (priv->input_mapper, "device-enabled",
                        G_CALLBACK (input_mapper_device_enabled_cb),
                        input_settings);
      g_signal_connect (priv->input_mapper, "device-aspect-ratio",
                        G_CALLBACK (input_mapper_device_aspect_ratio_cb),
                        input_settings);
    }

  priv->remote_access_controller =
    meta_remote_access_controller_new ();
  priv->dbus_session_watcher =
    g_object_new (META_TYPE_DBUS_SESSION_WATCHER, NULL);

#ifdef HAVE_REMOTE_DESKTOP
  priv->screen_cast = meta_screen_cast_new (backend);
  meta_remote_access_controller_add (
    priv->remote_access_controller,
    META_DBUS_SESSION_MANAGER (priv->screen_cast));
  priv->remote_desktop = meta_remote_desktop_new (backend);
  meta_remote_access_controller_add (
    priv->remote_access_controller,
    META_DBUS_SESSION_MANAGER (priv->remote_desktop));
#endif /* HAVE_REMOTE_DESKTOP */

  priv->input_capture = meta_input_capture_new (backend);
  meta_remote_access_controller_add (
    priv->remote_access_controller,
    META_DBUS_SESSION_MANAGER (priv->input_capture));

  if (!meta_monitor_manager_is_headless (priv->monitor_manager))
    init_pointer_position (backend);

  meta_monitor_manager_post_init (priv->monitor_manager);

  g_signal_connect (priv->context, "prepare-shutdown",
                    G_CALLBACK (on_prepare_shutdown), backend);
  g_signal_connect (priv->context, "started",
                    G_CALLBACK (on_started), backend);
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
meta_backend_real_is_lid_closed (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->lid_is_closed;
}

static MetaCursorTracker *
meta_backend_real_create_cursor_tracker (MetaBackend *backend)
{
  return g_object_new (META_TYPE_CURSOR_TRACKER,
                       "backend", backend,
                       NULL);
}

static gboolean
meta_backend_real_is_headless (MetaBackend *backend)
{
  return FALSE;
}

gboolean
meta_backend_is_lid_closed (MetaBackend *backend)
{
  return META_BACKEND_GET_CLASS (backend)->is_lid_closed (backend);
}

gboolean
meta_backend_is_headless (MetaBackend *backend)
{
  return META_BACKEND_GET_CLASS (backend)->is_headless (backend);
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
  gboolean reset_idle_time = FALSE;

  v = g_variant_lookup_value (changed_properties,
                              "LidIsClosed",
                              G_VARIANT_TYPE_BOOLEAN);
  if (v)
    {
      gboolean lid_is_closed;

      lid_is_closed = g_variant_get_boolean (v);
      g_variant_unref (v);

      if (lid_is_closed != priv->lid_is_closed)
        {
          priv->lid_is_closed = lid_is_closed;
          g_signal_emit (backend, signals[LID_IS_CLOSED_CHANGED], 0,
                         priv->lid_is_closed);

          if (!lid_is_closed)
            reset_idle_time = TRUE;
        }
    }

  v = g_variant_lookup_value (changed_properties,
                              "OnBattery",
                              G_VARIANT_TYPE_BOOLEAN);
  if (v)
    {
      gboolean on_battery;

      on_battery = g_variant_get_boolean (v);
      g_variant_unref (v);

      if (on_battery != priv->on_battery)
        {
          priv->on_battery = on_battery;
          reset_idle_time = TRUE;
        }
    }

  if (reset_idle_time)
    meta_idle_manager_reset_idle_time (priv->idle_manager);
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
  if (v)
    {
      priv->lid_is_closed = g_variant_get_boolean (v);
      g_variant_unref (v);

      if (priv->lid_is_closed)
        {
          g_signal_emit (backend, signals[LID_IS_CLOSED_CHANGED], 0,
                         priv->lid_is_closed);
        }
    }

  v = g_dbus_proxy_get_cached_property (proxy, "OnBattery");
  if (v)
    {
      priv->on_battery = g_variant_get_boolean (v);
      g_variant_unref (v);
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

  g_assert (priv->context);

  priv->settings = meta_settings_new (backend);

#ifdef HAVE_LIBWACOM
  priv->wacom_db = libwacom_database_new ();
  if (!priv->wacom_db)
    {
      g_warning ("Could not create database of Wacom devices, "
                 "expect tablets to misbehave");
    }
#endif

  if (backend_class->is_lid_closed == meta_backend_real_is_lid_closed)
    {
      priv->upower_watch_id = g_bus_watch_name (G_BUS_TYPE_SYSTEM,
                                                "org.freedesktop.UPower",
                                                G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                upower_appeared,
                                                upower_vanished,
                                                backend,
                                                NULL);
    }

#ifdef HAVE_EGL
  priv->egl = g_object_new (META_TYPE_EGL, NULL);
#endif

  G_OBJECT_CLASS (meta_backend_parent_class)->constructed (object);
}

static void
meta_backend_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  MetaBackend *backend = META_BACKEND (object);
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  switch (prop_id)
    {
    case PROP_CONTEXT:
      priv->context = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_backend_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  MetaBackend *backend = META_BACKEND (object);
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  switch (prop_id)
    {
    case PROP_CONTEXT:
      g_value_set_object (value, priv->context);
      break;
    case PROP_CAPABILITIES:
      g_value_set_flags (value, meta_backend_get_capabilities (backend));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_backend_class_init (MetaBackendClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_backend_dispose;
  object_class->constructed = meta_backend_constructed;
  object_class->set_property = meta_backend_set_property;
  object_class->get_property = meta_backend_get_property;

  klass->post_init = meta_backend_real_post_init;
  klass->grab_device = meta_backend_real_grab_device;
  klass->ungrab_device = meta_backend_real_ungrab_device;
  klass->select_stage_events = meta_backend_real_select_stage_events;
  klass->is_lid_closed = meta_backend_real_is_lid_closed;
  klass->create_cursor_tracker = meta_backend_real_create_cursor_tracker;
  klass->is_headless = meta_backend_real_is_headless;

  obj_props[PROP_CONTEXT] =
    g_param_spec_object ("context", NULL, NULL,
                         META_TYPE_CONTEXT,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_CAPABILITIES] =
    g_param_spec_flags ("capabilities", NULL, NULL,
                        META_TYPE_BACKEND_CAPABILITIES,
                        META_BACKEND_CAPABILITY_NONE,
                        G_PARAM_READABLE |
                        G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);

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
                  G_TYPE_NONE, 1, CLUTTER_TYPE_INPUT_DEVICE);
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
  signals[PREPARE_SHUTDOWN] =
    g_signal_new ("prepare-shutdown",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static MetaMonitorManager *
meta_backend_create_monitor_manager (MetaBackend *backend,
                                     GError     **error)
{
  return META_BACKEND_GET_CLASS (backend)->create_monitor_manager (backend,
                                                                   error);
}

static MetaColorManager *
meta_backend_create_color_manager (MetaBackend *backend)
{
  return META_BACKEND_GET_CLASS (backend)->create_color_manager (backend);
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
  MetaBackend *backend = user_data;
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  gboolean suspending;

  g_variant_get (parameters, "(b)", &suspending);
  if (suspending)
    return;

  meta_idle_manager_reset_idle_time (priv->idle_manager);
}

static void
system_bus_gotten_cb (GObject      *object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  MetaBackend *backend = user_data;
  MetaBackendPrivate *priv;
  GDBusConnection *bus;

  bus = g_bus_get_finish (res, NULL);
  if (!bus)
    return;

  priv = meta_backend_get_instance_private (backend);
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
                                        backend,
                                        NULL);
}

static void
update_last_device_from_event (MetaBackend  *backend,
                               ClutterEvent *event)
{
  ClutterInputDevice *source;
  ClutterEventType event_type;

  event_type = clutter_event_type (event);

  /* Handled elsewhere */
  if (event_type == CLUTTER_DEVICE_ADDED ||
      event_type == CLUTTER_DEVICE_REMOVED)
    return;

  source = clutter_event_get_source_device (event);
  if (source)
    meta_backend_update_last_device (backend, source);
}

static void
update_pointer_visibility_from_event (MetaBackend  *backend,
                                      ClutterEvent *event)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  MetaCursorTracker *cursor_tracker = priv->cursor_tracker;
  ClutterInputDevice *device;
  ClutterInputDeviceType device_type;
  uint32_t time_ms;

  g_warn_if_fail (!priv->in_init);

  device = clutter_event_get_source_device (event);
  if (!device)
    return;

  device_type = clutter_input_device_get_device_type (device);
  time_ms = clutter_event_get_time (event);

  switch (device_type)
    {
    case CLUTTER_TOUCHSCREEN_DEVICE:
      meta_cursor_tracker_set_pointer_visible (cursor_tracker, FALSE);
      break;
    case CLUTTER_POINTER_DEVICE:
    case CLUTTER_TOUCHPAD_DEVICE:
      priv->last_pointer_motion = time_ms;
      meta_cursor_tracker_set_pointer_visible (cursor_tracker, TRUE);
      break;
    case CLUTTER_TABLET_DEVICE:
    case CLUTTER_PEN_DEVICE:
    case CLUTTER_ERASER_DEVICE:
    case CLUTTER_CURSOR_DEVICE:
      if (meta_is_wayland_compositor () &&
          time_ms > priv->last_pointer_motion + HIDDEN_POINTER_TIMEOUT)
        meta_cursor_tracker_set_pointer_visible (cursor_tracker, FALSE);
      break;
    case CLUTTER_KEYBOARD_DEVICE:
    case CLUTTER_PAD_DEVICE:
    case CLUTTER_EXTENSION_DEVICE:
    case CLUTTER_JOYSTICK_DEVICE:
    default:
      break;
    }
}

static gboolean
dispatch_clutter_event (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
  ClutterEvent *event;

  event = clutter_event_get ();
  if (event)
    {
      g_warn_if_fail (!priv->in_init ||
                      clutter_event_type (event) == CLUTTER_DEVICE_ADDED);

      clutter_stage_handle_event (stage, event);
      meta_backend_update_from_event (backend, event);
      clutter_event_free (event);
      return TRUE;
    }

  return FALSE;
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
clutter_source_prepare (GSource *source,
                        int     *timeout)
{
  *timeout = -1;

  return clutter_events_pending ();
}

static gboolean
clutter_source_check (GSource *source)
{
  return clutter_events_pending ();
}

static gboolean
clutter_source_dispatch (GSource     *source,
                         GSourceFunc  callback,
                         gpointer     user_data)
{
  MetaBackendSource *backend_source = (MetaBackendSource *) source;

  dispatch_clutter_event (backend_source->backend);

  return TRUE;
}

static GSourceFuncs clutter_source_funcs = {
  clutter_source_prepare,
  clutter_source_check,
  clutter_source_dispatch
};

static ClutterBackend *
meta_clutter_backend_constructor (gpointer user_data)
{
  MetaBackend *backend = META_BACKEND (user_data);

  return META_BACKEND_GET_CLASS (backend)->create_clutter_backend (backend);
}

static ClutterSeat *
meta_backend_create_default_seat (MetaBackend  *backend,
                                  GError      **error)
{
  return META_BACKEND_GET_CLASS (backend)->create_default_seat (backend, error);
}

static gboolean
init_clutter (MetaBackend  *backend,
              GError      **error)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  MetaBackendSource *backend_source;
  GSource *source;

  priv->clutter_context = clutter_context_new (meta_clutter_backend_constructor,
                                               backend,
                                               error);
  if (!priv->clutter_context)
    return FALSE;

  priv->default_seat = meta_backend_create_default_seat (backend, error);
  if (!priv->default_seat)
    return FALSE;

  source = g_source_new (&clutter_source_funcs, sizeof (MetaBackendSource));
  g_source_set_name (source, "[mutter] Backend");
  backend_source = (MetaBackendSource *) source;
  backend_source->backend = backend;
  g_source_attach (source, NULL);
  g_source_unref (source);

  return TRUE;
}

static void
meta_backend_post_init (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  META_BACKEND_GET_CLASS (backend)->post_init (backend);

  meta_settings_post_init (priv->settings);
}

static gboolean
meta_backend_initable_init (GInitable     *initable,
                            GCancellable  *cancellable,
                            GError       **error)
{
  MetaBackend *backend = META_BACKEND (initable);
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  priv->orientation_manager = g_object_new (META_TYPE_ORIENTATION_MANAGER, NULL);

  priv->monitor_manager = meta_backend_create_monitor_manager (backend, error);
  if (!priv->monitor_manager)
    return FALSE;

  priv->color_manager = meta_backend_create_color_manager (backend);

  priv->renderer = meta_backend_create_renderer (backend, error);
  if (!priv->renderer)
    return FALSE;

  priv->cursor_tracker =
    META_BACKEND_GET_CLASS (backend)->create_cursor_tracker (backend);

  priv->dnd = meta_dnd_new (backend);

  priv->cancellable = g_cancellable_new ();
  g_bus_get (G_BUS_TYPE_SYSTEM,
             priv->cancellable,
             system_bus_gotten_cb,
             backend);

  if (!init_clutter (backend, error))
    return FALSE;

  meta_backend_post_init (backend);

  while (TRUE)
    {
      if (!dispatch_clutter_event (backend))
        break;
    }
  _clutter_stage_process_queued_events (CLUTTER_STAGE (priv->stage));

  priv->in_init = FALSE;

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
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  priv->in_init = TRUE;
}

/**
 * meta_backend_get_idle_monitor: (skip)
 */
MetaIdleMonitor *
meta_backend_get_idle_monitor (MetaBackend        *backend,
                               ClutterInputDevice *device)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return meta_idle_manager_get_monitor (priv->idle_manager, device);
}

/**
 * meta_backend_get_core_idle_monitor:
 *
 * Returns: (transfer none): the #MetaIdleMonitor that tracks server-global
 * idle time for all devices.
 */
MetaIdleMonitor *
meta_backend_get_core_idle_monitor (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return meta_idle_manager_get_core_monitor (priv->idle_manager);
}

/**
 * meta_backend_get_idle_manager: (skip)
 */
MetaIdleManager *
meta_backend_get_idle_manager (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->idle_manager;
}

/**
 * meta_backend_get_monitor_manager:
 *
 * Returns: (transfer none): A #MetaMonitorManager
 */
MetaMonitorManager *
meta_backend_get_monitor_manager (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->monitor_manager;
}

/**
 * meta_backend_get_color_manager: (skip)
 */
MetaColorManager *
meta_backend_get_color_manager (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->color_manager;
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
  ClutterInputDevice *pointer;

  if (!priv->default_seat)
    return NULL;

  pointer = clutter_seat_get_pointer (priv->default_seat);

  return meta_backend_get_cursor_renderer_for_device (backend, pointer);
}

MetaCursorRenderer *
meta_backend_get_cursor_renderer_for_device (MetaBackend        *backend,
                                             ClutterInputDevice *device)
{
  g_return_val_if_fail (META_IS_BACKEND (backend), NULL);
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), NULL);
  g_return_val_if_fail (clutter_input_device_get_device_type (device) !=
                        CLUTTER_KEYBOARD_DEVICE, NULL);

  return META_BACKEND_GET_CLASS (backend)->get_cursor_renderer (backend,
                                                                device);
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

MetaDbusSessionWatcher *
meta_backend_get_dbus_session_watcher (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->dbus_session_watcher;
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

/**
 * meta_backend_get_screen_cast: (skip)
 */
MetaScreenCast *
meta_backend_get_screen_cast (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->screen_cast;
}
#endif /* HAVE_REMOTE_DESKTOP */

MetaInputCapture *
meta_backend_get_input_capture (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->input_capture;
}

/**
 * meta_backend_get_remote_access_controller:
 * @backend: A #MetaBackend
 *
 * Return Value: (transfer none): The #MetaRemoteAccessController
 */
MetaRemoteAccessController *
meta_backend_get_remote_access_controller (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->remote_access_controller;
}

/**
 * meta_backend_is_rendering_hardware_accelerated:
 * @backend: A #MetaBackend
 *
 * Returns: %TRUE if the rendering is hardware accelerated, otherwise
 * %FALSE.
 */
gboolean
meta_backend_is_rendering_hardware_accelerated (MetaBackend *backend)
{
  MetaRenderer *renderer = meta_backend_get_renderer (backend);

  return meta_renderer_is_hardware_accelerated (renderer);
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
 * meta_backend_get_context:
 * @backend: the #MetaBackend
 *
 * Returns: (transfer none): The #MetaContext
 */
MetaContext *
meta_backend_get_context (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->context;
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
 * meta_backend_finish_touch_sequence: (skip)
 */
void
meta_backend_finish_touch_sequence (MetaBackend          *backend,
                                    ClutterEventSequence *sequence,
                                    MetaSequenceState     state)
{
  if (META_BACKEND_GET_CLASS (backend)->finish_touch_sequence)
    META_BACKEND_GET_CLASS (backend)->finish_touch_sequence (backend,
                                                             sequence,
                                                             state);
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

ClutterSeat *
meta_backend_get_default_seat (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->default_seat;
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
 * one. If @constraint is %NULL, this means that there is no
 * #MetaPointerConstraint active.
 */
void
meta_backend_set_client_pointer_constraint (MetaBackend           *backend,
                                            MetaPointerConstraint *constraint)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  META_BACKEND_GET_CLASS (backend)->set_pointer_constraint (backend, constraint);
  g_set_object (&priv->client_pointer_constraint, constraint);
}

ClutterBackend *
meta_backend_get_clutter_backend (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  ClutterContext *clutter_context;

  clutter_context = priv->clutter_context;
  if (!clutter_context)
    return NULL;

  return clutter_context_get_backend (clutter_context);
}

MetaBackendCapabilities
meta_backend_get_capabilities (MetaBackend *backend)
{
  return META_BACKEND_GET_CLASS (backend)->get_capabilities (backend);
}

gboolean
meta_backend_is_stage_views_scaled (MetaBackend *backend)
{
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaLogicalMonitorLayoutMode layout_mode;

  layout_mode = monitor_manager->layout_mode;

  return layout_mode == META_LOGICAL_MONITOR_LAYOUT_MODE_LOGICAL;
}

MetaInputMapper *
meta_backend_get_input_mapper (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->input_mapper;
}

MetaInputSettings *
meta_backend_get_input_settings (MetaBackend *backend)
{
  return META_BACKEND_GET_CLASS (backend)->get_input_settings (backend);
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

#ifdef HAVE_LIBWACOM
WacomDeviceDatabase *
meta_backend_get_wacom_database (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->wacom_db;
}
#endif

void
meta_backend_add_hw_cursor_inhibitor (MetaBackend           *backend,
                                      MetaHwCursorInhibitor *inhibitor)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  priv->hw_cursor_inhibitors = g_list_prepend (priv->hw_cursor_inhibitors,
                                               inhibitor);
}

void
meta_backend_remove_hw_cursor_inhibitor (MetaBackend           *backend,
                                         MetaHwCursorInhibitor *inhibitor)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  priv->hw_cursor_inhibitors = g_list_remove (priv->hw_cursor_inhibitors,
                                              inhibitor);
}

gboolean
meta_backend_is_hw_cursors_inhibited (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  GList *l;

  for (l = priv->hw_cursor_inhibitors; l; l = l->next)
    {
      MetaHwCursorInhibitor *inhibitor = l->data;

      if (meta_hw_cursor_inhibitor_is_cursor_inhibited (inhibitor))
        return TRUE;
    }

  return FALSE;
}

void
meta_backend_update_from_event (MetaBackend  *backend,
                                ClutterEvent *event)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  update_last_device_from_event (backend, event);

  if (!priv->in_init)
    update_pointer_visibility_from_event (backend, event);
}

/**
 * meta_backend_get_vendor_name:
 * @backend: A #MetaBackend object
 * @pnp_id: the PNP ID
 *
 * Find the full vendor name from the given PNP ID.
 *
 * Returns: (transfer full): A string containing the vendor name,
 *                           or NULL when not found.
 */
char *
meta_backend_get_vendor_name (MetaBackend *backend,
                              const char  *pnp_id)
{
#ifdef HAVE_GNOME_DESKTOP
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  if (!priv->pnp_ids)
    priv->pnp_ids = gnome_pnp_ids_new ();

  return gnome_pnp_ids_get_pnp_id (priv->pnp_ids, pnp_id);
#else
  return g_strdup (pnp_id);
#endif
}

uint32_t
meta_clutter_button_to_evdev (uint32_t clutter_button)
{
  switch (clutter_button)
    {
    case CLUTTER_BUTTON_PRIMARY:
      return BTN_LEFT;
    case CLUTTER_BUTTON_SECONDARY:
      return BTN_RIGHT;
    case CLUTTER_BUTTON_MIDDLE:
      return BTN_MIDDLE;
    }

  return (clutter_button + (BTN_LEFT - 1)) - 4;
}

uint32_t
meta_evdev_button_to_clutter (uint32_t evdev_button)
{
  switch (evdev_button)
    {
    case BTN_LEFT:
      return CLUTTER_BUTTON_PRIMARY;
    case BTN_RIGHT:
      return CLUTTER_BUTTON_SECONDARY;
    case BTN_MIDDLE:
      return CLUTTER_BUTTON_MIDDLE;
    }

  g_return_val_if_fail (evdev_button > BTN_LEFT, 0);

  return (evdev_button - (BTN_LEFT - 1)) + 4;
}
