/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "config.h"

#include "backends/native/meta-kms-monitor-control.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <xf86drm.h>

#include "backends/native/meta-drm-castkms.h"
#include "backends/native/meta-kms-connector.h"
#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-impl-device.h"
#include "backends/native/meta-kms-private.h"

G_STATIC_ASSERT (sizeof (struct drm_castkms_create_monitor_control) == 20);
G_STATIC_ASSERT (sizeof (struct drm_castkms_monitor_query) == 16);

struct _MetaKmsMonitorControl
{
  int control_fd;
  int revoke_fd;
};

typedef struct
{
  MetaKmsDevice *device;
  uint32_t connector_id;
  MetaKmsMonitorControl *control;
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
  struct drm_castkms_monitor_query query = {};

  if (ioctl_nointr (fd, DRM_IOCTL_CASTKMS_MONITOR_QUERY, &query) == -1)
    return set_errno_error (error, errno,
                            "Querying CastKMS monitor control");
  if (query.version != DRM_CASTKMS_MONITOR_CONTROL_VERSION ||
      query.flags != 0 ||
      query.max_edid_size < 128 ||
      query.max_edid_size % 128 != 0 ||
      query.reserved != 0)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_SUPPORTED,
                           "CastKMS returned an unsupported monitor-control "
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
  MetaKmsConnector *connector;
  struct drm_castkms_create_monitor_control request = {
    .connector_id = data->connector_id,
    .control_fd = -1,
    .revoke_fd = -1,
  };
  g_autoptr (MetaKmsMonitorControl) control = NULL;

  if (g_strcmp0 (meta_kms_impl_device_get_driver_name (impl_device),
                 "castkms") != 0)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_SUPPORTED,
                           "DRM device is not driven by CastKMS");
      return GINT_TO_POINTER (FALSE);
    }

  connector =
    meta_kms_device_find_connector_in_impl (data->device,
                                            data->connector_id);
  if (!connector ||
      meta_kms_connector_get_connector_type (connector) !=
      DRM_MODE_CONNECTOR_VIRTUAL)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_NOT_FOUND,
                   "CastKMS virtual connector %u does not exist",
                   data->connector_id);
      return GINT_TO_POINTER (FALSE);
    }

  if (!meta_kms_impl_device_ensure_fd (impl_device, error))
    return GINT_TO_POINTER (FALSE);

  if (drmIoctl (meta_kms_impl_device_get_fd (impl_device),
                DRM_IOCTL_CASTKMS_CREATE_MONITOR_CONTROL,
                &request) == -1)
    {
      set_errno_error (error, errno, "Creating CastKMS monitor control");
      return GINT_TO_POINTER (FALSE);
    }

  control = g_new0 (MetaKmsMonitorControl, 1);
  control->control_fd = request.control_fd;
  control->revoke_fd = request.revoke_fd;
  if (control->control_fd < 0 ||
      control->revoke_fd < 0 ||
      control->control_fd == control->revoke_fd)
    {
      if (control->control_fd == control->revoke_fd)
        control->control_fd = -1;
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "CastKMS returned invalid monitor-control "
                           "descriptors");
      return GINT_TO_POINTER (FALSE);
    }

  if (!validate_descriptor (control->control_fd,
                            "monitor-control descriptor",
                            error) ||
      !validate_descriptor (control->revoke_fd,
                            "monitor revocation descriptor",
                            error) ||
      !validate_contract (control->control_fd, error))
    return GINT_TO_POINTER (FALSE);

  data->control = g_steal_pointer (&control);
  return GINT_TO_POINTER (TRUE);
}

MetaKmsMonitorControl *
meta_kms_monitor_control_new (MetaKmsDevice  *device,
                              uint32_t        connector_id,
                              GError        **error)
{
  CreateData data = {
    .device = device,
    .connector_id = connector_id,
  };

  g_return_val_if_fail (META_IS_KMS_DEVICE (device), NULL);

  if (!connector_id)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_ARGUMENT,
                           "Monitor control requires a connector");
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
meta_kms_monitor_control_free (MetaKmsMonitorControl *control)
{
  g_clear_fd (&control->revoke_fd, NULL);
  g_clear_fd (&control->control_fd, NULL);
  g_free (control);
}

int
meta_kms_monitor_control_steal_fd (MetaKmsMonitorControl *control)
{
  return g_steal_fd (&control->control_fd);
}
