/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <xf86drmMode.h>

#include "backends/native/meta-drm-castkms.h"
#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-impl-device.h"
#include "backends/native/meta-kms-monitor-control.h"
#include "backends/native/meta-kms-private.h"

typedef enum
{
  CREATE_VALID,
  CREATE_FAIL,
  CREATE_DUPLICATE,
  CREATE_MISSING_CONTROL,
  CREATE_MISSING_REVOKE,
  CREATE_INHERITABLE_CONTROL,
  CREATE_INHERITABLE_REVOKE,
} CreateReply;

typedef enum
{
  QUERY_VALID,
  QUERY_FAIL,
  QUERY_INTERRUPT_ONCE,
  QUERY_BAD_VERSION,
  QUERY_BAD_FLAGS,
  QUERY_SMALL_EDID,
  QUERY_MISALIGNED_EDID,
  QUERY_BAD_RESERVED,
} QueryReply;

static gboolean in_impl;
static gboolean fail_dispatch;
static gboolean fail_open;
static gboolean have_connector;
static uint32_t connector_type;
static const char *driver_name;
static CreateReply create_reply;
static QueryReply query_reply;
static GObject *device_object;
static int issuer_fd;
static int issued_control;
static int issued_revoke;
static unsigned int tasks;
static unsigned int create_calls;
static unsigned int query_calls;

int __wrap_drmIoctl (int fd, unsigned long command, void *request);
int __wrap_ioctl (int fd, unsigned long command, ...);

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
meta_kms_run_impl_task_sync (MetaKms             *kms,
                             MetaThreadTaskFunc   func,
                             gpointer             data,
                             GError             **error)
{
  gpointer result;

  g_assert_false (in_impl);
  tasks++;
  if (fail_dispatch)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_CLOSED,
                           "KMS thread closed");
      return NULL;
    }
  in_impl = TRUE;
  result = func (NULL, data, error);
  in_impl = FALSE;
  return result;
}

MetaKmsConnector *
meta_kms_device_find_connector_in_impl (MetaKmsDevice *device,
                                        uint32_t       connector_id)
{
  g_assert_true (in_impl);
  g_assert_cmpuint (connector_id, ==, 11);
  return have_connector ? (MetaKmsConnector *) device : NULL;
}

uint32_t
meta_kms_connector_get_connector_type (MetaKmsConnector *connector)
{
  g_assert_true (in_impl);
  return connector_type;
}

const char *
meta_kms_impl_device_get_driver_name (MetaKmsImplDevice *impl_device)
{
  g_assert_true (in_impl);
  return driver_name;
}

gboolean
meta_kms_impl_device_ensure_fd (MetaKmsImplDevice  *impl_device,
                                GError            **error)
{
  g_assert_true (in_impl);
  if (fail_open)
    g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                         "Cannot open DRM device");
  return !fail_open;
}

int
meta_kms_impl_device_get_fd (MetaKmsImplDevice *impl_device)
{
  g_assert_true (in_impl);
  return issuer_fd;
}

int
__wrap_drmIoctl (int            fd,
                 unsigned long  command,
                 void          *data)
{
  struct drm_castkms_create_monitor_control *request = data;
  struct drm_castkms_monitor_files *files =
    (void *) (uintptr_t) request->files;
  int pipe_fds[2];

  g_assert_true (in_impl);
  g_assert_cmpint (fd, ==, issuer_fd);
  g_assert_cmpuint (command, ==,
                    DRM_IOCTL_CASTKMS_CREATE_MONITOR_CONTROL);
  g_assert_cmpuint (request->connector_id, ==, 11);
  g_assert_cmpuint (request->flags, ==, 0);
  g_assert_cmpuint (request->reserved[0], ==, 0);
  g_assert_cmpuint (request->reserved[1], ==, 0);
  g_assert_nonnull (files);
  g_assert_cmpint (files->control_fd, ==, -1);
  g_assert_cmpint (files->revoke_fd, ==, -1);
  create_calls++;

  if (create_reply == CREATE_FAIL)
    {
      errno = EIO;
      return -1;
    }

  g_assert_cmpint (pipe2 (pipe_fds, O_CLOEXEC), ==, 0);
  issued_control = pipe_fds[0];
  issued_revoke = pipe_fds[1];
  files->control_fd = issued_control;
  files->revoke_fd = issued_revoke;
  switch (create_reply)
    {
    case CREATE_DUPLICATE:
      g_clear_fd (&issued_revoke, NULL);
      files->revoke_fd = issued_control;
      break;
    case CREATE_MISSING_CONTROL:
      g_clear_fd (&issued_control, NULL);
      files->control_fd = -1;
      break;
    case CREATE_MISSING_REVOKE:
      g_clear_fd (&issued_revoke, NULL);
      files->revoke_fd = -1;
      break;
    case CREATE_INHERITABLE_CONTROL:
      g_assert_cmpint (fcntl (issued_control, F_SETFD, 0), ==, 0);
      break;
    case CREATE_INHERITABLE_REVOKE:
      g_assert_cmpint (fcntl (issued_revoke, F_SETFD, 0), ==, 0);
      break;
    default:
      break;
    }
  return 0;
}

int
__wrap_ioctl (int            fd,
              unsigned long  command,
              ...)
{
  struct drm_castkms_monitor_query *query;
  va_list args;

  g_assert_cmpint (fd, ==, issued_control);
  g_assert_cmpuint (command, ==, DRM_IOCTL_CASTKMS_MONITOR_QUERY);
  va_start (args, command);
  query = va_arg (args, struct drm_castkms_monitor_query *);
  va_end (args);
  query_calls++;

  if (query_reply == QUERY_FAIL ||
      (query_reply == QUERY_INTERRUPT_ONCE && query_calls == 1))
    {
      errno = query_reply == QUERY_FAIL ? EIO : EINTR;
      return -1;
    }

  *query = (struct drm_castkms_monitor_query) {
    .version = DRM_CASTKMS_MONITOR_CONTROL_VERSION,
    .max_edid_size = DRM_CASTKMS_MONITOR_MAX_EDID_SIZE,
  };
  switch (query_reply)
    {
    case QUERY_BAD_VERSION:
      query->version++;
      break;
    case QUERY_BAD_FLAGS:
      query->flags = 1;
      break;
    case QUERY_SMALL_EDID:
      query->max_edid_size = 127;
      break;
    case QUERY_MISALIGNED_EDID:
      query->max_edid_size--;
      break;
    case QUERY_BAD_RESERVED:
      query->reserved = 1;
      break;
    default:
      break;
    }
  return 0;
}

static void
begin_case (void)
{
  in_impl = fail_dispatch = fail_open = FALSE;
  have_connector = TRUE;
  connector_type = DRM_MODE_CONNECTOR_VIRTUAL;
  driver_name = "castkms";
  create_reply = CREATE_VALID;
  query_reply = QUERY_VALID;
  tasks = create_calls = query_calls = 0;
  issued_control = issued_revoke = -1;
  issuer_fd = open ("/dev/null", O_RDONLY | O_CLOEXEC);
  g_assert_cmpint (issuer_fd, >=, 0);
  device_object = g_object_new (G_TYPE_OBJECT, NULL);
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
  g_assert_cmpint (fcntl (issuer_fd, F_GETFD), >=, 0);
  g_clear_fd (&issuer_fd, NULL);
  g_clear_object (&device_object);
}

static void
test_ownership (void)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (MetaKmsMonitorControl) control = NULL;
  int holder_fd;

  begin_case ();
  control = meta_kms_monitor_control_new ((MetaKmsDevice *) device_object,
                                          11, &error);
  g_assert_no_error (error);
  g_assert_nonnull (control);
  g_assert_cmpuint (tasks, ==, 1);
  g_assert_cmpuint (create_calls, ==, 1);
  g_assert_cmpuint (query_calls, ==, 1);
  holder_fd = meta_kms_monitor_control_steal_fd (control);
  g_assert_cmpint (holder_fd, ==, issued_control);
  g_assert_cmpint (meta_kms_monitor_control_steal_fd (control), ==, -1);
  g_clear_pointer (&control, meta_kms_monitor_control_free);
  assert_closed (issued_revoke);
  g_assert_cmpint (fcntl (holder_fd, F_GETFD), >=, 0);
  g_clear_fd (&holder_fd, NULL);
  end_case ();
}

static void
test_query_interruption (void)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (MetaKmsMonitorControl) control = NULL;

  begin_case ();
  query_reply = QUERY_INTERRUPT_ONCE;
  control = meta_kms_monitor_control_new ((MetaKmsDevice *) device_object,
                                          11, &error);
  g_assert_no_error (error);
  g_assert_nonnull (control);
  g_assert_cmpuint (query_calls, ==, 2);
  end_case ();
}

static void
test_invalid_create_reply (void)
{
  const CreateReply replies[] = {
    CREATE_FAIL,
    CREATE_DUPLICATE,
    CREATE_MISSING_CONTROL,
    CREATE_MISSING_REVOKE,
    CREATE_INHERITABLE_CONTROL,
    CREATE_INHERITABLE_REVOKE,
  };

  for (unsigned int i = 0; i < G_N_ELEMENTS (replies); i++)
    {
      g_autoptr (GError) error = NULL;

      begin_case ();
      create_reply = replies[i];
      g_assert_null (meta_kms_monitor_control_new (
                       (MetaKmsDevice *) device_object, 11, &error));
      g_assert_nonnull (error);
      if (replies[i] != CREATE_FAIL)
        {
          assert_closed (issued_control);
          assert_closed (issued_revoke);
        }
      end_case ();
    }
}

static void
test_invalid_query_reply (void)
{
  const QueryReply replies[] = {
    QUERY_FAIL,
    QUERY_BAD_VERSION,
    QUERY_BAD_FLAGS,
    QUERY_SMALL_EDID,
    QUERY_MISALIGNED_EDID,
    QUERY_BAD_RESERVED,
  };

  for (unsigned int i = 0; i < G_N_ELEMENTS (replies); i++)
    {
      g_autoptr (GError) error = NULL;

      begin_case ();
      query_reply = replies[i];
      g_assert_null (meta_kms_monitor_control_new (
                       (MetaKmsDevice *) device_object, 11, &error));
      g_assert_nonnull (error);
      assert_closed (issued_control);
      assert_closed (issued_revoke);
      end_case ();
    }
}

static void
test_target_failures (void)
{
  for (unsigned int i = 0; i < 5; i++)
    {
      g_autoptr (GError) error = NULL;

      begin_case ();
      fail_dispatch = i == 0;
      driver_name = i == 1 ? "vkms" : "castkms";
      have_connector = i != 2;
      connector_type = i == 3 ? DRM_MODE_CONNECTOR_HDMIA :
                                DRM_MODE_CONNECTOR_VIRTUAL;
      fail_open = i == 4;
      g_assert_null (meta_kms_monitor_control_new (
                       (MetaKmsDevice *) device_object, 11, &error));
      g_assert_nonnull (error);
      g_assert_cmpuint (create_calls, ==, 0);
      end_case ();
    }
}

static void
test_invalid_connector (void)
{
  g_autoptr (GError) error = NULL;

  begin_case ();
  g_assert_null (meta_kms_monitor_control_new (
                   (MetaKmsDevice *) device_object, 0, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_assert_cmpuint (tasks, ==, 0);
  end_case ();
}

int
main (int    argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/kms/monitor-control/ownership", test_ownership);
  g_test_add_func ("/kms/monitor-control/query-interruption",
                   test_query_interruption);
  g_test_add_func ("/kms/monitor-control/invalid-create-reply",
                   test_invalid_create_reply);
  g_test_add_func ("/kms/monitor-control/invalid-query-reply",
                   test_invalid_query_reply);
  g_test_add_func ("/kms/monitor-control/target-failures",
                   test_target_failures);
  g_test_add_func ("/kms/monitor-control/invalid-connector",
                   test_invalid_connector);
  return g_test_run ();
}
