/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "config.h"

#include "backends/native/meta-kms-capture-files.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <sys/ioctl.h>
#include <xf86drm.h>

#include "backends/native/meta-drm-capture.h"

G_STATIC_ASSERT (sizeof (struct drm_capture_grant_files) == 8);
G_STATIC_ASSERT (sizeof (struct drm_mode_create_capture_grant) == 32);
G_STATIC_ASSERT (G_STRUCT_OFFSET (struct drm_mode_create_capture_grant, files) == 8);

struct _MetaKmsCaptureFiles
{
  int capture_fd;
  int control_fd;
};

static gboolean
validate_cloexec (int       fd,
                  GError  **error)
{
  int flags = fcntl (fd, F_GETFD);

  if (flags < 0)
    {
      int saved_errno = errno;

      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved_errno),
                   "Querying capture descriptor flags: %s",
                   g_strerror (saved_errno));
      return FALSE;
    }

  if (!(flags & FD_CLOEXEC))
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                           "Capture descriptor is not close-on-exec");
      return FALSE;
    }

  return TRUE;
}

MetaKmsCaptureFiles *
meta_kms_capture_files_create (int       drm_fd,
                               uint32_t  crtc_id,
                               uint32_t  connector_id,
                               GError  **error)
{
  struct drm_capture_grant_files output = { -1, -1 };
  struct drm_mode_create_capture_grant request = {
    .crtc_id = crtc_id,
    .connector_id = connector_id,
    .files = (uintptr_t) &output,
  };
  uint64_t supported = 0;
  g_autoptr (MetaKmsCaptureFiles) files = NULL;

  if (drm_fd < 0 || crtc_id == 0 || connector_id == 0)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                           "Capture requires a DRM file and nonzero output IDs");
      return NULL;
    }

  if (drmGetCap (drm_fd, DRM_CAP_CAPTURE_GRANT, &supported) != 0)
    {
      int saved_errno = errno;

      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved_errno),
                   "Querying capture support: %s", g_strerror (saved_errno));
      return NULL;
    }

  if (!supported)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                           "DRM device does not support capture grants");
      return NULL;
    }

  /* Failure may modify output memory without installing either descriptor. */
  if (ioctl (drm_fd, DRM_IOCTL_MODE_CREATE_CAPTURE_GRANT, &request) < 0)
    {
      int saved_errno = errno;

      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved_errno),
                   "Creating capture grant: %s", g_strerror (saved_errno));
      return NULL;
    }

  files = g_new0 (MetaKmsCaptureFiles, 1);
  files->capture_fd = output.capture_fd;
  files->control_fd = output.control_fd;

  if (files->capture_fd < 0 || files->control_fd < 0 ||
      files->capture_fd == files->control_fd)
    {
      if (files->capture_fd == files->control_fd)
        files->capture_fd = -1;
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                           "Capture grant returned invalid descriptors");
      return NULL;
    }

  if (!validate_cloexec (files->capture_fd, error) ||
      !validate_cloexec (files->control_fd, error))
    return NULL;

  return g_steal_pointer (&files);
}

void
meta_kms_capture_files_free (MetaKmsCaptureFiles *files)
{
  g_clear_fd (&files->control_fd, NULL);
  g_clear_fd (&files->capture_fd, NULL);
  g_free (files);
}

int
meta_kms_capture_files_steal_capture_fd (MetaKmsCaptureFiles *files)
{
  return g_steal_fd (&files->capture_fd);
}

int
meta_kms_capture_files_get_control_fd (MetaKmsCaptureFiles *files)
{
  return files->control_fd;
}
