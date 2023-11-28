/*
 * Copyright (C) 2019 Red Hat
 * Copyright (C) 2019 DisplayLink (UK) Ltd.
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

#include "backends/native/meta-kms-connector.h"
#include "backends/native/meta-kms-crtc.h"
#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-device.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <xf86drm.h>

#include "backends/native/meta-backend-native-private.h"
#include "backends/native/meta-device-pool.h"
#include "backends/native/meta-kms-impl-device-atomic.h"
#include "backends/native/meta-kms-impl-device-dummy.h"
#include "backends/native/meta-kms-impl-device-simple.h"
#include "backends/native/meta-kms-impl-device.h"
#include "backends/native/meta-kms-impl.h"
#include "backends/native/meta-kms-plane.h"
#include "backends/native/meta-kms-private.h"
#include "backends/native/meta-kms-update-private.h"

enum
{
  CRTC_NEEDS_FLUSH,

  N_SIGNALS
};

static int signals[N_SIGNALS];

struct _MetaKmsDevice
{
  GObject parent;

  MetaKms *kms;

  MetaKmsImplDevice *impl_device;

  MetaKmsDeviceFlag flags;
  char *path;
  char *driver_name;
  char *driver_description;

  GList *crtcs;
  GList *connectors;
  GList *planes;

  MetaKmsDeviceCaps caps;

  GList *fallback_modes;

  GHashTable *needs_flush_crtcs;
  GMutex needs_flush_mutex;
};

G_DEFINE_TYPE (MetaKmsDevice, meta_kms_device, G_TYPE_OBJECT);

MetaKms *
meta_kms_device_get_kms (MetaKmsDevice *device)
{
  return device->kms;
}

MetaKmsImplDevice *
meta_kms_device_get_impl_device (MetaKmsDevice *device)
{
  return device->impl_device;
}

const char *
meta_kms_device_get_path (MetaKmsDevice *device)
{
  return device->path;
}

const char *
meta_kms_device_get_driver_name (MetaKmsDevice *device)
{
  return device->driver_name;
}

const char *
meta_kms_device_get_driver_description (MetaKmsDevice *device)
{
  return device->driver_description;
}

MetaKmsDeviceFlag
meta_kms_device_get_flags (MetaKmsDevice *device)
{
  return device->flags;
}

gboolean
meta_kms_device_get_cursor_size (MetaKmsDevice *device,
                                 uint64_t      *out_cursor_width,
                                 uint64_t      *out_cursor_height)
{
  if (device->caps.has_cursor_size)
    {
      *out_cursor_width = device->caps.cursor_width;
      *out_cursor_height = device->caps.cursor_height;
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

gboolean
meta_kms_device_prefers_shadow_buffer (MetaKmsDevice *device)
{
  return device->caps.prefers_shadow_buffer;
}

gboolean
meta_kms_device_uses_monotonic_clock (MetaKmsDevice *device)
{
  return device->caps.uses_monotonic_clock;
}

GList *
meta_kms_device_get_connectors (MetaKmsDevice *device)
{
  return device->connectors;
}

MetaKmsCrtc *
meta_kms_device_find_crtc_in_impl (MetaKmsDevice *device,
                                   uint32_t       crtc_id)
{
  MetaKmsImplDevice *impl_device = meta_kms_device_get_impl_device (device);
  GList *l;

  meta_assert_in_kms_impl (device->kms);
  meta_assert_is_waiting_for_kms_impl_task (device->kms);

  for (l = meta_kms_impl_device_peek_crtcs (impl_device); l; l = l->next)
    {
      MetaKmsCrtc *crtc = META_KMS_CRTC (l->data);

      if (meta_kms_crtc_get_id (crtc) == crtc_id)
        return crtc;
    }

  return NULL;
}

MetaKmsConnector *
meta_kms_device_find_connector_in_impl (MetaKmsDevice *device,
                                        uint32_t       connector_id)
{
  MetaKmsImplDevice *impl_device = meta_kms_device_get_impl_device (device);
  GList *l;

  meta_assert_in_kms_impl (device->kms);
  meta_assert_is_waiting_for_kms_impl_task (device->kms);

  for (l = meta_kms_impl_device_peek_connectors (impl_device); l; l = l->next)
    {
      MetaKmsConnector *connector = META_KMS_CONNECTOR (l->data);

      if (meta_kms_connector_get_id (connector) == connector_id)
        return connector;
    }

  return NULL;
}

GList *
meta_kms_device_get_crtcs (MetaKmsDevice *device)
{
  return device->crtcs;
}

GList *
meta_kms_device_get_planes (MetaKmsDevice *device)
{
  return device->planes;
}

static gboolean
has_plane_with_type_for (MetaKmsDevice    *device,
                         MetaKmsCrtc      *crtc,
                         MetaKmsPlaneType  type)
{
  GList *l;

  for (l = meta_kms_device_get_planes (device); l; l = l->next)
    {
      MetaKmsPlane *plane = l->data;

      if (meta_kms_plane_get_plane_type (plane) != type)
        continue;

      if (meta_kms_plane_is_usable_with (plane, crtc))
        return TRUE;
    }

  return FALSE;
}

gboolean
meta_kms_device_has_cursor_plane_for (MetaKmsDevice*device,
                                      MetaKmsCrtc  *crtc)
{
  return has_plane_with_type_for (device, crtc, META_KMS_PLANE_TYPE_CURSOR);
}

GList *
meta_kms_device_get_fallback_modes (MetaKmsDevice *device)
{
  return device->fallback_modes;
}

static gpointer
disable_device_in_impl (MetaThreadImpl  *thread_impl,
                        gpointer         user_data,
                        GError         **error)
{
  MetaKmsImplDevice *impl_device = user_data;

  meta_kms_impl_device_disable (impl_device);

  return GINT_TO_POINTER (TRUE);
}

void
meta_kms_device_disable (MetaKmsDevice *device)
{
  meta_assert_not_in_kms_impl (device->kms);

  meta_kms_run_impl_task_sync (device->kms, disable_device_in_impl,
                               device->impl_device,
                               NULL);
}

MetaKmsResourceChanges
meta_kms_device_update_states_in_impl (MetaKmsDevice *device,
                                       uint32_t       crtc_id,
                                       uint32_t       connector_id)
{
  MetaKmsImplDevice *impl_device = meta_kms_device_get_impl_device (device);
  MetaKmsResourceChanges changes;

  meta_assert_in_kms_impl (device->kms);
  meta_assert_is_waiting_for_kms_impl_task (device->kms);

  changes = meta_kms_impl_device_update_states (impl_device, crtc_id,
                                                connector_id);

  if (changes == META_KMS_RESOURCE_CHANGE_NONE)
    return changes;

  g_list_free (device->crtcs);
  device->crtcs = meta_kms_impl_device_copy_crtcs (impl_device);

  g_list_free (device->connectors);
  device->connectors = meta_kms_impl_device_copy_connectors (impl_device);

  g_list_free (device->planes);
  device->planes = meta_kms_impl_device_copy_planes (impl_device);

  return changes;
}

typedef struct
{
  MetaKmsUpdate *update;
  MetaKmsUpdateFlag flags;
} PostUpdateData;

static gpointer
process_sync_update_in_impl (MetaThreadImpl  *thread_impl,
                             gpointer         user_data,
                             GError         **error)
{
  PostUpdateData *data = user_data;
  MetaKmsUpdate *update = data->update;
  MetaKmsDevice *device = meta_kms_update_get_device (update);
  MetaKmsImplDevice *impl_device = meta_kms_device_get_impl_device (device);

  return meta_kms_impl_device_process_update (impl_device, update, data->flags);
}

MetaKmsFeedback *
meta_kms_device_process_update_sync (MetaKmsDevice     *device,
                                     MetaKmsUpdate     *update,
                                     MetaKmsUpdateFlag  flags)
{
  MetaKms *kms = META_KMS (meta_kms_device_get_kms (device));
  PostUpdateData data;

  data = (PostUpdateData) {
    .update = update,
    .flags = flags,
  };
  return meta_kms_run_impl_task_sync (kms, process_sync_update_in_impl,
                                      &data, NULL);
}

static gpointer
process_async_update_in_impl (MetaThreadImpl  *thread_impl,
                              gpointer         user_data,
                              GError         **error)
{
  PostUpdateData *data = user_data;
  MetaKmsUpdate *update = data->update;
  MetaKmsDevice *device = meta_kms_update_get_device (update);
  MetaKmsImplDevice *impl_device = meta_kms_device_get_impl_device (device);

  meta_kms_impl_device_handle_update (impl_device, update, data->flags);

  return GINT_TO_POINTER (TRUE);
}

void
meta_kms_device_post_update (MetaKmsDevice       *device,
                             MetaKmsUpdate       *update,
                             MetaKmsUpdateFlag    flags)
{
  MetaKms *kms = META_KMS (meta_kms_device_get_kms (device));
  PostUpdateData *data;

  g_return_if_fail (meta_kms_update_get_device (update) == device);

  data = g_new0 (PostUpdateData, 1);
  *data = (PostUpdateData) {
    .update = update,
    .flags = flags,
  };

  meta_thread_post_impl_task (META_THREAD (kms),
                              process_async_update_in_impl,
                              data, g_free,
                              NULL, NULL);
}

static gpointer
await_flush_in_impl (MetaThreadImpl  *thread_impl,
                     gpointer         user_data,
                     GError         **error)
{
  MetaKmsCrtc *crtc = META_KMS_CRTC (user_data);
  MetaKmsDevice *device = meta_kms_crtc_get_device (crtc);
  MetaKmsImplDevice *impl_device = meta_kms_device_get_impl_device (device);

  meta_kms_impl_device_await_flush (impl_device, crtc);

  return NULL;
}

void
meta_kms_device_await_flush (MetaKmsDevice *device,
                             MetaKmsCrtc   *crtc)
{
  MetaKms *kms = meta_kms_device_get_kms (device);

  meta_thread_post_impl_task (META_THREAD (kms),
                              await_flush_in_impl,
                              crtc, NULL,
                              NULL, NULL);
}

static void
emit_crtc_needs_flush_in_main (MetaThread *thread,
                               gpointer    user_data)
{
  MetaKmsCrtc *crtc = user_data;

  g_signal_emit (meta_kms_crtc_get_device (crtc),
                 signals[CRTC_NEEDS_FLUSH], 0, crtc);
}

void
meta_kms_device_set_needs_flush (MetaKmsDevice *device,
                                 MetaKmsCrtc   *crtc)
{
  gboolean needs_flush;

  g_mutex_lock (&device->needs_flush_mutex);
  needs_flush = g_hash_table_add (device->needs_flush_crtcs, crtc);
  g_mutex_unlock (&device->needs_flush_mutex);

  if (needs_flush)
    {
      meta_kms_queue_callback (meta_kms_device_get_kms (device),
                               NULL, emit_crtc_needs_flush_in_main, crtc, NULL);
    }
}

gboolean
meta_kms_device_handle_flush (MetaKmsDevice *device,
                              MetaKmsCrtc   *crtc)
{
  gboolean needs_flush;

  g_mutex_lock (&device->needs_flush_mutex);
  needs_flush = g_hash_table_remove (device->needs_flush_crtcs, crtc);
  g_mutex_unlock (&device->needs_flush_mutex);

  return needs_flush;
}

void
meta_kms_device_add_fake_plane_in_impl (MetaKmsDevice    *device,
                                        MetaKmsPlaneType  plane_type,
                                        MetaKmsCrtc      *crtc)
{
  MetaKmsImplDevice *impl_device = device->impl_device;
  MetaKmsPlane *plane;

  meta_assert_in_kms_impl (device->kms);

  plane = meta_kms_impl_device_add_fake_plane (impl_device,
                                               plane_type,
                                               crtc);
  device->planes = g_list_append (device->planes, plane);
}

typedef struct _CreateImplDeviceData
{
  MetaKmsDevice *device;
  const char *path;
  MetaKmsDeviceFlag flags;

  MetaKmsImplDevice *out_impl_device;
  GList *out_crtcs;
  GList *out_connectors;
  GList *out_planes;
  MetaKmsDeviceCaps out_caps;
  GList *out_fallback_modes;
  char *out_driver_name;
  char *out_driver_description;
  char *out_path;
} CreateImplDeviceData;

static const char *
impl_device_type_to_string (GType type)
{
  if (type == META_TYPE_KMS_IMPL_DEVICE_ATOMIC)
    return "atomic modesetting";
  else if (type == META_TYPE_KMS_IMPL_DEVICE_SIMPLE)
    return "legacy modesetting";
  else if (type == META_TYPE_KMS_IMPL_DEVICE_DUMMY)
    return "no modesetting";
  g_assert_not_reached();
}

static MetaKmsImplDevice *
meta_create_kms_impl_device (MetaKmsDevice      *device,
                             MetaKmsImpl        *impl,
                             const char         *path,
                             MetaKmsDeviceFlag   flags,
                             GError            **error)
{
  meta_assert_in_kms_impl (meta_kms_impl_get_kms (impl));

  const char *env_kms_mode;
  enum {
    KMS_MODE_AUTO,
    KMS_MODE_ATOMIC,
    KMS_MODE_SIMPLE,
    KMS_MODE_HEADLESS,
  } kms_mode;

  meta_assert_in_kms_impl (meta_kms_impl_get_kms (impl));

  env_kms_mode = g_getenv ("MUTTER_DEBUG_FORCE_KMS_MODE");
  if (env_kms_mode)
    {
      if (g_strcmp0 (env_kms_mode, "auto") == 0)
        {
          kms_mode = KMS_MODE_AUTO;
        }
      else if (g_strcmp0 (env_kms_mode, "atomic") == 0)
        {
          kms_mode = KMS_MODE_ATOMIC;
        }
      else if (g_strcmp0 (env_kms_mode, "simple") == 0)
        {
          kms_mode = KMS_MODE_SIMPLE;
        }
      else if (g_strcmp0 (env_kms_mode, "headless") == 0)
        {
          kms_mode = KMS_MODE_HEADLESS;
        }
      else
        {
          g_warning ("Attempted to force invalid mode setting mode '%s",
                     env_kms_mode);
          kms_mode = KMS_MODE_AUTO;
        }
    }
  else
    {
      if (flags & META_KMS_DEVICE_FLAG_NO_MODE_SETTING)
        kms_mode = KMS_MODE_HEADLESS;
      else if (flags & META_KMS_DEVICE_FLAG_FORCE_LEGACY)
        kms_mode = KMS_MODE_SIMPLE;
      else
        kms_mode = KMS_MODE_AUTO;
    }

  if (kms_mode == KMS_MODE_AUTO)
    {
      GType impl_device_types[] = {
        META_TYPE_KMS_IMPL_DEVICE_ATOMIC,
        META_TYPE_KMS_IMPL_DEVICE_SIMPLE,
      };
      int i;

      for (i = 0; i < G_N_ELEMENTS (impl_device_types); i++)
        {
          MetaKmsImplDevice *impl_device;
          g_autoptr (GError) local_error = NULL;

          impl_device = g_initable_new (impl_device_types[i],
                                        NULL, &local_error,
                                        "device", device,
                                        "impl", impl,
                                        "path", path,
                                        "flags", flags,
                                        NULL);
          if (impl_device)
            return impl_device;

          if (local_error->domain != META_KMS_ERROR)
            {
              g_warning ("Failed to open %s backend: %s",
                         impl_device_type_to_string (impl_device_types[i]),
                         local_error->message);
            }
        }

      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No suitable mode setting backend found");

      return NULL;
    }
  else
    {
      GType type;

      switch (kms_mode)
        {
        case KMS_MODE_ATOMIC:
          type = META_TYPE_KMS_IMPL_DEVICE_ATOMIC;
          break;
        case KMS_MODE_SIMPLE:
          type = META_TYPE_KMS_IMPL_DEVICE_SIMPLE;
          break;
        case KMS_MODE_HEADLESS:
          type = META_TYPE_KMS_IMPL_DEVICE_DUMMY;
          break;
        default:
          g_assert_not_reached ();
        };

      return g_initable_new (type, NULL, error,
                             "device", device,
                             "impl", impl,
                             "path", path,
                             "flags", flags,
                             NULL);
    }
}

static gpointer
create_impl_device_in_impl (MetaThreadImpl  *thread_impl,
                            gpointer         user_data,
                            GError         **error)
{
  MetaKmsImpl *impl = META_KMS_IMPL (thread_impl);
  CreateImplDeviceData *data = user_data;
  MetaKmsImplDevice *impl_device;

  impl_device = meta_create_kms_impl_device (data->device,
                                             impl,
                                             data->path,
                                             data->flags,
                                             error);
  if (!impl_device)
    return FALSE;

  meta_kms_impl_add_impl_device (impl, impl_device);

  data->out_impl_device = impl_device;
  data->out_crtcs = meta_kms_impl_device_copy_crtcs (impl_device);
  data->out_connectors = meta_kms_impl_device_copy_connectors (impl_device);
  data->out_planes = meta_kms_impl_device_copy_planes (impl_device);
  data->out_caps = *meta_kms_impl_device_get_caps (impl_device);
  data->out_fallback_modes =
    meta_kms_impl_device_copy_fallback_modes (impl_device);
  data->out_driver_name =
    g_strdup (meta_kms_impl_device_get_driver_name (impl_device));
  data->out_driver_description =
    g_strdup (meta_kms_impl_device_get_driver_description (impl_device));
  data->out_path = g_strdup (meta_kms_impl_device_get_path (impl_device));

  return GINT_TO_POINTER (TRUE);
}

MetaKmsDevice *
meta_kms_device_new (MetaKms            *kms,
                     const char         *path,
                     MetaKmsDeviceFlag   flags,
                     GError            **error)
{
  MetaKmsDevice *device;
  CreateImplDeviceData data;

  device = g_object_new (META_TYPE_KMS_DEVICE, NULL);
  device->kms = kms;

  data = (CreateImplDeviceData) {
    .device = device,
    .path = path,
    .flags = flags,
  };
  if (!meta_kms_run_impl_task_sync (kms, create_impl_device_in_impl, &data,
                                    error))
    {
      g_object_unref (device);
      return NULL;
    }

  device->impl_device = data.out_impl_device;
  device->flags = flags;
  device->path = g_strdup (path);
  device->crtcs = data.out_crtcs;
  device->connectors = data.out_connectors;
  device->planes = data.out_planes;
  device->caps = data.out_caps;
  device->fallback_modes = data.out_fallback_modes;
  device->driver_name = data.out_driver_name;
  device->driver_description = data.out_driver_description;
  free (device->path);
  device->path = data.out_path;

  if (device->caps.addfb2_modifiers)
    device->flags |= META_KMS_DEVICE_FLAG_HAS_ADDFB2;

  return device;
}

static gpointer
free_impl_device_in_impl (MetaThreadImpl  *thread_impl,
                          gpointer         user_data,
                          GError         **error)
{
  MetaKmsImplDevice *impl_device = user_data;

  g_object_unref (impl_device);

  return GINT_TO_POINTER (TRUE);
}

static void
meta_kms_device_finalize (GObject *object)
{
  MetaKmsDevice *device = META_KMS_DEVICE (object);

  g_free (device->path);
  g_free (device->driver_name);
  g_free (device->driver_description);
  g_list_free (device->fallback_modes);
  g_list_free (device->crtcs);
  g_list_free (device->connectors);
  g_list_free (device->planes);

  if (device->impl_device)
    {
      meta_kms_run_impl_task_sync (device->kms, free_impl_device_in_impl,
                                   device->impl_device,
                                   NULL);
    }

  g_mutex_clear (&device->needs_flush_mutex);
  g_hash_table_unref (device->needs_flush_crtcs);

  G_OBJECT_CLASS (meta_kms_device_parent_class)->finalize (object);
}

static void
meta_kms_device_init (MetaKmsDevice *device)
{
  device->needs_flush_crtcs = g_hash_table_new (NULL, NULL);
  g_mutex_init (&device->needs_flush_mutex);
}

static void
meta_kms_device_class_init (MetaKmsDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_kms_device_finalize;

  signals[CRTC_NEEDS_FLUSH] =
    g_signal_new ("crtc-needs-flush",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  META_TYPE_KMS_CRTC);
}
