/*
 * Copyright (C) 2023 NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Written by:
 *     Austin Shafer <ashafer@nvidia.com>
 */

#include "config.h"

#include "backends/native/meta-backend-native-types.h"
#include "backends/native/meta-device-pool.h"
#include "backends/native/meta-renderer-native.h"
#include "meta/util.h"
#include "wayland/meta-wayland-buffer.h"
#include "wayland/meta-wayland-linux-drm-syncobj.h"
#include "wayland/meta-wayland-private.h"
#include <fcntl.h>
#include <glib/gstdio.h>

typedef struct _MetaWaylandDrmSyncobjManager
{
  GObject parent;

  int drm;
} MetaWaylandDrmSyncobjManager;

typedef struct _MetaWaylandSyncobjSurface
{
  GObject parent;

  struct wl_resource *resource;
  MetaWaylandSurface *surface;
  gulong surface_destroy_handler_id;
} MetaWaylandSyncobjSurface;

typedef struct _MetaWaylandSyncobjTimeline
{
  GObject parent;

  MetaDrmTimeline *drm_timeline;
} MetaWaylandSyncobjTimeline;

#define META_TYPE_WAYLAND_DRM_SYNCOBJ_MANAGER (meta_wayland_drm_syncobj_manager_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandDrmSyncobjManager, meta_wayland_drm_syncobj_manager,
                      META, WAYLAND_DRM_SYNCOBJ_MANAGER, GObject)

#define META_TYPE_WAYLAND_SYNCOBJ_SURFACE (meta_wayland_syncobj_surface_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandSyncobjSurface, meta_wayland_syncobj_surface,
                      META, WAYLAND_SYNCOBJ_SURFACE, GObject)

#define META_TYPE_WAYLAND_SYNCOBJ_TIMELINE (meta_wayland_syncobj_timeline_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandSyncobjTimeline, meta_wayland_syncobj_timeline,
                      META, WAYLAND_SYNCOBJ_TIMELINE, GObject)

#define META_TYPE_WAYLAND_DRM_SYNCOBJ_MANAGER (meta_wayland_drm_syncobj_manager_get_type ())
G_DEFINE_FINAL_TYPE (MetaWaylandDrmSyncobjManager, meta_wayland_drm_syncobj_manager,
                     G_TYPE_OBJECT)

#define META_TYPE_WAYLAND_SYNCOBJ_SURFACE (meta_wayland_syncobj_surface_get_type ())
G_DEFINE_FINAL_TYPE (MetaWaylandSyncobjSurface, meta_wayland_syncobj_surface,
                     G_TYPE_OBJECT)

#define META_TYPE_WAYLAND_SYNCOBJ_TIMELINE (meta_wayland_syncobj_timeline_get_type ())
G_DEFINE_FINAL_TYPE (MetaWaylandSyncobjTimeline, meta_wayland_syncobj_timeline,
                     G_TYPE_OBJECT)

G_DEFINE_FINAL_TYPE (MetaWaylandSyncPoint, meta_wayland_sync_point, G_TYPE_OBJECT);

static GQuark quark_syncobj_surface;

static void
meta_wayland_sync_point_set (MetaWaylandSyncPoint      **sync_point_ptr,
                             MetaWaylandSyncobjTimeline *syncobj_timeline,
                             uint32_t                    point_hi,
                             uint32_t                    point_lo)
{
  MetaWaylandSyncPoint *sync_point;

  if (!*sync_point_ptr)
    *sync_point_ptr = g_object_new (META_TYPE_WAYLAND_SYNC_POINT, NULL);

  sync_point = *sync_point_ptr;
  g_set_object (&sync_point->timeline, syncobj_timeline);
  sync_point->sync_point = (uint64_t)point_hi << 32 | point_lo;
}

static void
meta_wayland_sync_point_finalize (GObject *object)
{
  MetaWaylandSyncPoint *sync = META_WAYLAND_SYNC_POINT (object);

  g_object_unref (sync->timeline);

  G_OBJECT_CLASS (meta_wayland_sync_point_parent_class)->finalize (object);
}

static void
meta_wayland_sync_point_init (MetaWaylandSyncPoint *sync)
{
}

static void
meta_wayland_sync_point_class_init (MetaWaylandSyncPointClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_wayland_sync_point_finalize;
}

static void
syncobj_timeline_handle_resource_destroy (struct wl_resource *resource)
{
  MetaWaylandSyncobjTimeline *syncobj_timeline =
    wl_resource_get_user_data (resource);
  g_object_unref (syncobj_timeline);
}

static void
meta_wayland_syncobj_timeline_finalize (GObject *object)
{
  MetaWaylandSyncobjTimeline *syncobj_timeline =
    META_WAYLAND_SYNCOBJ_TIMELINE (object);

  g_clear_object (&syncobj_timeline->drm_timeline);

  G_OBJECT_CLASS (meta_wayland_syncobj_timeline_parent_class)->finalize (object);
}

static void
meta_wayland_syncobj_timeline_class_init (MetaWaylandSyncobjTimelineClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_wayland_syncobj_timeline_finalize;
}

static void
meta_wayland_syncobj_timeline_init (MetaWaylandSyncobjTimeline *syncobj_timeline)
{
  syncobj_timeline->drm_timeline = NULL;
}

static void
syncobj_timeline_handle_destroy (struct wl_client   *client,
                                 struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct wp_linux_drm_syncobj_timeline_v1_interface
  syncobj_timeline_implementation =
{
  syncobj_timeline_handle_destroy,
};

gboolean
meta_wayland_sync_timeline_set_sync_point (MetaWaylandSyncobjTimeline  *timeline,
                                           uint64_t                     sync_point,
                                           int                          sync_fd,
                                           GError                     **error)
{
  return meta_drm_timeline_set_sync_point (timeline->drm_timeline,
                                           sync_point,
                                           sync_fd,
                                           error);
}

int
meta_wayland_sync_timeline_get_eventfd (MetaWaylandSyncobjTimeline  *timeline,
                                        uint64_t                     sync_point,
                                        GError                     **error)
{
  return meta_drm_timeline_get_eventfd (timeline->drm_timeline,
                                        sync_point,
                                        error);
}

static void
syncobj_surface_handle_destroy (struct wl_client   *client,
                                struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
syncobj_surface_handle_set_acquire_point (struct wl_client   *client,
                                          struct wl_resource *resource,
                                          struct wl_resource *timeline_resource,
                                          uint32_t            point_hi,
                                          uint32_t            point_lo)
{
  MetaWaylandSyncobjSurface *syncobj_surface = wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = syncobj_surface->surface;
  MetaWaylandSyncobjTimeline *syncobj_timeline =
    wl_resource_get_user_data (timeline_resource);

  if (!surface)
    {
      wl_resource_post_error (resource,
                              WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_NO_SURFACE,
                              "Underlying surface object has been destroyed");
      return;
    }

  meta_wayland_sync_point_set (&surface->pending_state->drm_syncobj.acquire,
                               syncobj_timeline,
                               point_hi,
                               point_lo);
}

static void syncobj_surface_handle_set_release_point (struct wl_client   *client,
                                                      struct wl_resource *resource,
                                                      struct wl_resource *timeline_resource,
                                                      uint32_t            point_hi,
                                                      uint32_t            point_lo)
{
  MetaWaylandSyncobjSurface *syncobj_surface = wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = syncobj_surface->surface;
  MetaWaylandSyncobjTimeline *syncobj_timeline =
    wl_resource_get_user_data (timeline_resource);

  if (!surface)
    {
      wl_resource_post_error (resource,
                              WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_NO_SURFACE,
                              "Underlying surface object has been destroyed");
      return;
    }

  meta_wayland_sync_point_set (&surface->pending_state->drm_syncobj.release,
                               syncobj_timeline,
                               point_hi,
                               point_lo);
}

static const struct wp_linux_drm_syncobj_surface_v1_interface
  syncobj_surface_implementation =
{
  syncobj_surface_handle_destroy,
  syncobj_surface_handle_set_acquire_point,
  syncobj_surface_handle_set_release_point,
};

static void
syncobj_surface_resource_destroyed (MetaWaylandSurface        *surface,
                                    MetaWaylandSyncobjSurface *syncobj_surface)
{
  g_clear_signal_handler (&syncobj_surface->surface_destroy_handler_id,
                          syncobj_surface->surface);

  g_object_set_qdata (G_OBJECT (syncobj_surface->surface),
                      quark_syncobj_surface,
                      NULL);

  syncobj_surface->surface = NULL;
}

static void
syncobj_surface_destructor (struct wl_resource *resource)
{
  MetaWaylandSyncobjSurface *syncobj_surface =
    wl_resource_get_user_data (resource);

  if (syncobj_surface->surface)
    syncobj_surface_resource_destroyed (syncobj_surface->surface, syncobj_surface);

  g_object_unref (syncobj_surface);
}

static void
meta_wayland_syncobj_surface_class_init (MetaWaylandSyncobjSurfaceClass *klass)
{
}

static void
meta_wayland_syncobj_surface_init (MetaWaylandSyncobjSurface *syncobj_surface)
{
}

static void
drm_syncobj_manager_handle_destroy (struct wl_client   *client,
                                    struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
drm_syncobj_manager_handle_get_surface (struct wl_client   *client,
                                        struct wl_resource *resource,
                                        uint32_t            id,
                                        struct wl_resource *surface_resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandSyncobjSurface *syncobj_surface =
    g_object_get_qdata (G_OBJECT (surface), quark_syncobj_surface);
  struct wl_resource *sync_resource;

  if (syncobj_surface)
    {
      wl_resource_post_error (surface_resource,
                              WP_LINUX_DRM_SYNCOBJ_MANAGER_V1_ERROR_SURFACE_EXISTS,
                              "DRM Syncobj surface object already created for surface %d",
                              wl_resource_get_id (surface_resource));
      return;
    }

  sync_resource =
    wl_resource_create (client,
                        &wp_linux_drm_syncobj_surface_v1_interface,
                        wl_resource_get_version (resource),
                        id);
  if (sync_resource == NULL)
    {
      wl_resource_post_no_memory (resource);
      return;
    }

  syncobj_surface = g_object_new (META_TYPE_WAYLAND_SYNCOBJ_SURFACE, NULL);
  syncobj_surface->surface = surface;
  syncobj_surface->surface_destroy_handler_id =
    g_signal_connect (surface,
                      "destroy",
                      G_CALLBACK (syncobj_surface_resource_destroyed),
                      syncobj_surface);

  g_object_set_qdata (G_OBJECT (surface),
                      quark_syncobj_surface,
                      syncobj_surface);

  wl_resource_set_implementation (sync_resource,
                                  &syncobj_surface_implementation,
                                  syncobj_surface,
                                  syncobj_surface_destructor);
  syncobj_surface->resource = sync_resource;
}

static void
drm_syncobj_manager_handle_import_timeline (struct wl_client   *client,
                                            struct wl_resource *resource,
                                            uint32_t            id,
                                            int                 drm_syncobj_fd)
{
  MetaWaylandDrmSyncobjManager *drm_syncobj = wl_resource_get_user_data (resource);
  g_autoptr (GError) error = NULL;
  g_autoptr (MetaDrmTimeline) drm_timeline = NULL;
  g_autoptr (MetaWaylandSyncobjTimeline) syncobj_timeline = NULL;
  struct wl_resource *timeline_resource;

  drm_timeline = meta_drm_timeline_import_syncobj (drm_syncobj->drm,
                                                   drm_syncobj_fd,
                                                   &error);
  close (drm_syncobj_fd);
  if (!drm_timeline)
    {
      wl_resource_post_error (resource,
                              WP_LINUX_DRM_SYNCOBJ_MANAGER_V1_ERROR_INVALID_TIMELINE,
                              "Failed to import DRM syncobj: %s",
                              error->message);
      return;
    }

  syncobj_timeline = g_object_new (META_TYPE_WAYLAND_SYNCOBJ_TIMELINE, NULL);

  timeline_resource = wl_resource_create (client,
                                          &wp_linux_drm_syncobj_timeline_v1_interface,
                                          wl_resource_get_version (resource),
                                          id);
  if (timeline_resource == NULL)
    {
      wl_resource_post_no_memory (resource);
      return;
    }

  syncobj_timeline->drm_timeline = g_steal_pointer (&drm_timeline);
  wl_resource_set_implementation (timeline_resource,
                                  &syncobj_timeline_implementation,
                                  g_steal_pointer (&syncobj_timeline),
                                  syncobj_timeline_handle_resource_destroy);
}

static const struct wp_linux_drm_syncobj_manager_v1_interface
  drm_syncobj_manager_implementation =
{
  drm_syncobj_manager_handle_destroy,
  drm_syncobj_manager_handle_get_surface,
  drm_syncobj_manager_handle_import_timeline,
};

static void
meta_wayland_drm_syncobj_manager_finalize (GObject *object)
{
  MetaWaylandDrmSyncobjManager *drm_syncobj =
    META_WAYLAND_DRM_SYNCOBJ_MANAGER (object);

  g_clear_fd (&drm_syncobj->drm, NULL);

  G_OBJECT_CLASS (meta_wayland_drm_syncobj_manager_parent_class)->finalize (object);
}

static void
meta_wayland_drm_syncobj_manager_class_init (MetaWaylandDrmSyncobjManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_wayland_drm_syncobj_manager_finalize;

  quark_syncobj_surface = g_quark_from_static_string ("drm-syncobj-quark");
}

static void
meta_wayland_drm_syncobj_manager_init (MetaWaylandDrmSyncobjManager *drm_syncobj)
{
  drm_syncobj->drm = -1;
}

static void
drm_syncobj_manager_bind (struct wl_client *client,
                          void             *user_data,
                          uint32_t          version,
                          uint32_t          id)
{
  MetaWaylandDrmSyncobjManager *drm_syncobj_manager = user_data;
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &wp_linux_drm_syncobj_manager_v1_interface,
                                 version,
                                 id);
  wl_resource_set_implementation (resource,
                                  &drm_syncobj_manager_implementation,
                                  drm_syncobj_manager,
                                  NULL);
}

static MetaWaylandDrmSyncobjManager *
meta_wayland_drm_syncobj_manager_new (MetaWaylandCompositor *compositor,
                                      GError               **error)
{
  MetaContext *context =
    meta_wayland_compositor_get_context (compositor);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaEgl *egl = meta_backend_get_egl (backend);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  EGLDisplay egl_display = cogl_egl_context_get_egl_display (cogl_context);
  MetaWaylandDrmSyncobjManager *drm_syncobj_manager;
  EGLDeviceEXT egl_device;
  g_autofd int drm_fd = -1;
  EGLAttrib attrib;
  uint64_t timeline_supported = false;
  const char *device_path = NULL;

  g_assert (backend && egl && clutter_backend && cogl_context && egl_display);

  if (!meta_egl_query_display_attrib (egl, egl_display,
                                      EGL_DEVICE_EXT, &attrib,
                                      error))
    return NULL;

  egl_device = (EGLDeviceEXT) attrib;

  if (meta_egl_egl_device_has_extensions (egl, egl_device, NULL,
                                          "EGL_EXT_device_drm_render_node",
                                          NULL))
    {
      if (!meta_egl_query_device_string (egl, egl_device,
                                         EGL_DRM_RENDER_NODE_FILE_EXT,
                                         &device_path, error))
        return NULL;
    }

  if (!device_path &&
      meta_egl_egl_device_has_extensions (egl, egl_device, NULL,
                                          "EGL_EXT_device_drm",
                                          NULL))
    {
      if (!meta_egl_query_device_string (egl, egl_device,
                                         EGL_DRM_DEVICE_FILE_EXT,
                                         &device_path, error))
        return NULL;
    }

  if (!device_path)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_NOT_SUPPORTED,
                   "Failed to find EGL device to initialize linux-drm-syncobj-v1");
      return NULL;
    }

  drm_fd = open (device_path, O_RDWR | O_CLOEXEC);
  if (drm_fd < 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Failed to open DRM device %s",
                   device_path);
      return NULL;
    }

  if (drmGetCap (drm_fd, DRM_CAP_SYNCOBJ_TIMELINE, &timeline_supported) != 0
      || !timeline_supported)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_NOT_SUPPORTED,
                   "Failed to check DRM syncobj timeline capability");
      return NULL;
    }

#ifdef HAVE_EVENTFD
  if (drmSyncobjEventfd (drm_fd, 0, 0, -1, 0) != -1 || errno != ENOENT)
#endif
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_NOT_SUPPORTED,
                   "drmSyncobjEventfd failed: linux-drm-syncobj requires eventfd support");
      return NULL;
    }

  drm_syncobj_manager = g_object_new (META_TYPE_WAYLAND_DRM_SYNCOBJ_MANAGER, NULL);
  drm_syncobj_manager->drm = g_steal_fd (&drm_fd);

  if (!wl_global_create (compositor->wayland_display,
                         &wp_linux_drm_syncobj_manager_v1_interface,
                         1,
                         drm_syncobj_manager,
                         drm_syncobj_manager_bind))
    {
      g_error ("Failed to create wp_linux_drm_syncobj_manager_v1_interface global");
    }

  return drm_syncobj_manager;
}

void
meta_wayland_drm_syncobj_init (MetaWaylandCompositor *compositor)
{
  g_autoptr (GError) error = NULL;
  MetaWaylandDrmSyncobjManager *manager =
    meta_wayland_drm_syncobj_manager_new (compositor, &error);

  if (!manager)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED))
        {
          meta_topic (META_DEBUG_WAYLAND, "Disabling explicit sync: %s",
                      error->message);
        }
       else
        {
          g_warning ("Failed to create linux-drm-syncobj-manager: %s",
                     error->message);
        }
      return;
    }

  g_object_set_data_full (G_OBJECT (compositor), "-meta-wayland-drm-syncobj-manager",
                          manager,
                          g_object_unref);
}

/*
 * Validate that the appropriate acquire and release points have been set
 * for this surface.
 */
bool
meta_wayland_surface_explicit_sync_validate (MetaWaylandSurface      *surface,
                                             MetaWaylandSurfaceState *state)
{
  MetaWaylandSyncobjSurface *syncobj_surface = g_object_get_qdata (G_OBJECT (surface),
                                                                   quark_syncobj_surface);

  if (!syncobj_surface)
    return TRUE;

  if (state->buffer)
    {
      if (state->buffer->type != META_WAYLAND_BUFFER_TYPE_DMA_BUF)
        {
          wl_resource_post_error (syncobj_surface->resource,
                                  WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_UNSUPPORTED_BUFFER,
                                  "Explicit Sync only supported on dmabuf buffers");
          return FALSE;
        }

      if (!state->drm_syncobj.acquire)
        {
          wl_resource_post_error (syncobj_surface->resource,
                                  WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_NO_ACQUIRE_POINT,
                                  "No Acquire point provided");
          return FALSE;
        }

      if (!state->drm_syncobj.release)
        {
          wl_resource_post_error (syncobj_surface->resource,
                                  WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_NO_RELEASE_POINT,
                                  "No Release point provided");
          return FALSE;
        }

      if (state->drm_syncobj.acquire->timeline == state->drm_syncobj.release->timeline &&
          state->drm_syncobj.acquire->sync_point >= state->drm_syncobj.release->sync_point)
        {
          wl_resource_post_error (syncobj_surface->resource,
                                  WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_CONFLICTING_POINTS,
                                  "Invalid Release and Acquire point combination");
          return FALSE;
        }
    }
  else if (state->drm_syncobj.acquire || state->drm_syncobj.release)
    {
      wl_resource_post_error (syncobj_surface->resource,
                              WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_NO_BUFFER,
                              "Release or Acquire point set but no buffer attached");
      return FALSE;
    }

  return TRUE;
}
