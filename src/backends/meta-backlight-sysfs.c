/*
 * Copyright (C) 2024 Red Hat
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

#include "backends/meta-backlight-sysfs-private.h"

#include <glib.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-launcher.h"
#include "backends/meta-udev.h"
#include "backends/meta-monitor-private.h"

#include "meta-dbus-login1.h"

struct _MetaBacklightSysfs
{
  MetaBacklight parent;

  GUdevDevice *device;
  MetaDBusLogin1Session *session_proxy;
  char *device_name;
  char *device_path;
  char *brightness_path;
};

G_DEFINE_FINAL_TYPE (MetaBacklightSysfs,
                     meta_backlight_sysfs,
                     META_TYPE_BACKLIGHT)

static void
meta_backlight_sysfs_update (MetaBacklightSysfs *backlight)
{
  g_autoptr (GError) error = NULL;
  g_autofree char *contents = NULL;
  int brightness, brightness_raw, brightness_min, brightness_max;

  if (!g_file_get_contents (backlight->brightness_path, &contents, NULL, &error))
    {
      g_warning ("Backlight %s: Could not get brightness from sysfs: %s",
                 meta_backlight_get_name (META_BACKLIGHT (backlight)),
                 error->message);
      return;
    }

  brightness_raw = g_ascii_strtoll (contents, NULL, 0);

  meta_backlight_get_brightness_info (META_BACKLIGHT (backlight),
                                      &brightness_min, &brightness_max);

  /* e.g. brightness lower than our minimum. */
  brightness = CLAMP (brightness_raw, brightness_min, brightness_max);

  if (brightness != brightness_raw)
    {
      g_warning ("Backlight %s: Value read from sysfs is out of range",
                 meta_backlight_get_name (META_BACKLIGHT (backlight)));
    }

  meta_backlight_update_brightness_target (META_BACKLIGHT (backlight),
                                           brightness);
}

static void
on_helper_brightness_set (GObject      *source_object,
                          GAsyncResult *res,
                          gpointer      user_data)
{
  g_autoptr (GSubprocess) proc = G_SUBPROCESS (source_object);
  g_autoptr (GTask) task = G_TASK (user_data);
  int brightness = GPOINTER_TO_INT (g_task_get_task_data (task));
  g_autoptr (GError) error = NULL;

  if (!g_subprocess_wait_check_finish (proc, res, &error))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  g_task_return_int (task, brightness);
}

static void
meta_backlight_sysfs_set_brightness_helper (MetaBacklightSysfs *backlight_sysfs,
                                            int                 brightness,
                                            GTask              *the_task)
{
  g_autoptr (GTask) task = the_task;
  g_autoptr (GSubprocess) proc = NULL;
  g_autofree char *brightness_str = NULL;
  g_autoptr (GError) error = NULL;
  GCancellable *cancellable = g_task_get_cancellable (task);

  brightness_str = g_strdup_printf ("%d", brightness);

  proc = g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_SILENCE,
                           &error,
                           "pkexec",
                           MUTTER_LIBEXECDIR "/mutter-backlight-helper",
                           backlight_sysfs->device_path,
                           brightness_str,
                           NULL);
  if (!proc)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  g_subprocess_wait_check_async (g_steal_pointer (&proc),
                                 cancellable,
                                 on_helper_brightness_set,
                                 g_steal_pointer (&task));
}

static void
on_login1_brightness_set (GObject      *source_object,
                          GAsyncResult *res,
                          gpointer      user_data)
{
  MetaDBusLogin1Session *session_proxy =
    META_DBUS_LOGIN1_SESSION (source_object);
  g_autoptr (GTask) task = G_TASK (user_data);
  int brightness = GPOINTER_TO_INT (g_task_get_task_data (task));
  g_autoptr (GError) error = NULL;

  if (!meta_dbus_login1_session_call_set_brightness_finish (session_proxy,
                                                            res,
                                                            &error))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  g_task_return_int (task, brightness);
}

static void
meta_backlight_sysfs_set_brightness (MetaBacklight       *backlight,
                                     int                  brightness_target,
                                     GCancellable        *cancellable,
                                     GAsyncReadyCallback  callback,
                                     gpointer             user_data)
{
  MetaBacklightSysfs *backlight_sysfs = META_BACKLIGHT_SYSFS (backlight);
  g_autoptr (GTask) task = NULL;

  task = g_task_new (backlight_sysfs, cancellable, callback, user_data);
  g_task_set_task_data (task, GINT_TO_POINTER (brightness_target), NULL);

  if (backlight_sysfs->session_proxy)
    {
      meta_dbus_login1_session_call_set_brightness (backlight_sysfs->session_proxy,
                                                    "backlight",
                                                    backlight_sysfs->device_name,
                                                    brightness_target,
                                                    cancellable,
                                                    on_login1_brightness_set,
                                                    g_steal_pointer (&task));
    }
  else
    {
      meta_backlight_sysfs_set_brightness_helper (backlight_sysfs,
                                                  brightness_target,
                                                  g_steal_pointer (&task));
    }
}

static int
meta_backlight_sysfs_set_brightness_finish (MetaBacklight  *backlight,
                                            GAsyncResult   *result,
                                            GError        **error)
{
  g_return_val_if_fail (g_task_is_valid (result, backlight), -1);

  return g_task_propagate_int (G_TASK (result), error);
}

static void
meta_backlight_sysfs_dispose (GObject *object)
{
  MetaBacklightSysfs *backlight = META_BACKLIGHT_SYSFS (object);

  g_clear_object (&backlight->device);
  g_clear_pointer (&backlight->device_name, g_free);
  g_clear_pointer (&backlight->device_path, g_free);
  g_clear_pointer (&backlight->brightness_path, g_free);

  G_OBJECT_CLASS (meta_backlight_sysfs_parent_class)->dispose (object);
}

static void
meta_backlight_sysfs_class_init (MetaBacklightSysfsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaBacklightClass *backlight_class = META_BACKLIGHT_CLASS (klass);

  object_class->dispose = meta_backlight_sysfs_dispose;

  backlight_class->set_brightness =
    meta_backlight_sysfs_set_brightness;
  backlight_class->set_brightness_finish =
    meta_backlight_sysfs_set_brightness_finish;
}

static void
meta_backlight_sysfs_init (MetaBacklightSysfs *backlight)
{
}

static void
on_backlight_changed (MetaUdev    *udev,
                      GUdevDevice *udev_device,
                      gpointer     user_data)
{
  MetaBacklightSysfs *backlight = META_BACKLIGHT_SYSFS (user_data);

  if (g_strcmp0 (g_udev_device_get_sysfs_path (udev_device),
                 backlight->device_path) != 0)
    return;

  meta_backlight_sysfs_update (backlight);
}

static gboolean
get_backlight_info (GUdevDevice  *device,
                    int          *brightness_min_out,
                    int          *brightness_max_out,
                    GError      **error)
{
  int min, max;
  const char *device_type;

  max = g_udev_device_get_sysfs_attr_as_int (device, "max_brightness");
  min = MAX (1, max / 100);

  device_type = g_udev_device_get_sysfs_attr (device, "type");

  /* If the interface has less than 100 possible values, and it is of type
  * raw, then assume that 0 does not turn off the backlight completely. */
  if (max < 99 && g_strcmp0 (device_type, "raw") == 0)
    min = 0;

  /* Ignore a backlight which has no steps. */
  if (min >= max)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Backlight is unusable because the maximum brightness %d "
                   "is bigger than minimum brightness %d",
                   max, min);
      return FALSE;
    }

  if (brightness_min_out)
    *brightness_min_out = min;
  if (brightness_max_out)
    *brightness_max_out = max;

  return TRUE;
}

static MetaDBusLogin1Session *
get_session_proxy (MetaBackend *backend)
{
  MetaLauncher *launcher;
  MetaDBusLogin1Session *session_proxy;
  g_autoptr (GError) error = NULL;

  launcher = meta_backend_get_launcher (backend);
  if (!launcher)
    return NULL;

  session_proxy = meta_launcher_get_session_proxy (launcher);

  if (!meta_dbus_login1_session_call_set_brightness_sync (session_proxy,
                                                          "", "", 0,
                                                          NULL, &error))
    {
      if (g_error_matches (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD))
        return NULL;
    }

  return session_proxy;
}

MetaBacklightSysfs *
meta_backlight_sysfs_new (MetaBackend           *backend,
                          const MetaOutputInfo  *output_info,
                          GError               **error)
{
  static GOnce proxy_once = G_ONCE_INIT;
  MetaUdev *udev = meta_backend_get_udev (backend);
  g_autoptr (MetaBacklightSysfs) backlight = NULL;
  MetaDBusLogin1Session *session_proxy = NULL;
  g_autoptr (GUdevDevice) backlight_device = NULL;
  g_autofree char *brightness_path = NULL;
  g_autofree char *device_name = NULL;
  g_autofree char *device_path = NULL;
  gboolean is_internal;
  int min, max;

  is_internal = meta_output_info_is_builtin (output_info);

  backlight_device = meta_udev_backlight_find (udev,
                                               output_info->name,
                                               is_internal);
  if (!backlight_device)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "No matching backlight device found");
      return NULL;
    }

  if (!get_backlight_info (backlight_device, &min, &max, error))
    return NULL;

  device_name = g_strdup (g_udev_device_get_name (backlight_device));

  device_path =
    realpath (g_udev_device_get_sysfs_path (backlight_device), NULL);

  if (!device_path)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Could not get real path");
      return NULL;
    }

  brightness_path = g_build_filename (device_path, "brightness", NULL);

  session_proxy =
    g_once (&proxy_once, (GThreadFunc) get_session_proxy, backend);

  backlight = g_object_new (META_TYPE_BACKLIGHT_SYSFS,
                            "backend", backend,
                            "name", output_info->name,
                            "brightness-min", min,
                            "brightness-max", max,
                            NULL);
  backlight->device = g_steal_pointer (&backlight_device);
  backlight->session_proxy = session_proxy;
  backlight->device_name = g_steal_pointer (&device_name);
  backlight->device_path = g_steal_pointer (&device_path);
  backlight->brightness_path = g_steal_pointer (&brightness_path);

  g_signal_connect_object (udev, "backlight-changed",
                           G_CALLBACK (on_backlight_changed), backlight, 0);
  meta_backlight_sysfs_update (backlight);

  return g_steal_pointer (&backlight);
}
