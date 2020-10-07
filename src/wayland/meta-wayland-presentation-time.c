/*
 * presentation-time protocol
 *
 * Copyright (C) 2020 Ivan Molodetskikh <yalterz@gmail.com>
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
 */

#include "config.h"

#include "meta-wayland-presentation-time-private.h"

#include <glib.h>

#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-surface.h"
#include "wayland/meta-wayland-versions.h"

#include "presentation-time-server-protocol.h"

static void
wp_presentation_feedback_destructor (struct wl_resource *resource)
{
  MetaWaylandPresentationFeedback *feedback =
    wl_resource_get_user_data (resource);

  wl_list_remove (&feedback->link);
  g_free (feedback);
}

static void
wp_presentation_destroy (struct wl_client   *client,
                         struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
wp_presentation_feedback (struct wl_client   *client,
                          struct wl_resource *resource,
                          struct wl_resource *surface_resource,
                          uint32_t            callback_id)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandSurfaceState *pending;
  MetaWaylandPresentationFeedback *feedback;

  feedback = g_new0 (MetaWaylandPresentationFeedback, 1);
  wl_list_init (&feedback->link);
  feedback->resource = wl_resource_create (client,
                                           &wp_presentation_feedback_interface,
                                           wl_resource_get_version (resource),
                                           callback_id);
  wl_resource_set_implementation (feedback->resource,
                                  NULL,
                                  feedback,
                                  wp_presentation_feedback_destructor);

  if (surface == NULL)
    {
      g_warn_if_reached ();
      meta_wayland_presentation_feedback_discard (feedback);
      return;
    }

  pending = meta_wayland_surface_get_pending_state (surface);
  wl_list_insert (&pending->presentation_feedback_list, &feedback->link);

  feedback->surface = surface;
}

static const struct wp_presentation_interface
meta_wayland_presentation_interface = {
  wp_presentation_destroy,
  wp_presentation_feedback,
};

static void
wp_presentation_bind (struct wl_client *client,
                      void             *data,
                      uint32_t          version,
                      uint32_t          id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &wp_presentation_interface,
                                 version,
                                 id);
  wl_resource_set_implementation (resource,
                                  &meta_wayland_presentation_interface,
                                  NULL,
                                  NULL);

  /* Presentation timestamps in Mutter are guaranteed to be CLOCK_MONOTONIC. */
  wp_presentation_send_clock_id (resource, CLOCK_MONOTONIC);
}

void
meta_wayland_init_presentation_time (MetaWaylandCompositor *compositor)
{
  if (wl_global_create (compositor->wayland_display,
                        &wp_presentation_interface,
                        META_WP_PRESENTATION_VERSION,
                        NULL,
                        wp_presentation_bind) == NULL)
    g_error ("Failed to register a global wp_presentation object");
}

void
meta_wayland_presentation_feedback_discard (MetaWaylandPresentationFeedback *feedback)
{
  wp_presentation_feedback_send_discarded (feedback->resource);
  wl_resource_destroy (feedback->resource);
}
