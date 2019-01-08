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

#include "meta-wayland-outputs.h"

#include "meta-wayland-private.h"
#include "backends/meta-logical-monitor.h"
#include "meta-monitor-manager-private.h"
#include "xdg-output-unstable-v1-server-protocol.h"

#include <string.h>

enum {
  OUTPUT_DESTROYED,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (MetaWaylandOutput, meta_wayland_output, G_TYPE_OBJECT)

static void
send_xdg_output_events (struct wl_resource *resource,
                        MetaWaylandOutput  *wayland_output,
                        MetaLogicalMonitor *logical_monitor,
                        gboolean            need_all_events,
                        gboolean           *pending_done_event);

static void
output_resource_destroy (struct wl_resource *res)
{
  MetaWaylandOutput *wayland_output;

  wayland_output = wl_resource_get_user_data (res);
  if (!wayland_output)
    return;

  wayland_output->resources = g_list_remove (wayland_output->resources, res);
}

static MetaMonitor *
pick_main_monitor (MetaLogicalMonitor *logical_monitor)
{
  GList *monitors;

  monitors = meta_logical_monitor_get_monitors (logical_monitor);
  return g_list_first (monitors)->data;
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
}

static enum wl_output_subpixel
calculate_suitable_subpixel_order (MetaLogicalMonitor *logical_monitor)
{
  GList *monitors;
  GList *l;
  MetaMonitor *first_monitor;
  CoglSubpixelOrder subpixel_order;

  monitors = meta_logical_monitor_get_monitors (logical_monitor);
  first_monitor = monitors->data;
  subpixel_order = meta_monitor_get_subpixel_order (first_monitor);

  for (l = monitors->next; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;

      if (meta_monitor_get_subpixel_order (monitor) != subpixel_order)
        {
          subpixel_order = COGL_SUBPIXEL_ORDER_UNKNOWN;
          break;
        }
    }

  return cogl_subpixel_order_to_wl_output_subpixel (subpixel_order);
}

static int
calculate_wayland_output_scale (MetaLogicalMonitor *logical_monitor)
{
  float scale;

  scale = meta_logical_monitor_get_scale (logical_monitor);
  return ceilf (scale);
}

static void
send_output_events (struct wl_resource *resource,
                    MetaWaylandOutput  *wayland_output,
                    MetaLogicalMonitor *logical_monitor,
                    gboolean            need_all_events,
                    gboolean           *pending_done_event)
{
  int version = wl_resource_get_version (resource);
  MetaMonitor *monitor;
  MetaMonitorMode *current_mode;
  MetaMonitorMode *preferred_mode;
  guint mode_flags = WL_OUTPUT_MODE_CURRENT;
  MetaLogicalMonitor *old_logical_monitor;
  guint old_mode_flags;
  gint old_scale;
  float old_refresh_rate;
  float refresh_rate;

  old_logical_monitor = wayland_output->logical_monitor;
  old_mode_flags = wayland_output->mode_flags;
  old_scale = wayland_output->scale;
  old_refresh_rate = wayland_output->refresh_rate;

  monitor = pick_main_monitor (logical_monitor);

  current_mode = meta_monitor_get_current_mode (monitor);
  refresh_rate = meta_monitor_mode_get_refresh_rate (current_mode);

  gboolean need_done = FALSE;

  if (need_all_events ||
      old_logical_monitor->rect.x != logical_monitor->rect.x ||
      old_logical_monitor->rect.y != logical_monitor->rect.y)
    {
      int width_mm, height_mm;
      const char *vendor;
      const char *product;
      uint32_t transform;
      enum wl_output_subpixel subpixel_order;

      /*
       * While the wl_output carries information specific to a single monitor,
       * it is actually referring to a region of the compositor's screen region
       * (logical monitor), which may consist of multiple monitors (clones).
       * Arbitrarily use whatever monitor is the first in the logical monitor
       * and use that for these details.
       */
      meta_monitor_get_physical_dimensions (monitor, &width_mm, &height_mm);
      vendor = meta_monitor_get_vendor (monitor);
      product = meta_monitor_get_product (monitor);

      subpixel_order = calculate_suitable_subpixel_order (logical_monitor);

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

  if (need_all_events ||
      old_logical_monitor->rect.width != logical_monitor->rect.width ||
      old_logical_monitor->rect.height != logical_monitor->rect.height ||
      old_refresh_rate != refresh_rate ||
      old_mode_flags != mode_flags)
    {
      wl_output_send_mode (resource,
                           mode_flags,
                           logical_monitor->rect.width,
                           logical_monitor->rect.height,
                           (int32_t) (refresh_rate * 1000));
      need_done = TRUE;
    }

  if (version >= WL_OUTPUT_SCALE_SINCE_VERSION)
    {
      int scale;

      scale = calculate_wayland_output_scale (logical_monitor);
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
  MetaLogicalMonitor *logical_monitor = wayland_output->logical_monitor;
  struct wl_resource *resource;
  MetaMonitor *monitor;

  resource = wl_resource_create (client, &wl_output_interface, version, id);
  wayland_output->resources = g_list_prepend (wayland_output->resources, resource);

  wl_resource_set_user_data (resource, wayland_output);
  wl_resource_set_destructor (resource, output_resource_destroy);

  if (!logical_monitor)
    return;

  monitor = pick_main_monitor (logical_monitor);

  meta_verbose ("Binding monitor %p/%s (%u, %u, %u, %u) x %f\n",
                logical_monitor,
                meta_monitor_get_product (monitor),
                logical_monitor->rect.x, logical_monitor->rect.y,
                logical_monitor->rect.width, logical_monitor->rect.height,
                wayland_output->refresh_rate);

  send_output_events (resource, wayland_output, logical_monitor, TRUE, NULL);
}

static void
wayland_output_destroy_notify (gpointer data)
{
  MetaWaylandOutput *wayland_output = data;

  g_signal_emit (wayland_output, signals[OUTPUT_DESTROYED], 0);
  g_object_unref (wayland_output);
}

static void
meta_wayland_output_set_logical_monitor (MetaWaylandOutput  *wayland_output,
                                         MetaLogicalMonitor *logical_monitor)
{
  MetaMonitor *monitor;
  MetaMonitorMode *current_mode;
  MetaMonitorMode *preferred_mode;

  wayland_output->logical_monitor = logical_monitor;
  wayland_output->mode_flags = WL_OUTPUT_MODE_CURRENT;

  monitor = pick_main_monitor (logical_monitor);
  current_mode = meta_monitor_get_current_mode (monitor);
  preferred_mode = meta_monitor_get_preferred_mode (monitor);

  if (current_mode == preferred_mode)
    wayland_output->mode_flags |= WL_OUTPUT_MODE_PREFERRED;
  wayland_output->scale = calculate_wayland_output_scale (logical_monitor);
  wayland_output->refresh_rate = meta_monitor_mode_get_refresh_rate (current_mode);
}

static void
wayland_output_update_for_output (MetaWaylandOutput  *wayland_output,
                                  MetaLogicalMonitor *logical_monitor)
{
  GList *iter;
  gboolean pending_done_event;

  pending_done_event = FALSE;
  for (iter = wayland_output->resources; iter; iter = iter->next)
    {
      struct wl_resource *resource = iter->data;
      send_output_events (resource, wayland_output, logical_monitor, FALSE, &pending_done_event);
    }

  for (iter = wayland_output->xdg_output_resources; iter; iter = iter->next)
    {
      struct wl_resource *xdg_output = iter->data;
      send_xdg_output_events (xdg_output, wayland_output, logical_monitor, FALSE, &pending_done_event);
    }

  /* Send the "done" events if needed */
  if (pending_done_event)
    {
      for (iter = wayland_output->resources; iter; iter = iter->next)
        {
          struct wl_resource *resource = iter->data;
          if (wl_resource_get_version (resource) >= WL_OUTPUT_DONE_SINCE_VERSION)
            wl_output_send_done (resource);
        }

      for (iter = wayland_output->xdg_output_resources; iter; iter = iter->next)
        {
          struct wl_resource *xdg_output = iter->data;
          zxdg_output_v1_send_done (xdg_output);
        }
    }
  /* It's very important that we change the output pointer here, as
     the old structure is about to be freed by MetaMonitorManager */
  meta_wayland_output_set_logical_monitor (wayland_output, logical_monitor);
}

static MetaWaylandOutput *
meta_wayland_output_new (MetaWaylandCompositor *compositor,
                         MetaLogicalMonitor    *logical_monitor)
{
  MetaWaylandCompositor *wayland_compositor =
    meta_wayland_compositor_get_default ();
  MetaWaylandOutput *wayland_output;

  wayland_output = g_object_new (META_TYPE_WAYLAND_OUTPUT, NULL);
  wayland_output->global = wl_global_create (compositor->wayland_display,
                                             &wl_output_interface,
                                             META_WL_OUTPUT_VERSION,
                                             wayland_output, bind_output);
  meta_wayland_compositor_flush_clients (wayland_compositor);
  meta_wayland_output_set_logical_monitor (wayland_output, logical_monitor);

  return wayland_output;
}

static void
make_output_resources_inert (MetaWaylandOutput *wayland_output)
{
  GList *l;

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

  wayland_output->logical_monitor = NULL;
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
  GList *logical_monitors, *l;

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  new_table = g_hash_table_new_full (NULL, NULL, NULL,
                                     wayland_output_destroy_notify);

  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;
      MetaWaylandOutput *wayland_output;

      if (logical_monitor->winsys_id == 0)
        continue;

      wayland_output =
        g_hash_table_lookup (compositor->outputs,
                             GSIZE_TO_POINTER (logical_monitor->winsys_id));

      if (wayland_output)
        {
          g_hash_table_steal (compositor->outputs,
                              GSIZE_TO_POINTER (logical_monitor->winsys_id));
        }
      else
        {
          wayland_output = meta_wayland_output_new (compositor, logical_monitor);
        }

      wayland_output_update_for_output (wayland_output, logical_monitor);
      g_hash_table_insert (new_table,
                           GSIZE_TO_POINTER (logical_monitor->winsys_id),
                           wayland_output);
    }

  g_hash_table_foreach (compositor->outputs, make_output_inert, NULL);
  g_timeout_add_seconds (10, delayed_destroy_outputs, compositor->outputs);

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
  GList *l;

  wl_global_destroy (wayland_output->global);

  /* Make sure the wl_output destructor doesn't try to access MetaWaylandOutput
   * after we have freed it.
   */
  make_output_resources_inert (wayland_output);

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
                        MetaLogicalMonitor *logical_monitor,
                        gboolean            need_all_events,
                        gboolean           *pending_done_event)
{
  MetaRectangle new_layout;
  MetaRectangle old_layout;
  MetaLogicalMonitor *old_logical_monitor;
  gboolean need_done;

  need_done = FALSE;
  old_logical_monitor = wayland_output->logical_monitor;
  old_layout = meta_logical_monitor_get_layout (old_logical_monitor);
  new_layout = meta_logical_monitor_get_layout (logical_monitor);

  if (need_all_events ||
      old_layout.x != new_layout.x ||
      old_layout.y != new_layout.y)
    {
      zxdg_output_v1_send_logical_position (resource,
                                            new_layout.x,
                                            new_layout.y);
      need_done = TRUE;
    }

  if (need_all_events ||
      old_layout.width != new_layout.width ||
      old_layout.height != new_layout.height)
    {
      zxdg_output_v1_send_logical_size (resource,
                                        new_layout.width,
                                        new_layout.height);
      need_done = TRUE;
    }

  if (need_all_events)
    {
      zxdg_output_v1_send_done (resource);
      need_done = FALSE;
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

  xdg_output_resource = wl_resource_create (client,
                                            &zxdg_output_v1_interface,
                                            wl_resource_get_version (resource),
                                            id);

  wl_resource_set_implementation (xdg_output_resource,
                                  &meta_xdg_output_interface,
                                  NULL, meta_xdg_output_destructor);

  wayland_output = wl_resource_get_user_data (output);
  if (!wayland_output)
    return;

  wayland_output->xdg_output_resources =
    g_list_prepend (wayland_output->xdg_output_resources, xdg_output_resource);

  if (!wayland_output->logical_monitor)
    return;

  send_xdg_output_events (xdg_output_resource,
                          wayland_output,
                          wayland_output->logical_monitor,
                          TRUE, NULL);
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

  compositor->outputs = g_hash_table_new_full (NULL, NULL, NULL, wayland_output_destroy_notify);
  compositor->outputs = meta_wayland_compositor_update_outputs (compositor, monitors);

  wl_global_create (compositor->wayland_display,
                    &zxdg_output_manager_v1_interface,
                    META_ZXDG_OUTPUT_V1_VERSION,
                    NULL,
                    bind_xdg_output_manager);
}
