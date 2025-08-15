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

enum
{
  SURFACE_CONFIGURE,
  SURFACE_POINTER_ENTER,
  SURFACE_KEYBOARD_ENTER,
  N_SURFACE_SIGNALS
};

static guint surface_signals[N_SURFACE_SIGNALS];

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

typedef struct _WaylandSource
{
  GSource source;
  GPollFD pfd;
  gboolean reading;

  WaylandDisplay *display;
} WaylandSource;

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

  path = g_get_user_runtime_dir ();
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

  gpu_path = lookup_property_string (display, "gpu-path");
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
pointer_handle_enter (void              *user_data,
                      struct wl_pointer *pointer,
                      uint32_t           serial,
                      struct wl_surface *surface_resource,
                      wl_fixed_t         sx,
                      wl_fixed_t         sy)
{
  WaylandSurface *surface = wl_surface_get_user_data (surface_resource);

  g_signal_emit (surface, surface_signals[SURFACE_POINTER_ENTER],
                 0, pointer, serial);
}

static void
pointer_handle_leave (void              *user_data,
                      struct wl_pointer *pointer,
                      uint32_t           serial,
                      struct wl_surface *surface)
{
}

static void
pointer_handle_motion (void              *user_data,
                       struct wl_pointer *pointer,
                       uint32_t           time,
                       wl_fixed_t         sx,
                       wl_fixed_t         sy)
{
}

static void
pointer_handle_button (void              *user_data,
                       struct wl_pointer *wl_pointer,
                       uint32_t           serial,
                       uint32_t           time,
                       uint32_t           button,
                       uint32_t           state)
{
}

static void
pointer_handle_axis (void              *user_data,
                     struct wl_pointer *wl_pointer,
                     uint32_t           time,
                     uint32_t           axis,
                     wl_fixed_t         value)
{
}

static const struct wl_pointer_listener wl_pointer_listener = {
  pointer_handle_enter,
  pointer_handle_leave,
  pointer_handle_motion,
  pointer_handle_button,
  pointer_handle_axis,
};

static void
wl_keyboard_keymap (void               *user_data,
                    struct wl_keyboard *wl_keyboard,
                    uint32_t            format,
                    int32_t             fd,
                    uint32_t            size)
{
}

static void
wl_keyboard_enter (void               *user_data,
                   struct wl_keyboard *keyboard,
                   uint32_t            serial,
                   struct wl_surface  *surface_resource,
                   struct wl_array    *keys)
{
  WaylandSurface *surface = wl_surface_get_user_data (surface_resource);

  g_signal_emit (surface, surface_signals[SURFACE_KEYBOARD_ENTER],
                 0, keyboard, serial);
}

static void
wl_keyboard_leave (void               *user_data,
                   struct wl_keyboard *wl_keyboard,
                   uint32_t            serial,
                   struct wl_surface  *surface)
{
}

static void
wl_keyboard_key (void               *user_data,
                 struct wl_keyboard *wl_keyboard,
                 uint32_t            serial,
                 uint32_t            time,
                 uint32_t            key,
                 uint32_t            state)
{
}

static void
wl_keyboard_modifiers (void               *user_data,
                       struct wl_keyboard *wl_keyboard,
                       uint32_t            serial,
                       uint32_t            mods_depressed,
                       uint32_t            mods_latched,
                       uint32_t            mods_locked,
                       uint32_t            group)
{
}

static void
wl_keyboard_repeat_info (void               *data,
                         struct wl_keyboard *wl_keyboard,
                         int32_t             rate,
                         int32_t             delay)
{
}

static const struct wl_keyboard_listener wl_keyboard_listener = {
  wl_keyboard_keymap,
  wl_keyboard_enter,
  wl_keyboard_leave,
  wl_keyboard_key,
  wl_keyboard_modifiers,
  wl_keyboard_repeat_info,
};

static void
handle_wl_seat_capabilities (void           *user_data,
                             struct wl_seat *wl_seat,
                             uint32_t        capabilities)
{
  WaylandDisplay *display = user_data;

  if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !display->wl_pointer)
    {
      display->wl_pointer = wl_seat_get_pointer (wl_seat);
      wl_pointer_add_listener (display->wl_pointer,
                               &wl_pointer_listener, display);
    }
  else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) && display->wl_pointer)
    {
      g_clear_pointer (&display->wl_pointer, wl_pointer_release);
    }

  if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !display->wl_keyboard)
    {
      display->wl_keyboard = wl_seat_get_keyboard (wl_seat);
      wl_keyboard_add_listener (display->wl_keyboard,
                                &wl_keyboard_listener, display);
    }
  else if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && display->wl_keyboard)
    {
      g_clear_pointer (&display->wl_keyboard, wl_keyboard_release);
    }
}

static void
handle_wl_seat_name (void           *user_data,
		     struct wl_seat *wl_seat,
		     const char     *name)
{
}

static const struct wl_seat_listener wl_seat_listener = {
  handle_wl_seat_capabilities,
  handle_wl_seat_name,
};

static void
test_driver_handle_terminate (void               *user_data,
                              struct test_driver *test_driver)
{
  exit (EXIT_SUCCESS);
}

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
                        g_variant_new_string (value));
}

static void
test_driver_handle_property_int (void               *user_data,
                                 struct test_driver *test_driver,
                                 const char         *name,
                                 const uint32_t      value)
{
  WaylandDisplay *display = WAYLAND_DISPLAY (user_data);

  g_hash_table_replace (display->properties,
                        g_strdup (name),
                        g_variant_new_int32 (value));
}

static const struct test_driver_listener test_driver_listener = {
  test_driver_handle_terminate,
  test_driver_handle_sync_event,
  test_driver_handle_property,
  test_driver_handle_property_int,
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
                          MIN (version, 6));
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
  else if (strcmp (interface, wp_color_manager_v1_interface.name) == 0)
    {
      display->color_management_mgr =
        wl_registry_bind (registry, id,
                          &wp_color_manager_v1_interface, 1);
    }
  else if (strcmp (interface, wp_cursor_shape_manager_v1_interface.name) == 0)
    {
      int cusor_shape_version = 1;

      if (display->capabilities & WAYLAND_DISPLAY_CAPABILITY_CURSOR_SHAPE_V2)
        cusor_shape_version = 2;

      display->cursor_shape_mgr =
        wl_registry_bind (registry, id,
                          &wp_cursor_shape_manager_v1_interface,
                          cusor_shape_version);
    }
  else if (strcmp (interface, wp_viewporter_interface.name) == 0)
    {
      display->viewporter = wl_registry_bind (registry, id,
                                              &wp_viewporter_interface, 1);
    }
  else if (strcmp (interface, wp_color_representation_manager_v1_interface.name) == 0)
    {
      display->color_representation =
        wl_registry_bind (registry, id,
                          &wp_color_representation_manager_v1_interface, 1);
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
  else if (strcmp (interface, wl_seat_interface.name) == 0)
    {
      g_assert_null (display->wl_seat);

      display->wl_seat = wl_registry_bind (registry, id,
                                           &wl_seat_interface,
                                           3);
      wl_seat_add_listener (display->wl_seat, &wl_seat_listener, display);
      display->needs_roundtrip = TRUE;
    }
  else if (strcmp (interface, xdg_toplevel_tag_manager_v1_interface.name) == 0)
    {
      display->toplevel_tag_manager =
        wl_registry_bind (registry, id,
                          &xdg_toplevel_tag_manager_v1_interface, 1);
    }
  else if (strcmp (interface, xdg_activation_v1_interface.name) == 0)
    {
      display->xdg_activation =
        wl_registry_bind (registry, id,
                          &xdg_activation_v1_interface, 1);
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

static gboolean
wayland_source_prepare (GSource *base,
                        int     *timeout)
{
  WaylandSource *source = (WaylandSource *) base;

  *timeout = -1;

  if (source->reading)
    return FALSE;

  if (wl_display_prepare_read (source->display->display) != 0)
    return TRUE;
  source->reading = TRUE;

  if (wl_display_flush (source->display->display) < 0)
    g_error ("Error flushing display: %s", g_strerror (errno));

  return FALSE;
}

static gboolean
wayland_source_check (GSource *base)
{
  WaylandSource *source = (WaylandSource *) base;

  if (source->reading)
    {
      if (source->pfd.revents & G_IO_IN)
        {
          if (wl_display_read_events (source->display->display) < 0)
            {
              g_error ("Error reading events from display: %s",
                       g_strerror (errno));
            }
        }
      else
        {
          wl_display_cancel_read (source->display->display);
        }
      source->reading = FALSE;
    }

  return source->pfd.revents;
}

static gboolean
wayland_source_dispatch (GSource     *base,
                         GSourceFunc  callback,
                         gpointer     data)
{
  WaylandSource *source = (WaylandSource *) base;

  while (TRUE)
    {
      int ret;

      ret = wl_display_dispatch_pending (source->display->display);
      if (ret < 0)
        g_error ("Failed to dispatch pending: %s", g_strerror (errno));
      else if (ret == 0)
        break;
    }

  return TRUE;
}

static void
wayland_source_finalize (GSource *base)
{
  WaylandSource *source = (WaylandSource *) base;

  if (source->reading)
    wl_display_cancel_read (source->display->display);
  source->reading = FALSE;
}

static GSourceFuncs wayland_source_funcs = {
  .prepare = wayland_source_prepare,
  .check = wayland_source_check,
  .dispatch = wayland_source_dispatch,
  .finalize = wayland_source_finalize,
};

static GSource *
wayland_source_new (WaylandDisplay *display)
{
  GSource *source;
  WaylandSource *wayland_source;
  g_autofree char *name = NULL;

  source = g_source_new (&wayland_source_funcs,
			 sizeof (WaylandSource));
  name = g_strdup_printf ("Wayland GSource");
  g_source_set_name (source, name);
  wayland_source = (WaylandSource *) source;

  wayland_source->display = display;
  wayland_source->pfd.fd = wl_display_get_fd (display->display);
  wayland_source->pfd.events = G_IO_IN | G_IO_ERR | G_IO_HUP;
  g_source_add_poll (source, &wayland_source->pfd);

  g_source_set_priority (source, G_PRIORITY_DEFAULT);
  g_source_set_can_recurse (source, TRUE);

  return source;
}

WaylandDisplay *
wayland_display_new_full (WaylandDisplayCapabilities  capabilities,
                          struct wl_display          *wayland_display)
{
  WaylandDisplay *display;

  g_assert_nonnull (wayland_display);

  display = g_object_new (WAYLAND_TYPE_DISPLAY, NULL);

  display->capabilities = capabilities;
  display->properties = g_hash_table_new_full (g_str_hash, g_str_equal,
                                               g_free,
                                               (GDestroyNotify) g_variant_unref);
  display->formats = g_hash_table_new_full (NULL, NULL, NULL, g_free);

  display->display = wayland_display;

  display->registry = wl_display_get_registry (display->display);
  wl_registry_add_listener (display->registry, &registry_listener, display);
  wl_display_roundtrip (display->display);

  while (display->needs_roundtrip)
    {
      display->needs_roundtrip = FALSE;
      wl_display_roundtrip (display->display);
    }

  g_assert_nonnull (display->compositor);
  g_assert_nonnull (display->subcompositor);
  g_assert_nonnull (display->shm);
  g_assert_nonnull (display->single_pixel_mgr);
  g_assert_nonnull (display->viewporter);
  g_assert_nonnull (display->xdg_wm_base);
  g_assert_nonnull (display->toplevel_tag_manager);
  g_assert_nonnull (display->xdg_activation);

  if (capabilities & WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER)
    g_assert_nonnull (display->test_driver);

  wl_display_roundtrip (display->display);

  display->gbm_device = create_gbm_device (display);

  display->source = wayland_source_new (display);
  g_source_attach (display->source, g_main_context_get_thread_default ());
  g_source_unref (display->source);

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

  g_clear_pointer (&display->source, g_source_destroy);
  g_clear_pointer (&display->test_state, display->destroy_test_state);
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

  if (surface->fixed_size)
    {
      surface->width = surface->default_width;
      surface->height = surface->default_height;
    }
  else
    {
      if (width == 0)
        surface->width = surface->default_width;
      else
        surface->width = width;

      if (height == 0)
        surface->height = surface->default_height;
      else
        surface->height = height;
    }

  g_clear_pointer (&surface->pending_state, g_hash_table_unref);
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

void
wayland_surface_commit (WaylandSurface *surface)
{
  if (!surface->has_alpha)
    {
      struct wl_region *opaque_region;

      opaque_region = wl_compositor_create_region (surface->display->compositor);
      wl_region_add (opaque_region, 0, 0, surface->width, surface->height);
      wl_surface_set_opaque_region (surface->wl_surface, opaque_region);
      wl_region_destroy (opaque_region);
    }

  wl_surface_damage_buffer (surface->wl_surface,
                            0, 0, surface->width, surface->height);

  xdg_surface_ack_configure (surface->xdg_surface, surface->last_serial);
  wl_surface_commit (surface->wl_surface);

  g_clear_pointer (&surface->current_state, g_hash_table_unref);
  surface->current_state = g_steal_pointer (&surface->pending_state);

  g_signal_emit (surface->display, signals[SURFACE_PAINTED], 0, surface);
}

static void
handle_xdg_surface_configure (void               *data,
                              struct xdg_surface *xdg_surface,
                              uint32_t            serial)
{
  WaylandSurface *surface = data;

  surface->last_serial = serial;

  g_signal_emit (surface, surface_signals[SURFACE_CONFIGURE], 0);

  if (surface->manual_paint)
    return;

  draw_surface (surface->display,
                surface->wl_surface,
                surface->width, surface->height,
                surface->color);
  wayland_surface_commit (surface);
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

  surface_signals[SURFACE_CONFIGURE] =
    g_signal_new ("configure",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  surface_signals[SURFACE_POINTER_ENTER] =
    g_signal_new ("pointer-enter",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 2,
                  G_TYPE_POINTER,
                  G_TYPE_UINT);

  surface_signals[SURFACE_KEYBOARD_ENTER] =
    g_signal_new ("keyboard-enter",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 2,
                  G_TYPE_POINTER,
                  G_TYPE_UINT);
}

static void
wayland_surface_init (WaylandSurface *surface)
{
}

static void
surface_enter (void              *user_data,
               struct wl_surface *wl_surface,
               struct wl_output  *output)
{
}

static void
surface_leave (void              *user_data,
               struct wl_surface *wl_surface,
               struct wl_output  *output)
{
}

static void
surface_preferred_buffer_scale (void              *user_data,
                                struct wl_surface *wl_surface,
                                int32_t            factor)
{
  WaylandSurface *surface = user_data;

  surface->preferred_buffer_scale = factor;
}

static void
surface_preferred_buffer_transform (void              *user_data,
                                    struct wl_surface *wl_surface,
                                    uint32_t           transform)
{
}

static const struct wl_surface_listener surface_listener = {
  surface_enter,
  surface_leave,
  surface_preferred_buffer_scale,
  surface_preferred_buffer_transform,
};

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
  wl_surface_add_listener (surface->wl_surface,
                           &surface_listener,
                           surface);
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

WaylandSurface *
wayland_surface_new_unassigned (WaylandDisplay *display)
{
  WaylandSurface *surface;

  surface = g_object_new (WAYLAND_TYPE_SURFACE, NULL);

  surface->display = display;
  surface->wl_surface = wl_compositor_create_surface (display->compositor);
  wl_surface_add_listener (surface->wl_surface,
                           &surface_listener,
                           surface);

  return surface;
}

gboolean
wayland_surface_has_state (WaylandSurface          *surface,
                           enum xdg_toplevel_state  state)
{
  if (surface->pending_state &&
      g_hash_table_contains (surface->pending_state, GUINT_TO_POINTER (state)))
    return TRUE;

  if (surface->current_state &&
      g_hash_table_contains (surface->current_state, GUINT_TO_POINTER (state)))
    return TRUE;

  return FALSE;
}

void
wayland_surface_fixate_size (WaylandSurface *surface)
{
  surface->fixed_size = TRUE;
  xdg_toplevel_set_min_size (surface->xdg_toplevel,
                             surface->default_width,
                             surface->default_height);
  xdg_toplevel_set_max_size (surface->xdg_toplevel,
                             surface->default_width,
                             surface->default_height);
}

const char *
lookup_property_string (WaylandDisplay *display,
                        const char     *name)
{
  GVariant *variant;
  const char *value;

  variant = g_hash_table_lookup (display->properties, name);
  if (!variant)
    return NULL;

  g_variant_get (variant, "&s", &value);

  return value;
}

int32_t
lookup_property_int (WaylandDisplay *display,
                     const char     *name)
{
  GVariant *variant;

  variant = g_hash_table_lookup (display->properties, name);
  g_return_val_if_fail (variant, -1);
  return g_variant_get_int32 (variant);
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
on_configure (WaylandSurface *surface,
              gboolean       *configured)
{
  *configured = TRUE;
}

void
wait_for_window_configured (WaylandDisplay *display,
                            WaylandSurface *surface)
{
  gboolean configured = FALSE;
  gulong configure_handler_id;

  configure_handler_id = g_signal_connect (surface, "configure",
                                           G_CALLBACK (on_configure),
                                           &configured);
  while (!configured)
    g_main_context_iteration (NULL, TRUE);
  g_signal_handler_disconnect (surface, configure_handler_id);
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
    case DRM_FORMAT_YUV422:
      shm->n_planes = 3;
      shm_format = priv->format;
      bpp[0] = 1;
      bpp[1] = 1;
      bpp[2] = 1;
      hsub[0] = 1;
      hsub[1] = 2;
      hsub[2] = 2;
      vsub[0] = 1;
      vsub[1] = 1;
      vsub[2] = 1;
      break;
    case DRM_FORMAT_YUV444:
      shm->n_planes = 3;
      shm_format = priv->format;
      bpp[0] = 1;
      bpp[1] = 1;
      bpp[2] = 1;
      hsub[0] = 1;
      hsub[1] = 1;
      hsub[2] = 1;
      vsub[0] = 1;
      vsub[1] = 1;
      vsub[2] = 1;
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
  g_assert_true (plane < shm->n_planes);

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
    case DRM_FORMAT_YUV422:
      dmabuf->n_planes = 3;
      formats[0] = DRM_FORMAT_R8;
      formats[1] = DRM_FORMAT_R8;
      formats[2] = DRM_FORMAT_R8;
      hsub[0] = 1;
      hsub[1] = 2;
      hsub[2] = 2;
      vsub[0] = 1;
      vsub[1] = 1;
      vsub[2] = 1;
      break;
    case DRM_FORMAT_YUV444:
      dmabuf->n_planes = 3;
      formats[0] = DRM_FORMAT_R8;
      formats[1] = DRM_FORMAT_R8;
      formats[2] = DRM_FORMAT_R8;
      hsub[0] = 1;
      hsub[1] = 1;
      hsub[2] = 1;
      vsub[0] = 1;
      vsub[1] = 1;
      vsub[2] = 1;
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
  g_assert_true (plane < dmabuf->n_planes);

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
