/* SPDX-License-Identifier: MIT */

#include "drm-mock-preparation.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xf86drm.h>

#include "../../backends/native/meta-drm-preparation.h"

static gboolean delay_next;
static int ticket_fd = -1;
static int observer_fd = -1;
static int pipe_writer = -1;
static struct stat pipe_identity;

/*
 * Keep the real kernel ticket private while returning a quiet pipe. Restoring
 * the real ticket allows the ordinary atomic ioctl to validate and consume it.
 * Only readiness is delayed here; native source accounting is not simulated.
 */
DRM_MOCK_EXPORT int
drmIoctl (int            fd,
          unsigned long request,
          void          *arg)
{
  int (* real_ioctl) (int, unsigned long, void *) = dlsym (RTLD_NEXT, "drmIoctl");
  int ret;

  g_assert_nonnull (real_ioctl);
  ret = real_ioctl (fd, request, arg);
  if (request == DRM_IOCTL_MODE_PREPARE_REPLACE && ret >= 0 && delay_next)
    {
      int pipe_fds[2];

      g_assert_cmpint (pipe2 (pipe_fds, O_CLOEXEC | O_NONBLOCK), ==, 0);
      ticket_fd = ret;
      observer_fd = pipe_fds[0];
      pipe_writer = pipe_fds[1];
      g_assert_cmpint (fstat (observer_fd, &pipe_identity), ==, 0);
      delay_next = FALSE;
      return observer_fd;
    }
  return ret;
}

void
drm_mock_preparation_delay_next (void)
{
  g_assert_false (delay_next);
  g_assert_cmpint (ticket_fd, ==, -1);
  g_assert_cmpint (pipe_writer, ==, -1);
  delay_next = TRUE;
}

gboolean
drm_mock_preparation_was_issued (void)
{
  return ticket_fd >= 0;
}

gboolean
drm_mock_preparation_was_closed (void)
{
  struct pollfd pfd = { .fd = pipe_writer, .events = POLLOUT };

  g_assert_cmpint (pipe_writer, >=, 0);
  g_assert_cmpint (poll (&pfd, 1, 0), >=, 0);
  return (pfd.revents & POLLERR) != 0;
}

void
drm_mock_preparation_release (void)
{
  struct stat identity;

  g_assert_false (drm_mock_preparation_was_closed ());
  g_assert_cmpint (fstat (observer_fd, &identity), ==, 0);
  g_assert_cmpuint (identity.st_dev, ==, pipe_identity.st_dev);
  g_assert_cmpuint (identity.st_ino, ==, pipe_identity.st_ino);
  g_assert_cmpint (dup3 (ticket_fd, observer_fd, O_CLOEXEC), ==, observer_fd);
  g_clear_fd (&ticket_fd, NULL);
  g_clear_fd (&pipe_writer, NULL);
  observer_fd = -1;
}

void
drm_mock_preparation_hangup (void)
{
  g_assert_false (drm_mock_preparation_was_closed ());
  g_clear_fd (&pipe_writer, NULL);
}

void
drm_mock_preparation_clear (void)
{
  if (pipe_writer >= 0)
    g_assert_true (drm_mock_preparation_was_closed ());
  g_clear_fd (&ticket_fd, NULL);
  g_clear_fd (&pipe_writer, NULL);
  observer_fd = -1;
  delay_next = FALSE;
}
