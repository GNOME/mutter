/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "config.h"

#include "backends/native/meta-kms-preparation.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <xf86drm.h>

#include "backends/native/meta-drm-preparation.h"

struct _MetaKmsPreparation
{
  int fd;
  GArray *crtc_ids;
};

MetaKmsPreparation *
meta_kms_preparation_create (int              drm_fd,
                             const uint32_t  *crtc_ids,
                             unsigned int     n_crtcs,
                             GError         **error)
{
  struct drm_mode_prepare_replace request = {
    .crtc_ids = (uintptr_t) crtc_ids,
    .count_crtcs = n_crtcs,
  };
  MetaKmsPreparation *preparation;
  int fd;

  fd = drmIoctl (drm_fd, DRM_IOCTL_MODE_PREPARE_REPLACE, &request);
  if (fd < 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Preparing display replacement: %s", g_strerror (errno));
      return NULL;
    }

  preparation = g_new0 (MetaKmsPreparation, 1);
  preparation->fd = fd;
  preparation->crtc_ids = g_array_sized_new (FALSE, FALSE, sizeof (uint32_t), n_crtcs);
  g_array_append_vals (preparation->crtc_ids, crtc_ids, n_crtcs);
  return preparation;
}

void
meta_kms_preparation_free (MetaKmsPreparation *preparation)
{
  g_clear_fd (&preparation->fd, NULL);
  g_array_unref (preparation->crtc_ids);
  g_free (preparation);
}

int
meta_kms_preparation_get_fd (MetaKmsPreparation *preparation)
{
  return preparation->fd;
}

const GArray *
meta_kms_preparation_get_crtc_ids (MetaKmsPreparation *preparation)
{
  return preparation->crtc_ids;
}

gboolean
meta_kms_preparation_is_pending (MetaKmsPreparation *preparation)
{
  GPollFD poll_fd = {
    .fd = preparation->fd,
    .events = G_IO_IN,
  };
  int ret;

  do
    ret = g_poll (&poll_fd, 1, 0);
  while (ret < 0 && errno == EINTR);

  /* Errors and terminal events proceed to the status check, not another wait. */
  return ret == 0;
}

gboolean
meta_kms_preparation_wait (MetaKmsPreparation  *preparation,
                           GError             **error)
{
  struct drm_prepare_query query;
  GPollFD poll_fd = {
    .fd = preparation->fd,
    .events = G_IO_IN,
  };

  for (;;)
    {
      if (drmIoctl (preparation->fd, DRM_IOCTL_PREPARE_QUERY, &query) < 0)
        {
          g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                       "Querying display preparation: %s", g_strerror (errno));
          return FALSE;
        }
      if (query.status == DRM_PREPARE_READY)
        return TRUE;
      if (query.status != DRM_PREPARE_PENDING)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Display preparation ended with status %u", query.status);
          return FALSE;
        }
      if (g_poll (&poll_fd, 1, -1) < 0 && errno != EINTR)
        {
          g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                       "Waiting for display preparation: %s", g_strerror (errno));
          return FALSE;
        }
    }
}
