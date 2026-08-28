/*
 * Copyright (C) 2026 Red Hat
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

#include "backends/native/meta-kms-cast-grant.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <xf86drmMode.h>

#include "backends/native/meta-kms-cast-uapi.h"
#include "backends/native/meta-kms-connector.h"
#include "backends/native/meta-kms-crtc.h"
#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-impl-device.h"
#include "backends/native/meta-kms-private.h"
#include "backends/native/meta-kms.h"

G_STATIC_ASSERT (sizeof (struct drm_castkms_capture_query_caps) == 40);
G_STATIC_ASSERT (sizeof (struct drm_castkms_create_grant) == 32);
G_STATIC_ASSERT (sizeof (struct drm_castkms_get_grant) == 32);
G_STATIC_ASSERT (G_STRUCT_OFFSET (struct drm_castkms_get_grant,
                                 output_index) == 20);

typedef struct _CreateGrantData
{
  MetaKmsDevice *kms_device;
  uint32_t connector_id;
  uint32_t rights;

  int holder_fd;
  int control_fd;
  MetaKmsCastGrantInfo info;
} CreateGrantData;

struct _MetaKmsCastGrant
{
  GObject parent;

  MetaKmsDevice *kms_device;
  MetaKmsCastGrantInfo info;
  int holder_fd;
  int control_fd;
  gboolean has_device_fd_hold;
};

G_DEFINE_TYPE (MetaKmsCastGrant, meta_kms_cast_grant, G_TYPE_OBJECT)

static int
ioctl_nointr (int            fd,
              unsigned long  request,
              void          *data)
{
  int ret;

  do
    ret = ioctl (fd, request, data);
  while (ret == -1 && errno == EINTR);

  return ret;
}

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

static gboolean
validate_cloexec (int          fd,
                  const char  *description,
                  GError     **error)
{
  int descriptor_flags;

  descriptor_flags = fcntl (fd, F_GETFD);
  if (descriptor_flags == -1)
    {
      int saved_errno = errno;
      g_autofree char *operation =
        g_strdup_printf ("Query CastKMS %s descriptor flags", description);

      return set_errno_error (error, saved_errno, operation);
    }

  if (!(descriptor_flags & FD_CLOEXEC))
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_DATA,
                   "CastKMS returned a %s without FD_CLOEXEC",
                   description);
      return FALSE;
    }

  return TRUE;
}

static gboolean
validate_holder_flags (int      holder_fd,
                       GError **error)
{
  int status_flags;

  if (!validate_cloexec (holder_fd, "grant fd", error))
    return FALSE;

  status_flags = fcntl (holder_fd, F_GETFL);
  if (status_flags == -1)
    return set_errno_error (error, errno, "Query CastKMS grant status flags");
  if (!(status_flags & O_NONBLOCK))
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "CastKMS returned a blocking grant fd");
      return FALSE;
    }

  return TRUE;
}

static gboolean
validate_control_fd (int      control_fd,
                     GError **error)
{
  struct pollfd poll_fd = {
    .fd = control_fd,
  };
  int ret;

  if (!validate_cloexec (control_fd, "grant control fd", error))
    return FALSE;

  do
    ret = poll (&poll_fd, 1, 0);
  while (ret == -1 && errno == EINTR);

  if (ret == -1)
    return set_errno_error (error, errno, "Poll CastKMS grant control fd");

  if (poll_fd.revents & (POLLHUP | POLLERR | POLLNVAL))
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "CastKMS returned an already-terminal grant "
                           "control fd");
      return FALSE;
    }

  return TRUE;
}

static gboolean
query_capture_uapi (MetaKmsImplDevice  *impl_device,
                    int                 fd,
                    uint16_t           *out_major,
                    uint16_t           *out_minor,
                    GError            **error)
{
  GList *crtcs = meta_kms_impl_device_peek_crtcs (impl_device);
  MetaKmsCrtc *crtc;
  struct drm_castkms_capture_query_caps query = {};

  if (!crtcs)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_SUPPORTED,
                           "CastKMS device has no capture CRTC");
      return FALSE;
    }

  crtc = crtcs->data;
  query.crtc_id = meta_kms_crtc_get_id (crtc);
  if (ioctl_nointr (fd, DRM_IOCTL_CASTKMS_CAPTURE_QUERY_CAPS, &query) == -1)
    return set_errno_error (error, errno, "Query CastKMS capture UAPI");

  if (query.uapi_major != DRM_CASTKMS_CAPTURE_UAPI_MAJOR ||
      query.uapi_minor < DRM_CASTKMS_CAPTURE_UAPI_MINOR ||
      query.uapi_major > UINT16_MAX ||
      query.uapi_minor > UINT16_MAX ||
      !(query.flags & DRM_CASTKMS_CAPTURE_CAP_GRANT_FD) ||
      !(query.flags & DRM_CASTKMS_CAPTURE_CAP_GRANT_CONTROL_FD) ||
      query.reserved != 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_NOT_SUPPORTED,
                   "Unsupported CastKMS capture UAPI %u.%u",
                   query.uapi_major,
                   query.uapi_minor);
      return FALSE;
    }

  *out_major = query.uapi_major;
  *out_minor = query.uapi_minor;
  return TRUE;
}

static gboolean
validate_holder_query (const struct drm_castkms_get_grant   *query,
                       uint32_t                              grant_id,
                       uint32_t                              connector_id,
                       uint32_t                              rights,
                       GError                              **error)
{
  if (query->grant_id != grant_id ||
      query->connector_id != connector_id ||
      query->rights != rights ||
      query->flags != 0 ||
      query->state > DRM_CASTKMS_GRANT_STATE_SUSPENDED_FOREIGN_CONTENT ||
      query->state == DRM_CASTKMS_GRANT_STATE_REVOKED ||
      query->reserved != 0)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "CastKMS returned inconsistent normal-grant "
                           "metadata");
      return FALSE;
    }

  return TRUE;
}

static gpointer
create_grant_in_impl (MetaThreadImpl  *thread_impl,
                      gpointer         user_data,
                      GError         **error)
{
  CreateGrantData *data = user_data;
  MetaKmsImplDevice *impl_device =
    meta_kms_device_get_impl_device (data->kms_device);
  MetaKmsConnector *connector;
  struct drm_castkms_create_grant create = {
    .connector_id = data->connector_id,
    .rights = data->rights,
    /* No creation flags: Mutter's current owner master creates the grant. */
    .flags = 0,
    .fd = -1,
    .control_fd = -1,
    .fd_flags = O_NONBLOCK,
  };
  struct drm_castkms_get_grant query = {};
  uint16_t uapi_major;
  uint16_t uapi_minor;
  int fd;
  int saved_errno;

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
    meta_kms_device_find_connector_in_impl (data->kms_device,
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

  /* Keep a disconnected secondary card open until the grant terminates. */
  meta_kms_impl_device_hold_fd (impl_device);
  fd = meta_kms_impl_device_get_fd (impl_device);

  if (!query_capture_uapi (impl_device, fd,
                           &uapi_major, &uapi_minor,
                           error))
    goto fail_unhold;

  if (ioctl_nointr (fd, DRM_IOCTL_CASTKMS_CREATE_GRANT, &create) == -1)
    {
      saved_errno = errno;
      set_errno_error (error, saved_errno, "Create normal CastKMS grant");
      goto fail_unhold;
    }

  if (create.fd < 0 ||
      create.control_fd < 0 ||
      create.fd == create.control_fd ||
      create.grant_id == 0 ||
      create.reserved != 0)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "CastKMS returned invalid grant descriptors");
      goto fail_grant;
    }

  if (!validate_holder_flags (create.fd, error))
    goto fail_grant;
  if (!validate_control_fd (create.control_fd, error))
    goto fail_grant;

  if (ioctl_nointr (create.fd, DRM_IOCTL_CASTKMS_GET_GRANT, &query) == -1)
    {
      saved_errno = errno;
      set_errno_error (error, saved_errno, "Verify normal CastKMS grant");
      goto fail_grant;
    }

  if (!validate_holder_query (&query,
                              create.grant_id,
                              data->connector_id,
                              data->rights,
                              error))
    goto fail_grant;

  data->holder_fd = create.fd;
  data->control_fd = create.control_fd;
  data->info = (MetaKmsCastGrantInfo) {
    .grant_id = query.grant_id,
    .connector_id = query.connector_id,
    .output_index = query.output_index,
    .rights = query.rights,
    .flags = query.flags,
    .initial_state = query.state,
    .capture_uapi_major = uapi_major,
    .capture_uapi_minor = uapi_minor,
  };

  return GINT_TO_POINTER (TRUE);

fail_grant:
  /* Closing the control endpoint revokes the grant. */
  if (create.fd == create.control_fd)
    create.fd = -1;
  g_clear_fd (&create.control_fd, NULL);
  g_clear_fd (&create.fd, NULL);
fail_unhold:
  meta_kms_impl_device_unhold_fd (impl_device);
  return GINT_TO_POINTER (FALSE);
}

static gpointer
release_device_fd_hold_in_impl (MetaThreadImpl  *thread_impl,
                                gpointer         user_data,
                                GError         **error)
{
  MetaKmsDevice *kms_device = user_data;
  MetaKmsImplDevice *impl_device =
    meta_kms_device_get_impl_device (kms_device);

  meta_kms_impl_device_unhold_fd (impl_device);

  return GINT_TO_POINTER (TRUE);
}

static gboolean
release_device_fd_hold (MetaKmsCastGrant  *grant,
                        GError           **error)
{
  MetaKms *kms;

  if (!grant->has_device_fd_hold)
    return TRUE;

  kms = meta_kms_device_get_kms (grant->kms_device);

  if (!meta_kms_run_impl_task_sync (kms,
                                    release_device_fd_hold_in_impl,
                                    grant->kms_device,
                                    error))
    return FALSE;

  grant->has_device_fd_hold = FALSE;
  return TRUE;
}

MetaKmsCastGrant *
meta_kms_cast_grant_new (MetaKmsDevice  *kms_device,
                         uint32_t        connector_id,
                         uint32_t        rights,
                         GError        **error)
{
  MetaKms *kms;
  CreateGrantData data = {
    .kms_device = kms_device,
    .connector_id = connector_id,
    .rights = rights,
    .holder_fd = -1,
    .control_fd = -1,
  };
  MetaKmsCastGrant *grant;

  g_return_val_if_fail (META_IS_KMS_DEVICE (kms_device), NULL);
  g_return_val_if_fail (connector_id != 0, NULL);
  g_return_val_if_fail (rights != 0, NULL);

  kms = meta_kms_device_get_kms (kms_device);

  if (!meta_kms_run_impl_task_sync (kms,
                                    create_grant_in_impl,
                                    &data,
                                    error))
    return NULL;

  grant = g_object_new (META_TYPE_KMS_CAST_GRANT, NULL);
  grant->kms_device = g_object_ref (kms_device);
  grant->info = data.info;
  grant->holder_fd = data.holder_fd;
  grant->control_fd = data.control_fd;
  grant->has_device_fd_hold = TRUE;

  return grant;
}

const MetaKmsCastGrantInfo *
meta_kms_cast_grant_get_info (MetaKmsCastGrant *grant)
{
  g_return_val_if_fail (META_IS_KMS_CAST_GRANT (grant), NULL);

  return &grant->info;
}

int
meta_kms_cast_grant_steal_holder_fd (MetaKmsCastGrant *grant)
{
  g_return_val_if_fail (META_IS_KMS_CAST_GRANT (grant), -1);

  return g_steal_fd (&grant->holder_fd);
}

int
meta_kms_cast_grant_get_control_fd (MetaKmsCastGrant *grant)
{
  g_return_val_if_fail (META_IS_KMS_CAST_GRANT (grant), -1);

  return grant->control_fd;
}

gboolean
meta_kms_cast_grant_revoke (MetaKmsCastGrant  *grant,
                            GError           **error)
{
  g_return_val_if_fail (META_IS_KMS_CAST_GRANT (grant), FALSE);

  /* The kernel interprets closing the control endpoint as revocation. */
  g_clear_fd (&grant->control_fd, NULL);

  return release_device_fd_hold (grant, error);
}

static void
meta_kms_cast_grant_dispose (GObject *object)
{
  MetaKmsCastGrant *grant = META_KMS_CAST_GRANT (object);

  if (grant->control_fd >= 0 || grant->has_device_fd_hold)
    {
      g_autoptr (GError) error = NULL;

      if (!meta_kms_cast_grant_revoke (grant, &error))
        {
          g_warning ("Failed to release the device fd for CastKMS grant %u: "
                     "%s",
                     grant->info.grant_id,
                     error ? error->message : "unknown error");

          /*
           * The control fd is already closed, so the grant is revoked. The
           * KMS implementation is no longer reachable and device teardown
           * must release its file; a disposed grant cannot retry the hold.
           */
          grant->has_device_fd_hold = FALSE;
        }
    }

  g_clear_object (&grant->kms_device);

  G_OBJECT_CLASS (meta_kms_cast_grant_parent_class)->dispose (object);
}

static void
meta_kms_cast_grant_finalize (GObject *object)
{
  MetaKmsCastGrant *grant = META_KMS_CAST_GRANT (object);

  g_clear_fd (&grant->control_fd, NULL);
  g_clear_fd (&grant->holder_fd, NULL);

  G_OBJECT_CLASS (meta_kms_cast_grant_parent_class)->finalize (object);
}

static void
meta_kms_cast_grant_class_init (MetaKmsCastGrantClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_kms_cast_grant_dispose;
  object_class->finalize = meta_kms_cast_grant_finalize;
}

static void
meta_kms_cast_grant_init (MetaKmsCastGrant *grant)
{
  grant->holder_fd = -1;
  grant->control_fd = -1;
}
