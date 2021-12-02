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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include "config.h"

#include "wayland/meta-wayland-outputs.h"

#include <string.h>

#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor.h"
#include "backends/meta-monitor-manager-private.h"
#include "wayland/meta-wayland-private.h"

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

  struct wl_global *global;
  uint32_t mode_flags;
  float refresh_rate;
  int scale;
  int mode_width;
  int mode_height;

  GList *resources;
  GList *xdg_output_resources;

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

MetaLogicalMonitor *
meta_wayland_output_get_logical_monitor (MetaWaylandOutput *wayland_output)
{
  return meta_monitor_get_logical_monitor (wayland_output->monitor);
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

static enum wl_output_subpixel
cogl_subpixel_order_to_wl_output_subpixel (CoglSubpixelOrder subpixel_order)
{
  switch (subpixel_order)
    {
    case COGL_SUBPIXEL_ORDER_UNKNOWN:
      return WL_OUTPUT_SUBPIXEL_UNKNOWN;
    case COGL_SUBPIXEL_ORDER_NONE:
      return WL_OUTPUT_SUBPIXEL_NONE;
    case COGL_SUBPIXEL_ORDER_HORIZONTAL_RGB:
      return WL_OUTPUT_SUBPIXEL_HORIZONTAL_RGB;
    case COGL_SUBPIXEL_ORDER_HORIZONTAL_BGR:
      return WL_OUTPUT_SUBPIXEL_HORIZONTAL_BGR;
    case COGL_SUBPIXEL_ORDER_VERTICAL_RGB:
      return WL_OUTPUT_SUBPIXEL_VERTICAL_RGB;
    case COGL_SUBPIXEL_ORDER_VERTICAL_BGR:
      return WL_OUTPUT_SUBPIXEL_VERTICAL_BGR;
    }

  g_assert_not_reached ();
  return WL_OUTPUT_SUBPIXEL_UNKNOWN;
}

static int
calculate_wayland_output_scale (MetaMonitor *monitor)
{
  MetaLogicalMonitor *logical_monitor;
  float scale;

  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  scale = meta_logical_monitor_get_scale (logical_monitor);
  return ceilf (scale);
}

static void
get_rotated_physical_dimensions (MetaMonitor *monitor,
                                 int         *width_mm,
                                 int         *height_mm)
{
  int monitor_width_mm, monitor_height_mm;
  MetaLogicalMonitor *logical_monitor;

  meta_monitor_get_physical_dimensions (monitor,
                                        &monitor_width_mm,
                                        &monitor_height_mm);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);

  if (meta_monitor_transform_is_rotated (logical_monitor->transform))
    {
      *width_mm = monitor_height_mm;
      *height_mm = monitor_width_mm;
    }
  else
    {
      *width_mm = monitor_width_mm;
      *height_mm = monitor_height_mm;
    }
}

static gboolean
is_different_rotation (MetaLogicalMonitor *a,
                       MetaLogicalMonitor *b)
{
  return (meta_monitor_transform_is_rotated (a->transform) !=
          meta_monitor_transform_is_rotated (b->transform));
}

static void
get_native_output_mode_resolution (MetaMonitor     *monitor,
                                   MetaMonitorMode *mode,
                                   int             *mode_width,
                                   int             *mode_height)
{
  MetaLogicalMonitor *logical_monitor;
  MetaMonitorTransform transform;

  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  transform = meta_logical_monitor_get_transform (logical_monitor);
  if (meta_monitor_transform_is_rotated (transform))
    meta_monitor_mode_get_resolution (mode, mode_height, mode_width);
  else
    meta_monitor_mode_get_resolution (mode, mode_width, mode_height);
}

static void
send_output_events (struct wl_resource *resource,
                    MetaWaylandOutput  *wayland_output,
                    MetaMonitor        *monitor,
                    gboolean            need_all_events,
                    gboolean           *pending_done_event)
{
  int version = wl_resource_get_version (resource);
  MetaMonitorMode *current_mode;
  MetaMonitorMode *preferred_mode;
  guint mode_flags = WL_OUTPUT_MODE_CURRENT;
  MetaLogicalMonitor *logical_monitor;
  MetaLogicalMonitor *old_logical_monitor;
  guint old_mode_flags;
  gint old_scale;
  float old_refresh_rate;
  float refresh_rate;
  int new_width, new_height;

  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  old_logical_monitor =
    meta_monitor_get_logical_monitor (wayland_output->monitor);
  old_mode_flags = wayland_output->mode_flags;
  old_scale = wayland_output->scale;
  old_refresh_rate = wayland_output->refresh_rate;

  current_mode = meta_monitor_get_current_mode (monitor);
  refresh_rate = meta_monitor_mode_get_refresh_rate (current_mode);

  gboolean need_done = FALSE;

  if (need_all_events ||
      old_logical_monitor->rect.x != logical_monitor->rect.x ||
      old_logical_monitor->rect.y != logical_monitor->rect.y ||
      is_different_rotation (old_logical_monitor, logical_monitor))
    {
      int width_mm, height_mm;
      const char *vendor;
      const char *product;
      uint32_t transform;
      CoglSubpixelOrder cogl_subpixel_order;
      enum wl_output_subpixel subpixel_order;

      get_rotated_physical_dimensions (monitor, &width_mm, &height_mm);
      vendor = meta_monitor_get_vendor (monitor);
      product = meta_monitor_get_product (monitor);

      cogl_subpixel_order = meta_monitor_get_subpixel_order (monitor);
      subpixel_order =
        cogl_subpixel_order_to_wl_output_subpixel (cogl_subpixel_order);

      /*
       * TODO: When we support wl_surface.set_buffer_transform, pass along
       * the correct transform here instead of always pretending its 'normal'.
       * The reason for this is to try stopping clients from setting any buffer
       * transform other than 'normal'.
       */
      transform = WL_OUTPUT_TRANSFORM_NORMAL;

      wl_output_send_geometry (resource,
                               logical_monitor->rect.x,
                               logical_monitor->rect.y,
                               width_mm,
                               height_mm,
                               subpixel_order,
                               vendor,
                               product,
                               transform);
      need_done = TRUE;
    }

  preferred_mode = meta_monitor_get_preferred_mode (monitor);
  if (current_mode == preferred_mode)
    mode_flags |= WL_OUTPUT_MODE_PREFERRED;

  get_native_output_mode_resolution (monitor,
                                     current_mode,
                                     &new_width,
                                     &new_height);
  if (need_all_events ||
      wayland_output->mode_width != new_width ||
      wayland_output->mode_height != new_height ||
      old_refresh_rate != refresh_rate ||
      old_mode_flags != mode_flags)
    {
      wl_output_send_mode (resource,
                           mode_flags,
                           new_width,
                           new_height,
                           (int32_t) (refresh_rate * 1000));
      need_done = TRUE;
    }

  if (version >= WL_OUTPUT_SCALE_SINCE_VERSION)
    {
      int scale;

      scale = calculate_wayland_output_scale (monitor);
      if (need_all_events ||
          old_scale != scale)
        {
          wl_output_send_scale (resource, scale);
          need_done = TRUE;
        }
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
#endif

  resource = wl_resource_create (client, &wl_output_interface, version, id);

  monitor = wayland_output->monitor;
  if (!monitor)
    return;

  wayland_output->resources = g_list_prepend (wayland_output->resources,
                                              resource);
  wl_resource_set_user_data (resource, wayland_output);
  wl_resource_set_destructor (resource, output_resource_destroy);

#ifdef WITH_VERBOSE_MODE
  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  meta_verbose ("Binding monitor %p/%s (%u, %u, %u, %u) x %f",
                logical_monitor,
                meta_monitor_get_product (monitor),
                logical_monitor->rect.x, logical_monitor->rect.y,
                wayland_output->mode_width,
                wayland_output->mode_height,
                wayland_output->refresh_rate);
#endif

  send_output_events (resource, wayland_output, monitor, TRUE, NULL);

  g_signal_emit (wayland_output, signals[OUTPUT_BOUND], 0, resource);
}

static void
meta_wayland_output_set_monitor (MetaWaylandOutput *wayland_output,
                                 MetaMonitor       *monitor)
{
  MetaMonitorMode *current_mode;
  MetaMonitorMode *preferred_mode;

  wayland_output->monitor = monitor;
  wayland_output->mode_flags = WL_OUTPUT_MODE_CURRENT;

  current_mode = meta_monitor_get_current_mode (monitor);
  preferred_mode = meta_monitor_get_preferred_mode (monitor);

  if (current_mode == preferred_mode)
    wayland_output->mode_flags |= WL_OUTPUT_MODE_PREFERRED;
  wayland_output->scale = calculate_wayland_output_scale (monitor);
  wayland_output->refresh_rate = meta_monitor_mode_get_refresh_rate (current_mode);

  get_native_output_mode_resolution (monitor,
                                     current_mode,
                                     &wayland_output->mode_width,
                                     &wayland_output->mode_height);
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
  wayland_output->global = wl_global_create (compositor->wayland_display,
                                             &wl_output_interface,
                                             META_WL_OUTPUT_VERSION,
                                             wayland_output, bind_output);
  meta_wayland_compositor_flush_clients (compositor);
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

static gboolean
delayed_destroy_outputs (gpointer data)
{
  g_hash_table_destroy (data);
  return G_SOURCE_REMOVE;
}

static GHashTable *
meta_wayland_compositor_update_outputs (MetaWaylandCompositor *compositor,
                                        MetaMonitorManager    *monitor_manager)
{
  GHashTable *new_table;
  GList *monitors, *l;

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  new_table = g_hash_table_new_full (meta_monitor_spec_hash,
                                     (GEqualFunc) meta_monitor_spec_equals,
                                     (GDestroyNotify) meta_monitor_spec_free,
                                     g_object_unref);

  for (l = monitors; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;
      MetaMonitorSpec *monitor_spec = meta_monitor_get_spec (monitor);
      MetaWaylandOutput *wayland_output = NULL;

      if (!meta_monitor_is_active (monitor))
        continue;

      if (compositor->outputs)
        wayland_output = g_hash_table_lookup (compositor->outputs, monitor_spec);

      if (wayland_output)
        g_hash_table_steal (compositor->outputs, monitor_spec);
      else
        wayland_output = meta_wayland_output_new (compositor, monitor);

      wayland_output_update_for_output (wayland_output, monitor);
      g_hash_table_insert (new_table,
                           meta_monitor_spec_clone (monitor_spec),
                           wayland_output);
    }

  if (compositor->outputs)
    {
      g_hash_table_foreach (compositor->outputs, make_output_inert, NULL);
      g_timeout_add_seconds (10, delayed_destroy_outputs, compositor->outputs);
    }

  return new_table;
}

static void
on_monitors_changed (MetaMonitorManager    *monitors,
                     MetaWaylandCompositor *compositor)
{
  compositor->outputs = meta_wayland_compositor_update_outputs (compositor, monitors);
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
  MetaRectangle layout;
  MetaLogicalMonitor *logical_monitor;
  int version;

  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  layout = meta_logical_monitor_get_layout (logical_monitor);

  zxdg_output_v1_send_logical_position (resource, layout.x, layout.y);
  zxdg_output_v1_send_logical_size (resource, layout.width, layout.height);

  version = wl_resource_get_version (resource);

  if (need_all_events && version >= ZXDG_OUTPUT_V1_NAME_SINCE_VERSION)
    {
      const char *name;

      name = meta_monitor_get_connector (monitor);
      zxdg_output_v1_send_name (resource, name);
    }

  if (need_all_events && version >= ZXDG_OUTPUT_V1_DESCRIPTION_SINCE_VERSION)
    {
      const char *description;

      description = meta_monitor_get_display_name (monitor);
      zxdg_output_v1_send_description (resource, description);
    }

  if (pending_done_event)
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
meta_wayland_outputs_init (MetaWaylandCompositor *compositor)
{
  MetaMonitorManager *monitors;

  monitors = meta_monitor_manager_get ();
  g_signal_connect (monitors, "monitors-changed-internal",
                    G_CALLBACK (on_monitors_changed), compositor);

  compositor->outputs = meta_wayland_compositor_update_outputs (compositor, monitors);

  wl_global_create (compositor->wayland_display,
                    &zxdg_output_manager_v1_interface,
                    META_ZXDG_OUTPUT_V1_VERSION,
                    NULL,
                    bind_xdg_output_manager);
}
