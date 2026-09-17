/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "config.h"

#include "backends/native/meta-kms-renderer-control.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <xf86drm.h>

#include "backends/native/meta-drm-castkms.h"
#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-impl-device.h"
#include "backends/native/meta-kms-private.h"

G_STATIC_ASSERT (sizeof (struct drm_castkms_renderer_files) == 8);
G_STATIC_ASSERT (sizeof (struct drm_castkms_create_renderer_control) == 32);
G_STATIC_ASSERT (sizeof (struct drm_castkms_renderer_query) == 32);

struct _MetaKmsRendererControl
{
  int renderer_fd;
  int revoke_fd;
};

typedef struct
{
  MetaKmsDevice *device;
  uint32_t crtc_id;
  uint32_t connector_id;
  MetaKmsRendererControl *control;
} CreateData;

static gboolean
set_errno_error (GError      **error,
                 int           saved_errno,
                 const char   *operation)
{
  g_set_error (error,
               G_IO_ERROR,
               g_io_error_from_errno (saved_errno),
               "%s: %s",
               operation,
               g_strerror (saved_errno));
  return FALSE;
}

static int
ioctl_nointr (int            fd,
              unsigned long  command,
              void          *request)
{
  int ret;

  do
    ret = ioctl (fd, command, request);
  while (ret == -1 && errno == EINTR);

  return ret;
}

static gboolean
validate_descriptor (int          fd,
                     const char  *description,
                     GError     **error)
{
  int flags;

  flags = fcntl (fd, F_GETFD);
  if (flags == -1)
    return set_errno_error (error, errno, description);
  if (!(flags & FD_CLOEXEC))
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_DATA,
                   "CastKMS returned an inheritable %s",
                   description);
      return FALSE;
    }

  return TRUE;
}

static gboolean
validate_contract (int      fd,
                   GError **error)
{
  struct drm_castkms_renderer_query query = {};

  if (ioctl_nointr (fd, DRM_IOCTL_CASTKMS_RENDERER_QUERY, &query) == -1)
    return set_errno_error (error, errno, "Querying CastKMS renderer control");
  if (query.version != DRM_CASTKMS_RENDERER_VERSION ||
      query.state != DRM_CASTKMS_RENDERER_STATE_EMPTY ||
      query.constraints_id != 0 ||
      query.reserved[0] != 0 ||
      query.reserved[1] != 0)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_SUPPORTED,
                           "CastKMS returned an unsupported renderer-control "
                           "contract");
      return FALSE;
    }

  return TRUE;
}

static gpointer
create_in_impl (MetaThreadImpl  *thread_impl,
                gpointer         user_data,
                GError         **error)
{
  CreateData *data = user_data;
  MetaKmsImplDevice *impl_device =
    meta_kms_device_get_impl_device (data->device);
  struct drm_castkms_renderer_files files = {
    .renderer_fd = -1,
    .revoke_fd = -1,
  };
  struct drm_castkms_create_renderer_control request = {
    .crtc_id = data->crtc_id,
    .connector_id = data->connector_id,
    .files = (uintptr_t) &files,
  };
  g_autoptr (MetaKmsRendererControl) control = NULL;

  if (g_strcmp0 (meta_kms_impl_device_get_driver_name (impl_device),
                 "castkms") != 0)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_SUPPORTED,
                           "DRM device is not driven by CastKMS");
      return GINT_TO_POINTER (FALSE);
    }

  if (!meta_kms_device_find_crtc_in_impl (data->device, data->crtc_id) ||
      !meta_kms_device_find_connector_in_impl (data->device,
                                               data->connector_id))
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_FOUND,
                           "Renderer output objects do not belong to the "
                           "device");
      return GINT_TO_POINTER (FALSE);
    }

  if (!meta_kms_impl_device_ensure_fd (impl_device, error))
    return GINT_TO_POINTER (FALSE);

  if (drmIoctl (meta_kms_impl_device_get_fd (impl_device),
                DRM_IOCTL_CASTKMS_CREATE_RENDERER_CONTROL,
                &request) == -1)
    {
      set_errno_error (error, errno, "Creating CastKMS renderer control");
      return GINT_TO_POINTER (FALSE);
    }

  control = g_new0 (MetaKmsRendererControl, 1);
  control->renderer_fd = files.renderer_fd;
  control->revoke_fd = files.revoke_fd;
  if (control->renderer_fd < 0 ||
      control->revoke_fd < 0 ||
      control->renderer_fd == control->revoke_fd)
    {
      if (control->renderer_fd == control->revoke_fd)
        control->renderer_fd = -1;
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "CastKMS returned invalid renderer-control "
                           "descriptors");
      return GINT_TO_POINTER (FALSE);
    }

  if (!validate_descriptor (control->renderer_fd,
                            "renderer descriptor",
                            error) ||
      !validate_descriptor (control->revoke_fd,
                            "renderer revocation descriptor",
                            error) ||
      !validate_contract (control->renderer_fd, error))
    return GINT_TO_POINTER (FALSE);

  data->control = g_steal_pointer (&control);
  return GINT_TO_POINTER (TRUE);
}

MetaKmsRendererControl *
meta_kms_renderer_control_new (MetaKmsDevice  *device,
                               uint32_t        crtc_id,
                               uint32_t        connector_id,
                               GError        **error)
{
  CreateData data = {
    .device = device,
    .crtc_id = crtc_id,
    .connector_id = connector_id,
  };

  g_return_val_if_fail (META_IS_KMS_DEVICE (device), NULL);

  if (!crtc_id || !connector_id)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_ARGUMENT,
                           "Renderer control requires nonzero output object "
                           "IDs");
      return NULL;
    }

  if (!meta_kms_run_impl_task_sync (meta_kms_device_get_kms (device),
                                    create_in_impl,
                                    &data,
                                    error))
    return NULL;

  return data.control;
}

void
meta_kms_renderer_control_free (MetaKmsRendererControl *control)
{
  g_clear_fd (&control->revoke_fd, NULL);
  g_clear_fd (&control->renderer_fd, NULL);
  g_free (control);
}

int
meta_kms_renderer_control_steal_fd (MetaKmsRendererControl *control)
{
  return g_steal_fd (&control->renderer_fd);
}
