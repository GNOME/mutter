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
 * MetaBackendNative:
 *
 * A native (KMS/evdev) MetaBackend
 *
 * MetaBackendNative is an implementation of #MetaBackend that uses "native"
 * technologies like DRM/KMS and libinput/evdev to perform the necessary
 * functions.
 */

#include "config.h"

#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-backend-native-private.h"
#include "backends/native/meta-input-thread.h"

#include <drm_fourcc.h>
#include <stdlib.h>

#include "backends/meta-a11y-manager.h"
#include "backends/meta-color-manager.h"
#include "backends/meta-cursor-tracker-private.h"
#include "backends/meta-idle-manager.h"
#include "backends/meta-keymap-utils.h"
#include "backends/meta-logical-monitor-private.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-pointer-constraint.h"
#include "backends/meta-settings-private.h"
#include "backends/meta-stage-private.h"
#include "backends/native/meta-clutter-backend-native.h"
#include "backends/native/meta-device-pool-private.h"
#include "backends/native/meta-drm-lease.h"
#include "backends/native/meta-kms.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-monitor-manager-native.h"
#include "backends/native/meta-render-device-gbm.h"
#include "backends/native/meta-renderer-native.h"
#include "backends/native/meta-seat-native.h"
#include "backends/native/meta-stage-native.h"
#include "cogl/cogl.h"
#include "core/meta-border.h"
#include "meta/main.h"
#include "meta-dbus-rtkit1.h"

#ifdef HAVE_EGL_DEVICE
#include "backends/native/meta-render-device-egl-stream.h"
#endif

#include "meta-private-enum-types.h"

enum
{
  PROP_0,

  PROP_MODE,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MetaBackendNativePrivate
{
  MetaBackend parent;

  MetaDevicePool *device_pool;
  MetaKms *kms;

  GHashTable *startup_render_devices;

  MetaBackendNativeMode mode;

#ifdef HAVE_EGL_DEVICE
  MetaRenderDeviceEglStream *render_device_egl_stream;
#endif

  MetaDrmLeaseManager *drm_lease_manager;
} MetaBackendNativePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaBackendNative,
                            meta_backend_native,
                            META_TYPE_BACKEND)

static void meta_backend_native_resume (MetaBackend *backend);

static void meta_backend_native_pause (MetaBackend *backend);

static void
meta_backend_native_dispose (GObject *object)
{
  MetaBackendNative *native = META_BACKEND_NATIVE (object);
  MetaBackendNativePrivate *priv =
    meta_backend_native_get_instance_private (native);

  G_OBJECT_CLASS (meta_backend_native_parent_class)->dispose (object);

  g_clear_pointer (&priv->startup_render_devices, g_hash_table_unref);
  g_clear_object (&priv->kms);
  g_clear_object (&priv->device_pool);
}

static ClutterBackend *
meta_backend_native_create_clutter_backend (MetaBackend    *backend,
                                            ClutterContext *context)
{
  return CLUTTER_BACKEND (meta_clutter_backend_native_new (backend, context));
}

static ClutterSeat *
meta_backend_native_create_default_seat (MetaBackend  *backend,
                                         GError      **error)
{
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaBackendNativePrivate *priv =
    meta_backend_native_get_instance_private (backend_native);
  ClutterContext *clutter_context =
    meta_backend_get_clutter_context (backend);
  MetaLauncher *launcher = meta_backend_get_launcher (backend);
  const char *seat_id = NULL;
  MetaSeatNativeFlag flags = META_SEAT_NATIVE_FLAG_NONE;

  switch (priv->mode)
    {
    case META_BACKEND_NATIVE_MODE_DEFAULT:
      seat_id = meta_launcher_get_seat_id (launcher);
      break;
    case META_BACKEND_NATIVE_MODE_HEADLESS:
    case META_BACKEND_NATIVE_MODE_TEST_HEADLESS:
      seat_id = META_BACKEND_HEADLESS_INPUT_SEAT;
      flags = META_SEAT_NATIVE_FLAG_NO_LIBINPUT;
      break;
    case META_BACKEND_NATIVE_MODE_TEST_VKMS:
      seat_id = META_BACKEND_TEST_INPUT_SEAT;
      break;
    }

  return CLUTTER_SEAT (g_object_new (META_TYPE_SEAT_NATIVE,
                                     "backend", backend,
                                     "context", clutter_context,
                                     "seat-id", seat_id,
                                     "name", seat_id,
                                     "flags", flags,
                                     NULL));
}

static void
update_viewports (MetaBackend *backend)
{
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  MetaSeatNative *seat =
    META_SEAT_NATIVE (clutter_backend_get_default_seat (clutter_backend));
  MetaViewportInfo *viewports;

  viewports = meta_monitor_manager_get_viewports (monitor_manager);
  meta_seat_native_set_viewports (seat, viewports);
  g_object_unref (viewports);
}

static void
on_a11y_modifiers_changed (MetaA11yManager *a11y_manager,
                           MetaBackend     *backend)
{
  MetaSeatNative *seat;
  ClutterBackend *clutter_backend;
  g_autofree uint32_t *modifiers;
  int n_modifiers;

  clutter_backend = meta_backend_get_clutter_backend (backend);
  seat = META_SEAT_NATIVE (clutter_backend_get_default_seat (clutter_backend));
  modifiers = meta_a11y_manager_get_modifier_keysyms (a11y_manager,
                                                      &n_modifiers);

  meta_seat_native_set_a11y_modifiers (seat, modifiers, n_modifiers);
}

static gboolean
meta_backend_native_init_post (MetaBackend  *backend,
                               GError      **error)
{
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaBackendNativePrivate *priv =
    meta_backend_native_get_instance_private (backend_native);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaA11yManager *a11y_manager = meta_backend_get_a11y_manager (backend);

  g_clear_pointer (&priv->startup_render_devices,
                   g_hash_table_unref);

  g_signal_connect_swapped (monitor_manager, "monitors-changed-internal",
                            G_CALLBACK (update_viewports), backend);
  update_viewports (backend);

  g_signal_connect_object (a11y_manager,
                           "a11y-modifiers-changed",
                           G_CALLBACK (on_a11y_modifiers_changed),
                           backend,
                           G_CONNECT_DEFAULT);

  priv->drm_lease_manager = g_object_new (META_TYPE_DRM_LEASE_MANAGER,
                                          "backend", backend,
                                          NULL);

  return TRUE;
}

static MetaBackendCapabilities
meta_backend_native_get_capabilities (MetaBackend *backend)
{
  return META_BACKEND_CAPABILITY_BARRIERS;
}

static MetaMonitorManager *
meta_backend_native_create_monitor_manager (MetaBackend *backend,
                                            GError     **error)
{
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaBackendNativePrivate *priv =
    meta_backend_native_get_instance_private (backend_native);
  MetaMonitorManager *manager;
  gboolean needs_outputs;

  needs_outputs = !(priv->mode & META_BACKEND_NATIVE_MODE_HEADLESS);
  manager = g_initable_new (META_TYPE_MONITOR_MANAGER_NATIVE, NULL, error,
                            "backend", backend,
                            "needs-outputs", needs_outputs,
                            NULL);
  if (!manager)
    return NULL;

  return manager;
}

static MetaColorManager *
meta_backend_native_create_color_manager (MetaBackend *backend)
{
  return g_object_new (META_TYPE_COLOR_MANAGER,
                       "backend", backend,
                       NULL);
}

static MetaCursorRenderer *
meta_backend_native_get_cursor_renderer (MetaBackend   *backend,
                                         ClutterSprite *sprite)
{
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  MetaSeatNative *seat_native =
    META_SEAT_NATIVE (clutter_backend_get_default_seat (clutter_backend));

  return meta_seat_native_maybe_ensure_cursor_renderer (seat_native, sprite);
}

static MetaRenderer *
meta_backend_native_create_renderer (MetaBackend *backend,
                                     GError     **error)
{
  MetaBackendNative *native = META_BACKEND_NATIVE (backend);
  MetaRendererNative *renderer_native;

  renderer_native = meta_renderer_native_new (native, error);
  if (!renderer_native)
    return NULL;

  return META_RENDERER (renderer_native);
}

static MetaInputSettings *
meta_backend_native_get_input_settings (MetaBackend *backend)
{
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  MetaSeatNative *seat_native =
    META_SEAT_NATIVE (clutter_backend_get_default_seat (clutter_backend));

  return meta_seat_impl_get_input_settings (seat_native->impl);
}

static MetaLogicalMonitor *
meta_backend_native_get_current_logical_monitor (MetaBackend *backend)
{
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  graphene_point_t point;

  meta_cursor_tracker_get_pointer (cursor_tracker, &point, NULL);
  return meta_monitor_manager_get_logical_monitor_at (monitor_manager,
                                                      point.x, point.y);
}

static void
set_keyboard_map_cb (GObject      *source_object,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  MetaSeatNative *seat_native = META_SEAT_NATIVE (source_object);
  g_autoptr (GTask) task = G_TASK (user_data);
  g_autoptr (GError) error = NULL;
  MetaBackend *backend;

  if (!meta_seat_native_set_keyboard_map_finish (seat_native, result, &error))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  backend = META_BACKEND (g_task_get_source_object (task));
  meta_backend_notify_keymap_changed (backend);

  g_task_return_boolean (task, TRUE);
}

static void
meta_backend_native_set_keymap_async (MetaBackend *backend,
                                      const char  *layouts,
                                      const char  *variants,
                                      const char  *options,
                                      const char  *model,
                                      GTask       *task)
{
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat;

  seat = clutter_backend_get_default_seat (clutter_backend);
  meta_seat_native_set_keyboard_map_async (META_SEAT_NATIVE (seat),
                                           layouts, variants, options, model,
                                           g_task_get_cancellable (task),
                                           set_keyboard_map_cb,
                                           task);

}

static struct xkb_keymap *
meta_backend_native_get_keymap (MetaBackend *backend)
{
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat;

  seat = clutter_backend_get_default_seat (clutter_backend);
  return meta_seat_native_get_keyboard_map (META_SEAT_NATIVE (seat));
}

static xkb_layout_index_t
meta_backend_native_get_keymap_layout_group (MetaBackend *backend)
{
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat;

  seat = clutter_backend_get_default_seat (clutter_backend);
  return meta_seat_native_get_keyboard_layout_index (META_SEAT_NATIVE (seat));
}

static void
set_layout_index_cb (GObject      *source_object,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  MetaSeatNative *seat_native = META_SEAT_NATIVE (source_object);
  g_autoptr (GTask) task = G_TASK (user_data);
  MetaBackend *backend = META_BACKEND (g_task_get_source_object (task));
  g_autoptr (GError) error = NULL;
  gboolean index_changed;

  index_changed =
    meta_seat_native_set_keyboard_layout_index_finish (seat_native,
                                                       result,
                                                       &error);
  if (error)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  if (index_changed)
    {
      xkb_layout_index_t idx;

      idx = meta_seat_native_get_keyboard_layout_index (seat_native);
      meta_backend_notify_keymap_layout_group_changed (backend, idx);
    }

  g_task_return_boolean (task, TRUE);
}

static void
meta_backend_native_set_keymap_layout_group_async (MetaBackend        *backend,
                                                   xkb_layout_index_t  idx,
                                                   GTask              *task)
{
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat;

  seat = clutter_backend_get_default_seat (clutter_backend);
  meta_seat_native_set_keyboard_layout_index_async (META_SEAT_NATIVE (seat),
                                                    idx,
                                                    g_task_get_cancellable (task),
                                                    set_layout_index_cb,
                                                    task);
}

static gboolean
meta_backend_native_is_headless (MetaBackend *backend)
{
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaBackendNativePrivate *priv =
    meta_backend_native_get_instance_private (backend_native);

  return (priv->mode == META_BACKEND_NATIVE_MODE_HEADLESS ||
          priv->mode == META_BACKEND_NATIVE_MODE_TEST_HEADLESS);
}

static void
meta_backend_native_set_pointer_constraint (MetaBackend           *backend,
                                            MetaPointerConstraint *constraint)
{
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);
  MetaPointerConstraintImpl *constraint_impl = NULL;
  MtkRegion *region;

  if (constraint)
    {
      graphene_point_t origin;
      double min_edge_distance;

      region = meta_pointer_constraint_get_region (constraint, &origin);
      min_edge_distance =
        meta_pointer_constraint_get_min_edge_distance (constraint);
      constraint_impl = meta_pointer_constraint_impl_native_new (constraint,
                                                                 seat,
                                                                 region,
                                                                 origin,
                                                                 min_edge_distance);
    }

  meta_seat_native_set_pointer_constraint (META_SEAT_NATIVE (seat),
                                           constraint_impl);
}

static void
meta_backend_native_update_stage (MetaBackend *backend)
{
  ClutterActor *stage = meta_backend_get_stage (backend);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  int width, height;

  meta_stage_rebuild_views (META_STAGE (stage));

  meta_monitor_manager_get_screen_size (monitor_manager, &width, &height);
  clutter_actor_set_size (stage, width, height);
}

static MetaRenderDevice *
create_render_device (MetaBackendNative  *backend_native,
                      const char         *device_path,
                      GError            **error)
{
  MetaBackend *backend = META_BACKEND (backend_native);
  MetaDevicePool *device_pool =
    meta_backend_native_get_device_pool (backend_native);
  g_autoptr (MetaDeviceFile) device_file = NULL;
  MetaDeviceFileFlags device_file_flags;
  g_autoptr (MetaRenderDeviceGbm) render_device_gbm = NULL;
  g_autoptr (GError) gbm_error = NULL;
#ifdef HAVE_EGL_DEVICE
  MetaBackendNativePrivate *priv =
    meta_backend_native_get_instance_private (backend_native);
  g_autoptr (GError) egl_stream_error = NULL;
#endif

  if (meta_backend_is_headless (backend))
    device_file_flags = META_DEVICE_FILE_FLAG_NONE;
  else
    device_file_flags = META_DEVICE_FILE_FLAG_TAKE_CONTROL;

  device_file = meta_device_pool_open (device_pool,
                                       device_path,
                                       device_file_flags,
                                       error);
  if (!device_file)
    return NULL;

#ifdef HAVE_EGL_DEVICE
  if (g_strcmp0 (getenv ("MUTTER_DEBUG_FORCE_EGL_STREAM"), "1") != 0)
#endif
    {
      render_device_gbm = meta_render_device_gbm_new (backend, device_file,
                                                      &gbm_error);
      if (render_device_gbm)
        {
          MetaRenderDevice *render_device =
            META_RENDER_DEVICE (render_device_gbm);

          if (meta_render_device_is_hardware_accelerated (render_device))
            return META_RENDER_DEVICE (g_steal_pointer (&render_device_gbm));
        }
    }
#ifdef HAVE_EGL_DEVICE
  else
    {
      g_set_error (&gbm_error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "GBM backend was disabled using env var");
    }
#endif

#ifdef HAVE_EGL_DEVICE
  if (!priv->render_device_egl_stream)
    {
      MetaRenderDeviceEglStream *device;

      device = meta_render_device_egl_stream_new (backend,
                                                  device_file,
                                                  &egl_stream_error);
      if (device)
        {
          g_object_add_weak_pointer (G_OBJECT (device),
                                     (gpointer *) &priv->render_device_egl_stream);
          return META_RENDER_DEVICE (device);
        }
    }
  else if (!render_device_gbm)
    {
      g_set_error (&egl_stream_error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "it's not GBM-compatible and one EGLDevice was already found");
    }
#endif

  if (render_device_gbm)
    return META_RENDER_DEVICE (g_steal_pointer (&render_device_gbm));

  g_set_error (error, G_IO_ERROR,
               G_IO_ERROR_FAILED,
               "Failed to initialize render device for %s: "
               "%s"
#ifdef HAVE_EGL_DEVICE
               ", %s"
#endif
               , device_path
               , gbm_error->message
#ifdef HAVE_EGL_DEVICE
               , egl_stream_error->message
#endif
               );

  return NULL;
}

static gboolean
add_drm_device (MetaBackendNative  *backend_native,
                GUdevDevice        *device,
                GError            **error)
{
  MetaBackendNativePrivate *priv =
    meta_backend_native_get_instance_private (backend_native);
  MetaKmsDeviceFlag flags = META_KMS_DEVICE_FLAG_NONE;
  const char *device_path;
  g_autoptr (MetaRenderDevice) render_device = NULL;
  MetaKmsDevice *kms_device;
  MetaGpuKms *gpu_kms;

  if (meta_is_udev_device_platform_device (device))
    flags |= META_KMS_DEVICE_FLAG_PLATFORM_DEVICE;

  if (meta_is_udev_device_boot_vga (device))
    flags |= META_KMS_DEVICE_FLAG_BOOT_VGA;

  if (meta_is_udev_device_disable_modifiers (device))
    flags |= META_KMS_DEVICE_FLAG_DISABLE_MODIFIERS;

  if (meta_is_udev_device_disable_vrr (device))
    flags |= META_KMS_DEVICE_FLAG_DISABLE_VRR;

  if (meta_is_udev_device_preferred_primary (device))
    flags |= META_KMS_DEVICE_FLAG_PREFERRED_PRIMARY;

  device_path = g_udev_device_get_device_file (device);

  render_device = create_render_device (backend_native, device_path, error);
  if (!render_device)
    return FALSE;

#ifdef HAVE_EGL_DEVICE
  if (META_IS_RENDER_DEVICE_EGL_STREAM (render_device))
    flags |= META_KMS_DEVICE_FLAG_FORCE_LEGACY;
#endif

  kms_device = meta_kms_create_device (priv->kms, device_path, flags,
                                       error);
  if (!kms_device)
    return FALSE;

  if (priv->startup_render_devices)
    {
      g_hash_table_insert (priv->startup_render_devices,
                           g_strdup (device_path),
                           g_steal_pointer (&render_device));
    }

  gpu_kms = meta_gpu_kms_new (backend_native, kms_device, error);
  meta_backend_add_gpu (META_BACKEND (backend_native), META_GPU (gpu_kms));
  return TRUE;
}

static gboolean
should_ignore_device (MetaBackendNative *backend_native,
                      GUdevDevice       *device)
{
  MetaBackendNativePrivate *priv =
    meta_backend_native_get_instance_private (backend_native);

  switch (priv->mode)
    {
    case META_BACKEND_NATIVE_MODE_DEFAULT:
    case META_BACKEND_NATIVE_MODE_HEADLESS:
      return meta_is_udev_device_ignore (device);
    case META_BACKEND_NATIVE_MODE_TEST_HEADLESS:
      return TRUE;
    case META_BACKEND_NATIVE_MODE_TEST_VKMS:
      return !meta_is_udev_test_device (device);
    }
  g_assert_not_reached ();
}

static void
on_udev_device_added (MetaUdev          *udev,
                      GUdevDevice       *device,
                      MetaBackendNative *native)
{
  MetaBackend *backend = META_BACKEND (native);
  g_autoptr (GError) error = NULL;
  const char *device_path;
  GList *gpus, *l;

  if (!meta_udev_is_drm_device (udev, device))
    return;

  device_path = g_udev_device_get_device_file (device);

  gpus = meta_backend_get_gpus (backend);
  for (l = gpus; l; l = l->next)
    {
      MetaGpuKms *gpu_kms = l->data;

      if (!g_strcmp0 (device_path, meta_gpu_kms_get_file_path (gpu_kms)))
        {
          g_warning ("Failed to hotplug secondary gpu '%s': %s",
                     device_path, "device already present");
          return;
        }
    }

  if (should_ignore_device (native, device))
    {
      g_message ("Ignoring DRM device '%s'", device_path);
      return;
    }

  if (!add_drm_device (native, device, &error))
    {
      if (meta_backend_is_headless (backend) &&
          g_error_matches (error, G_IO_ERROR,
                           G_IO_ERROR_PERMISSION_DENIED))
        {
          meta_topic (META_DEBUG_BACKEND,
                      "Ignoring unavailable secondary gpu '%s': %s",
                      device_path, error->message);
        }
      else
        {
          g_warning ("Failed to hotplug secondary gpu '%s': %s",
                     device_path, error->message);
        }
    }
}

static gboolean
init_gpus (MetaBackendNative  *native,
           GError            **error)
{
  MetaBackendNativePrivate *priv =
    meta_backend_native_get_instance_private (native);
  MetaBackend *backend = META_BACKEND (native);
  MetaUdev *udev = meta_backend_get_udev (backend);
  MetaKms *kms = meta_backend_native_get_kms (native);
  g_autoptr (GError) local_error = NULL;
  MetaUdevDeviceType device_type = 0;
  GList *devices;
  GList *l;

  switch (priv->mode)
    {
    case META_BACKEND_NATIVE_MODE_DEFAULT:
    case META_BACKEND_NATIVE_MODE_TEST_VKMS:
      device_type = META_UDEV_DEVICE_TYPE_CARD;
      break;
    case META_BACKEND_NATIVE_MODE_HEADLESS:
    case META_BACKEND_NATIVE_MODE_TEST_HEADLESS:
      device_type = META_UDEV_DEVICE_TYPE_RENDER_NODE;
      break;
    }

  devices = meta_udev_list_drm_devices (udev, device_type, &local_error);
  if (local_error)
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  for (l = devices; l; l = l->next)
    {
      GUdevDevice *device = l->data;
      GError *device_error = NULL;

      if (should_ignore_device (native, device))
        {
          g_message ("Ignoring DRM device '%s'",
                     g_udev_device_get_device_file (device));
          continue;
        }

      if (!add_drm_device (native, device, &device_error))
        {
          if (meta_backend_is_headless (backend) &&
              (g_error_matches (device_error, G_IO_ERROR,
                                G_IO_ERROR_PERMISSION_DENIED) ||
               (g_strcmp0 (g_getenv ("RUNNING_UNDER_RR"), "1") == 0 &&
                g_error_matches (device_error, G_IO_ERROR,
                                 G_IO_ERROR_NOT_FOUND))))
            {
              meta_topic (META_DEBUG_BACKEND,
                          "Ignoring unavailable gpu '%s': %s'",
                          g_udev_device_get_device_file (device),
                          device_error->message);
            }
          else
            {
              g_warning ("Failed to open gpu '%s': %s",
                         g_udev_device_get_device_file (device),
                         device_error->message);
            }

          g_clear_error (&device_error);
          continue;
        }
    }

  g_list_free_full (devices, g_object_unref);

  meta_kms_notify_probed (kms);

  if (!meta_backend_is_headless (backend) &&
      g_list_length (meta_backend_get_gpus (backend)) == 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "No GPUs found");
      return FALSE;
    }

  g_signal_connect_object (udev, "device-added",
                           G_CALLBACK (on_udev_device_added),
                           native,
                           G_CONNECT_DEFAULT);

  return TRUE;
}

static void
on_started (MetaContext *context,
            MetaBackend *backend)
{
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat;

  seat = clutter_backend_get_default_seat (clutter_backend);
  meta_seat_native_start (META_SEAT_NATIVE (seat));
}

static gboolean
meta_backend_native_create_launcher (MetaBackend   *backend,
                                     MetaLauncher **launcher_out,
                                     GError       **error)
{
  MetaBackendNative *native = META_BACKEND_NATIVE (backend);
  MetaBackendNativePrivate *priv =
    meta_backend_native_get_instance_private (native);
  g_autoptr (MetaLauncher) launcher = NULL;
  g_autoptr (GError) local_error = NULL;

  /* We don't want to track the session the headless mode got started on. */
  if (priv->mode == META_BACKEND_NATIVE_MODE_HEADLESS)
    {
      *launcher_out = NULL;
      return TRUE;
    }

  launcher = meta_launcher_new (backend, &local_error);

  /* Headless test is allowed to run with and without a launcher */
  if (!launcher && priv->mode == META_BACKEND_NATIVE_MODE_TEST_HEADLESS)
    {
      *launcher_out = NULL;
      return TRUE;
    }

  /* For everything else we do need a launcher */
  if (!launcher)
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  /* If we have no seat, go headless without launcher */
  if (!meta_launcher_get_seat_id (launcher) &&
      priv->mode == META_BACKEND_NATIVE_MODE_DEFAULT)
    {
      priv->mode = META_BACKEND_NATIVE_MODE_HEADLESS;
      g_message ("No seat assigned, running headlessly");

      *launcher_out = NULL;
      return TRUE;
    }

  /* When there is a head (default or vkms modes), we need to take control */
  if (!meta_launcher_take_control (launcher, error) &&
      priv->mode != META_BACKEND_NATIVE_MODE_TEST_HEADLESS)
    {
      g_prefix_error_literal (error, "Failed to take control of the session: ");
      return FALSE;
    }

  *launcher_out = g_steal_pointer (&launcher);
  return TRUE;
}


static gboolean
meta_backend_native_init_basic (MetaBackend  *backend,
                                GError      **error)
{
  MetaBackendNative *native = META_BACKEND_NATIVE (backend);
  MetaBackendNativePrivate *priv =
    meta_backend_native_get_instance_private (native);
  MetaKmsFlags kms_flags;

  priv->startup_render_devices =
    g_hash_table_new_full (g_str_hash, g_str_equal,
                           g_free, g_object_unref);

  priv->device_pool = meta_device_pool_new (native);

  kms_flags = META_KMS_FLAG_NONE;
  if (meta_backend_is_headless (backend))
    kms_flags |= META_KMS_FLAG_NO_MODE_SETTING;

  priv->kms = meta_kms_new (META_BACKEND (native), kms_flags, error);
  if (!priv->kms)
    return FALSE;

  if (!init_gpus (native, error))
    return FALSE;

  g_signal_connect (meta_backend_get_context (backend),
                    "started",
                    G_CALLBACK (on_started),
                    backend);

  return TRUE;
}

static void
meta_backend_native_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (object);
  MetaBackendNativePrivate *priv =
    meta_backend_native_get_instance_private (backend_native);

  switch (prop_id)
    {
    case PROP_MODE:
      priv->mode = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_backend_native_class_init (MetaBackendNativeClass *klass)
{
  MetaBackendClass *backend_class = META_BACKEND_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = meta_backend_native_set_property;
  object_class->dispose = meta_backend_native_dispose;

  backend_class->create_clutter_backend = meta_backend_native_create_clutter_backend;
  backend_class->create_default_seat = meta_backend_native_create_default_seat;

  backend_class->init_basic = meta_backend_native_init_basic;
  backend_class->init_post = meta_backend_native_init_post;
  backend_class->get_capabilities = meta_backend_native_get_capabilities;

  backend_class->create_launcher = meta_backend_native_create_launcher;
  backend_class->create_monitor_manager = meta_backend_native_create_monitor_manager;
  backend_class->create_color_manager = meta_backend_native_create_color_manager;
  backend_class->get_cursor_renderer = meta_backend_native_get_cursor_renderer;
  backend_class->create_renderer = meta_backend_native_create_renderer;
  backend_class->get_input_settings = meta_backend_native_get_input_settings;

  backend_class->get_current_logical_monitor = meta_backend_native_get_current_logical_monitor;

  backend_class->set_keymap_async = meta_backend_native_set_keymap_async;
  backend_class->get_keymap = meta_backend_native_get_keymap;
  backend_class->get_keymap_layout_group = meta_backend_native_get_keymap_layout_group;
  backend_class->set_keymap_layout_group_async = meta_backend_native_set_keymap_layout_group_async;
  backend_class->update_stage = meta_backend_native_update_stage;

  backend_class->set_pointer_constraint = meta_backend_native_set_pointer_constraint;

  backend_class->is_headless = meta_backend_native_is_headless;

  backend_class->pause = meta_backend_native_pause;
  backend_class->resume = meta_backend_native_resume;

  obj_props[PROP_MODE] =
    g_param_spec_enum ("mode", NULL, NULL,
                       META_TYPE_BACKEND_NATIVE_MODE,
                       META_BACKEND_NATIVE_MODE_DEFAULT,
                       G_PARAM_WRITABLE |
                       G_PARAM_CONSTRUCT_ONLY |
                       G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
meta_backend_native_init (MetaBackendNative *backend_native)
{
}

MetaDevicePool *
meta_backend_native_get_device_pool (MetaBackendNative *backend_native)
{
  MetaBackendNativePrivate *priv =
    meta_backend_native_get_instance_private (backend_native);

  return priv->device_pool;
}

MetaKms *
meta_backend_native_get_kms (MetaBackendNative *backend_native)
{
  MetaBackendNativePrivate *priv =
    meta_backend_native_get_instance_private (backend_native);

  return priv->kms;
}

MetaDrmLeaseManager *
meta_backend_native_get_drm_lease_manager (MetaBackendNative *backend_native)
{
  MetaBackendNativePrivate *priv =
    meta_backend_native_get_instance_private (backend_native);

  return priv->drm_lease_manager;
}

gboolean
meta_backend_native_activate_vt (MetaBackendNative  *backend_native,
                                 int                 vt,
                                 GError            **error)
{
  MetaBackendNativePrivate *priv =
    meta_backend_native_get_instance_private (backend_native);
  MetaLauncher *launcher =
    meta_backend_get_launcher (META_BACKEND (backend_native));

  switch (priv->mode)
    {
    case META_BACKEND_NATIVE_MODE_DEFAULT:
      return meta_launcher_activate_vt (launcher, vt, error);
    case META_BACKEND_NATIVE_MODE_HEADLESS:
    case META_BACKEND_NATIVE_MODE_TEST_HEADLESS:
    case META_BACKEND_NATIVE_MODE_TEST_VKMS:
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Can't switch VT while headless");
      return FALSE;
    }

  g_assert_not_reached ();
}

static void
meta_backend_native_pause (MetaBackend *backend)
{
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaBackendNativePrivate *priv =
    meta_backend_native_get_instance_private (backend_native);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerNative *monitor_manager_native =
    META_MONITOR_MANAGER_NATIVE (monitor_manager);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  MetaSeatNative *seat =
    META_SEAT_NATIVE (clutter_backend_get_default_seat (clutter_backend));

  meta_seat_native_release_devices (seat);
  meta_monitor_manager_native_pause (monitor_manager_native);
  meta_drm_lease_manager_pause (priv->drm_lease_manager);

  META_BACKEND_CLASS (meta_backend_native_parent_class)->pause (backend);
}

static void
meta_backend_native_resume (MetaBackend *backend)
{
  MetaBackendNative *native = META_BACKEND_NATIVE (backend);
  MetaBackendNativePrivate *priv =
    meta_backend_native_get_instance_private (native);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerNative *monitor_manager_native =
    META_MONITOR_MANAGER_NATIVE (monitor_manager);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  MetaSeatNative *seat =
    META_SEAT_NATIVE (clutter_backend_get_default_seat (clutter_backend));
  MetaIdleManager *idle_manager = meta_backend_get_idle_manager (backend);
  MetaInputSettings *input_settings = meta_backend_get_input_settings (backend);

  META_BACKEND_CLASS (meta_backend_native_parent_class)->resume (backend);

  meta_monitor_manager_native_resume (monitor_manager_native);
  meta_kms_resume (priv->kms);
  meta_drm_lease_manager_resume (priv->drm_lease_manager);

  meta_seat_native_reclaim_devices (seat);

  idle_manager = meta_backend_get_idle_manager (backend);
  meta_idle_manager_reset_idle_time (idle_manager);

  meta_input_settings_maybe_restore_numlock_state (input_settings);

  clutter_seat_ensure_a11y_state (CLUTTER_SEAT (seat));
}

static MetaRenderDevice *
meta_backend_native_create_render_device (MetaBackendNative  *backend_native,
                                          const char         *device_path,
                                          GError            **error)
{
  g_autoptr (MetaRenderDevice) render_device = NULL;

  render_device = create_render_device (backend_native, device_path, error);
  return g_steal_pointer (&render_device);
}

MetaRenderDevice *
meta_backend_native_take_render_device (MetaBackendNative  *backend_native,
                                        const char         *device_path,
                                        GError            **error)
{
  MetaBackendNativePrivate *priv =
    meta_backend_native_get_instance_private (backend_native);
  MetaRenderDevice *render_device;
  g_autofree char *stolen_device_path = NULL;

  if (priv->startup_render_devices &&
      g_hash_table_steal_extended (priv->startup_render_devices,
                                   device_path,
                                   (gpointer *) &stolen_device_path,
                                   (gpointer *) &render_device))
    {
      return render_device;
    }
  else
    {
      return meta_backend_native_create_render_device (backend_native,
                                                       device_path, error);
    }
}
