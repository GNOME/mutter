/*
 * Copyright (C) 2025 Red Hat Inc.
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

#include "config.h"

#include "backends/meta-color-calibration-session.h"

#include "backends/meta-color-device.h"
#include "backends/meta-color-manager.h"
#include "backends/meta-dbus-session-manager.h"
#include "backends/meta-dbus-session-watcher.h"
#include "backends/meta-monitor-private.h"

#include "meta-dbus-color-manager.h"

#define META_COLOR_CALIBRATION_SESSION_DBUS_PATH "/org/gnome/Mutter/ColorManager/Calibration"

enum
{
  PROP_0,

  PROP_COLOR_MANAGER,
  PROP_MONITOR,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

struct _MetaColorCalibrationSession
{
  MetaDBusColorManagerCalibrationSkeleton parent;

  MetaDbusSessionManager *session_manager;
  MetaColorManager *color_manager;

  MetaMonitor *monitor;

  char *peer_name;
  char *session_id;
  char *object_path;
};

static void initable_init_iface (GInitableIface *iface);

static void meta_dbus_color_calibration_init_iface (MetaDBusColorManagerCalibrationIface *iface);

static void meta_dbus_session_init_iface (MetaDbusSessionInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaColorCalibrationSession,
                         meta_color_calibration_session,
                         META_DBUS_TYPE_COLOR_MANAGER_CALIBRATION_SKELETON,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                initable_init_iface)
                         G_IMPLEMENT_INTERFACE (META_DBUS_TYPE_COLOR_MANAGER_CALIBRATION,
                                                meta_dbus_color_calibration_init_iface)
                         G_IMPLEMENT_INTERFACE (META_TYPE_DBUS_SESSION,
                                                meta_dbus_session_init_iface))

static void
on_monitor_disposed (gpointer  user_data,
                     GObject  *where_the_object_was)
{
  MetaColorCalibrationSession *session =
    META_COLOR_CALIBRATION_SESSION (user_data);

  meta_dbus_session_close (META_DBUS_SESSION (session));
}

static gboolean
meta_color_calibration_session_initable_init (GInitable     *initable,
                                              GCancellable  *cancellable,
                                              GError       **error)
{
  MetaColorCalibrationSession *session =
    META_COLOR_CALIBRATION_SESSION (initable);
  GDBusInterfaceSkeleton *interface_skeleton =
    G_DBUS_INTERFACE_SKELETON (session);
  MetaColorDevice *color_device;
  GDBusConnection *connection =
    meta_dbus_session_manager_get_connection (session->session_manager);

  color_device = meta_color_manager_get_color_device (session->color_manager,
                                                      session->monitor);
  if (!meta_color_device_start_calibration (color_device, error))
    return FALSE;

  meta_dbus_color_manager_calibration_set_gamma_lut_size (
    META_DBUS_COLOR_MANAGER_CALIBRATION (session),
    meta_color_device_get_calibration_lut_size (color_device));

  if (!g_dbus_interface_skeleton_export (interface_skeleton,
                                         connection,
                                         session->object_path,
                                         error))
    return FALSE;

  return TRUE;
}

static void
meta_color_calibration_session_close (MetaDbusSession *dbus_session)
{
  MetaColorCalibrationSession *session =
    META_COLOR_CALIBRATION_SESSION (dbus_session);
  MetaColorDevice *color_device;

  color_device = meta_color_manager_get_color_device (session->color_manager,
                                                      session->monitor);
  meta_color_device_stop_calibration (color_device);

  meta_dbus_session_notify_closed (dbus_session);
  g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (session));

  g_object_unref (session);
}

static gboolean
handle_set_crtc_gamma_lut (MetaDBusColorManagerCalibration *object,
                           GDBusMethodInvocation           *invocation,
                           GVariant                        *arg_red,
                           GVariant                        *arg_green,
                           GVariant                        *arg_blue)
{
  MetaColorCalibrationSession *session =
    META_COLOR_CALIBRATION_SESSION (object);
  MetaColorDevice *color_device =
    meta_color_manager_get_color_device (session->color_manager,
                                         session->monitor);
  g_autoptr (GBytes) red_bytes = NULL;
  g_autoptr (GBytes) green_bytes = NULL;
  g_autoptr (GBytes) blue_bytes = NULL;
  MetaGammaLut lut;

  red_bytes = g_variant_get_data_as_bytes (arg_red);
  green_bytes = g_variant_get_data_as_bytes (arg_green);
  blue_bytes = g_variant_get_data_as_bytes (arg_blue);

  lut.size = g_bytes_get_size (red_bytes) / sizeof (uint16_t);
  lut.red = (uint16_t *) g_bytes_get_data (red_bytes, NULL);
  lut.green = (uint16_t *) g_bytes_get_data (green_bytes, NULL);
  lut.blue = (uint16_t *) g_bytes_get_data (blue_bytes, NULL);

  meta_color_device_set_calibration_lut (color_device, &lut);

  meta_dbus_color_manager_calibration_complete_set_crtc_gamma_lut (object,
                                                                   invocation);
  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
handle_stop  (MetaDBusColorManagerCalibration *object,
              GDBusMethodInvocation           *invocation)
{
  meta_dbus_session_close (META_DBUS_SESSION (object));
  meta_dbus_color_manager_calibration_complete_stop (object,
                                                     invocation);
  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static void
initable_init_iface (GInitableIface *iface)
{
  iface->init = meta_color_calibration_session_initable_init;
}

static void
meta_dbus_color_calibration_init_iface (MetaDBusColorManagerCalibrationIface *iface)
{
  iface->handle_set_crtc_gamma_lut = handle_set_crtc_gamma_lut;
  iface->handle_stop = handle_stop;
}

static void
meta_dbus_session_init_iface (MetaDbusSessionInterface *iface)
{
  iface->close = meta_color_calibration_session_close;
}

static void
meta_color_calibration_session_finalize (GObject *object)
{
  MetaColorCalibrationSession *session =
    META_COLOR_CALIBRATION_SESSION (object);

  g_object_weak_unref (G_OBJECT (session->monitor),
                       on_monitor_disposed,
                       session);
  g_free (session->peer_name);
  g_free (session->session_id);
  g_free (session->object_path);

  G_OBJECT_CLASS (meta_color_calibration_session_parent_class)->finalize (object);
}

static void
meta_color_calibration_session_set_property (GObject      *object,
                                             guint         prop_id,
                                             const GValue *value,
                                             GParamSpec   *pspec)
{
  MetaColorCalibrationSession *session = META_COLOR_CALIBRATION_SESSION (object);

  switch (prop_id)
    {
    case PROP_COLOR_MANAGER:
      session->color_manager = g_value_get_object (value);
      break;
    case PROP_MONITOR:
      session->monitor = g_value_get_object (value);
      g_object_weak_ref (G_OBJECT (session->monitor),
                         on_monitor_disposed,
                         session);
      break;
    case N_PROPS + META_DBUS_SESSION_PROP_SESSION_MANAGER:
      session->session_manager = g_value_get_object (value);
      break;
    case N_PROPS + META_DBUS_SESSION_PROP_PEER_NAME:
      session->peer_name = g_value_dup_string (value);
      break;
    case N_PROPS + META_DBUS_SESSION_PROP_ID:
      session->session_id = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_color_calibration_session_get_property (GObject    *object,
                                             guint       prop_id,
                                             GValue     *value,
                                             GParamSpec *pspec)
{
  MetaColorCalibrationSession *session = META_COLOR_CALIBRATION_SESSION (object);

  switch (prop_id)
    {
    case PROP_COLOR_MANAGER:
      g_value_set_object (value, session->color_manager);
      break;
    case PROP_MONITOR:
      g_value_set_object (value, session->monitor);
      break;
    case N_PROPS + META_DBUS_SESSION_PROP_SESSION_MANAGER:
      g_value_set_object (value, session->session_manager);
      break;
    case N_PROPS + META_DBUS_SESSION_PROP_PEER_NAME:
      g_value_set_string (value, session->peer_name);
      break;
    case N_PROPS + META_DBUS_SESSION_PROP_ID:
      g_value_set_string (value, session->session_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_color_calibration_session_class_init (MetaColorCalibrationSessionClass *klass)
{

  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_color_calibration_session_finalize;
  object_class->set_property = meta_color_calibration_session_set_property;
  object_class->get_property = meta_color_calibration_session_get_property;

  obj_props[PROP_COLOR_MANAGER] =
    g_param_spec_object ("color-manager", NULL, NULL,
                         META_TYPE_COLOR_MANAGER,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_MONITOR] =
    g_param_spec_object ("monitor", NULL, NULL,
                         META_TYPE_MONITOR,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
  meta_dbus_session_install_properties (object_class, N_PROPS);
}

static void
meta_color_calibration_session_init (MetaColorCalibrationSession *session)
{
  static unsigned int global_session_number = 0;

  session->object_path =
    g_strdup_printf (META_COLOR_CALIBRATION_SESSION_DBUS_PATH "/u%u",
                     ++global_session_number);
}

const char *
meta_color_calibration_session_get_object_path (MetaColorCalibrationSession *session)
{
  return session->object_path;
}
