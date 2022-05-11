#ifndef WAYLAND_TEST_CLIENT_UTILS_H
#define WAYLAND_TEST_CLIENT_UTILS_H

#include <stdio.h>
#include <wayland-client.h>

#include "test-driver-client-protocol.h"
#include "xdg-shell-client-protocol.h"

typedef enum _WaylandDisplayCapabilities
{
  WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER = 1 << 0,
} WaylandDisplayCapabilities;

typedef struct _WaylandDisplay
{
  WaylandDisplayCapabilities capabilities;

  struct wl_display *display;
  struct wl_registry *registry;
  struct wl_compositor *compositor;
  struct wl_subcompositor *subcompositor;
  struct xdg_wm_base *xdg_wm_base;
  struct wl_shm *shm;
  struct test_driver *test_driver;
} WaylandDisplay;

int create_anonymous_file (off_t size);

WaylandDisplay * wayland_display_new (WaylandDisplayCapabilities capabilities);

#endif /* WAYLAND_TEST_CLIENT_UTILS_H */
