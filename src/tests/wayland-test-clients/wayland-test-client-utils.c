/*
 * Copyright © 2012 Collabora, Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "wayland-test-client-utils.h"

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

G_DEFINE_TYPE (WaylandDisplay, wayland_display, G_TYPE_OBJECT)

static int
create_tmpfile_cloexec (char *tmpname)
{
  int fd;

  fd = mkostemp (tmpname, O_CLOEXEC);
  if (fd >= 0)
    unlink (tmpname);

  return fd;
}

int
create_anonymous_file (off_t size)
{
  static const char template[] = "/wayland-test-client-shared-XXXXXX";
  const char *path;
  char *name;
  int fd;
  int ret;

  path = getenv ("XDG_RUNTIME_DIR");
  if (!path)
    {
      errno = ENOENT;
      return -1;
    }

  name = malloc (strlen (path) + sizeof (template));
  if (!name)
    return -1;

  strcpy (name, path);
  strcat (name, template);

  fd = create_tmpfile_cloexec (name);

  free (name);

  if (fd < 0)
    return -1;

  do
    ret = posix_fallocate (fd, 0, size);
  while (ret == EINTR);

  if (ret != 0)
    {
      close (fd);
      errno = ret;
      return -1;
    }

  return fd;
}

static void
handle_xdg_wm_base_ping (void               *user_data,
                         struct xdg_wm_base *xdg_wm_base,
                         uint32_t            serial)
{
  xdg_wm_base_pong (xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
  handle_xdg_wm_base_ping,
};

static void
handle_registry_global (void               *user_data,
                        struct wl_registry *registry,
                        uint32_t            id,
                        const char         *interface,
                        uint32_t            version)
{
  WaylandDisplay *display = WAYLAND_DISPLAY (user_data);

  if (strcmp (interface, "wl_compositor") == 0)
    {
      display->compositor =
        wl_registry_bind (registry, id, &wl_compositor_interface, 1);
    }
  else if (strcmp (interface, "wl_subcompositor") == 0)
    {
      display->subcompositor =
        wl_registry_bind (registry, id, &wl_subcompositor_interface, 1);
    }
  else if (strcmp (interface, "wl_shm") == 0)
    {
      display->shm = wl_registry_bind (registry,
                              id, &wl_shm_interface, 1);
    }
  else if (strcmp (interface, "xdg_wm_base") == 0)
    {
      display->xdg_wm_base = wl_registry_bind (registry, id,
                                               &xdg_wm_base_interface, 1);
      xdg_wm_base_add_listener (display->xdg_wm_base, &xdg_wm_base_listener,
                                NULL);
    }

  if (display->capabilities & WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER)
    {
      if (strcmp (interface, "test_driver") == 0)
        {
          display->test_driver =
            wl_registry_bind (registry, id, &test_driver_interface, 1);
        }
    }
}

static void
handle_registry_global_remove (void               *data,
                               struct wl_registry *registry,
                               uint32_t            name)
{
}

static const struct wl_registry_listener registry_listener = {
  handle_registry_global,
  handle_registry_global_remove
};

WaylandDisplay *
wayland_display_new (WaylandDisplayCapabilities capabilities)
{
  WaylandDisplay *display;

  display = g_object_new (wayland_display_get_type (), NULL);

  display->capabilities = capabilities;
  display->display = wl_display_connect (NULL);
  g_assert_nonnull (display->display);

  display->registry = wl_display_get_registry (display->display);
  wl_registry_add_listener (display->registry, &registry_listener, display);
  wl_display_roundtrip (display->display);

  g_assert_nonnull (display->compositor);
  g_assert_nonnull (display->subcompositor);
  g_assert_nonnull (display->shm);
  g_assert_nonnull (display->xdg_wm_base);

  if (capabilities & WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER)
    g_assert_nonnull (display->test_driver);

  wl_display_roundtrip (display->display);

  return display;
}

static void
wayland_display_finalize (GObject *object)
{
  WaylandDisplay *display = WAYLAND_DISPLAY (object);

  wl_display_disconnect (display->display);

  G_OBJECT_CLASS (wayland_display_parent_class)->finalize (object);
}

static void
wayland_display_class_init (WaylandDisplayClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = wayland_display_finalize;
}

static void
wayland_display_init (WaylandDisplay *display)
{
}
