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
 * SECTION:meta-backend-native
 * @title: MetaBackendNative
 * @short_description: A native (KMS/evdev) MetaBackend
 *
 * MetaBackendNative is an implementation of #MetaBackend that uses "native"
 * technologies like DRM/KMS and libinput/evdev to perform the necessary
 * functions.
 */

#include "config.h"

#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-backend-native-private.h"
#include "backends/native/meta-input-thread.h"

#include <sched.h>
#include <stdlib.h>

#include "backends/meta-cursor-tracker-private.h"
#include "backends/meta-idle-monitor-private.h"
#include "backends/meta-keymap-utils.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-pointer-constraint.h"
#include "backends/meta-settings-private.h"
#include "backends/meta-stage-private.h"
#include "backends/native/meta-clutter-backend-native.h"
#include "backends/native/meta-kms.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-launcher.h"
#include "backends/native/meta-monitor-manager-native.h"
#include "backends/native/meta-renderer-native.h"
#include "backends/native/meta-seat-native.h"
#include "backends/native/meta-stage-native.h"
#include "cogl/cogl.h"
#include "core/meta-border.h"
#include "meta/main.h"

#ifdef HAVE_REMOTE_DESKTOP
#include "backends/meta-screen-cast.h"
#endif

enum
{
  PROP_0,

  PROP_HEADLESS,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

struct _MetaBackendNative
{
  MetaBackend parent;

  MetaLauncher *launcher;
  MetaUdev *udev;
  MetaKms *kms;

  gboolean is_headless;

  gulong udev_device_added_handler_id;
};

static GInitableIface *initable_parent_iface;

static void
initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_TYPE_WITH_CODE (MetaBackendNative, meta_backend_native, META_TYPE_BACKEND,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                initable_iface_init))

static void
disconnect_udev_device_added_handler (MetaBackendNative *native);

static void
meta_backend_native_dispose (GObject *object)
{
  MetaBackendNative *native = META_BACKEND_NATIVE (object);

  if (native->udev_device_added_handler_id)
    {
      disconnect_udev_device_added_handler (native);
      native->udev_device_added_handler_id = 0;
    }

  if (native->kms)
    meta_kms_prepare_shutdown (native->kms);

  G_OBJECT_CLASS (meta_backend_native_parent_class)->dispose (object);

  g_clear_object (&native->kms);
  g_clear_object (&native->udev);
  g_clear_pointer (&native->launcher, meta_launcher_free);
}

static ClutterBackend *
meta_backend_native_create_clutter_backend (MetaBackend *backend)
{
  return g_object_new (META_TYPE_CLUTTER_BACKEND_NATIVE, NULL);
}

static ClutterSeat *
meta_backend_native_create_default_seat (MetaBackend  *backend,
                                         GError      **error)
{
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  const char *seat_id;
  MetaSeatNativeFlag flags;

  seat_id = meta_backend_native_get_seat_id (backend_native);

  if (meta_backend_native_is_headless (backend_native))
    flags = META_SEAT_NATIVE_FLAG_NO_LIBINPUT;
  else
    flags = META_SEAT_NATIVE_FLAG_NONE;

  return CLUTTER_SEAT (g_object_new (META_TYPE_SEAT_NATIVE,
                                     "backend", clutter_backend,
                                     "seat-id", seat_id,
                                     "flags", flags,
                                     NULL));
}

#ifdef HAVE_REMOTE_DESKTOP
static void
maybe_disable_screen_cast_dma_bufs (MetaBackendNative *native)
{
  MetaBackend *backend = META_BACKEND (native);
  MetaSettings *settings = meta_backend_get_settings (backend);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  MetaRendererNative *renderer_native = META_RENDERER_NATIVE (renderer);
  MetaScreenCast *screen_cast = meta_backend_get_screen_cast (backend);
  MetaGpuKms *primary_gpu;
  MetaKmsDevice *kms_device;
  const char *driver_name;
  static const char *enable_dma_buf_drivers[] = {
    "i915",
    NULL,
  };

  primary_gpu = meta_renderer_native_get_primary_gpu (renderer_native);
  if (!primary_gpu)
    {
      g_message ("Disabling DMA buffer screen sharing (surfaceless)");
      goto disable_dma_bufs;
    }

  kms_device = meta_gpu_kms_get_kms_device (primary_gpu);
  driver_name = meta_kms_device_get_driver_name (kms_device);

  if (g_strv_contains (enable_dma_buf_drivers, driver_name))
    return;

  if (meta_settings_is_experimental_feature_enabled (settings,
        META_EXPERIMENTAL_FEATURE_DMA_BUF_SCREEN_SHARING))
    return;

  g_message ("Disabling DMA buffer screen sharing for driver '%s'.",
             driver_name);

disable_dma_bufs:
  meta_screen_cast_disable_dma_bufs (screen_cast);
}
#endif /* HAVE_REMOTE_DESKTOP */

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
meta_backend_native_post_init (MetaBackend *backend)
{
  MetaSettings *settings = meta_backend_get_settings (backend);

  META_BACKEND_CLASS (meta_backend_native_parent_class)->post_init (backend);

  if (meta_settings_is_experimental_feature_enabled (settings,
                                                     META_EXPERIMENTAL_FEATURE_RT_SCHEDULER))
    {
      int retval;
      struct sched_param sp = {
        .sched_priority = sched_get_priority_min (SCHED_RR)
      };

      retval = sched_setscheduler (0, SCHED_RR | SCHED_RESET_ON_FORK, &sp);

      if (retval != 0)
        g_warning ("Failed to set RT scheduler: %m");
    }

#ifdef HAVE_REMOTE_DESKTOP
  maybe_disable_screen_cast_dma_bufs (META_BACKEND_NATIVE (backend));
#endif

  update_viewports (backend);

#ifdef HAVE_WAYLAND
  meta_backend_init_wayland (backend);
#endif
}

static MetaMonitorManager *
meta_backend_native_create_monitor_manager (MetaBackend *backend,
                                            GError     **error)
{
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaMonitorManager *manager;

  manager = g_initable_new (META_TYPE_MONITOR_MANAGER_NATIVE, NULL, error,
                            "backend", backend,
                            "needs-outputs", !backend_native->is_headless,
                            NULL);
  if (!manager)
    return NULL;

  g_signal_connect_swapped (manager, "monitors-changed-internal",
                            G_CALLBACK (update_viewports), backend);

  return manager;
}

static MetaCursorRenderer *
meta_backend_native_get_cursor_renderer (MetaBackend        *backend,
                                         ClutterInputDevice *device)
{
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  MetaSeatNative *seat_native =
    META_SEAT_NATIVE (clutter_backend_get_default_seat (clutter_backend));

  return meta_seat_native_maybe_ensure_cursor_renderer (seat_native, device);
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
meta_backend_native_set_keymap (MetaBackend *backend,
                                const char  *layouts,
                                const char  *variants,
                                const char  *options)
{
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat;

  seat = clutter_backend_get_default_seat (clutter_backend);
  meta_seat_native_set_keyboard_map (META_SEAT_NATIVE (seat),
                                     layouts, variants, options);

  meta_backend_notify_keymap_changed (backend);
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
meta_backend_native_lock_layout_group (MetaBackend *backend,
                                       guint        idx)
{
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  xkb_layout_index_t old_idx;
  ClutterSeat *seat;

  old_idx = meta_backend_native_get_keymap_layout_group (backend);
  if (old_idx == idx)
    return;

  seat = clutter_backend_get_default_seat (clutter_backend);
  meta_seat_native_set_keyboard_layout_index (META_SEAT_NATIVE (seat), idx);
  meta_backend_notify_keymap_layout_group_changed (backend, idx);
}

const char *
meta_backend_native_get_seat_id (MetaBackendNative *backend_native)
{
  if (backend_native->is_headless)
    return "seat0";
  else
    return meta_launcher_get_seat_id (backend_native->launcher);
}

gboolean
meta_backend_native_is_headless (MetaBackendNative *backend_native)
{
  return backend_native->is_headless;
}

static void
meta_backend_native_set_pointer_constraint (MetaBackend           *backend,
                                            MetaPointerConstraint *constraint)
{
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);
  MetaPointerConstraintImpl *constraint_impl = NULL;
  cairo_region_t *region;

  if (constraint)
    {
      region = meta_pointer_constraint_get_region (constraint);
      constraint_impl = meta_pointer_constraint_impl_native_new (constraint,
                                                                 region);
    }

  meta_seat_native_set_pointer_constraint (META_SEAT_NATIVE (seat),
                                           constraint_impl);
}

static void
meta_backend_native_update_screen_size (MetaBackend *backend,
                                        int width, int height)
{
  ClutterActor *stage = meta_backend_get_stage (backend);
  ClutterStageWindow *stage_window =
    _clutter_stage_get_window (CLUTTER_STAGE (stage));
  MetaStageNative *stage_native = META_STAGE_NATIVE (stage_window);

  meta_stage_native_rebuild_views (stage_native);

  clutter_actor_set_size (stage, width, height);
}

static MetaGpuKms *
create_gpu_from_udev_device (MetaBackendNative  *native,
                             GUdevDevice        *device,
                             GError            **error)
{
  MetaKmsDeviceFlag flags = META_KMS_DEVICE_FLAG_NONE;
  const char *device_path;
  MetaKmsDevice *kms_device;

  if (meta_is_udev_device_platform_device (device))
    flags |= META_KMS_DEVICE_FLAG_PLATFORM_DEVICE;

  if (meta_is_udev_device_boot_vga (device))
    flags |= META_KMS_DEVICE_FLAG_BOOT_VGA;

  if (meta_is_udev_device_disable_modifiers (device))
    flags |= META_KMS_DEVICE_FLAG_DISABLE_MODIFIERS;

  if (meta_is_udev_device_preferred_primary (device))
    flags |= META_KMS_DEVICE_FLAG_PREFERRED_PRIMARY;

  device_path = g_udev_device_get_device_file (device);

  kms_device = meta_kms_create_device (native->kms, device_path, flags,
                                       error);
  if (!kms_device)
    return NULL;

  return meta_gpu_kms_new (native, kms_device, error);
}

static void
on_udev_device_added (MetaUdev          *udev,
                      GUdevDevice       *device,
                      MetaBackendNative *native)
{
  MetaBackend *backend = META_BACKEND (native);
  g_autoptr (GError) error = NULL;
  const char *device_path;
  MetaGpuKms *new_gpu_kms;
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

  if (meta_is_udev_device_ignore (device))
    {
      g_message ("Ignoring DRM device '%s' (from udev rule)", device_path);
      return;
    }

  new_gpu_kms = create_gpu_from_udev_device (native, device, &error);
  if (!new_gpu_kms)
    {
      g_warning ("Failed to hotplug secondary gpu '%s': %s",
                 device_path, error->message);
      return;
    }

  meta_backend_add_gpu (backend, META_GPU (new_gpu_kms));
}

static void
connect_udev_device_added_handler (MetaBackendNative *native)
{
  native->udev_device_added_handler_id =
    g_signal_connect (native->udev, "device-added",
                      G_CALLBACK (on_udev_device_added), native);
}

static void
disconnect_udev_device_added_handler (MetaBackendNative *native)
{
  g_clear_signal_handler (&native->udev_device_added_handler_id, native->udev);
}

static gboolean
init_gpus (MetaBackendNative  *native,
           GError            **error)
{
  MetaBackend *backend = META_BACKEND (native);
  MetaUdev *udev = meta_backend_native_get_udev (native);
  GList *devices;
  GList *l;

  devices = meta_udev_list_drm_devices (udev, error);
  if (*error)
    return FALSE;

  for (l = devices; l; l = l->next)
    {
      GUdevDevice *device = l->data;
      MetaGpuKms *gpu_kms;
      GError *local_error = NULL;

      if (meta_is_udev_device_ignore (device))
        {
          g_message ("Ignoring DRM device '%s' (from udev rule)",
                     g_udev_device_get_device_file (device));
          continue;
        }

      gpu_kms = create_gpu_from_udev_device (native, device, &local_error);

      if (!gpu_kms)
        {
          g_warning ("Failed to open gpu '%s': %s",
                     g_udev_device_get_device_file (device),
                     local_error->message);
          g_clear_error (&local_error);
          continue;
        }

      meta_backend_add_gpu (backend, META_GPU (gpu_kms));
    }

  g_list_free_full (devices, g_object_unref);

  if (!native->is_headless &&
      g_list_length (meta_backend_get_gpus (backend)) == 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "No GPUs found");
      return FALSE;
    }

  connect_udev_device_added_handler (native);

  return TRUE;
}

static gboolean
meta_backend_native_initable_init (GInitable     *initable,
                                   GCancellable  *cancellable,
                                   GError       **error)
{
  MetaBackendNative *native = META_BACKEND_NATIVE (initable);
  MetaKmsFlags kms_flags;

  if (!meta_is_stage_views_enabled ())
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "The native backend requires stage views");
      return FALSE;
    }

  if (!native->is_headless)
    {
      native->launcher = meta_launcher_new (error);
      if (!native->launcher)
        return FALSE;
    }

#ifdef HAVE_WAYLAND
  meta_backend_init_wayland_display (META_BACKEND (native));
#endif

  native->udev = meta_udev_new (native);

  kms_flags = META_KMS_FLAG_NONE;
  if (native->is_headless)
    kms_flags |= META_KMS_FLAG_NO_MODE_SETTING;

  native->kms = meta_kms_new (META_BACKEND (native), kms_flags, error);
  if (!native->kms)
    return FALSE;

  if (!init_gpus (native, error))
    return FALSE;

  return initable_parent_iface->init (initable, cancellable, error);
}

static void
meta_backend_native_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (object);

  switch (prop_id)
    {
    case PROP_HEADLESS:
      backend_native->is_headless = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
  initable_parent_iface = g_type_interface_peek_parent (initable_iface);

  initable_iface->init = meta_backend_native_initable_init;
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

  backend_class->post_init = meta_backend_native_post_init;

  backend_class->create_monitor_manager = meta_backend_native_create_monitor_manager;
  backend_class->get_cursor_renderer = meta_backend_native_get_cursor_renderer;
  backend_class->create_renderer = meta_backend_native_create_renderer;
  backend_class->get_input_settings = meta_backend_native_get_input_settings;

  backend_class->get_current_logical_monitor = meta_backend_native_get_current_logical_monitor;

  backend_class->set_keymap = meta_backend_native_set_keymap;
  backend_class->get_keymap = meta_backend_native_get_keymap;
  backend_class->get_keymap_layout_group = meta_backend_native_get_keymap_layout_group;
  backend_class->lock_layout_group = meta_backend_native_lock_layout_group;
  backend_class->update_screen_size = meta_backend_native_update_screen_size;

  backend_class->set_pointer_constraint = meta_backend_native_set_pointer_constraint;

  obj_props[PROP_HEADLESS] =
    g_param_spec_boolean ("headless",
                          "headless",
                          "Headless",
                          FALSE,
                          G_PARAM_WRITABLE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
meta_backend_native_init (MetaBackendNative *native)
{
}

MetaLauncher *
meta_backend_native_get_launcher (MetaBackendNative *native)
{
  return native->launcher;
}

MetaUdev *
meta_backend_native_get_udev (MetaBackendNative *native)
{
  return native->udev;
}

MetaKms *
meta_backend_native_get_kms (MetaBackendNative *native)
{
  return native->kms;
}

gboolean
meta_activate_vt (int vt, GError **error)
{
  MetaBackend *backend = meta_get_backend ();
  MetaBackendNative *native = META_BACKEND_NATIVE (backend);
  MetaLauncher *launcher = meta_backend_native_get_launcher (native);

  if (native->is_headless)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Can't switch VT while headless");
      return FALSE;
    }

  return meta_launcher_activate_vt (launcher, vt, error);
}

void
meta_backend_native_pause (MetaBackendNative *native)
{
  MetaBackend *backend = META_BACKEND (native);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerNative *monitor_manager_native =
    META_MONITOR_MANAGER_NATIVE (monitor_manager);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  MetaSeatNative *seat =
    META_SEAT_NATIVE (clutter_backend_get_default_seat (clutter_backend));
  MetaRenderer *renderer = meta_backend_get_renderer (backend);

  COGL_TRACE_BEGIN_SCOPED (MetaBackendNativePause,
                           "Backend (pause)");

  meta_seat_native_release_devices (seat);
  meta_renderer_pause (renderer);

  disconnect_udev_device_added_handler (native);

  meta_monitor_manager_native_pause (monitor_manager_native);
}

void meta_backend_native_resume (MetaBackendNative *native)
{
  MetaBackend *backend = META_BACKEND (native);
  ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerNative *monitor_manager_native =
    META_MONITOR_MANAGER_NATIVE (monitor_manager);
  MetaIdleMonitor *idle_monitor;
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  MetaSeatNative *seat =
    META_SEAT_NATIVE (clutter_backend_get_default_seat (clutter_backend));
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  MetaInputSettings *input_settings;

  COGL_TRACE_BEGIN_SCOPED (MetaBackendNativeResume,
                           "Backend (resume)");

  meta_monitor_manager_native_resume (monitor_manager_native);
  meta_kms_resume (native->kms);

  connect_udev_device_added_handler (native);

  meta_seat_native_reclaim_devices (seat);
  meta_renderer_resume (renderer);

  clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));

  idle_monitor = meta_idle_monitor_get_core ();
  meta_idle_monitor_reset_idletime (idle_monitor);

  input_settings = meta_backend_get_input_settings (backend);
  meta_input_settings_maybe_restore_numlock_state (input_settings);

  clutter_seat_ensure_a11y_state (CLUTTER_SEAT (seat));
}
