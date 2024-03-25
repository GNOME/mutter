#pragma once

#include <drm_fourcc.h>
#include <gbm.h>
#include <glib-object.h>
#include <stdio.h>
#include <wayland-client.h>

#include "fractional-scale-v1-client-protocol.h"
#include "linux-dmabuf-v1-client-protocol.h"
#include "single-pixel-buffer-v1-client-protocol.h"
#include "test-driver-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "xdg-shell-client-protocol.h"

typedef enum _WaylandDisplayCapabilities
{
  WAYLAND_DISPLAY_CAPABILITY_NONE = 0,
  WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER = 1 << 0,
  WAYLAND_DISPLAY_CAPABILITY_XDG_SHELL_V4 = 1 << 1,
  WAYLAND_DISPLAY_CAPABILITY_XDG_SHELL_V6 = 1 << 2,
} WaylandDisplayCapabilities;

typedef struct _DmaBufFormat
{
  uint32_t format;
  uint64_t *modifiers;
  int n_modifiers;
} DmaBufFormat;

typedef struct _WaylandDisplay
{
  GObject parent;

  WaylandDisplayCapabilities capabilities;

  struct wl_display *display;
  struct wl_registry *registry;
  struct wl_compositor *compositor;
  struct wl_subcompositor *subcompositor;
  struct wl_shm *shm;
  struct zwp_linux_dmabuf_v1 *linux_dmabuf;
  struct wp_fractional_scale_manager_v1 *fractional_scale_mgr;
  struct wp_single_pixel_buffer_manager_v1 *single_pixel_mgr;
  struct wp_viewporter *viewporter;
  struct xdg_wm_base *xdg_wm_base;
  struct test_driver *test_driver;

  uint32_t sync_event_serial_next;

  GHashTable *properties;

  struct gbm_device *gbm_device;

  /* format to DmaBufFormat mapping */
  GHashTable *formats;
} WaylandDisplay;

#define WAYLAND_TYPE_DISPLAY (wayland_display_get_type ())
G_DECLARE_FINAL_TYPE (WaylandDisplay, wayland_display,
                      WAYLAND, DISPLAY,
                      GObject)

typedef struct _WaylandSurface
{
  GObject parent;

  WaylandDisplay *display;

  struct wl_surface *wl_surface;
  struct xdg_surface *xdg_surface;
  struct xdg_toplevel *xdg_toplevel;

  GHashTable *pending_state;
  GHashTable *current_state;

  int default_width;
  int default_height;
  int width;
  int height;

  uint32_t color;
  gboolean is_opaque;
} WaylandSurface;

#define WAYLAND_TYPE_SURFACE (wayland_surface_get_type ())
G_DECLARE_FINAL_TYPE (WaylandSurface, wayland_surface,
                      WAYLAND, SURFACE,
                      GObject)

#define WAYLAND_TYPE_BUFFER (wayland_buffer_get_type ())
G_DECLARE_DERIVABLE_TYPE (WaylandBuffer, wayland_buffer,
                          WAYLAND, BUFFER,
                          GObject)

int create_anonymous_file (off_t size);

WaylandDisplay * wayland_display_new (WaylandDisplayCapabilities capabilities);

WaylandDisplay * wayland_display_new_full (WaylandDisplayCapabilities  capabilities,
                                           struct wl_display          *wayland_display);

void wayland_display_dispatch (WaylandDisplay *display);

WaylandSurface * wayland_surface_new (WaylandDisplay *display,
                                      const char     *title,
                                      int             default_width,
                                      int             default_height,
                                      uint32_t        color);

gboolean wayland_surface_has_state (WaylandSurface          *surface,
                                    enum xdg_toplevel_state  state);

void wayland_surface_set_opaque (WaylandSurface *surface);

void draw_surface (WaylandDisplay    *display,
                   struct wl_surface *surface,
                   int                width,
                   int                height,
                   uint32_t           color);

const char * lookup_property_value (WaylandDisplay *display,
                                    const char     *name);

void wait_for_effects_completed (WaylandDisplay    *display,
                                 struct wl_surface *surface);

void wait_for_window_shown (WaylandDisplay    *display,
                            struct wl_surface *surface);

void wait_for_view_verified (WaylandDisplay *display,
                             int             sequence);

void wait_for_sync_event (WaylandDisplay *display,
                          uint32_t        serial);

WaylandBuffer *wayland_buffer_create (WaylandDisplay                  *display,
                                      const struct wl_buffer_listener *listener,
                                      uint32_t                         width,
                                      uint32_t                         height,
                                      uint32_t                         format,
                                      uint64_t                        *modifiers,
                                      unsigned int                     n_modifiers,
                                      uint32_t                         bo_flags);

struct wl_buffer * wayland_buffer_get_wl_buffer (WaylandBuffer *buffer);

void wayland_buffer_fill_color (WaylandBuffer *buffer,
                                uint32_t       color);

void wayland_buffer_draw_pixel (WaylandBuffer *buffer,
                                size_t         x,
                                size_t         y,
                                uint32_t       color);

void * wayland_buffer_mmap_plane (WaylandBuffer *buffer,
                                  int            plane,
                                  size_t        *stride_out);
