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
#include <sys/mman.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum
{
  SYNC_EVENT,
  N_SIGNALS
};

static guint signals[N_SIGNALS];
static struct wl_callback *effects_complete_callback;
static struct wl_callback *view_verification_callback;

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
test_driver_handle_sync_event (void               *user_data,
                               struct test_driver *test_driver,
                               uint32_t            serial)
{
  WaylandDisplay *display = WAYLAND_DISPLAY (user_data);

  g_signal_emit (display, signals[SYNC_EVENT], 0, serial);
}

static void
test_driver_handle_property (void               *user_data,
                             struct test_driver *test_driver,
                             const char         *name,
                             const char         *value)
{
  WaylandDisplay *display = WAYLAND_DISPLAY (user_data);

  g_hash_table_replace (display->properties,
                        g_strdup (name),
                        g_strdup (value));
}

static const struct test_driver_listener test_driver_listener = {
  test_driver_handle_sync_event,
  test_driver_handle_property,
};

static void
handle_registry_global (void               *user_data,
                        struct wl_registry *registry,
                        uint32_t            id,
                        const char         *interface,
                        uint32_t            version)
{
  WaylandDisplay *display = WAYLAND_DISPLAY (user_data);

  if (strcmp (interface, wl_compositor_interface.name) == 0)
    {
      display->compositor =
        wl_registry_bind (registry, id, &wl_compositor_interface,
                          MIN (version, 5));
    }
  else if (strcmp (interface, wl_subcompositor_interface.name) == 0)
    {
      display->subcompositor =
        wl_registry_bind (registry, id, &wl_subcompositor_interface, 1);
    }
  else if (strcmp (interface, wl_shm_interface.name) == 0)
    {
      display->shm = wl_registry_bind (registry,
                                       id, &wl_shm_interface, 1);
    }
  else if (strcmp (interface, wp_fractional_scale_manager_v1_interface.name) == 0)
    {
      display->fractional_scale_mgr =
        wl_registry_bind (registry, id,
                          &wp_fractional_scale_manager_v1_interface, 1);
    }
  else if (strcmp (interface, wp_single_pixel_buffer_manager_v1_interface.name) == 0)
    {
      display->single_pixel_mgr =
        wl_registry_bind (registry, id,
                          &wp_single_pixel_buffer_manager_v1_interface, 1);
    }
  else if (strcmp (interface, wp_viewporter_interface.name) == 0)
    {
      display->viewporter = wl_registry_bind (registry, id,
                                              &wp_viewporter_interface, 1);
    }
  else if (strcmp (interface, xdg_wm_base_interface.name) == 0)
    {
      int xdg_wm_base_version = 1;

      if (display->capabilities &
          WAYLAND_DISPLAY_CAPABILITY_XDG_SHELL_V4)
        xdg_wm_base_version = 4;

      g_assert_cmpint (version, >=, xdg_wm_base_version);

      display->xdg_wm_base = wl_registry_bind (registry, id,
                                               &xdg_wm_base_interface,
                                               xdg_wm_base_version);
      xdg_wm_base_add_listener (display->xdg_wm_base, &xdg_wm_base_listener,
                                NULL);
    }

  if (display->capabilities & WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER)
    {
      if (strcmp (interface, "test_driver") == 0)
        {
          display->test_driver =
            wl_registry_bind (registry, id, &test_driver_interface, 1);
          test_driver_add_listener (display->test_driver, &test_driver_listener,
                                    display);
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
wayland_display_new_full (WaylandDisplayCapabilities  capabilities,
                          struct wl_display          *wayland_display)
{
  WaylandDisplay *display;

  g_assert_nonnull (wayland_display);

  display = g_object_new (wayland_display_get_type (), NULL);

  display->capabilities = capabilities;
  display->properties = g_hash_table_new_full (g_str_hash, g_str_equal,
                                               g_free, g_free);
  display->display = wayland_display;

  display->registry = wl_display_get_registry (display->display);
  wl_registry_add_listener (display->registry, &registry_listener, display);
  wl_display_roundtrip (display->display);

  g_assert_nonnull (display->compositor);
  g_assert_nonnull (display->subcompositor);
  g_assert_nonnull (display->shm);
  g_assert_nonnull (display->single_pixel_mgr);
  g_assert_nonnull (display->viewporter);
  g_assert_nonnull (display->xdg_wm_base);

  if (capabilities & WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER)
    g_assert_nonnull (display->test_driver);

  wl_display_roundtrip (display->display);

  return display;
}

WaylandDisplay *
wayland_display_new (WaylandDisplayCapabilities capabilities)
{
  return wayland_display_new_full (capabilities,
                                   wl_display_connect (NULL));
}

static void
wayland_display_finalize (GObject *object)
{
  WaylandDisplay *display = WAYLAND_DISPLAY (object);

  wl_display_disconnect (display->display);
  g_clear_pointer (&display->properties, g_hash_table_unref);

  G_OBJECT_CLASS (wayland_display_parent_class)->finalize (object);
}

static void
wayland_display_class_init (WaylandDisplayClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = wayland_display_finalize;

  signals[SYNC_EVENT] =
    g_signal_new ("sync-event",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_UINT);
}

static void
wayland_display_init (WaylandDisplay *display)
{
}

static void
handle_buffer_release (void             *data,
                       struct wl_buffer *buffer)
{
  wl_buffer_destroy (buffer);
}

static const struct wl_buffer_listener buffer_listener = {
  handle_buffer_release
};

gboolean
create_shm_buffer (WaylandDisplay    *display,
                   int                width,
                   int                height,
                   struct wl_buffer **out_buffer,
                   void             **out_data,
                   int               *out_size)
{
  struct wl_shm_pool *pool;
  static struct wl_buffer *buffer;
  int fd, size, stride;
  int bytes_per_pixel;
  void *data;

  bytes_per_pixel = 4;
  stride = width * bytes_per_pixel;
  size = stride * height;

  fd = create_anonymous_file (size);
  if (fd < 0)
    {
      fprintf (stderr, "Creating a buffer file for %d B failed: %m\n",
               size);
      return FALSE;
    }

  data = mmap (NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (data == MAP_FAILED)
    {
      fprintf (stderr, "mmap failed: %m\n");
      close (fd);
      return FALSE;
    }

  pool = wl_shm_create_pool (display->shm, fd, size);
  buffer = wl_shm_pool_create_buffer (pool, 0,
                                      width, height,
                                      stride,
                                      WL_SHM_FORMAT_ARGB8888);
  wl_buffer_add_listener (buffer, &buffer_listener, buffer);
  wl_shm_pool_destroy (pool);
  close (fd);

  *out_buffer = buffer;
  *out_data = data;
  *out_size = size;

  return TRUE;
}

static void
fill (void    *buffer_data,
      int      width,
      int      height,
      uint32_t color)
{
  uint32_t *pixels = buffer_data;
  int x, y;

  for (y = 0; y < height; y++)
    {
      for (x = 0; x < width; x++)
        pixels[y * width + x] = color;
    }
}

void
draw_surface (WaylandDisplay    *display,
              struct wl_surface *surface,
              int                width,
              int                height,
              uint32_t           color)
{
  struct wl_buffer *buffer;
  void *buffer_data;
  int size;

  if (!create_shm_buffer (display, width, height,
                          &buffer, &buffer_data, &size))
    g_error ("Failed to create shm buffer");

  fill (buffer_data, width, height, color);

  wl_surface_attach (surface, buffer, 0, 0);
}

static void
handle_xdg_toplevel_configure (void                *data,
                               struct xdg_toplevel *xdg_toplevel,
                               int32_t              width,
                               int32_t              height,
                               struct wl_array     *state)
{
  WaylandSurface *surface = data;

  if (width == 0)
    surface->width = surface->default_width;
  else
    surface->width = width;

  if (height == 0)
    surface->height = surface->default_height;
  else
    surface->height = height;
}

static void
handle_xdg_toplevel_close (void                *data,
                           struct xdg_toplevel *xdg_toplevel)
{
  g_assert_not_reached ();
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
  handle_xdg_toplevel_configure,
  handle_xdg_toplevel_close,
};

static void
handle_xdg_surface_configure (void               *data,
                              struct xdg_surface *xdg_surface,
                              uint32_t            serial)
{
  WaylandSurface *surface = data;

  draw_surface (surface->display,
                surface->wl_surface,
                surface->width, surface->height,
                surface->color);

  xdg_surface_ack_configure (xdg_surface, serial);
  wl_surface_commit (surface->wl_surface);
}

static const struct xdg_surface_listener xdg_surface_listener = {
  handle_xdg_surface_configure,
};

WaylandSurface *
wayland_surface_new (WaylandDisplay *display,
                     const char     *title,
                     int             default_width,
                     int             default_height,
                     uint32_t        color)
{
  WaylandSurface *surface;

  surface = g_new0 (WaylandSurface, 1);
  surface->display = display;
  surface->default_width = default_width;
  surface->default_height = default_height;
  surface->color = color;
  surface->wl_surface = wl_compositor_create_surface (display->compositor);
  surface->xdg_surface = xdg_wm_base_get_xdg_surface (display->xdg_wm_base,
                                                      surface->wl_surface);
  xdg_surface_add_listener (surface->xdg_surface, &xdg_surface_listener,
                            surface);
  surface->xdg_toplevel = xdg_surface_get_toplevel (surface->xdg_surface);
  xdg_toplevel_add_listener (surface->xdg_toplevel, &xdg_toplevel_listener,
                             surface);
  xdg_toplevel_set_title (surface->xdg_toplevel, title);

  return surface;
}

void
wayland_surface_free (WaylandSurface *surface)
{
  g_clear_pointer (&surface->xdg_toplevel, xdg_toplevel_destroy);
  g_clear_pointer (&surface->xdg_surface, xdg_surface_destroy);
  g_clear_pointer (&surface->wl_surface, wl_surface_destroy);
  g_free (surface);
}

const char *
lookup_property_value (WaylandDisplay *display,
                       const char     *name)
{
  return g_hash_table_lookup (display->properties, name);
}

static void
effects_completed (void               *data,
                   struct wl_callback *callback,
                   uint32_t            serial)
{
  wl_callback_destroy (callback);
  effects_complete_callback = NULL;
}

static const struct wl_callback_listener effects_complete_listener = {
  effects_completed,
};

void
wait_for_effects_completed (WaylandDisplay    *display,
                            struct wl_surface *surface)
{
  effects_complete_callback =
    test_driver_sync_effects_completed (display->test_driver, surface);
  wl_callback_add_listener (effects_complete_callback,
                            &effects_complete_listener,
                            NULL);

  while (effects_complete_callback)
    {
      if (wl_display_dispatch (display->display) == -1)
        g_error ("%s: Failed to dispatch Wayland display", __func__);
    }
}

static void
view_verified (void               *data,
               struct wl_callback *callback,
               uint32_t            serial)
{
  wl_callback_destroy (callback);
  view_verification_callback = NULL;
}

static const struct wl_callback_listener view_verification_listener = {
  view_verified,
};

void
wait_for_view_verified (WaylandDisplay *display,
                        int             sequence)
{
  view_verification_callback =
    test_driver_verify_view (display->test_driver, sequence);
  wl_callback_add_listener (view_verification_callback,
                            &view_verification_listener, NULL);

  while (view_verification_callback)
    {
      if (wl_display_dispatch (display->display) == -1)
        g_error ("%s: Failed to dispatch Wayland display", __func__);
    }
}

static void
on_sync_event (WaylandDisplay *display,
               uint32_t        serial,
               void           *user_data)
{
  g_assert_cmpuint (serial, ==, display->sync_event_serial_next);
  display->sync_event_serial_next = serial + 1;
}

void
wait_for_sync_event (WaylandDisplay *display,
                     uint32_t        expected_serial)
{
  gulong handler_id;
  handler_id = g_signal_connect (display, "sync-event", G_CALLBACK (on_sync_event), NULL);

  while (expected_serial + 1 > display->sync_event_serial_next)
    {
      if (wl_display_dispatch (display->display) == -1)
        g_error ("%s: Failed to dispatch Wayland display", __func__);
    }

  g_signal_handler_disconnect (display, handler_id);
}
