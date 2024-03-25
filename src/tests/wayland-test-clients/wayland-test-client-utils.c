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
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <sys/mman.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum
{
  SYNC_EVENT,
  SURFACE_PAINTED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];
static struct wl_callback *effects_complete_callback;
static struct wl_callback *window_shown_callback;
static struct wl_callback *view_verification_callback;

struct _WaylandBufferClass
{
  GObjectClass parent_class;

  gboolean (* allocate) (WaylandBuffer *buffer,
                         unsigned int   n_modifiers,
                         uint64_t      *modifiers,
                         uint32_t       bo_flags);

  void * (* mmap_plane) (WaylandBuffer *buffer,
                         int            plane,
                         size_t        *stride_out);
};

#define WAYLAND_TYPE_BUFFER_SHM (wayland_buffer_shm_get_type ())
G_DECLARE_FINAL_TYPE (WaylandBufferShm, wayland_buffer_shm,
                      WAYLAND, BUFFER_SHM,
                      WaylandBuffer)

#define WAYLAND_TYPE_BUFFER_DMABUF (wayland_buffer_dmabuf_get_type ())
G_DECLARE_FINAL_TYPE (WaylandBufferDmabuf, wayland_buffer_dmabuf,
                      WAYLAND, BUFFER_DMABUF,
                      WaylandBuffer)

G_DEFINE_TYPE (WaylandDisplay,
               wayland_display,
               G_TYPE_OBJECT)

G_DEFINE_TYPE (WaylandSurface,
               wayland_surface,
               G_TYPE_OBJECT)

typedef struct _WaylandBufferPrivate
{
  WaylandDisplay *display;
  uint32_t format;
  uint32_t width;
  uint32_t height;
  struct wl_buffer *buffer;
} WaylandBufferPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (WaylandBuffer,
                                     wayland_buffer,
                                     G_TYPE_OBJECT)

struct _WaylandBufferShm
{
  WaylandBuffer parent;

  int n_planes;
  size_t plane_offset[4];
  size_t stride[4];
  size_t size;
  int fd;
  void *data;
};

G_DEFINE_TYPE (WaylandBufferShm,
               wayland_buffer_shm,
               WAYLAND_TYPE_BUFFER)

struct _WaylandBufferDmabuf
{
  WaylandBuffer parent;

  uint64_t modifier;
  int n_planes;
  struct gbm_bo *bo[4];
  int fd[4];
  uint32_t offset[4];
  uint32_t stride[4];
  void *data[4];
  void *map_data[4];
  uint32_t map_stride[4];
};

G_DEFINE_TYPE (WaylandBufferDmabuf,
               wayland_buffer_dmabuf,
               WAYLAND_TYPE_BUFFER)

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

static struct gbm_device *
create_gbm_device (WaylandDisplay *display)
{
  const char *gpu_path;
  int fd;

  gpu_path = lookup_property_value (display, "gpu-path");
  if (!gpu_path)
    return NULL;

  fd = open (gpu_path, O_RDWR);
  if (fd < 0)
    {
      g_error ("Failed to open drm render node %s: %s",
               gpu_path, g_strerror (errno));
    }

  return gbm_create_device (fd);
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
dmabuf_modifiers (void                       *user_data,
                  struct zwp_linux_dmabuf_v1 *zwp_linux_dmabuf,
                  uint32_t                    format,
                  uint32_t                    modifier_hi,
                  uint32_t                    modifier_lo)
{
  WaylandDisplay *display = WAYLAND_DISPLAY (user_data);
  DmaBufFormat *dma_format;
  uint64_t modifier;


  dma_format = g_hash_table_lookup (display->formats,
                                    GUINT_TO_POINTER (format));
  if (!dma_format)
    {
      dma_format = g_new0 (DmaBufFormat, 1);
      dma_format->format = format;
      g_hash_table_insert (display->formats,
                           GUINT_TO_POINTER (format),
                           dma_format);
    }

  modifier = ((uint64_t)modifier_hi << 32) | modifier_lo;

  if (modifier != DRM_FORMAT_MOD_INVALID)
    {
      dma_format->n_modifiers++;
      dma_format->modifiers = g_realloc_n (dma_format->modifiers,
                                           dma_format->n_modifiers,
                                           sizeof (uint64_t));
      dma_format->modifiers[dma_format->n_modifiers - 1] = modifier;
    }
}

static void
dmabuf_format (void                       *user_data,
               struct zwp_linux_dmabuf_v1 *zwp_linux_dmabuf,
               uint32_t                    format)
{
}

static const struct zwp_linux_dmabuf_v1_listener dmabuf_listener = {
  dmabuf_format,
  dmabuf_modifiers
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
  else if (strcmp (interface, zwp_linux_dmabuf_v1_interface.name) == 0)
    {
      display->linux_dmabuf =
        wl_registry_bind (registry,
                          id, &zwp_linux_dmabuf_v1_interface, 3);
      zwp_linux_dmabuf_v1_add_listener (display->linux_dmabuf,
                                        &dmabuf_listener, display);
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
      if (display->capabilities &
          WAYLAND_DISPLAY_CAPABILITY_XDG_SHELL_V6)
        xdg_wm_base_version = 6;

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

  display = g_object_new (WAYLAND_TYPE_DISPLAY, NULL);

  display->capabilities = capabilities;
  display->properties = g_hash_table_new_full (g_str_hash, g_str_equal,
                                               g_free, g_free);
  display->formats = g_hash_table_new_full (NULL, NULL, NULL, g_free);

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

  display->gbm_device = create_gbm_device (display);

  return display;
}

WaylandDisplay *
wayland_display_new (WaylandDisplayCapabilities capabilities)
{
  return wayland_display_new_full (capabilities,
                                   wl_display_connect (NULL));
}

void
wayland_display_dispatch (WaylandDisplay *display)
{
  if (wl_display_dispatch (display->display) == -1)
    g_error ("wl_display_dispatch failed");
}

static void
wayland_display_finalize (GObject *object)
{
  WaylandDisplay *display = WAYLAND_DISPLAY (object);

  wl_display_disconnect (display->display);
  g_clear_pointer (&display->properties, g_hash_table_unref);
  g_clear_pointer (&display->formats, g_hash_table_unref);

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

  signals[SURFACE_PAINTED] =
    g_signal_new ("surface-painted",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1,
                  WAYLAND_TYPE_SURFACE);
}

static void
wayland_display_init (WaylandDisplay *display)
{
}

void
draw_surface (WaylandDisplay    *display,
              struct wl_surface *surface,
              int                width,
              int                height,
              uint32_t           color)
{
  WaylandBuffer *buffer;

  buffer = wayland_buffer_create (display, NULL,
                                  width, height,
                                  DRM_FORMAT_ARGB8888,
                                  NULL, 0,
                                  GBM_BO_USE_LINEAR);

  if (!buffer)
    g_error ("Failed to create buffer");

  wayland_buffer_fill_color (buffer, color);

  wl_surface_attach (surface, wayland_buffer_get_wl_buffer (buffer), 0, 0);
}

static void
handle_xdg_toplevel_configure (void                *data,
                               struct xdg_toplevel *xdg_toplevel,
                               int32_t              width,
                               int32_t              height,
                               struct wl_array     *states)
{
  WaylandSurface *surface = data;
  uint32_t *p;

  if (width == 0)
    surface->width = surface->default_width;
  else
    surface->width = width;

  if (height == 0)
    surface->height = surface->default_height;
  else
    surface->height = height;

  g_assert_null (surface->pending_state);
  surface->pending_state = g_hash_table_new (NULL, NULL);

  wl_array_for_each (p, states)
    {
      uint32_t state = *p;

      g_hash_table_add (surface->pending_state, GUINT_TO_POINTER (state));
    }
}

static void
handle_xdg_toplevel_close (void                *data,
                           struct xdg_toplevel *xdg_toplevel)
{
  g_assert_not_reached ();
}

static void
handle_xdg_toplevel_bounds (void                *data,
                            struct xdg_toplevel *xdg_toplevel,
                            int32_t              width,
                            int32_t              height)
{
}

static void
handle_xdg_toplevel_wm_capabilities (void                *data,
                                     struct xdg_toplevel *xdg_toplevel,
                                     struct wl_array     *capabilities)
{
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
  handle_xdg_toplevel_configure,
  handle_xdg_toplevel_close,
  handle_xdg_toplevel_bounds,
  handle_xdg_toplevel_wm_capabilities,
};

static void
handle_xdg_surface_configure (void               *data,
                              struct xdg_surface *xdg_surface,
                              uint32_t            serial)
{
  WaylandSurface *surface = data;
  struct wl_region *opaque_region;

  draw_surface (surface->display,
                surface->wl_surface,
                surface->width, surface->height,
                surface->color);
  opaque_region = wl_compositor_create_region (surface->display->compositor);
  wl_region_add (opaque_region, 0, 0, surface->width, surface->height);
  wl_surface_set_opaque_region (surface->wl_surface, opaque_region);
  wl_region_destroy (opaque_region);

  xdg_surface_ack_configure (xdg_surface, serial);
  wl_surface_commit (surface->wl_surface);

  g_clear_pointer (&surface->current_state, g_hash_table_unref);
  surface->current_state = g_steal_pointer (&surface->pending_state);

  g_signal_emit (surface->display, signals[SURFACE_PAINTED], 0, surface);
}

static const struct xdg_surface_listener xdg_surface_listener = {
  handle_xdg_surface_configure,
};

static void
wayland_surface_dispose (GObject *object)
{
  WaylandSurface *surface = WAYLAND_SURFACE (object);

  g_clear_pointer (&surface->xdg_toplevel, xdg_toplevel_destroy);
  g_clear_pointer (&surface->xdg_surface, xdg_surface_destroy);
  g_clear_pointer (&surface->wl_surface, wl_surface_destroy);
  g_clear_pointer (&surface->pending_state, g_hash_table_unref);
  g_clear_pointer (&surface->current_state, g_hash_table_unref);

  G_OBJECT_CLASS (wayland_surface_parent_class)->dispose (object);
}

static void
wayland_surface_class_init (WaylandSurfaceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = wayland_surface_dispose;
}

static void
wayland_surface_init (WaylandSurface *surface)
{
}

WaylandSurface *
wayland_surface_new (WaylandDisplay *display,
                     const char     *title,
                     int             default_width,
                     int             default_height,
                     uint32_t        color)
{
  WaylandSurface *surface;

  surface = g_object_new (WAYLAND_TYPE_SURFACE, NULL);

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

gboolean
wayland_surface_has_state (WaylandSurface          *surface,
                           enum xdg_toplevel_state  state)
{
  return g_hash_table_contains (surface->current_state, GUINT_TO_POINTER (state));
}

void
wayland_surface_set_opaque (WaylandSurface *surface)
{
  surface->is_opaque = TRUE;
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
    wayland_display_dispatch (display);
}

static void
window_shown (void               *data,
              struct wl_callback *callback,
              uint32_t            serial)
{
  wl_callback_destroy (callback);
  window_shown_callback = NULL;
}

static const struct wl_callback_listener window_shown_listener = {
  window_shown,
};

void
wait_for_window_shown (WaylandDisplay    *display,
                       struct wl_surface *surface)
{
  window_shown_callback =
    test_driver_sync_window_shown (display->test_driver, surface);
  wl_callback_add_listener (window_shown_callback,
                            &window_shown_listener,
                            NULL);

  while (window_shown_callback)
    wayland_display_dispatch (display);
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
    wayland_display_dispatch (display);
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
    wayland_display_dispatch (display);

  g_signal_handler_disconnect (display, handler_id);
}

struct wl_buffer *
wayland_buffer_get_wl_buffer (WaylandBuffer *buffer)
{
  WaylandBufferPrivate *priv = wayland_buffer_get_instance_private (buffer);

  return priv->buffer;
}

void
wayland_buffer_fill_color (WaylandBuffer *buffer,
                           uint32_t       color)
{
  WaylandBufferPrivate *priv = wayland_buffer_get_instance_private (buffer);
  int i, j;

  for (i = 0; i < priv->height; i++)
    {
      for (j = 0; j < priv->width; j++)
        {
          wayland_buffer_draw_pixel (buffer, j, i, color);
        }
    }
}

void
wayland_buffer_draw_pixel (WaylandBuffer *buffer,
                           size_t         x,
                           size_t         y,
                           uint32_t       rgba)
{
  WaylandBufferPrivate *priv = wayland_buffer_get_instance_private (buffer);
  uint8_t *data;
  size_t stride;
  uint8_t alpha = (rgba >> 24) & 0xff;
  uint8_t red = (rgba >> 16) & 0xff;
  uint8_t green = (rgba >> 8) & 0xff;
  uint8_t blue = (rgba >> 0) & 0xff;

  data = wayland_buffer_mmap_plane (buffer, 0, &stride);

  switch (priv->format)
    {
    case DRM_FORMAT_ARGB8888:
      {
        uint8_t *pixel;

        pixel = (data + (stride * y) + (x * 4));
        pixel[0] = blue;
        pixel[1] = green;
        pixel[2] = red;
        pixel[3] = alpha;
      }
      break;
    case DRM_FORMAT_XRGB8888:
      {
        uint8_t *pixel;

        pixel = (data + (stride * y) + (x * 4));
        pixel[0] = blue;
        pixel[1] = green;
        pixel[2] = red;
        pixel[3] = 255;
      }
      break;
    default:
      g_assert_not_reached ();
    }
}

void *
wayland_buffer_mmap_plane (WaylandBuffer *buffer,
                           int            plane,
                           size_t        *stride_out)
{
  return WAYLAND_BUFFER_GET_CLASS (buffer)->mmap_plane (buffer,
                                                        plane,
                                                        stride_out);
}

static gboolean
wayland_buffer_allocate (WaylandBuffer *buffer,
                         unsigned int   n_modifiers,
                         uint64_t      *modifiers,
                         uint32_t       bo_flags)
{
  return WAYLAND_BUFFER_GET_CLASS (buffer)->allocate (buffer,
                                                      n_modifiers,
                                                      modifiers,
                                                      bo_flags);
}

static void
wayland_buffer_dispose (GObject *object)
{
  WaylandBuffer *buffer = WAYLAND_BUFFER (object);
  WaylandBufferPrivate *priv = wayland_buffer_get_instance_private (buffer);

  g_clear_object (&priv->display);

  G_OBJECT_CLASS (wayland_buffer_parent_class)->dispose (object);
}

static void
wayland_buffer_class_init (WaylandBufferClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = wayland_buffer_dispose;
}

static void
wayland_buffer_init (WaylandBuffer *buffer)
{
}

static void
handle_buffer_release (void             *user_data,
                       struct wl_buffer *buffer_resource)
{
  WaylandBuffer *buffer = WAYLAND_BUFFER (user_data);

  wl_buffer_destroy (buffer_resource);
  g_object_unref (buffer);
}

static const struct wl_buffer_listener default_buffer_listener = {
  handle_buffer_release
};

WaylandBuffer *
wayland_buffer_create (WaylandDisplay                  *display,
                       const struct wl_buffer_listener *listener,
                       uint32_t                         width,
                       uint32_t                         height,
                       uint32_t                         format,
                       uint64_t                        *modifiers,
                       unsigned int                     n_modifiers,
                       uint32_t                         bo_flags)
{
  g_autoptr (WaylandBuffer) buffer;
  WaylandBufferPrivate *priv;

  if (display->gbm_device)
    {
      buffer = g_object_new (WAYLAND_TYPE_BUFFER_DMABUF, NULL);
    }
  else
    {
      buffer = g_object_new (WAYLAND_TYPE_BUFFER_SHM, NULL);
    }

  priv = wayland_buffer_get_instance_private (buffer);
  priv->display = g_object_ref (display);
  priv->format = format;
  priv->width = width;
  priv->height = height;

  if (!wayland_buffer_allocate (buffer, n_modifiers, modifiers, bo_flags))
    return NULL;

  if (!listener)
    listener = &default_buffer_listener;

  wl_buffer_add_listener (priv->buffer, listener, buffer);

  return g_steal_pointer (&buffer);
}

static gboolean
wayland_buffer_shm_allocate (WaylandBuffer *buffer,
                             unsigned int   n_modifiers,
                             uint64_t      *modifiers,
                             uint32_t       bo_flags)
{
  WaylandBufferShm *shm = WAYLAND_BUFFER_SHM (buffer);
  WaylandBufferPrivate *priv = wayland_buffer_get_instance_private (buffer);
  WaylandDisplay *display = priv->display;
  g_autofd int fd = -1;
  struct wl_shm_pool *pool;
  enum wl_shm_format shm_format;
  int bpp[4];
  int hsub[4];
  int vsub[4];
  gboolean may_alloc_linear;
  int i;

  may_alloc_linear = !modifiers;
  for (i = 0; i < n_modifiers; i++)
    {
      if (modifiers[i] == DRM_FORMAT_MOD_INVALID ||
          modifiers[i] == DRM_FORMAT_MOD_LINEAR)
        {
          may_alloc_linear = TRUE;
          break;
        }
    }

  if (!may_alloc_linear)
    return FALSE;

  switch (priv->format)
    {
    case DRM_FORMAT_ARGB8888:
      shm->n_planes = 1;
      shm_format = WL_SHM_FORMAT_ARGB8888;
      bpp[0] = 4;
      hsub[0] = 1;
      vsub[0] = 1;
      break;
    case DRM_FORMAT_XRGB8888:
      shm->n_planes = 1;
      shm_format = WL_SHM_FORMAT_XRGB8888;
      bpp[0] = 4;
      hsub[0] = 1;
      vsub[0] = 1;
      break;
    case DRM_FORMAT_YUYV:
      shm->n_planes = 1;
      shm_format = priv->format;
      bpp[0] = 2;
      hsub[0] = 1;
      vsub[0] = 1;
      break;
    case DRM_FORMAT_NV12:
      shm->n_planes = 2;
      shm_format = priv->format;
      bpp[0] = 1;
      bpp[1] = 2;
      hsub[0] = 1;
      hsub[1] = 2;
      vsub[0] = 1;
      vsub[1] = 2;
      break;
    case DRM_FORMAT_P010:
      shm->n_planes = 2;
      shm_format = priv->format;
      bpp[0] = 2;
      bpp[1] = 4;
      hsub[0] = 1;
      hsub[1] = 2;
      vsub[0] = 1;
      vsub[1] = 2;
      break;
    case DRM_FORMAT_YUV420:
      shm->n_planes = 3;
      shm_format = priv->format;
      bpp[0] = 1;
      bpp[1] = 1;
      bpp[2] = 1;
      hsub[0] = 1;
      hsub[1] = 2;
      hsub[2] = 2;
      vsub[0] = 1;
      vsub[1] = 2;
      vsub[2] = 2;
      break;
    default:
      g_assert_not_reached ();
    }

  for (i = 0; i < shm->n_planes; i++)
    {
      int stride;
      int size;

      stride = priv->width / hsub[i] * bpp[i];
      size = priv->height / vsub[i] * stride;

      shm->plane_offset[i] = shm->size;
      shm->stride[i] = stride;

      shm->size += size;
    }

  fd = create_anonymous_file (shm->size);
  if (fd < 0)
    {
      fprintf (stderr, "Creating a buffer file for %ld B failed: %m\n",
               shm->size);
      return FALSE;
    }

  shm->data = mmap (NULL, shm->size,
                    PROT_READ | PROT_WRITE, MAP_SHARED,
                    fd, 0);

  if (!shm->data)
    {
      fprintf (stderr, "mmaping shm buffer failed: %m\n");
      return FALSE;
    }

  pool = wl_shm_create_pool (display->shm, fd, shm->size);
  priv->buffer = wl_shm_pool_create_buffer (pool, 0,
                                            priv->width, priv->height,
                                            shm->stride[0],
                                            shm_format);
  wl_shm_pool_destroy (pool);

  shm->fd = g_steal_fd (&fd);

  return TRUE;
}

static void *
wayland_buffer_shm_mmap_plane (WaylandBuffer *buffer,
                               int            plane,
                               size_t        *stride_out)
{
  WaylandBufferShm *shm = WAYLAND_BUFFER_SHM (buffer);
  g_assert (plane < shm->n_planes);

  if (stride_out)
    *stride_out = shm->stride[plane];

  return ((uint8_t *) shm->data) + shm->plane_offset[plane];
}

static void
wayland_buffer_shm_dispose (GObject *object)
{
  WaylandBufferShm *shm = WAYLAND_BUFFER_SHM (object);

  g_clear_fd (&shm->fd, NULL);

  if (shm->data)
    {
      munmap (shm->data, shm->size);
      shm->data = NULL;
    }

  G_OBJECT_CLASS (wayland_buffer_shm_parent_class)->dispose (object);
}

static void
wayland_buffer_shm_class_init (WaylandBufferShmClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  WaylandBufferClass *buffer_class = WAYLAND_BUFFER_CLASS (klass);

  buffer_class->allocate = wayland_buffer_shm_allocate;
  buffer_class->mmap_plane = wayland_buffer_shm_mmap_plane;
  object_class->dispose = wayland_buffer_shm_dispose;
}

static void
wayland_buffer_shm_init (WaylandBufferShm *shm)
{
  shm->fd = -1;
}

static gboolean
alloc_dmabuf_simple (WaylandBuffer *buffer,
                     unsigned int   n_modifiers,
                     uint64_t      *modifiers,
                     uint32_t       bo_flags)
{
  WaylandBufferDmabuf *dmabuf = WAYLAND_BUFFER_DMABUF (buffer);
  WaylandBufferPrivate *priv = wayland_buffer_get_instance_private (buffer);
  WaylandDisplay *display = priv->display;
  struct gbm_device *gbm_device = display->gbm_device;
  struct zwp_linux_dmabuf_v1 *wl_dmabuf = display->linux_dmabuf;
  struct zwp_linux_buffer_params_v1 *wl_params;
  struct gbm_bo *bo = NULL;
  int i;

  if (n_modifiers > 0)
    {
      bo = gbm_bo_create_with_modifiers2 (gbm_device,
                                          priv->width, priv->height,
                                          priv->format,
                                          modifiers, n_modifiers,
                                          bo_flags);
  }

  if (!bo)
    {
      bo = gbm_bo_create (gbm_device,
                          priv->width, priv->height,
                          priv->format,
                          bo_flags);
    }

  if (!bo)
    return FALSE;

  dmabuf->modifier = gbm_bo_get_modifier (bo);
  dmabuf->bo[0] = bo;
  dmabuf->n_planes = gbm_bo_get_plane_count (bo);

  if (dmabuf->modifier == DRM_FORMAT_MOD_LINEAR ||
      (dmabuf->modifier == DRM_FORMAT_MOD_INVALID &&
       (bo_flags & GBM_BO_USE_LINEAR)))
    {
      dmabuf->data[0] = gbm_bo_map (dmabuf->bo[0], 0, 0,
                                    priv->width, priv->height,
                                    GBM_BO_TRANSFER_WRITE,
                                    &dmabuf->map_stride[0],
                                    &dmabuf->map_data[0]);

      if (!dmabuf->data[0])
        {
          g_clear_pointer (&bo, gbm_bo_destroy);
          return FALSE;
        }
    }

  wl_params = zwp_linux_dmabuf_v1_create_params (wl_dmabuf);

  for (i = 0; i < dmabuf->n_planes; i++)
    {
      dmabuf->fd[i] = gbm_bo_get_fd_for_plane (bo, i);
      dmabuf->stride[i] = gbm_bo_get_stride_for_plane (bo, i);
      dmabuf->offset[i] = gbm_bo_get_offset (bo, i);

      g_assert_cmpint (dmabuf->fd[i], >=, 0);
      g_assert_cmpint (dmabuf->stride[i], >, 0);

      zwp_linux_buffer_params_v1_add (wl_params, dmabuf->fd[i], i,
                                      dmabuf->offset[i], dmabuf->stride[i],
                                      dmabuf->modifier >> 32,
                                      dmabuf->modifier & 0xffffffff);
    }

  priv->buffer =
    zwp_linux_buffer_params_v1_create_immed (wl_params,
                                             priv->width,
                                             priv->height,
                                             priv->format,
                                             0);
  g_assert_nonnull (priv->buffer);

  return TRUE;
}

static gboolean
alloc_dmabuf_complex (WaylandBuffer *buffer,
                      uint32_t       bo_flags)
{
  WaylandBufferDmabuf *dmabuf = WAYLAND_BUFFER_DMABUF (buffer);
  WaylandBufferPrivate *priv = wayland_buffer_get_instance_private (buffer);
  WaylandDisplay *display = priv->display;
  struct gbm_device *gbm_device = display->gbm_device;
  struct zwp_linux_dmabuf_v1 *wl_dmabuf = display->linux_dmabuf;
  struct zwp_linux_buffer_params_v1 *wl_params;
  uint32_t formats[4];
  int hsub[4];
  int vsub[4];
  int i;

  dmabuf->modifier = DRM_FORMAT_MOD_LINEAR;

  switch (priv->format)
    {
    case DRM_FORMAT_YUYV:
      dmabuf->n_planes = 1;
      formats[0] = DRM_FORMAT_ARGB8888;
      hsub[0] = 2;
      vsub[0] = 1;
      break;
    case DRM_FORMAT_NV12:
      dmabuf->n_planes = 2;
      formats[0] = DRM_FORMAT_R8;
      formats[1] = DRM_FORMAT_RG88;
      hsub[0] = 1;
      hsub[1] = 2;
      vsub[0] = 1;
      vsub[1] = 2;
      break;
    case DRM_FORMAT_P010:
      dmabuf->n_planes = 2;
      formats[0] = DRM_FORMAT_R16;
      formats[1] = DRM_FORMAT_RG1616;
      hsub[0] = 1;
      hsub[1] = 2;
      vsub[0] = 1;
      vsub[1] = 2;
      break;
    case DRM_FORMAT_YUV420:
      dmabuf->n_planes = 3;
      formats[0] = DRM_FORMAT_R8;
      formats[1] = DRM_FORMAT_R8;
      formats[2] = DRM_FORMAT_R8;
      hsub[0] = 1;
      hsub[1] = 2;
      hsub[2] = 2;
      vsub[0] = 1;
      vsub[1] = 2;
      vsub[2] = 2;
      break;
    default:
      return FALSE;
    }

  wl_params = zwp_linux_dmabuf_v1_create_params (wl_dmabuf);

  for (i = 0; i < dmabuf->n_planes; i++)
    {
      size_t width = priv->width / hsub[i];
      size_t height = priv->height / vsub[i];

      dmabuf->bo[i] = gbm_bo_create_with_modifiers2 (gbm_device,
                                                     width, height,
                                                     formats[i],
                                                     &dmabuf->modifier, 1,
                                                     bo_flags | GBM_BO_USE_LINEAR);

      if (!dmabuf->bo[i])
        {
          dmabuf->bo[i] = gbm_bo_create (gbm_device,
                                         width, height,
                                         formats[i],
                                         bo_flags | GBM_BO_USE_LINEAR);
        }

      if (!dmabuf->bo[i])
        break;

      dmabuf->data[i] = gbm_bo_map (dmabuf->bo[i], 0, 0,
                                    width, height,
                                    GBM_BO_TRANSFER_WRITE,
                                    &dmabuf->map_stride[i],
                                    &dmabuf->map_data[i]);

      if (!dmabuf->data[i])
        break;

      dmabuf->fd[i] = gbm_bo_get_fd_for_plane (dmabuf->bo[i], 0);
      dmabuf->stride[i] = gbm_bo_get_stride_for_plane (dmabuf->bo[i], 0);
      dmabuf->offset[i] = gbm_bo_get_offset (dmabuf->bo[i], 0);

      zwp_linux_buffer_params_v1_add (wl_params, dmabuf->fd[i], i,
                                      dmabuf->offset[i], dmabuf->stride[i],
                                      dmabuf->modifier >> 32,
                                      dmabuf->modifier & 0xffffffff);
    }

  if (i != dmabuf->n_planes)
    {
      for (i = 0; i < dmabuf->n_planes; i++)
        {
          if (dmabuf->data[i])
            {
              gbm_bo_unmap (dmabuf->bo[i], dmabuf->map_data[i]);
              dmabuf->data[i] = NULL;
              dmabuf->map_data[i] = NULL;
            }

          g_clear_pointer (&dmabuf->bo[i], gbm_bo_destroy);
        }

      return FALSE;
    }

  priv->buffer =
    zwp_linux_buffer_params_v1_create_immed (wl_params,
                                             priv->width,
                                             priv->height,
                                             priv->format,
                                             0);
  g_assert_nonnull (priv->buffer);

  return TRUE;
}

static gboolean
wayland_buffer_dmabuf_allocate (WaylandBuffer *buffer,
                                unsigned int   n_modifiers,
                                uint64_t      *modifiers,
                                uint32_t       bo_flags)
{
  WaylandBufferPrivate *priv = wayland_buffer_get_instance_private (buffer);
  WaylandDisplay *display = priv->display;
  DmaBufFormat *dma_buf_format;
  gboolean may_alloc_linear;
  int i;

  dma_buf_format = g_hash_table_lookup (display->formats,
                                        GUINT_TO_POINTER (priv->format));
  g_assert_nonnull (dma_buf_format);

  if (alloc_dmabuf_simple (buffer, n_modifiers, modifiers, bo_flags))
    return TRUE;

  may_alloc_linear = !modifiers;
  for (i = 0; i < n_modifiers; i++)
    {
      if (modifiers[i] == DRM_FORMAT_MOD_INVALID ||
          modifiers[i] == DRM_FORMAT_MOD_LINEAR)
        {
          may_alloc_linear = TRUE;
          break;
        }
    }

  if (!may_alloc_linear)
    return FALSE;

  return alloc_dmabuf_complex (buffer, bo_flags);
}

static void *
wayland_buffer_dmabuf_mmap_plane (WaylandBuffer *buffer,
                                  int            plane,
                                  size_t        *stride_out)
{
  WaylandBufferDmabuf *dmabuf = WAYLAND_BUFFER_DMABUF (buffer);
  g_assert (plane < dmabuf->n_planes);

  if (stride_out)
    *stride_out = dmabuf->map_stride[plane];

  return (uint8_t *) dmabuf->data[plane];
}

static void
wayland_buffer_dmabuf_dispose (GObject *object)
{
  WaylandBufferDmabuf *dmabuf = WAYLAND_BUFFER_DMABUF (object);
  int i;

  for (i = 0; i < 4; i++)
    {
      if (dmabuf->data[i])
        {
          gbm_bo_unmap (dmabuf->bo[i], dmabuf->map_data[i]);
          dmabuf->data[i] = NULL;
          dmabuf->map_data[i] = NULL;
        }

      g_clear_pointer (&dmabuf->bo[i], gbm_bo_destroy);
    }

  for (i = 0; i < dmabuf->n_planes; i++)
    g_clear_fd (&dmabuf->fd[i], NULL);
}

static void
wayland_buffer_dmabuf_class_init (WaylandBufferDmabufClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  WaylandBufferClass *buffer_class = WAYLAND_BUFFER_CLASS (klass);

  buffer_class->allocate = wayland_buffer_dmabuf_allocate;
  buffer_class->mmap_plane = wayland_buffer_dmabuf_mmap_plane;
  object_class->dispose = wayland_buffer_dmabuf_dispose;
}

static void
wayland_buffer_dmabuf_init (WaylandBufferDmabuf *dmabuf)
{
  int i;

  for (i = 0; i < 4; i++)
    dmabuf->fd[i] = -1;
}
