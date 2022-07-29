#ifndef WAYLAND_TEST_CLIENT_UTILS_H
#define WAYLAND_TEST_CLIENT_UTILS_H

#include <glib-object.h>
#include <stdio.h>
#include <wayland-client.h>

#include "single-pixel-buffer-v1-client-protocol.h"
#include "test-driver-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "xdg-shell-client-protocol.h"

typedef enum _WaylandDisplayCapabilities
{
  WAYLAND_DISPLAY_CAPABILITY_NONE = 0,
  WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER = 1 << 0,
  WAYLAND_DISPLAY_CAPABILITY_XDG_SHELL_V4 = 1 << 1,
} WaylandDisplayCapabilities;

typedef struct _WaylandDisplay
{
  GObject parent;

  WaylandDisplayCapabilities capabilities;

  struct wl_display *display;
  struct wl_registry *registry;
  struct wl_compositor *compositor;
  struct wl_subcompositor *subcompositor;
  struct wl_shm *shm;
  struct wp_single_pixel_buffer_manager_v1 *single_pixel_mgr;
  struct wp_viewporter *viewporter;
  struct xdg_wm_base *xdg_wm_base;
  struct test_driver *test_driver;

  GHashTable *properties;
} WaylandDisplay;

G_DECLARE_FINAL_TYPE (WaylandDisplay, wayland_display,
                      WAYLAND, DISPLAY,
                      GObject)

int create_anonymous_file (off_t size);

WaylandDisplay * wayland_display_new (WaylandDisplayCapabilities capabilities);

gboolean create_shm_buffer (WaylandDisplay    *display,
                            int                width,
                            int                height,
                            struct wl_buffer **out_buffer,
                            void             **out_data,
                            int               *out_size);

void draw_surface (WaylandDisplay    *display,
                   struct wl_surface *surface,
                   int                width,
                   int                height,
                   uint32_t           color);

const char * lookup_property_value (WaylandDisplay *display,
                                    const char     *name);

void wait_for_effects_completed (WaylandDisplay    *display,
                                 struct wl_surface *surface);

void wait_for_view_verified (WaylandDisplay *display,
                             int             sequence);

#endif /* WAYLAND_TEST_CLIENT_UTILS_H */
