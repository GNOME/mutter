/*
 * Copyright (C) 2021 Jeremy Cline
 * Copyright (C) 2021 Red Hat Inc.
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
 */

/**
 * MetaColorManager:
 *
 * Interfaces for managing color-related properties like
 * color look-up tables and color spaces.
 *
 * Each MetaBackend has a MetaColorManager which includes interfaces for querying
 * and altering the color-related properties for displays associated with that
 * backend.
 *
 * These tasks include configuring the hardware's lookup tables (LUTs) used to
 * apply or remove transfer functions (traditionally called "gamma"), set up
 * color space conversions (CSCs), and for determining or setting the output
 * color space and transfer function.
 *
 * Mutter itself does not store and manage device ICC profiles; this task is
 * handled by [colord](https://www.freedesktop.org/software/colord/). Colord
 * maintains a database of devices (displays, printers, etc) and color profiles,
 * including the default output profile for a device. Users configure colord
 * with their preferred color profile for a device via an external application
 * like GNOME Control Center or the colormgr CLI.
 *
 * Colord defines [a specification for device and profile names](
 * https://github.com/hughsie/colord/blob/1.4.5/doc/device-and-profile-naming-spec.txt)
 * which is used to map Colord's devices to Mutter's #MetaMonitor.
 */

#include "config.h"

#include "backends/meta-color-manager-private.h"

#include "backends/meta-backend-types.h"
#include "backends/meta-color-calibration-session.h"
#include "backends/meta-color-device.h"
#include "backends/meta-color-store.h"
#include "backends/meta-dbus-session-manager.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-monitor-private.h"

#include "meta-dbus-color-manager.h"
#include "meta-dbus-gsd-color.h"
#include "meta-dbus-gsd-power-screen.h"

#define DEFAULT_TEMPERATURE 6500 /* Kelvin */

enum
{
  DEVICE_CALIBRATION_CHANGED,
  DEVICE_COLOR_STATE_CHANGED,
  READY,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

enum
{
  PROP_0,

  PROP_BACKEND,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MetaColorManagerPrivate
{
  MetaBackend *backend;

  MetaColorStore *color_store;

  cmsContext lcms_context;

  CdClient *cd_client;
  GCancellable *cancellable;

  GHashTable *devices;

  MetaDBusSettingsDaemonColor *gsd_color;
  MetaDBusSettingsDaemonPowerScreen *gsd_power_screen;

  gboolean is_ready;

  /* The temperature (in Kelvin) adjustment to apply to the color LUTs;
   * used to shift the screen towards red for Night Light.
   */
  unsigned int temperature;

  MetaDBusColorManager *api;
  MetaDbusSessionManager *session_manager;
} MetaColorManagerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaColorManager, meta_color_manager, G_TYPE_OBJECT)

static void
on_device_ready (MetaColorDevice  *color_device,
                 gboolean          success,
                 MetaColorManager *color_manager)
{
  if (!success)
    {
      meta_topic (META_DEBUG_COLOR, "Color device '%s' failed to become ready",
                  meta_color_device_get_id (color_device));
      return;
    }

  meta_color_device_update (color_device);
}

static void
on_device_calibration_changed (MetaColorDevice  *color_device,
                               MetaColorManager *color_manager)
{
  g_signal_emit (color_manager, signals[DEVICE_CALIBRATION_CHANGED], 0,
                 color_device);
}

static void
on_color_state_changed (MetaColorDevice  *color_device,
                        MetaColorManager *color_manager)
{
  g_signal_emit (color_manager, signals[DEVICE_COLOR_STATE_CHANGED],
                 0, color_device);
}

static char *
generate_monitor_id (MetaMonitor *monitor)
{
  const char *vendor;
  const char *product;
  const char *serial;
  const char *connector;
  GString *id;

  vendor = meta_monitor_get_vendor (monitor);
  product = meta_monitor_get_product (monitor);
  serial = meta_monitor_get_serial (monitor);
  connector = meta_monitor_get_connector (monitor);

  id = g_string_new ("");

  if (vendor)
    g_string_append_printf (id, "v:%s", vendor);
  if (product)
    g_string_append_printf (id, "%sp:%s", id->len > 0 ? ";" : "", product);
  if (serial)
    g_string_append_printf (id, "%ss:%s", id->len > 0 ? ";" : "", serial);
  if (connector)
    g_string_append_printf (id, "%sc:%s", id->len > 0 ? ";" : "", connector);

  return g_string_free (id, FALSE);
}

static void
update_devices (MetaColorManager *color_manager)
{
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (priv->backend);
  GList *l;
  GHashTable *devices;

  devices = g_hash_table_new_full (g_str_hash,
                                   g_str_equal,
                                   g_free,
                                   (GDestroyNotify) g_object_unref);

  for (l = meta_monitor_manager_get_monitors (monitor_manager); l; l = l->next)
    {
      MetaMonitor *monitor = META_MONITOR (l->data);
      g_autofree char *monitor_id = NULL;
      g_autofree char *stolen_monitor_id = NULL;
      MetaColorDevice *color_device;

      monitor_id = generate_monitor_id (monitor);

      if (priv->devices &&
          g_hash_table_steal_extended (priv->devices,
                                       monitor_id,
                                       (gpointer *) &stolen_monitor_id,
                                       (gpointer *) &color_device))
        {
          meta_topic (META_DEBUG_COLOR,
                      "Updating color device '%s' monitor instance",
                      meta_color_device_get_id (color_device));
          meta_color_device_update_monitor (color_device, monitor);
          g_hash_table_insert (devices,
                               g_steal_pointer (&monitor_id),
                               color_device);
        }
      else
        {
          color_device = meta_color_device_new (color_manager, monitor);
          meta_topic (META_DEBUG_COLOR,
                      "Created new color device '%s' for monitor %s",
                      meta_color_device_get_id (color_device),
                      meta_monitor_get_connector (monitor));
          g_hash_table_insert (devices,
                               g_steal_pointer (&monitor_id),
                               color_device);

          g_signal_connect_object (color_device, "ready",
                                   G_CALLBACK (on_device_ready),
                                   color_manager, 0);
          g_signal_connect_object (color_device, "calibration-changed",
                                   G_CALLBACK (on_device_calibration_changed),
                                   color_manager, 0);
          g_signal_connect_object (color_device, "color-state-changed",
                                   G_CALLBACK (on_color_state_changed),
                                   color_manager, 0);
        }
    }

  if (priv->devices)
    {
      if (g_hash_table_size (priv->devices) > 0)
        {
          meta_topic (META_DEBUG_COLOR, "Removing %u color devices",
                      g_hash_table_size (priv->devices));
        }
      g_clear_pointer (&priv->devices, g_hash_table_unref);
    }
  priv->devices = devices;
}

static void
update_device_properties (MetaColorManager *color_manager)
{
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (priv->backend);
  GList *l;

  for (l = meta_monitor_manager_get_monitors (monitor_manager); l; l = l->next)
    {
      MetaMonitor *monitor = META_MONITOR (l->data);
      MetaColorDevice *color_device;

      color_device = meta_color_manager_get_color_device (color_manager,
                                                          monitor);
      if (!color_device)
        continue;

      meta_color_device_update (color_device);
    }
}

void
meta_color_manager_monitors_changed (MetaColorManager *color_manager)
{
  update_devices (color_manager);
  update_device_properties (color_manager);
}

static void
cd_client_connect_cb (GObject      *source_object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  CdClient *client = CD_CLIENT (source_object);
  MetaColorManager *color_manager = META_COLOR_MANAGER (user_data);
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);
  g_autoptr (GError) error = NULL;

  if (!cd_client_connect_finish (client, res, &error))
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Failed to connect to colord daemon: %s", error->message);
      return;
    }

  if (!cd_client_get_has_server (client))
    {
      g_warning ("There is no colord server available");
      return;
    }

  priv->color_store = meta_color_store_new (color_manager);

  update_devices (color_manager);

  priv->is_ready = TRUE;
  g_signal_emit (color_manager, signals[READY], 0);
}

static void
update_temperature (MetaColorManager *color_manager)
{
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);
  unsigned int temperature;

  temperature = meta_dbus_settings_daemon_color_get_temperature (priv->gsd_color);
  if (temperature == 0 || priv->temperature == temperature)
    return;

  if (temperature < 1000 || temperature > 10000)
    {
      g_warning ("Invalid temperature from gsd-color: %u K", temperature);
      return;
    }

  priv->temperature = temperature;

  update_device_properties (color_manager);
}

static void
on_temperature_changed (MetaDBusSettingsDaemonColor *gsd_color,
                        GParamSpec                  *pspec,
                        MetaColorManager            *color_manager)
{
  update_temperature (color_manager);
}

static void
on_gsd_color_ready (GObject      *source_object,
                    GAsyncResult *res,
                    gpointer      user_data)
{
  MetaColorManager *color_manager = META_COLOR_MANAGER (user_data);
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);
  MetaDBusSettingsDaemonColor *gsd_color;
  g_autoptr (GError) error = NULL;

  gsd_color =
    meta_dbus_settings_daemon_color_proxy_new_for_bus_finish (res, &error);
  if (!gsd_color)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

      g_warning ("Failed to create gsd-color D-Bus proxy: %s", error->message);
      return;
    }

  meta_topic (META_DEBUG_COLOR,
              "Connection to org.gnome.SettingsDaemon.Color established");
  priv->gsd_color = gsd_color;

  g_signal_connect (gsd_color, "notify::temperature",
                    G_CALLBACK (on_temperature_changed),
                    color_manager);

  update_temperature (color_manager);
}

static void
on_gsd_power_screen_ready (GObject      *source_object,
                           GAsyncResult *res,
                           gpointer      user_data)
{
  MetaColorManager *color_manager = META_COLOR_MANAGER (user_data);
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);
  MetaDBusSettingsDaemonPowerScreen *gsd_power_screen;
  g_autoptr (GError) error = NULL;

  gsd_power_screen =
    meta_dbus_settings_daemon_power_screen_proxy_new_for_bus_finish (res,
                                                                     &error);
  if (!gsd_power_screen)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

      g_warning ("Failed to create gsd-power-screen D-Bus proxy: %s", error->message);
      return;
    }

  meta_topic (META_DEBUG_COLOR,
              "Connection to org.gnome.SettingsDaemon.PowerScreen established");
  priv->gsd_power_screen = gsd_power_screen;

  update_device_properties (color_manager);
}

static gboolean
handle_calibrate_monitor (MetaDBusColorManager  *api,
                          GDBusMethodInvocation *invocation,
                          const char            *arg_connector,
                          MetaColorManager      *color_manager)
{
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (priv->backend);
  MetaDbusSessionManager *session_manager = priv->session_manager;
  MetaMonitor *monitor;
  MetaDbusSession *dbus_session;
  MetaColorCalibrationSession *session;
  g_autoptr (GError) error = NULL;
  const char *session_path;

  monitor = meta_monitor_manager_get_monitor_from_connector (monitor_manager,
                                                             arg_connector);
  if (!monitor)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "Unknown monitor connector '%s'",
                                             arg_connector);
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  dbus_session =
    meta_dbus_session_manager_create_session (session_manager,
                                              invocation,
                                              &error,
                                              "color-manager", color_manager,
                                              "monitor", monitor,
                                              NULL);
  if (!dbus_session)
    {
      g_dbus_method_invocation_return_error_literal (invocation, G_DBUS_ERROR,
                                                     G_DBUS_ERROR_FAILED,
                                                     error->message);
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  session = META_COLOR_CALIBRATION_SESSION (dbus_session);
  session_path = meta_color_calibration_session_get_object_path (session);
  meta_dbus_color_manager_complete_calibrate_monitor (priv->api,
                                                      invocation,
                                                      session_path);

  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static void
meta_color_manager_constructed (GObject *object)
{
  MetaColorManager *color_manager = META_COLOR_MANAGER (object);
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);

  priv->lcms_context = cmsCreateContext (NULL, NULL);

  priv->cancellable = g_cancellable_new ();
  priv->temperature = DEFAULT_TEMPERATURE;

  priv->cd_client = cd_client_new ();
  cd_client_connect (priv->cd_client, priv->cancellable, cd_client_connect_cb,
                     color_manager);

  meta_dbus_settings_daemon_color_proxy_new_for_bus (
    G_BUS_TYPE_SESSION,
    G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
    "org.gnome.SettingsDaemon.Color",
    "/org/gnome/SettingsDaemon/Color",
    priv->cancellable,
    on_gsd_color_ready,
    color_manager);

  meta_dbus_settings_daemon_power_screen_proxy_new_for_bus (
    G_BUS_TYPE_SESSION,
    G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
    "org.gnome.SettingsDaemon.Power.Screen",
    "/org/gnome/SettingsDaemon/Power",
    priv->cancellable,
    on_gsd_power_screen_ready,
    color_manager);

  update_devices (color_manager);
  update_device_properties (color_manager);

  priv->api = meta_dbus_color_manager_skeleton_new ();
  priv->session_manager =
    meta_dbus_session_manager_new (priv->backend,
                                   "org.gnome.Mutter.ColorManager",
                                   "/org/gnome/Mutter/ColorManager",
                                   META_TYPE_COLOR_CALIBRATION_SESSION,
                                   G_DBUS_INTERFACE_SKELETON (priv->api));
  g_signal_connect_object (priv->api, "handle-calibrate-monitor",
                           G_CALLBACK (handle_calibrate_monitor),
                           color_manager, G_CONNECT_DEFAULT);
}

static void
meta_color_manager_dispose (GObject *object)
{
  MetaColorManager *color_manager = META_COLOR_MANAGER (object);
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);

  g_cancellable_cancel (priv->cancellable);
  g_clear_object (&priv->cancellable);
  g_clear_pointer (&priv->devices, g_hash_table_unref);
  g_clear_object (&priv->gsd_power_screen);
  g_clear_object (&priv->gsd_color);
  g_clear_object (&priv->color_store);
  g_clear_pointer (&priv->lcms_context, cmsDeleteContext);
  g_clear_object (&priv->session_manager);
  g_clear_object (&priv->api);

  G_OBJECT_CLASS (meta_color_manager_parent_class)->dispose (object);
}

static void
meta_color_manager_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  MetaColorManager *color_manager = META_COLOR_MANAGER (object);
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);

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
meta_color_manager_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  MetaColorManager *color_manager = META_COLOR_MANAGER (object);
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);

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
meta_color_manager_class_init (MetaColorManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_color_manager_constructed;
  object_class->dispose = meta_color_manager_dispose;
  object_class->set_property = meta_color_manager_set_property;
  object_class->get_property = meta_color_manager_get_property;

  obj_props[PROP_BACKEND] =
    g_param_spec_object ("backend", NULL, NULL,
                         META_TYPE_BACKEND,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);

  signals[DEVICE_CALIBRATION_CHANGED] =
    g_signal_new ("device-calibration-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST, 0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  META_TYPE_COLOR_DEVICE);

  signals[DEVICE_COLOR_STATE_CHANGED] =
    g_signal_new ("device-color-state-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST, 0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  META_TYPE_COLOR_DEVICE);
  signals[READY] =
    g_signal_new ("ready",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST, 0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
meta_color_manager_init (MetaColorManager *color_manager)
{
}

MetaBackend *
meta_color_manager_get_backend (MetaColorManager *color_manager)
{
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);

  return priv->backend;
}

CdClient *
meta_color_manager_get_cd_client (MetaColorManager *color_manager)
{
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);

  return priv->cd_client;
}

MetaColorStore *
meta_color_manager_get_color_store (MetaColorManager *color_manager)
{
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);

  return priv->color_store;
}

MetaColorDevice *
meta_color_manager_get_color_device (MetaColorManager *color_manager,
                                     MetaMonitor      *monitor)
{
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);
  g_autofree char *monitor_id = NULL;

  monitor_id = generate_monitor_id (monitor);
  return g_hash_table_lookup (priv->devices, monitor_id);
}

gboolean
meta_color_manager_is_ready (MetaColorManager *color_manager)
{
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);

  return priv->is_ready;
}

int
meta_color_manager_get_num_color_devices (MetaColorManager *color_manager)
{
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);

  return g_hash_table_size (priv->devices);
}

cmsContext
meta_color_manager_get_lcms_context (MetaColorManager *color_manager)
{
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);

  return priv->lcms_context;
}

unsigned int
meta_color_manager_get_temperature (MetaColorManager *color_manager)
{
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);

  return priv->temperature;
}

void
meta_color_manager_set_brightness (MetaColorManager *color_manager,
                                   int               brightness)
{
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);

  if (!priv->gsd_power_screen)
    {
      meta_topic (META_DEBUG_COLOR,
                  "No org.gnome.SettingsDaemon.Power.Screen service available, "
                  "not setting brightness");
      return;
    }

  meta_dbus_settings_daemon_power_screen_set_brightness (priv->gsd_power_screen,
                                                         brightness);
}

unsigned int
meta_color_manager_get_default_temperature (MetaColorManager *color_manager)
{
  return DEFAULT_TEMPERATURE;
}
