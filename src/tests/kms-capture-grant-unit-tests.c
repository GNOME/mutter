/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <unistd.h>

#include "backends/native/meta-kms-capture-files.h"
#include "backends/native/meta-kms-capture-grant.h"
#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-impl-device.h"
#include "backends/native/meta-kms-private.h"

/* Substitute the KMS thread/device boundary, not the grant being tested. */
static gboolean in_impl;
static gboolean fail_dispatch;
static gboolean fail_open;
static gboolean fail_create;
static gboolean have_crtc;
static gboolean have_connector;
static unsigned int holds;
static unsigned int releases;
static unsigned int tasks;
static unsigned int revocations;
static int control_fd;
static GObject *device_object;

struct _MetaKmsCaptureFiles
{
  int capture_fd;
  int control_fd;
};

GType
meta_kms_device_get_type (void)
{
  return G_TYPE_OBJECT;
}

MetaKms *
meta_kms_device_get_kms (MetaKmsDevice *device)
{
  g_assert_true ((GObject *) device == device_object);
  return (MetaKms *) device;
}

MetaKmsImplDevice *
meta_kms_device_get_impl_device (MetaKmsDevice *device)
{
  g_assert_true (in_impl);
  return (MetaKmsImplDevice *) device;
}

gpointer
meta_kms_run_impl_task_sync (MetaKms            *kms,
                             MetaThreadTaskFunc  func,
                             gpointer            data,
                             GError            **error)
{
  gpointer result;

  g_assert_false (in_impl);
  tasks++;
  if (fail_dispatch)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_CLOSED, "Thread closed");
      return NULL;
    }
  in_impl = TRUE;
  result = func (NULL, data, error);
  in_impl = FALSE;
  return result;
}

MetaKmsCrtc *
meta_kms_device_find_crtc_in_impl (MetaKmsDevice *device,
                                  uint32_t       id)
{
  g_assert_true (in_impl);
  g_assert_cmpuint (id, ==, 7);
  return have_crtc ? (MetaKmsCrtc *) device : NULL;
}

MetaKmsConnector *
meta_kms_device_find_connector_in_impl (MetaKmsDevice *device,
                                       uint32_t       id)
{
  g_assert_true (in_impl);
  g_assert_cmpuint (id, ==, 11);
  return have_connector ? (MetaKmsConnector *) device : NULL;
}

gboolean
meta_kms_impl_device_ensure_fd (MetaKmsImplDevice  *device,
                                GError            **error)
{
  g_assert_true (in_impl);
  if (fail_open)
    g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                         "Cannot open device");
  return !fail_open;
}

void
meta_kms_impl_device_hold_fd (MetaKmsImplDevice *device)
{
  g_assert_true (in_impl);
  holds++;
}

void
meta_kms_impl_device_unhold_fd (MetaKmsImplDevice *device)
{
  g_assert_true (in_impl);
  g_assert_cmpuint (holds, ==, 1);
  if (control_fd >= 0)
    {
      g_assert_cmpint (fcntl (control_fd, F_GETFD), ==, -1);
      g_assert_cmpint (errno, ==, EBADF);
    }
  holds--;
  releases++;
}

int
meta_kms_impl_device_get_fd (MetaKmsImplDevice *device)
{
  g_assert_true (in_impl);
  g_assert_cmpuint (holds, ==, 1);
  return 42;
}

MetaKmsCaptureFiles *
meta_kms_capture_files_create (int       fd,
                               uint32_t  crtc_id,
                               uint32_t  connector_id,
                               GError  **error)
{
  MetaKmsCaptureFiles *files;
  int fds[2];

  g_assert_true (in_impl);
  g_assert_cmpuint (holds, ==, 1);
  g_assert_cmpint (fd, ==, 42);
  g_assert_cmpuint (crtc_id, ==, 7);
  g_assert_cmpuint (connector_id, ==, 11);
  if (fail_create)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                           "Capture unsupported");
      return NULL;
    }
  g_assert_cmpint (pipe2 (fds, O_CLOEXEC), ==, 0);
  files = g_new0 (MetaKmsCaptureFiles, 1);
  files->capture_fd = fds[0];
  files->control_fd = control_fd = fds[1];
  return files;
}

void
meta_kms_capture_files_free (MetaKmsCaptureFiles *files)
{
  g_assert_false (in_impl);
  g_assert_cmpuint (holds, ==, 1);
  revocations++;
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

static void
begin_case (void)
{
  in_impl = fail_dispatch = fail_open = fail_create = FALSE;
  have_crtc = have_connector = TRUE;
  holds = releases = tasks = revocations = 0;
  control_fd = -1;
  device_object = g_object_new (G_TYPE_OBJECT, NULL);
}

static MetaKmsCaptureGrant *
create_grant (GError **error)
{
  return meta_kms_capture_grant_new ((MetaKmsDevice *) device_object, 7, 11, error);
}

static void
test_lifetime (void)
{
  g_autoptr (GError) error = NULL;
  MetaKmsCaptureGrant *grant;
  gpointer weak_device;
  int capture_fd;

  begin_case ();
  weak_device = device_object;
  g_object_add_weak_pointer (device_object, &weak_device);
  grant = create_grant (&error);
  g_assert_no_error (error);
  g_assert_nonnull (grant);
  g_object_unref (device_object);
  g_assert_nonnull (weak_device);
  g_assert_cmpuint (holds, ==, 1);
  g_assert_cmpint (meta_kms_capture_grant_get_control_fd (grant), ==, control_fd);
  capture_fd = meta_kms_capture_grant_steal_capture_fd (grant);
  g_assert_cmpint (capture_fd, >=, 0);
  g_assert_true (meta_kms_capture_grant_revoke (grant, &error));
  g_assert_true (meta_kms_capture_grant_revoke (grant, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (holds, ==, 0);
  g_assert_cmpuint (releases, ==, 1);
  g_assert_cmpuint (revocations, ==, 1);
  g_assert_cmpint (meta_kms_capture_grant_get_control_fd (grant), ==, -1);
  g_assert_cmpint (meta_kms_capture_grant_steal_capture_fd (grant), ==, -1);
  g_assert_cmpint (fcntl (capture_fd, F_GETFD), >=, 0);
  g_object_unref (grant);
  g_assert_null (weak_device);
  g_clear_fd (&capture_fd, NULL);
}

static void
test_dispose (void)
{
  g_autoptr (GError) error = NULL;
  MetaKmsCaptureGrant *grant;

  begin_case ();
  grant = create_grant (&error);
  g_assert_no_error (error);
  g_assert_nonnull (grant);
  g_object_run_dispose (G_OBJECT (grant));
  g_object_run_dispose (G_OBJECT (grant));
  g_object_unref (grant);
  g_assert_cmpuint (holds, ==, 0);
  g_assert_cmpuint (releases, ==, 1);
  g_assert_cmpuint (revocations, ==, 1);
  g_clear_object (&device_object);
}

static void
test_release_retry (void)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (MetaKmsCaptureGrant) grant = NULL;

  begin_case ();
  grant = create_grant (&error);
  g_assert_no_error (error);
  g_assert_nonnull (grant);
  fail_dispatch = TRUE;
  g_assert_false (meta_kms_capture_grant_revoke (grant, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CLOSED);
  g_assert_cmpint (meta_kms_capture_grant_get_control_fd (grant), ==, -1);
  g_assert_cmpuint (revocations, ==, 1);
  g_assert_cmpuint (holds, ==, 1);
  g_clear_error (&error);
  fail_dispatch = FALSE;
  g_assert_true (meta_kms_capture_grant_revoke (grant, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (holds, ==, 0);
  g_assert_cmpuint (releases, ==, 1);
  g_assert_cmpuint (revocations, ==, 1);
  g_clear_object (&grant);
  g_clear_object (&device_object);
}

static void
test_creation_failures (void)
{
  for (unsigned int i = 0; i < 5; i++)
    {
      g_autoptr (GError) error = NULL;

      begin_case ();
      fail_dispatch = i == 0;
      fail_open = i == 1;
      fail_create = i == 2;
      have_crtc = i != 3;
      have_connector = i != 4;
      g_assert_null (create_grant (&error));
      g_assert_nonnull (error);
      g_assert_cmpuint (holds, ==, 0);
      g_assert_cmpuint (releases, ==, i == 2 ? 1 : 0);
      g_assert_cmpuint (revocations, ==, 0);
      g_clear_object (&device_object);
    }
}

static void
test_invalid_target (void)
{
  g_autoptr (GError) error = NULL;

  begin_case ();
  g_assert_null (meta_kms_capture_grant_new ((MetaKmsDevice *) device_object,
                                            0, 11, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_clear_error (&error);
  g_assert_null (meta_kms_capture_grant_new ((MetaKmsDevice *) device_object,
                                            7, 0, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_assert_cmpuint (tasks, ==, 0);
  g_assert_cmpuint (holds, ==, 0);
  g_clear_object (&device_object);
}

int
main (int    argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/kms/capture-grant/lifetime", test_lifetime);
  g_test_add_func ("/kms/capture-grant/dispose", test_dispose);
  g_test_add_func ("/kms/capture-grant/release-retry", test_release_retry);
  g_test_add_func ("/kms/capture-grant/creation-failures", test_creation_failures);
  g_test_add_func ("/kms/capture-grant/invalid-target", test_invalid_target);
  return g_test_run ();
}
