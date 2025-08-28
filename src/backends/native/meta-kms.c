/*
 * Copyright (C) 2018 Red Hat
 * Copyright 2020 DisplayLink (UK) Ltd.
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

#include "backends/native/meta-kms-private.h"

#include "backends/meta-udev.h"
#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-kms-cursor-manager.h"
#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-impl.h"
#include "backends/native/meta-kms-update-private.h"
#include "backends/native/meta-thread-private.h"
#include "cogl/cogl.h"

#include "meta-private-enum-types.h"

enum
{
  RESOURCES_CHANGED,
  DEVICE_ADDED,
  LEASE_CHANGED,

  N_SIGNALS
};

static int signals[N_SIGNALS];

struct _MetaKms
{
  MetaThread parent;

  MetaKmsFlags flags;

  gulong hotplug_handler_id;
  gulong lease_handler_id;
  gulong removed_handler_id;

  GList *devices;

  int kernel_thread_inhibit_count;

  MetaKmsCursorManager *cursor_manager;

  gboolean shutting_down;
};

G_DEFINE_TYPE (MetaKms, meta_kms, META_TYPE_THREAD)

G_DEFINE_QUARK (-meta-kms-error-quark, meta_kms_error)

static void
invoke_result_listener (MetaThread *thread,
                        gpointer    user_data)
{
  MetaKmsResultListener *listener = user_data;

  meta_kms_result_listener_notify (listener);
}

void
meta_kms_queue_result_callback (MetaKms               *kms,
                                MetaKmsResultListener *listener)
{
  GMainContext *main_context =
    meta_kms_result_listener_get_main_context (listener);

  meta_kms_queue_callback  (kms,
                            main_context,
                            invoke_result_listener,
                            listener,
                            (GDestroyNotify) meta_kms_result_listener_free);
}

static gpointer
meta_kms_discard_pending_page_flips_in_impl (MetaThreadImpl  *thread_impl,
                                             gpointer         user_data,
                                             GError         **error)
{
  MetaKmsImpl *impl = META_KMS_IMPL (thread_impl);

  meta_kms_impl_discard_pending_page_flips (impl);
  return GINT_TO_POINTER (TRUE);
}

void
meta_kms_discard_pending_page_flips (MetaKms *kms)
{
  meta_kms_run_impl_task_sync (kms,
                               meta_kms_discard_pending_page_flips_in_impl,
                               NULL,
                               NULL);
}

static gpointer
meta_kms_notify_modes_set_in_impl (MetaThreadImpl  *thread_impl,
                                   gpointer         user_data,
                                   GError         **error)
{
  MetaKmsImpl *impl = META_KMS_IMPL (thread_impl);

  meta_kms_impl_notify_modes_set (impl);
  return GINT_TO_POINTER (TRUE);
}

void
meta_kms_notify_modes_set (MetaKms *kms)
{
  MetaThread *thread = META_THREAD (kms);

  meta_thread_run_impl_task_sync (thread,
                                  meta_kms_notify_modes_set_in_impl,
                                  NULL,
                                  NULL);
}

void
meta_kms_queue_callback (MetaKms            *kms,
                         GMainContext       *main_context,
                         MetaThreadCallback  callback,
                         gpointer            user_data,
                         GDestroyNotify      user_data_destroy)
{
  MetaThread *thread = META_THREAD (kms);

  meta_thread_queue_callback (thread,
                              main_context,
                              callback,
                              user_data,
                              user_data_destroy);
}

gpointer
meta_kms_run_impl_task_sync (MetaKms             *kms,
                             MetaThreadTaskFunc   func,
                             gpointer             user_data,
                             GError             **error)
{
  MetaThread *thread = META_THREAD (kms);

  return meta_thread_run_impl_task_sync (thread, func, user_data, error);
}

gboolean
meta_kms_in_impl_task (MetaKms *kms)
{
  MetaThread *thread = META_THREAD (kms);

  return meta_thread_is_in_impl_task (thread);
}

gboolean
meta_kms_is_waiting_for_impl_task (MetaKms *kms)
{
  MetaThread *thread = META_THREAD (kms);

  return meta_thread_is_waiting_for_impl_task (thread);
}

static MetaKmsResourceChanges
meta_kms_update_states_in_impl (MetaKms *kms,
                                char    *hotplug_event)
{
  MetaKmsResourceChanges changes = META_KMS_RESOURCE_CHANGE_NONE;
  uint32_t crtc_id = 0, connector_id = 0;
  char *path = NULL;
  GList *l;

  COGL_TRACE_BEGIN_SCOPED (MetaKmsUpdateStates,
                           "Meta::Kms::update_states_in_impl()");

  meta_assert_in_kms_impl (kms);

  if (!kms->devices)
    return META_KMS_RESOURCE_CHANGE_NO_DEVICES;

  if (hotplug_event)
    {
      sscanf (hotplug_event, "%08x:%08x:%*s", &crtc_id, &connector_id);
      path = hotplug_event + 2 * strlen ("12345678:");
    }

  for (l = kms->devices; l; l = l->next)
    {
      MetaKmsDevice *kms_device = META_KMS_DEVICE (l->data);
      const char *kms_device_path = meta_kms_device_get_path (kms_device);

      if (path && strcmp (path, kms_device_path) != 0)
        continue;

      if (crtc_id > 0 &&
          !meta_kms_device_find_crtc_in_impl (kms_device, crtc_id))
        continue;

      if (connector_id > 0 &&
          !meta_kms_device_find_connector_in_impl (kms_device, connector_id))
        continue;

      changes |=
        meta_kms_device_update_states_in_impl (kms_device, crtc_id, connector_id);
    }

  return changes;
}

static gpointer
update_states_in_impl (MetaThreadImpl  *thread_impl,
                       gpointer         user_data,
                       GError         **error)
{
  char *hotplug_event = user_data;
  MetaKmsImpl *impl = META_KMS_IMPL (thread_impl);
  MetaKms *kms = meta_kms_impl_get_kms (impl);

  return GUINT_TO_POINTER (meta_kms_update_states_in_impl (kms, hotplug_event));
}

static MetaKmsResourceChanges
update_states_sync (MetaKms *kms,
                    char    *hotplug_event)
{
  gpointer ret;

  ret = meta_kms_run_impl_task_sync (kms, update_states_in_impl,
                                     hotplug_event, NULL);

  return GPOINTER_TO_UINT (ret);
}

MetaKmsResourceChanges
meta_kms_update_states_sync (MetaKms *kms)
{
  return update_states_sync (kms, NULL);
}

static void
(meta_kms_update_resources) (MetaKms                *kms,
                             char                   *hotplug_event,
                             MetaKmsResourceChanges  changes,
                             const char             *caller)
{
  changes |= update_states_sync (kms, hotplug_event);

  meta_topic (META_DEBUG_KMS, "%s -> %s for '%s', changes=0x%x",
              caller, G_STRFUNC, hotplug_event, changes);

  if (changes != META_KMS_RESOURCE_CHANGE_NONE)
    meta_kms_emit_resources_changed (kms, changes);
}

#define meta_kms_update_resources(kms, hotplug_event, changes) \
  (meta_kms_update_resources) ((kms), (hotplug_event), (changes), G_STRFUNC);

static gpointer
resume_in_impl (MetaThreadImpl  *thread_impl,
                gpointer         user_data,
                GError         **error)
{
  MetaKmsImpl *impl = META_KMS_IMPL (thread_impl);

  meta_kms_impl_resume (impl);
  return GINT_TO_POINTER (TRUE);
}

void
meta_kms_resume (MetaKms *kms)
{
  meta_kms_update_resources (kms, NULL, META_KMS_RESOURCE_CHANGE_FULL);

  meta_kms_run_impl_task_sync (kms, resume_in_impl, NULL, NULL);
}

static char *
hotplug_event_from_udev_device (GUdevDevice *udev_device)
{
  const gchar *device_path;
  uint32_t crtc_id, connector_id;

  if (!udev_device)
    return g_strdup ("");

  device_path = g_udev_device_get_device_file (udev_device);
  crtc_id =
    CLAMP (g_udev_device_get_property_as_int (udev_device, "CRTC"),
           0, UINT32_MAX);
  connector_id =
    CLAMP (g_udev_device_get_property_as_int (udev_device, "CONNECTOR"),
           0, UINT32_MAX);
  return g_strdup_printf ("%08x:%08x:%s", crtc_id, connector_id, device_path);
}

static void
on_udev_hotplug (MetaUdev    *udev,
                 GUdevDevice *udev_device,
                 MetaKms     *kms)
{
  g_autofree char *hotplug_event = NULL;

  if (meta_is_topic_enabled (META_DEBUG_KMS))
    {
      meta_topic (META_DEBUG_KMS,
                  "%s called at %" G_GINT64_FORMAT,
                  G_STRFUNC, g_get_monotonic_time ());
    }

  hotplug_event = hotplug_event_from_udev_device (udev_device);
  meta_kms_update_resources (kms, hotplug_event, META_KMS_RESOURCE_CHANGE_NONE);
}

static void
on_udev_device_removed (MetaUdev    *udev,
                        GUdevDevice *device,
                        MetaKms     *kms)
{
  meta_kms_update_resources (kms, NULL, META_KMS_RESOURCE_CHANGE_NONE);
}

static void
on_udev_lease (MetaUdev    *udev,
               GUdevDevice *udev_device,
               MetaKms     *kms)
{
  g_signal_emit (kms, signals[LEASE_CHANGED], 0);
}

MetaBackend *
meta_kms_get_backend (MetaKms *kms)
{
  return meta_thread_get_backend (META_THREAD (kms));
}

GList *
meta_kms_get_devices (MetaKms *kms)
{
  return kms->devices;
}

MetaKmsDevice *
meta_kms_create_device (MetaKms            *kms,
                        const char         *path,
                        MetaKmsDeviceFlag   flags,
                        GError            **error)
{
  MetaKmsDevice *device;

  if (kms->flags & META_KMS_FLAG_NO_MODE_SETTING)
    flags |= META_KMS_DEVICE_FLAG_NO_MODE_SETTING;

  device = meta_kms_device_new (kms, path, flags, error);
  if (!device)
    return NULL;

  kms->devices = g_list_append (kms->devices, device);

  g_signal_emit (kms, signals[DEVICE_ADDED], 0, device);

  return device;
}

gboolean
meta_kms_is_shutting_down (MetaKms *kms)
{
  return kms->shutting_down;
}

static gpointer
prepare_shutdown_in_impl (MetaThreadImpl  *thread_impl,
                          gpointer         user_data,
                          GError         **error)
{
  MetaKmsImpl *impl = META_KMS_IMPL (thread_impl);

  meta_kms_impl_prepare_shutdown (impl);
  return GINT_TO_POINTER (TRUE);
}

static void
on_prepare_shutdown (MetaBackend *backend,
                     MetaKms     *kms)
{
  kms->shutting_down = TRUE;
  meta_kms_run_impl_task_sync (kms, prepare_shutdown_in_impl, NULL, NULL);
  meta_thread_flush_callbacks (META_THREAD (kms));

  g_clear_object (&kms->cursor_manager);
}

MetaKms *
meta_kms_new (MetaBackend   *backend,
              MetaKmsFlags   flags,
              GError       **error)
{
  MetaUdev *udev = meta_backend_get_udev (backend);
  MetaKms *kms;
  const char *thread_type_string;
  const char *preferred_scheduling_priority_string;
  MetaThreadType thread_type = META_THREAD_TYPE_KERNEL;
  MetaSchedulingPriority preferred_scheduling_priority;

  thread_type_string = g_getenv ("MUTTER_DEBUG_KMS_THREAD_TYPE");
  if (thread_type_string)
    {
      if (g_strcmp0 (thread_type_string, "user") == 0)
        thread_type = META_THREAD_TYPE_USER;
      else if (g_strcmp0 (thread_type_string, "kernel") == 0)
        thread_type = META_THREAD_TYPE_KERNEL;
      else
        g_assert_not_reached ();
    }
  if (flags & META_KMS_FLAG_NO_MODE_SETTING)
    thread_type = META_THREAD_TYPE_USER;

  preferred_scheduling_priority_string =
    g_getenv ("MUTTER_DEBUG_KMS_SCHEDULING_PRIORITY");
  if (preferred_scheduling_priority_string)
    {
      if (g_strcmp0 (preferred_scheduling_priority_string,
                     "normal") == 0)
        preferred_scheduling_priority = META_SCHEDULING_PRIORITY_NORMAL;
      else if (g_strcmp0 (preferred_scheduling_priority_string,
                          "realtime") == 0)
        preferred_scheduling_priority = META_SCHEDULING_PRIORITY_REALTIME;
      else if (g_strcmp0 (preferred_scheduling_priority_string,
                          "high-priority") == 0)
        preferred_scheduling_priority = META_SCHEDULING_PRIORITY_HIGH_PRIORITY;
      else
        g_assert_not_reached ();
    }
  else
    {
      if (flags & META_KMS_FLAG_NO_MODE_SETTING)
        preferred_scheduling_priority = META_SCHEDULING_PRIORITY_NORMAL;
      else
        preferred_scheduling_priority = META_SCHEDULING_PRIORITY_HIGH_PRIORITY;
    }

  kms = g_initable_new (META_TYPE_KMS,
                        NULL, error,
                        "backend", backend,
                        "name", "KMS thread",
                        "thread-type", thread_type,
                        "preferred-scheduling-priority", preferred_scheduling_priority,
                        NULL);
  kms->flags = flags;

  if (!(flags & META_KMS_FLAG_NO_MODE_SETTING))
    {
      kms->hotplug_handler_id =
        g_signal_connect (udev, "hotplug", G_CALLBACK (on_udev_hotplug), kms);
      kms->lease_handler_id =
        g_signal_connect (udev, "lease", G_CALLBACK (on_udev_lease), kms);
    }

  kms->removed_handler_id =
    g_signal_connect (udev, "device-removed",
                      G_CALLBACK (on_udev_device_removed), kms);

  g_signal_connect (backend, "prepare-shutdown",
                    G_CALLBACK (on_prepare_shutdown),
                    kms);

  return kms;
}

static gpointer
notify_probed_in_impl (MetaThreadImpl  *thread_impl,
                       gpointer         user_data,
                       GError         **error)
{
  meta_kms_impl_notify_probed (META_KMS_IMPL (thread_impl));
  return NULL;
}

void
meta_kms_notify_probed (MetaKms *kms)
{
  meta_thread_post_impl_task (META_THREAD (kms),
                              notify_probed_in_impl,
                              NULL, NULL, NULL, NULL);
}

static void
meta_kms_finalize (GObject *object)
{
  MetaKms *kms = META_KMS (object);
  MetaBackend *backend = meta_thread_get_backend (META_THREAD (kms));
  MetaUdev *udev = meta_backend_get_udev (backend);

  g_list_free_full (kms->devices, g_object_unref);

  g_clear_signal_handler (&kms->hotplug_handler_id, udev);
  g_clear_signal_handler (&kms->lease_handler_id, udev);
  g_clear_signal_handler (&kms->removed_handler_id, udev);

  G_OBJECT_CLASS (meta_kms_parent_class)->finalize (object);
}

static void
meta_kms_init (MetaKms *kms)
{
  kms->cursor_manager = meta_kms_cursor_manager_new (kms);
}

static void
meta_kms_class_init (MetaKmsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaThreadClass *thread_class = META_THREAD_CLASS (klass);

  object_class->finalize = meta_kms_finalize;

  signals[RESOURCES_CHANGED] =
    g_signal_new ("resources-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  META_TYPE_KMS_RESOURCE_CHANGES);

  signals[DEVICE_ADDED] =
    g_signal_new ("device-added",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  META_TYPE_KMS_DEVICE);

  signals[LEASE_CHANGED] =
    g_signal_new ("lease-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  meta_thread_class_register_impl_type (thread_class, META_TYPE_KMS_IMPL);
}

void
meta_kms_emit_resources_changed (MetaKms                *kms,
                                 MetaKmsResourceChanges  changes)
{
  g_signal_emit (kms, signals[RESOURCES_CHANGED], 0, changes);
}

void
meta_kms_inhibit_kernel_thread (MetaKms *kms)
{
  kms->kernel_thread_inhibit_count++;

  if (kms->kernel_thread_inhibit_count == 1)
    meta_thread_reset_thread_type (META_THREAD (kms), META_THREAD_TYPE_USER);
}

void
meta_kms_uninhibit_kernel_thread (MetaKms *kms)
{
  g_return_if_fail (kms->kernel_thread_inhibit_count > 0);

  kms->kernel_thread_inhibit_count--;

  if (kms->kernel_thread_inhibit_count == 0)
    meta_thread_reset_thread_type (META_THREAD (kms), META_THREAD_TYPE_KERNEL);
}

MetaKmsCursorManager *
meta_kms_get_cursor_manager (MetaKms *kms)
{
  return kms->cursor_manager;
}
