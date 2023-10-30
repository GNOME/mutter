#pragma once

#include <glib-object.h>
#include <stdio.h>
#include <wayland-client.h>

#include "fractional-scale-v1-client-protocol.h"
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
  struct wp_fractional_scale_manager_v1 *fractional_scale_mgr;
  struct wp_single_pixel_buffer_manager_v1 *single_pixel_mgr;
  struct wp_viewporter *viewporter;
  struct xdg_wm_base *xdg_wm_base;
  struct test_driver *test_driver;

  uint32_t sync_event_serial_next;

  GHashTable *properties;
} WaylandDisplay;

typedef struct _WaylandSurface
{
  WaylandDisplay *display;

  struct wl_surface *wl_surface;
  struct xdg_surface *xdg_surface;
  struct xdg_toplevel *xdg_toplevel;

  int default_width;
  int default_height;
  int width;
  int height;

  uint32_t color;
} WaylandSurface;

G_DECLARE_FINAL_TYPE (WaylandDisplay, wayland_display,
                      WAYLAND, DISPLAY,
                      GObject)

int create_anonymous_file (off_t size);

WaylandDisplay * wayland_display_new (WaylandDisplayCapabilities capabilities);

WaylandDisplay * wayland_display_new_full (WaylandDisplayCapabilities  capabilities,
                                           struct wl_display          *wayland_display);

WaylandSurface * wayland_surface_new (WaylandDisplay *display,
                                      const char     *title,
                                      int             default_width,
                                      int             default_height,
                                      uint32_t        color);

void wayland_surface_free (WaylandSurface *surface);

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

void wait_for_sync_event (WaylandDisplay *display,
                          uint32_t        serial);
