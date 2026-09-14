/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "config.h"

#include "backends/native/meta-kms-capture-grant.h"

#include "backends/native/meta-kms-capture-files.h"
#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-impl-device.h"
#include "backends/native/meta-kms-private.h"

struct _MetaKmsCaptureGrant
{
  GObject parent;

  MetaKmsDevice *device;
  MetaKmsCaptureFiles *files;
  gboolean has_file_hold;
};

typedef struct
{
  MetaKmsDevice *device;
  uint32_t crtc_id;
  uint32_t connector_id;
  MetaKmsCaptureFiles *files;
} CreateData;

G_DEFINE_TYPE (MetaKmsCaptureGrant, meta_kms_capture_grant, G_TYPE_OBJECT)

static gpointer
create_in_impl (MetaThreadImpl  *thread_impl,
                gpointer         user_data,
                GError         **error)
{
  CreateData *data = user_data;
  MetaKmsImplDevice *impl_device = meta_kms_device_get_impl_device (data->device);

  if (!meta_kms_device_find_crtc_in_impl (data->device, data->crtc_id) ||
      !meta_kms_device_find_connector_in_impl (data->device, data->connector_id))
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                           "Capture output objects do not belong to the device");
      return GINT_TO_POINTER (FALSE);
    }

  if (!meta_kms_impl_device_ensure_fd (impl_device, error))
    return GINT_TO_POINTER (FALSE);

  meta_kms_impl_device_hold_fd (impl_device);
  data->files = meta_kms_capture_files_create (
    meta_kms_impl_device_get_fd (impl_device), data->crtc_id,
    data->connector_id, error);
  if (!data->files)
    {
      meta_kms_impl_device_unhold_fd (impl_device);
      return GINT_TO_POINTER (FALSE);
    }

  return GINT_TO_POINTER (TRUE);
}

static gpointer
release_in_impl (MetaThreadImpl  *thread_impl,
                 gpointer         user_data,
                 GError         **error)
{
  MetaKmsDevice *device = user_data;

  meta_kms_impl_device_unhold_fd (meta_kms_device_get_impl_device (device));
  return GINT_TO_POINTER (TRUE);
}

MetaKmsCaptureGrant *
meta_kms_capture_grant_new (MetaKmsDevice  *device,
                            uint32_t        crtc_id,
                            uint32_t        connector_id,
                            GError        **error)
{
  CreateData data = {
    .device = device,
    .crtc_id = crtc_id,
    .connector_id = connector_id,
  };
  MetaKmsCaptureGrant *grant;

  g_return_val_if_fail (META_IS_KMS_DEVICE (device), NULL);

  if (!crtc_id || !connector_id)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                           "Capture requires nonzero output object IDs");
      return NULL;
    }

  if (!meta_kms_run_impl_task_sync (meta_kms_device_get_kms (device),
                                    create_in_impl, &data, error))
    return NULL;

  grant = g_object_new (META_TYPE_KMS_CAPTURE_GRANT, NULL);
  grant->device = g_object_ref (device);
  grant->files = data.files;
  grant->has_file_hold = TRUE;
  return grant;
}

int
meta_kms_capture_grant_steal_capture_fd (MetaKmsCaptureGrant *grant)
{
  g_return_val_if_fail (META_IS_KMS_CAPTURE_GRANT (grant), -1);

  return grant->files ?
    meta_kms_capture_files_steal_capture_fd (grant->files) : -1;
}

int
meta_kms_capture_grant_get_control_fd (MetaKmsCaptureGrant *grant)
{
  g_return_val_if_fail (META_IS_KMS_CAPTURE_GRANT (grant), -1);

  return grant->files ? meta_kms_capture_files_get_control_fd (grant->files) : -1;
}

gboolean
meta_kms_capture_grant_revoke (MetaKmsCaptureGrant  *grant,
                              GError              **error)
{
  g_return_val_if_fail (META_IS_KMS_CAPTURE_GRANT (grant), FALSE);

  g_clear_pointer (&grant->files, meta_kms_capture_files_free);
  if (!grant->has_file_hold)
    return TRUE;

  if (!meta_kms_run_impl_task_sync (meta_kms_device_get_kms (grant->device),
                                    release_in_impl, grant->device, error))
    return FALSE;

  grant->has_file_hold = FALSE;
  return TRUE;
}

static void
meta_kms_capture_grant_dispose (GObject *object)
{
  MetaKmsCaptureGrant *grant = META_KMS_CAPTURE_GRANT (object);
  g_autoptr (GError) error = NULL;

  if (!meta_kms_capture_grant_revoke (grant, &error))
    {
      g_warning ("Failed to release capture grant's DRM file hold: %s",
                 error ? error->message : "unknown error");
      /* Authority is revoked; device teardown must release its file. */
      grant->has_file_hold = FALSE;
    }
  g_clear_object (&grant->device);
  G_OBJECT_CLASS (meta_kms_capture_grant_parent_class)->dispose (object);
}

static void
meta_kms_capture_grant_class_init (MetaKmsCaptureGrantClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = meta_kms_capture_grant_dispose;
}

static void
meta_kms_capture_grant_init (MetaKmsCaptureGrant *grant)
{
}
