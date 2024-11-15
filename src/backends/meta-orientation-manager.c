/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2017 Red Hat
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
 * MetaOrientationManager:
 *
 * A screen orientation manager
 *
 * #MetaOrientationManager is a final class which contains methods to
 * read the current screen orientation, as well as a signal that is
 * triggered whenever a screen changes its orientation.
 */

#include "config.h"

#include "meta/meta-orientation-manager.h"

#include <gio/gio.h>

enum
{
  ORIENTATION_CHANGED,
  SENSOR_ACTIVE,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

enum
{
  PROP_0,

  PROP_HAS_ACCELEROMETER,

  PROP_LAST
};

static GParamSpec *props[PROP_LAST];

struct _MetaOrientationManager
{
  GObject parent_instance;

  GCancellable *cancellable;

  guint iio_watch_id;
  guint properties_changed_idle_id;
  GDBusProxy *iio_proxy;
  MetaOrientation orientation;
  gboolean has_accel;
  gboolean orientation_locked;
  gboolean should_claim;
  gboolean is_claimed;
  int inhibited_count;

  GSettings *settings;
};

G_DEFINE_TYPE (MetaOrientationManager, meta_orientation_manager, G_TYPE_OBJECT)

#define CONF_SCHEMA "org.gnome.settings-daemon.peripherals.touchscreen"
#define ORIENTATION_LOCK_KEY "orientation-lock"

MtkMonitorTransform
meta_orientation_to_transform (MetaOrientation orientation)
{
  switch (orientation)
    {
    case META_ORIENTATION_BOTTOM_UP:
      return MTK_MONITOR_TRANSFORM_180;
    case META_ORIENTATION_LEFT_UP:
      return MTK_MONITOR_TRANSFORM_90;
    case META_ORIENTATION_RIGHT_UP:
      return MTK_MONITOR_TRANSFORM_270;
    case META_ORIENTATION_UNDEFINED:
    case META_ORIENTATION_NORMAL:
    default:
      return MTK_MONITOR_TRANSFORM_NORMAL;
    }
}

static MetaOrientation
orientation_from_string (const char *orientation)
{
  if (g_strcmp0 (orientation, "normal") == 0)
    return META_ORIENTATION_NORMAL;
  if (g_strcmp0 (orientation, "bottom-up") == 0)
    return META_ORIENTATION_BOTTOM_UP;
  if (g_strcmp0 (orientation, "left-up") == 0)
    return META_ORIENTATION_LEFT_UP;
  if (g_strcmp0 (orientation, "right-up") == 0)
    return META_ORIENTATION_RIGHT_UP;

  return META_ORIENTATION_UNDEFINED;
}

static void
sync_state (MetaOrientationManager *self)
{
  g_autoptr (GVariant) v = NULL;
  MetaOrientation new_orientation = META_ORIENTATION_UNDEFINED;

  v = g_dbus_proxy_get_cached_property (self->iio_proxy, "AccelerometerOrientation");
  if (v)
    new_orientation = orientation_from_string (g_variant_get_string (v, NULL));

  if (self->orientation == new_orientation)
    return;

  self->orientation = new_orientation;

  g_signal_emit (self, signals[ORIENTATION_CHANGED], 0);
}

static void
update_has_accel (MetaOrientationManager *self)
{
  gboolean has_accel = FALSE;

  if (self->iio_proxy)
    {
      g_autoptr (GVariant) v = NULL;

      v = g_dbus_proxy_get_cached_property (self->iio_proxy, "HasAccelerometer");
      if (v)
        has_accel = !!g_variant_get_boolean (v);
    }

  if (self->has_accel == has_accel)
    return;

  self->has_accel = has_accel;
  if (!has_accel && self->orientation != META_ORIENTATION_UNDEFINED)
    {
      self->orientation = META_ORIENTATION_UNDEFINED;
      g_signal_emit (self, signals[ORIENTATION_CHANGED], 0);
    }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_HAS_ACCELEROMETER]);
}

static void
iio_properties_changed_idle (gpointer user_data)
{
  MetaOrientationManager *self = user_data;

  self->properties_changed_idle_id = 0;
  update_has_accel (self);

  if (self->has_accel && self->should_claim && self->is_claimed)
    sync_state (self);
}

static void
iio_properties_changed (GDBusProxy *proxy,
                        GVariant   *changed_properties,
                        GStrv       invalidated_properties,
                        gpointer    user_data)
{
  MetaOrientationManager *self = user_data;

  /* We need this idle to avoid triggering events happening while the session
   * is not active (under X11), ideally this should be handled by stopping
   * events if the session is not active, but we'll need a MetaLogind available
   * in all the backends for having this working.
   */
  if (self->properties_changed_idle_id)
    return;

  self->properties_changed_idle_id = g_idle_add_once (iio_properties_changed_idle, self);
}

static void
on_get_properties (GObject      *connection,
                   GAsyncResult *res,
                   gpointer      user_data)
{
  MetaOrientationManager *self = user_data;
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) prop_value = NULL;
  g_autoptr (GVariant) property_variant = NULL;

  prop_value = g_dbus_connection_call_finish ((GDBusConnection *) connection, res, &error);
  if (!prop_value)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Failed to get accelerometer property: %s", error->message);

      return;
    }

  g_variant_get (prop_value, "(v)", &property_variant);
  g_dbus_proxy_set_cached_property (self->iio_proxy, "AccelerometerOrientation", property_variant);

  if (self->has_accel && self->should_claim)
    {
      sync_state (self);
      g_signal_emit (self, signals[SENSOR_ACTIVE], 0);
    }
}

static void
on_accelerometer_claimed (GObject      *source,
                          GAsyncResult *res,
                          gpointer      user_data)
{
  MetaOrientationManager *self = user_data;
  g_autoptr (GVariant) v = NULL;
  g_autoptr (GError) error = NULL;

  v = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
  if (!v)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Failed to claim accelerometer: %s", error->message);

      return;
    }

  self->is_claimed = TRUE;

  if (self->has_accel && self->should_claim)
    {
      GDBusConnection *connection = g_dbus_proxy_get_connection (self->iio_proxy);

      /* iio-sensor-proxy doesn't emit PropertiesChanged signals to clients which
       * don't claim the sensor. This will mess with the GLib properties cache.
       * So get the property manually after claiming and fix up the properties cache,
       * and only then emit ::sensor-active.
       */
      g_dbus_connection_call (connection,
                              "net.hadess.SensorProxy",
                              "/net/hadess/SensorProxy",
                              "org.freedesktop.DBus.Properties",
                              "Get",
                              g_variant_new ("(ss)",
                                             "net.hadess.SensorProxy",
                                             "AccelerometerOrientation"),
                              G_VARIANT_TYPE ("(v)"),
                              G_DBUS_CALL_FLAGS_NO_AUTO_START,
                              -1,
                              self->cancellable,
                              on_get_properties,
                              self);
    }
}

static void
on_accelerometer_released (GObject      *source,
                           GAsyncResult *res,
                           gpointer      user_data)
{
  MetaOrientationManager *self = user_data;
  g_autoptr (GVariant) v = NULL;
  g_autoptr (GError) error = NULL;

  v = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
  if (!v)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Failed to release accelerometer: %s", error->message);

      return;
    }

  self->is_claimed = FALSE;
}

static void
sync_accelerometer_claimed (MetaOrientationManager *self)
{
  gboolean should_claim;

  should_claim = self->iio_proxy && self->inhibited_count == 0;

  if (self->should_claim == should_claim)
    return;

  self->should_claim = should_claim;

  if (should_claim)
    {
      g_dbus_proxy_call (self->iio_proxy,
                         "ClaimAccelerometer",
                         NULL,
                         G_DBUS_CALL_FLAGS_NONE,
                         -1,
                         self->cancellable,
                         on_accelerometer_claimed,
                         self);
    }
  else
    {
      if (!self->iio_proxy)
        {
          self->is_claimed = FALSE;
          return;
        }

      g_dbus_proxy_call (self->iio_proxy,
                         "ReleaseAccelerometer",
                         NULL,
                         G_DBUS_CALL_FLAGS_NONE,
                         -1,
                         self->cancellable,
                         on_accelerometer_released,
                         self);
    }
}

static void
orientation_lock_changed (MetaOrientationManager *self)
{
  gboolean orientation_locked;

  orientation_locked = g_settings_get_boolean (self->settings, ORIENTATION_LOCK_KEY);

  if (self->orientation_locked == orientation_locked)
    return;

  self->orientation_locked = orientation_locked;

  if (self->orientation_locked)
    meta_orientation_manager_inhibit_tracking (self);
  else
    meta_orientation_manager_uninhibit_tracking (self);
}

static void
iio_proxy_ready (GObject      *source,
                 GAsyncResult *res,
                 gpointer      user_data)
{
  MetaOrientationManager *self = user_data;
  GDBusProxy *proxy;
  g_autoptr (GError) error = NULL;

  proxy = g_dbus_proxy_new_finish (res, &error);
  if (!proxy)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Failed to obtain IIO DBus proxy: %s", error->message);

      return;
    }

  self->iio_proxy = proxy;
  g_signal_connect_object (self->iio_proxy, "g-properties-changed",
                           G_CALLBACK (iio_properties_changed), self, 0);

  update_has_accel (self);
  sync_accelerometer_claimed (self);
}

static void
iio_sensor_appeared_cb (GDBusConnection *connection,
                        const gchar     *name,
                        const gchar     *name_owner,
                        gpointer         user_data)
{
  MetaOrientationManager *self = user_data;

  self->cancellable = g_cancellable_new ();
  g_dbus_proxy_new (connection,
                    G_DBUS_PROXY_FLAGS_NONE,
                    NULL,
                    "net.hadess.SensorProxy",
                    "/net/hadess/SensorProxy",
                    "net.hadess.SensorProxy",
                    self->cancellable,
                    iio_proxy_ready,
                    self);
}

static void
iio_sensor_vanished_cb (GDBusConnection *connection,
                        const gchar     *name,
                        gpointer         user_data)
{
  MetaOrientationManager *self = user_data;

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);

  g_clear_object (&self->iio_proxy);

  sync_accelerometer_claimed (self);
  update_has_accel (self);
}

static void
meta_orientation_manager_init (MetaOrientationManager *self)
{
  GSettingsSchemaSource *schema_source = g_settings_schema_source_get_default ();
  g_autoptr (GSettingsSchema) schema = NULL;

  self->orientation = META_ORIENTATION_UNDEFINED;

  self->iio_watch_id = g_bus_watch_name (G_BUS_TYPE_SYSTEM,
                                         "net.hadess.SensorProxy",
                                         G_BUS_NAME_WATCHER_FLAGS_NONE,
                                         iio_sensor_appeared_cb,
                                         iio_sensor_vanished_cb,
                                         self,
                                         NULL);

  schema = g_settings_schema_source_lookup (schema_source, CONF_SCHEMA, TRUE);
  if (schema != NULL)
    {
      self->settings = g_settings_new (CONF_SCHEMA);
      g_signal_connect_object (self->settings,
                               "changed::"ORIENTATION_LOCK_KEY,
                               G_CALLBACK (orientation_lock_changed),
                               self, G_CONNECT_SWAPPED);
      orientation_lock_changed (self);
    }
}

static void
meta_orientation_manager_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  MetaOrientationManager *self = META_ORIENTATION_MANAGER (object);

  switch (prop_id)
    {
    case PROP_HAS_ACCELEROMETER:
      g_value_set_boolean (value, self->has_accel);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_orientation_manager_finalize (GObject *object)
{
  MetaOrientationManager *self = META_ORIENTATION_MANAGER (object);

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);

  g_bus_unwatch_name (self->iio_watch_id);
  g_clear_handle_id (&self->properties_changed_idle_id, g_source_remove);
  g_clear_object (&self->iio_proxy);

  g_clear_object (&self->settings);

  G_OBJECT_CLASS (meta_orientation_manager_parent_class)->finalize (object);
}

static void
meta_orientation_manager_class_init (MetaOrientationManagerClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = meta_orientation_manager_finalize;
  gobject_class->get_property = meta_orientation_manager_get_property;

  signals[ORIENTATION_CHANGED] =
    g_signal_new ("orientation-changed",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  signals[SENSOR_ACTIVE] =
    g_signal_new ("sensor-active",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  props[PROP_HAS_ACCELEROMETER] =
    g_param_spec_boolean ("has-accelerometer", NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (gobject_class, PROP_LAST, props);
}

MetaOrientation
meta_orientation_manager_get_orientation (MetaOrientationManager *self)
{
  return self->orientation;
}

gboolean
meta_orientation_manager_has_accelerometer (MetaOrientationManager *self)
{
  return self->has_accel;
}

void
meta_orientation_manager_inhibit_tracking (MetaOrientationManager *self)
{
  self->inhibited_count++;

  if (self->inhibited_count == 1)
    sync_accelerometer_claimed (self);
}

void
meta_orientation_manager_uninhibit_tracking (MetaOrientationManager *self)
{
  self->inhibited_count--;

  if (self->inhibited_count == 0)
    sync_accelerometer_claimed (self);
}
