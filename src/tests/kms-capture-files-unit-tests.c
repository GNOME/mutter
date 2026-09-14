/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <stdarg.h>
#include <unistd.h>

#include "backends/native/meta-drm-capture.h"
#include "backends/native/meta-kms-capture-files.h"

typedef enum
{
  REPLY_VALID,
  REPLY_PARTIAL_FAILURE,
  REPLY_INTERRUPTED,
  REPLY_DUPLICATE,
  REPLY_MISSING_CAPTURE,
  REPLY_MISSING_CONTROL,
  REPLY_CAPTURE_INHERITABLE,
  REPLY_CONTROL_INHERITABLE,
} Reply;

static Reply reply;
static int issuer_fd;
static int issued_capture;
static int issued_control;
static unsigned int cap_calls;
static unsigned int create_calls;
static uint64_t capability;
static int capability_error;

int __wrap_drmGetCap (int fd, uint64_t name, uint64_t *value);
int __wrap_ioctl (int fd, unsigned long request, ...);

int
__wrap_drmGetCap (int        fd,
                  uint64_t   name,
                  uint64_t  *value)
{
  g_assert_cmpint (fd, ==, issuer_fd);
  g_assert_cmpuint (name, ==, DRM_CAP_CAPTURE_GRANT);
  cap_calls++;
  if (capability_error)
    {
      errno = capability_error;
      return -1;
    }
  *value = capability;
  return 0;
}

int
__wrap_ioctl (int            fd,
               unsigned long  command,
               ...)
{
  struct drm_mode_create_capture_grant *request;
  struct drm_capture_grant_files *output;
  va_list args;
  int pipe_fds[2];

  g_assert_cmpint (fd, ==, issuer_fd);
  g_assert_cmpuint (command, ==, DRM_IOCTL_MODE_CREATE_CAPTURE_GRANT);
  va_start (args, command);
  request = va_arg (args, struct drm_mode_create_capture_grant *);
  va_end (args);
  g_assert_cmpuint (request->crtc_id, ==, 7);
  g_assert_cmpuint (request->connector_id, ==, 11);
  g_assert_cmpuint (request->flags, ==, 0);
  for (unsigned int i = 0; i < G_N_ELEMENTS (request->reserved); i++)
    g_assert_cmpuint (request->reserved[i], ==, 0);
  output = (void *) (uintptr_t) request->files;
  g_assert_cmpint (output->capture_fd, ==, -1);
  g_assert_cmpint (output->control_fd, ==, -1);
  create_calls++;

  if (reply == REPLY_PARTIAL_FAILURE || reply == REPLY_INTERRUPTED)
    {
      /* Existing unrelated descriptors are not installed grant files. */
      output->capture_fd = issuer_fd;
      output->control_fd = issuer_fd;
      errno = reply == REPLY_INTERRUPTED ? EINTR : EFAULT;
      return -1;
    }

  g_assert_cmpint (pipe2 (pipe_fds, O_CLOEXEC), ==, 0);
  issued_capture = pipe_fds[0];
  issued_control = pipe_fds[1];
  output->capture_fd = issued_capture;
  output->control_fd = issued_control;
  switch (reply)
    {
    case REPLY_DUPLICATE:
      g_clear_fd (&issued_control, NULL);
      output->control_fd = issued_capture;
      break;
    case REPLY_MISSING_CAPTURE:
      g_clear_fd (&issued_capture, NULL);
      output->capture_fd = -1;
      break;
    case REPLY_MISSING_CONTROL:
      g_clear_fd (&issued_control, NULL);
      output->control_fd = -1;
      break;
    case REPLY_CAPTURE_INHERITABLE:
      g_assert_cmpint (fcntl (issued_capture, F_SETFD, 0), ==, 0);
      break;
    case REPLY_CONTROL_INHERITABLE:
      g_assert_cmpint (fcntl (issued_control, F_SETFD, 0), ==, 0);
      break;
    default:
      break;
    }
  return 0;
}

static void
begin_case (Reply selected_reply)
{
  issuer_fd = open ("/dev/null", O_RDONLY | O_CLOEXEC);
  g_assert_cmpint (issuer_fd, >=, 0);
  reply = selected_reply;
  cap_calls = create_calls = 0;
  issued_capture = issued_control = -1;
  capability = 1;
  capability_error = 0;
}

static void
assert_closed (int fd)
{
  if (fd < 0)
    return;
  errno = 0;
  g_assert_cmpint (fcntl (fd, F_GETFD), ==, -1);
  g_assert_cmpint (errno, ==, EBADF);
}

static void
end_case (void)
{
  /* No operation may consume the caller's DRM file. */
  g_assert_cmpint (fcntl (issuer_fd, F_GETFD), >=, 0);
  g_clear_fd (&issuer_fd, NULL);
}

static void
test_ownership (void)
{
  g_autoptr (GError) error = NULL;
  MetaKmsCaptureFiles *files;
  int capture_fd;

  begin_case (REPLY_VALID);
  files = meta_kms_capture_files_create (issuer_fd, 7, 11, &error);
  g_assert_no_error (error);
  g_assert_nonnull (files);
  g_assert_cmpuint (cap_calls, ==, 1);
  g_assert_cmpuint (create_calls, ==, 1);
  g_assert_cmpint (meta_kms_capture_files_get_control_fd (files),
                   ==, issued_control);
  capture_fd = meta_kms_capture_files_steal_capture_fd (files);
  g_assert_cmpint (capture_fd, ==, issued_capture);
  g_assert_cmpint (meta_kms_capture_files_steal_capture_fd (files), ==, -1);
  meta_kms_capture_files_free (files);
  assert_closed (issued_control);
  g_assert_cmpint (fcntl (capture_fd, F_GETFD), >=, 0);
  g_clear_fd (&capture_fd, NULL);
  end_case ();
}

static void
test_untransferred_capture (void)
{
  g_autoptr (GError) error = NULL;
  MetaKmsCaptureFiles *files;

  begin_case (REPLY_VALID);
  files = meta_kms_capture_files_create (issuer_fd, 7, 11, &error);
  g_assert_no_error (error);
  g_assert_nonnull (files);
  meta_kms_capture_files_free (files);
  assert_closed (issued_capture);
  assert_closed (issued_control);
  end_case ();
}

static void
test_failed_ioctl (void)
{
  const Reply failures[] = { REPLY_PARTIAL_FAILURE, REPLY_INTERRUPTED };

  for (unsigned int i = 0; i < G_N_ELEMENTS (failures); i++)
    {
      g_autoptr (GError) error = NULL;

      begin_case (failures[i]);
      g_assert_null (meta_kms_capture_files_create (issuer_fd, 7, 11, &error));
      g_assert_error (error, G_IO_ERROR,
                      (int) g_io_error_from_errno (i == 0 ? EFAULT : EINTR));
      g_assert_cmpuint (create_calls, ==, 1);
      end_case ();
    }
}

static void
test_invalid_reply (void)
{
  const Reply failures[] = {
    REPLY_DUPLICATE,
    REPLY_MISSING_CAPTURE,
    REPLY_MISSING_CONTROL,
    REPLY_CAPTURE_INHERITABLE,
    REPLY_CONTROL_INHERITABLE,
  };

  for (unsigned int i = 0; i < G_N_ELEMENTS (failures); i++)
    {
      g_autoptr (GError) error = NULL;

      begin_case (failures[i]);
      g_assert_null (meta_kms_capture_files_create (issuer_fd, 7, 11, &error));
      g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
      assert_closed (issued_capture);
      assert_closed (issued_control);
      end_case ();
    }
}

static void
test_unsupported (void)
{
  g_autoptr (GError) error = NULL;

  begin_case (REPLY_VALID);
  capability = 0;
  g_assert_null (meta_kms_capture_files_create (issuer_fd, 7, 11, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
  g_assert_cmpuint (create_calls, ==, 0);
  g_clear_error (&error);
  capability_error = EACCES;
  g_assert_null (meta_kms_capture_files_create (issuer_fd, 7, 11, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
  g_assert_cmpuint (create_calls, ==, 0);
  end_case ();
}

static void
test_invalid_target (void)
{
  g_autoptr (GError) error = NULL;

  begin_case (REPLY_VALID);
  g_assert_null (meta_kms_capture_files_create (issuer_fd, 0, 11, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_clear_error (&error);
  g_assert_null (meta_kms_capture_files_create (issuer_fd, 7, 0, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_clear_error (&error);
  g_assert_null (meta_kms_capture_files_create (-1, 7, 11, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_assert_cmpuint (cap_calls, ==, 0);
  g_assert_cmpuint (create_calls, ==, 0);
  end_case ();
}

static void
test_abi (void)
{
  g_assert_cmpuint (DRM_CAP_CAPTURE_GRANT, ==, 0x17);
  g_assert_cmpuint (sizeof (struct drm_capture_grant_files), ==, 8);
  g_assert_cmpuint (sizeof (struct drm_mode_create_capture_grant), ==, 32);
  g_assert_cmpuint (G_STRUCT_OFFSET (struct drm_mode_create_capture_grant,
                                     files), ==, 8);
  g_assert_cmpuint (_IOC_NR (DRM_IOCTL_MODE_CREATE_CAPTURE_GRANT), ==, 0xd4);
  g_assert_cmpuint (_IOC_SIZE (DRM_IOCTL_MODE_CREATE_CAPTURE_GRANT), ==, 32);
  g_assert_cmpuint (_IOC_DIR (DRM_IOCTL_MODE_CREATE_CAPTURE_GRANT), ==, _IOC_WRITE);
}

int
main (int    argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/kms/capture-files/ownership", test_ownership);
  g_test_add_func ("/kms/capture-files/untransferred", test_untransferred_capture);
  g_test_add_func ("/kms/capture-files/failed-ioctl", test_failed_ioctl);
  g_test_add_func ("/kms/capture-files/invalid-reply", test_invalid_reply);
  g_test_add_func ("/kms/capture-files/unsupported", test_unsupported);
  g_test_add_func ("/kms/capture-files/invalid-target", test_invalid_target);
  g_test_add_func ("/kms/capture-files/abi", test_abi);
  return g_test_run ();
}
