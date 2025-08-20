/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
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
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include "config.h"

#include "wayland/meta-wayland-outputs.h"

#include <string.h>

#include "backends/meta-logical-monitor-private.h"
#include "backends/meta-monitor-private.h"
#include "backends/meta-monitor-manager-private.h"
#include "wayland/meta-wayland-private.h"

#ifdef HAVE_XWAYLAND
#include "wayland/meta-xwayland.h"
#endif

#include "xdg-output-unstable-v1-server-protocol.h"

/* Wayland protocol headers list new additions, not deprecations */
#define NO_XDG_OUTPUT_DONE_SINCE_VERSION 3

enum
{
  OUTPUT_DESTROYED,
  OUTPUT_BOUND,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

struct _MetaWaylandOutput
{
  GObject parent;

  MetaWaylandCompositor *compositor;

  struct wl_global *global;
  GList *resources;
  GList *xdg_output_resources;

  /* Protocol state */
  MtkRectangle layout;
  MetaSubpixelOrder subpixel_order;
  MtkMonitorTransform transform;
  MetaMonitorMode *mode;
  MetaMonitorMode *preferred_mode;
  float scale;

  MetaMonitor *monitor;
};

G_DEFINE_TYPE (MetaWaylandOutput, meta_wayland_output, G_TYPE_OBJECT)

static void
send_xdg_output_events (struct wl_resource *resource,
                        MetaWaylandOutput  *wayland_output,
                        MetaMonitor        *monitor,
                        gboolean            need_all_events,
                        gboolean           *pending_done_event);

const GList *
meta_wayland_output_get_resources (MetaWaylandOutput *wayland_output)
{
  return wayland_output->resources;
}

MetaMonitor *
meta_wayland_output_get_monitor (MetaWaylandOutput *wayland_output)
{
  return wayland_output->monitor;
}

MetaMonitorMode *
meta_wayland_output_get_monitor_mode (MetaWaylandOutput *wayland_output)
{
  return wayland_output->mode;
}

static void
output_resource_destroy (struct wl_resource *res)
{
  MetaWaylandOutput *wayland_output;

  wayland_output = wl_resource_get_user_data (res);
  if (!wayland_output)
    return;

  wayland_output->resources = g_list_remove (wayland_output->resources, res);
}

static void
meta_wl_output_release (struct wl_client   *client,
                        struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct wl_output_interface meta_wl_output_interface = {
  meta_wl_output_release,
};

static enum wl_output_subpixel
meta_subpixel_order_to_wl_output_subpixel (MetaSubpixelOrder subpixel_order)
{
  switch (subpixel_order)
    {
    case META_SUBPIXEL_ORDER_UNKNOWN:
      return WL_OUTPUT_SUBPIXEL_UNKNOWN;
    case META_SUBPIXEL_ORDER_NONE:
      return WL_OUTPUT_SUBPIXEL_NONE;
    case META_SUBPIXEL_ORDER_HORIZONTAL_RGB:
      return WL_OUTPUT_SUBPIXEL_HORIZONTAL_RGB;
    case META_SUBPIXEL_ORDER_HORIZONTAL_BGR:
      return WL_OUTPUT_SUBPIXEL_HORIZONTAL_BGR;
    case META_SUBPIXEL_ORDER_VERTICAL_RGB:
      return WL_OUTPUT_SUBPIXEL_VERTICAL_RGB;
    case META_SUBPIXEL_ORDER_VERTICAL_BGR:
      return WL_OUTPUT_SUBPIXEL_VERTICAL_BGR;
    }

  g_assert_not_reached ();
  return WL_OUTPUT_SUBPIXEL_UNKNOWN;
}

static enum wl_output_transform
wl_output_transform_from_transform (MtkMonitorTransform transform)
{
  switch (transform)
    {
    case MTK_MONITOR_TRANSFORM_NORMAL:
      return WL_OUTPUT_TRANSFORM_NORMAL;
    case MTK_MONITOR_TRANSFORM_90:
      return WL_OUTPUT_TRANSFORM_90;
    case MTK_MONITOR_TRANSFORM_180:
      return WL_OUTPUT_TRANSFORM_180;
    case MTK_MONITOR_TRANSFORM_270:
      return WL_OUTPUT_TRANSFORM_270;
    case MTK_MONITOR_TRANSFORM_FLIPPED:
      return WL_OUTPUT_TRANSFORM_FLIPPED;
    case MTK_MONITOR_TRANSFORM_FLIPPED_90:
      return WL_OUTPUT_TRANSFORM_FLIPPED_90;
    case MTK_MONITOR_TRANSFORM_FLIPPED_180:
      return WL_OUTPUT_TRANSFORM_FLIPPED_180;
    case MTK_MONITOR_TRANSFORM_FLIPPED_270:
      return WL_OUTPUT_TRANSFORM_FLIPPED_270;
    }
  g_assert_not_reached ();
}

#ifdef HAVE_XWAYLAND
static gboolean
is_xwayland_resource (MetaWaylandOutput  *wayland_output,
                      struct wl_resource *resource)
{
  MetaXWaylandManager *manager = &wayland_output->compositor->xwayland_manager;

  return resource && wl_resource_get_client (resource) == manager->client;
}
#endif

static void
maybe_scale_for_xwayland (MetaWaylandOutput  *wayland_output,
                          struct wl_resource *resource,
                          int                *x,
                          int                *y)
{
#ifdef HAVE_XWAYLAND
  if (is_xwayland_resource (wayland_output, resource))
    {
      MetaXWaylandManager *xwayland_manager =
        &wayland_output->compositor->xwayland_manager;
      int xwayland_scale;

      xwayland_scale = meta_xwayland_get_effective_scale (xwayland_manager);
      *x *= xwayland_scale;
      *y *= xwayland_scale;
    }
#endif
}

static void
send_output_events (struct wl_resource *resource,
                    MetaWaylandOutput  *wayland_output,
                    MetaMonitor        *monitor,
                    gboolean            need_all_events,
                    gboolean           *pending_done_event)
{
  MetaLogicalMonitor *logical_monitor =
    meta_monitor_get_logical_monitor (monitor);
  int version = wl_resource_get_version (resource);
  MtkRectangle layout;
  MtkRectangle old_layout;
  MtkMonitorTransform transform;
  MtkMonitorTransform old_transform;
  MetaMonitorMode *mode;
  MetaMonitorMode *old_mode;
  MetaMonitorMode *preferred_mode;
  MetaMonitorMode *old_preferred_mode;
  guint mode_flags;
  guint old_mode_flags;
  int32_t refresh_rate_khz;
  int32_t old_refresh_rate_khz;
  int scale_int;
  int old_scale_int;
  int mode_width, mode_height;
  int old_mode_width, old_mode_height;
  gboolean need_done = FALSE;

  layout = meta_logical_monitor_get_layout (logical_monitor);
  old_layout = wayland_output->layout;

  transform = meta_logical_monitor_get_transform (logical_monitor);
  old_transform = wayland_output->transform;

  mode = meta_monitor_get_current_mode (monitor);
  old_mode = wayland_output->mode;

  preferred_mode = meta_monitor_get_preferred_mode (monitor);
  old_preferred_mode = wayland_output->preferred_mode;

  mode_flags = WL_OUTPUT_MODE_CURRENT;
  if (mode == preferred_mode)
    mode_flags |= WL_OUTPUT_MODE_PREFERRED;

  old_mode_flags = WL_OUTPUT_MODE_CURRENT;
  if (old_mode == old_preferred_mode)
    old_mode_flags |= WL_OUTPUT_MODE_PREFERRED;

  refresh_rate_khz =
    (int32_t) (meta_monitor_mode_get_refresh_rate (mode) * 1000);
  old_refresh_rate_khz =
    (int32_t) (meta_monitor_mode_get_refresh_rate (old_mode) * 1000);

  scale_int = (int) ceilf (meta_logical_monitor_get_scale (logical_monitor));
  old_scale_int = (int) ceilf (wayland_output->scale);

  meta_monitor_mode_get_resolution (mode, &mode_width, &mode_height);
  meta_monitor_mode_get_resolution (old_mode,
                                    &old_mode_width, &old_mode_height);

  if (need_all_events ||
      old_layout.x != layout.x || old_layout.y != layout.y ||
      old_transform != transform)
    {
      const char *vendor;
      const char *product;
      int physical_width_mm;
      int physical_height_mm;
      MetaSubpixelOrder subpixel_order;
      enum wl_output_subpixel wl_subpixel_order;
      uint32_t wl_transform;

      vendor = meta_monitor_get_vendor (monitor);
      product = meta_monitor_get_product (monitor);

      meta_monitor_get_physical_dimensions (monitor,
                                            &physical_width_mm,
                                            &physical_height_mm);

      subpixel_order = meta_monitor_get_subpixel_order (monitor);
      wl_subpixel_order =
        meta_subpixel_order_to_wl_output_subpixel (subpixel_order);

      wl_transform = wl_output_transform_from_transform (transform);

      maybe_scale_for_xwayland (wayland_output, resource,
                                &layout.x,
                                &layout.y);
      wl_output_send_geometry (resource,
                               layout.x,
                               layout.y,
                               physical_width_mm,
                               physical_height_mm,
                               wl_subpixel_order,
                               vendor ? vendor : "unknown",
                               product ? product : "unknown",
                               wl_transform);
      need_done = TRUE;
    }

  if (need_all_events ||
      old_mode_width != mode_width ||
      old_mode_height != mode_height ||
      old_refresh_rate_khz != refresh_rate_khz ||
      old_mode_flags != mode_flags)
    {
      wl_output_send_mode (resource,
                           mode_flags,
                           mode_width,
                           mode_height,
                           refresh_rate_khz);
      need_done = TRUE;
    }

  if (version >= WL_OUTPUT_SCALE_SINCE_VERSION &&
      (need_all_events || old_scale_int != scale_int))
    {
      wl_output_send_scale (resource, scale_int);
      need_done = TRUE;
    }

  if (need_all_events && version >= WL_OUTPUT_NAME_SINCE_VERSION)
    {
      const char *name;

      name = meta_monitor_get_connector (monitor);
      wl_output_send_name (resource, name);
      need_done = TRUE;
    }

  if (need_all_events && version >= WL_OUTPUT_DESCRIPTION_SINCE_VERSION)
    {
      const char *description;

      description = meta_monitor_get_display_name (monitor);
      wl_output_send_description (resource, description);
      need_done = TRUE;
    }

  if (need_all_events && version >= WL_OUTPUT_DONE_SINCE_VERSION)
    {
      wl_output_send_done (resource);
      need_done = FALSE;
    }

  if (pending_done_event && need_done)
    *pending_done_event = TRUE;
}

static void
bind_output (struct wl_client *client,
             void *data,
             guint32 version,
             guint32 id)
{
  MetaWaylandOutput *wayland_output = data;
  MetaMonitor *monitor;
  struct wl_resource *resource;
#ifdef WITH_VERBOSE_MODE
  MetaLogicalMonitor *logical_monitor;
  int mode_width, mode_height;
#endif

  resource = wl_resource_create (client, &wl_output_interface, version, id);

  monitor = wayland_output->monitor;
  if (!monitor)
    {
      wl_resource_set_implementation (resource,
                                      &meta_wl_output_interface,
                                      NULL, NULL);
      return;
    }

  wayland_output->resources = g_list_prepend (wayland_output->resources,
                                              resource);
  wl_resource_set_implementation (resource,
                                  &meta_wl_output_interface,
                                  wayland_output,
                                  output_resource_destroy);

#ifdef WITH_VERBOSE_MODE
  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  meta_monitor_mode_get_resolution (wayland_output->mode, &mode_width, &mode_height);

  meta_topic (META_DEBUG_WAYLAND,
              "Binding monitor %p/%s (%u, %u, %u, %u) x %f",
              logical_monitor,
              meta_monitor_get_product (monitor),
              wayland_output->layout.x, wayland_output->layout.y,
              mode_width, mode_height,
              meta_monitor_mode_get_refresh_rate (wayland_output->mode));
#endif

  send_output_events (resource, wayland_output, monitor, TRUE, NULL);

  g_signal_emit (wayland_output, signals[OUTPUT_BOUND], 0, resource);
}

static void
meta_wayland_output_set_monitor (MetaWaylandOutput *wayland_output,
                                 MetaMonitor       *monitor)
{
  MetaLogicalMonitor *logical_monitor;

  wayland_output->monitor = monitor;

  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  wayland_output->layout = meta_logical_monitor_get_layout (logical_monitor);
  wayland_output->subpixel_order = meta_monitor_get_subpixel_order (monitor);
  wayland_output->transform =
    meta_logical_monitor_get_transform (logical_monitor);
  g_set_object (&wayland_output->mode, meta_monitor_get_current_mode (monitor));
  g_set_object (&wayland_output->preferred_mode,
                meta_monitor_get_preferred_mode (monitor));
  wayland_output->scale = meta_logical_monitor_get_scale (logical_monitor);
}

static void
wayland_output_update_for_output (MetaWaylandOutput *wayland_output,
                                  MetaMonitor       *monitor)
{
  GList *l;
  gboolean pending_done_event;

  pending_done_event = FALSE;
  for (l = wayland_output->resources; l; l = l->next)
    {
      struct wl_resource *resource = l->data;

      send_output_events (resource, wayland_output, monitor,
                          FALSE, &pending_done_event);
    }

  for (l = wayland_output->xdg_output_resources; l; l = l->next)
    {
      struct wl_resource *xdg_output = l->data;

      send_xdg_output_events (xdg_output, wayland_output, monitor,
                              FALSE, &pending_done_event);
    }

  /* Send the "done" events if needed */
  if (pending_done_event)
    {
      for (l = wayland_output->resources; l; l = l->next)
        {
          struct wl_resource *resource = l->data;

          if (wl_resource_get_version (resource) >= WL_OUTPUT_DONE_SINCE_VERSION)
            wl_output_send_done (resource);
        }

      for (l = wayland_output->xdg_output_resources; l; l = l->next)
        {
          struct wl_resource *xdg_output = l->data;

          if (wl_resource_get_version (xdg_output) < NO_XDG_OUTPUT_DONE_SINCE_VERSION)
            zxdg_output_v1_send_done (xdg_output);
        }
    }

  meta_wayland_output_set_monitor (wayland_output, monitor);
}

static MetaWaylandOutput *
meta_wayland_output_new (MetaWaylandCompositor *compositor,
                         MetaMonitor           *monitor)
{
  MetaWaylandOutput *wayland_output;

  wayland_output = g_object_new (META_TYPE_WAYLAND_OUTPUT, NULL);
  wayland_output->compositor = compositor;
  wayland_output->global = wl_global_create (compositor->wayland_display,
                                             &wl_output_interface,
                                             META_WL_OUTPUT_VERSION,
                                             wayland_output, bind_output);
  meta_wayland_output_set_monitor (wayland_output, monitor);

  return wayland_output;
}

static void
make_output_resources_inert (MetaWaylandOutput *wayland_output)
{
  GList *l;

  wl_global_remove (wayland_output->global);

  for (l = wayland_output->resources; l; l = l->next)
    {
      struct wl_resource *output_resource = l->data;

      wl_resource_set_user_data (output_resource, NULL);
    }
  g_list_free (wayland_output->resources);
  wayland_output->resources = NULL;

  for (l = wayland_output->xdg_output_resources; l; l = l->next)
    {
      struct wl_resource *xdg_output_resource = l->data;

      wl_resource_set_user_data (xdg_output_resource, NULL);
    }
  g_list_free (wayland_output->xdg_output_resources);
  wayland_output->xdg_output_resources = NULL;
}

static void
make_output_inert (gpointer key,
                   gpointer value,
                   gpointer data)
{
  MetaWaylandOutput *wayland_output = value;

  g_signal_emit (wayland_output, signals[OUTPUT_DESTROYED], 0);

  wayland_output->monitor = NULL;
  make_output_resources_inert (wayland_output);
}

static void
delayed_destroy_outputs (gpointer data)
{
  g_hash_table_destroy (data);
}

static void
meta_wayland_compositor_update_outputs (MetaWaylandCompositor *compositor,
                                        MetaMonitorManager    *monitor_manager)
{
  g_autoptr (GHashTable) new_table = NULL;
  g_autoptr (GHashTable) old_table = NULL;
  GList *monitors, *l;

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  new_table = g_hash_table_new_full (meta_monitor_spec_hash,
                                     (GEqualFunc) meta_monitor_spec_equals,
                                     (GDestroyNotify) meta_monitor_spec_free,
                                     g_object_unref);
  old_table = g_steal_pointer (&compositor->outputs);

  for (l = monitors; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;
      MetaMonitorSpec *lookup_monitor_spec = meta_monitor_get_spec (monitor);
      g_autoptr (MetaMonitorSpec) monitor_spec = NULL;
      g_autoptr (MetaWaylandOutput) wayland_output = NULL;

      if (!meta_monitor_is_active (monitor))
        continue;

      if (!old_table ||
          !g_hash_table_steal_extended (old_table, lookup_monitor_spec,
                                        (gpointer *) &monitor_spec,
                                        (gpointer *) &wayland_output))
        {
          monitor_spec = meta_monitor_spec_clone (lookup_monitor_spec),
          wayland_output = meta_wayland_output_new (compositor, monitor);
        }

      wayland_output_update_for_output (wayland_output, monitor);
      g_hash_table_insert (new_table,
                           g_steal_pointer (&monitor_spec),
                           g_steal_pointer (&wayland_output));
    }

  compositor->outputs = g_steal_pointer (&new_table);

  if (old_table)
    {
      g_hash_table_foreach (old_table, make_output_inert, NULL);
      g_timeout_add_seconds_once (10, delayed_destroy_outputs,
                                  g_steal_pointer (&old_table));
    }
}

static void
on_monitors_changing (MetaMonitorManager    *monitors,
                      MetaWaylandCompositor *compositor)
{
  meta_wayland_compositor_update_outputs (compositor, monitors);
}

static void
meta_wayland_output_init (MetaWaylandOutput *wayland_output)
{
}

static void
meta_wayland_output_finalize (GObject *object)
{
  MetaWaylandOutput *wayland_output = META_WAYLAND_OUTPUT (object);

  g_warn_if_fail (!wayland_output->resources);
  g_warn_if_fail (!wayland_output->xdg_output_resources);

  g_clear_object (&wayland_output->mode);
  g_clear_object (&wayland_output->preferred_mode);

  wl_global_destroy (wayland_output->global);

  G_OBJECT_CLASS (meta_wayland_output_parent_class)->finalize (object);
}

static void
meta_wayland_output_class_init (MetaWaylandOutputClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_wayland_output_finalize;

  signals[OUTPUT_DESTROYED] = g_signal_new ("output-destroyed",
                                            G_TYPE_FROM_CLASS (object_class),
                                            G_SIGNAL_RUN_LAST,
                                            0,
                                            NULL, NULL, NULL,
                                            G_TYPE_NONE, 0);

  signals[OUTPUT_BOUND] = g_signal_new ("output-bound",
                                        G_TYPE_FROM_CLASS (object_class),
                                        G_SIGNAL_RUN_LAST,
                                        0,
                                        NULL, NULL, NULL,
                                        G_TYPE_NONE, 1,
                                        G_TYPE_POINTER);
}

static void
meta_xdg_output_destructor (struct wl_resource *resource)
{
  MetaWaylandOutput *wayland_output;

  wayland_output = wl_resource_get_user_data (resource);
  if (!wayland_output)
    return;

  wayland_output->xdg_output_resources =
    g_list_remove (wayland_output->xdg_output_resources, resource);
}

static void
meta_xdg_output_destroy (struct wl_client   *client,
                         struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct zxdg_output_v1_interface
  meta_xdg_output_interface = {
    meta_xdg_output_destroy,
  };

static void
send_xdg_output_events (struct wl_resource *resource,
                        MetaWaylandOutput  *wayland_output,
                        MetaMonitor        *monitor,
                        gboolean            need_all_events,
                        gboolean           *pending_done_event)
{
  MetaLogicalMonitor *logical_monitor =
    meta_monitor_get_logical_monitor (monitor);
  int version = wl_resource_get_version (resource);
  MtkRectangle layout;
  MtkRectangle old_layout;
  gboolean need_done = FALSE;

  layout = meta_logical_monitor_get_layout (logical_monitor);
  old_layout = wayland_output->layout;

  if (need_all_events ||
      old_layout.x != layout.x || old_layout.y != layout.y)
    {
      maybe_scale_for_xwayland (wayland_output, resource, &layout.x, &layout.y);
      zxdg_output_v1_send_logical_position (resource, layout.x, layout.y);
      need_done = TRUE;
    }

  if (need_all_events ||
      old_layout.width != layout.width || old_layout.height != layout.height)
    {
      maybe_scale_for_xwayland (wayland_output, resource, &layout.width, &layout.height);
      zxdg_output_v1_send_logical_size (resource, layout.width, layout.height);
      need_done = TRUE;
    }

  if (need_all_events && version >= ZXDG_OUTPUT_V1_NAME_SINCE_VERSION)
    {
      const char *name;

      name = meta_monitor_get_connector (monitor);
      zxdg_output_v1_send_name (resource, name);
      need_done = TRUE;
    }

  if (need_all_events && version >= ZXDG_OUTPUT_V1_DESCRIPTION_SINCE_VERSION)
    {
      const char *description;

      description = meta_monitor_get_display_name (monitor);
      zxdg_output_v1_send_description (resource, description);
      need_done = TRUE;
    }

  if (pending_done_event && need_done)
    *pending_done_event = TRUE;
}

static void
meta_xdg_output_manager_get_xdg_output (struct wl_client   *client,
                                        struct wl_resource *resource,
                                        uint32_t            id,
                                        struct wl_resource *output)
{
  struct wl_resource *xdg_output_resource;
  MetaWaylandOutput *wayland_output;
  int xdg_output_version;
  int wl_output_version;

  xdg_output_resource = wl_resource_create (client,
                                            &zxdg_output_v1_interface,
                                            wl_resource_get_version (resource),
                                            id);

  wayland_output = wl_resource_get_user_data (output);
  wl_resource_set_implementation (xdg_output_resource,
                                  &meta_xdg_output_interface,
                                  wayland_output, meta_xdg_output_destructor);

  if (!wayland_output)
    goto done;

  wayland_output->xdg_output_resources =
    g_list_prepend (wayland_output->xdg_output_resources, xdg_output_resource);

  if (!wayland_output->monitor)
    goto done;

  send_xdg_output_events (xdg_output_resource,
                          wayland_output,
                          wayland_output->monitor,
                          TRUE, NULL);

done:
  xdg_output_version = wl_resource_get_version (xdg_output_resource);
  wl_output_version = wl_resource_get_version (output);

  if (xdg_output_version < NO_XDG_OUTPUT_DONE_SINCE_VERSION)
    zxdg_output_v1_send_done (xdg_output_resource);
  else if (wl_output_version >= WL_OUTPUT_DONE_SINCE_VERSION)
    wl_output_send_done (output);
}

static void
meta_xdg_output_manager_destroy (struct wl_client   *client,
                                 struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct zxdg_output_manager_v1_interface
  meta_xdg_output_manager_interface = {
    meta_xdg_output_manager_destroy,
    meta_xdg_output_manager_get_xdg_output,
  };

static void
bind_xdg_output_manager (struct wl_client *client,
                         void             *data,
                         uint32_t          version,
                         uint32_t          id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &zxdg_output_manager_v1_interface,
                                 version, id);

  wl_resource_set_implementation (resource,
                                  &meta_xdg_output_manager_interface,
                                  NULL, NULL);
}

void
meta_wayland_outputs_finalize (MetaWaylandCompositor *compositor)
{
  MetaBackend *backend = meta_context_get_backend (compositor->context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  g_signal_handlers_disconnect_by_func (monitor_manager, on_monitors_changing,
                                        compositor);

  g_hash_table_destroy (compositor->outputs);
}

void
meta_wayland_outputs_init (MetaWaylandCompositor *compositor)
{
  MetaContext *context = meta_wayland_compositor_get_context (compositor);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  g_signal_connect (monitor_manager, "monitors-changing",
                    G_CALLBACK (on_monitors_changing), compositor);

  meta_wayland_compositor_update_outputs (compositor, monitor_manager);

  wl_global_create (compositor->wayland_display,
                    &zxdg_output_manager_v1_interface,
                    META_ZXDG_OUTPUT_V1_VERSION,
                    NULL,
                    bind_xdg_output_manager);
}
