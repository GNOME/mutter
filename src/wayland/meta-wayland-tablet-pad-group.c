/*
 * Wayland Support
 *
 * Copyright (C) 2016 Red Hat
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include <wayland-server.h>

#include "compositor/meta-surface-actor-wayland.h"
#include "wayland/meta-wayland-tablet-pad-group.h"
#include "wayland/meta-wayland-tablet-pad-ring.h"
#include "wayland/meta-wayland-tablet-pad-strip.h"
#include "wayland/meta-wayland-tablet-pad-dial.h"
#include "wayland/meta-wayland-tablet-pad.h"
#include "wayland/meta-wayland-tablet-seat.h"

#include "tablet-v2-server-protocol.h"

static void
unbind_resource (struct wl_resource *resource)
{
  wl_list_remove (wl_resource_get_link (resource));
}

MetaWaylandTabletPadGroup *
meta_wayland_tablet_pad_group_new (MetaWaylandTabletPad *pad)
{
  MetaWaylandTabletPadGroup *group;

  group = g_new0 (MetaWaylandTabletPadGroup, 1);
  wl_list_init (&group->resource_list);
  wl_list_init (&group->focus_resource_list);
  group->pad = pad;

  return group;
}

void
meta_wayland_tablet_pad_group_free (MetaWaylandTabletPadGroup *group)
{
  struct wl_resource *resource, *next;

  wl_resource_for_each_safe (resource, next, &group->resource_list)
    {
      wl_list_remove (wl_resource_get_link (resource));
      wl_list_init (wl_resource_get_link (resource));
    }

  g_list_free (group->rings);
  g_list_free (group->strips);
  g_list_free (group->dials);

  g_free (group);
}

static void
tablet_pad_group_destroy (struct wl_client   *client,
                          struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct zwp_tablet_pad_group_v2_interface group_interface = {
  tablet_pad_group_destroy
};

struct wl_resource *
meta_wayland_tablet_pad_group_create_new_resource (MetaWaylandTabletPadGroup *group,
                                                   struct wl_client          *client,
                                                   struct wl_resource        *pad_resource,
                                                   uint32_t                   id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client, &zwp_tablet_pad_group_v2_interface,
                                 wl_resource_get_version (pad_resource), id);
  wl_resource_set_implementation (resource, &group_interface,
                                  group, unbind_resource);
  wl_resource_set_user_data (resource, group);
  wl_list_insert (&group->resource_list, wl_resource_get_link (resource));

  return resource;
}

gboolean
meta_wayland_tablet_pad_group_has_button (MetaWaylandTabletPadGroup *group,
                                          guint                      button)
{
  int n_group = g_list_index (group->pad->groups, group);

  if (clutter_input_device_get_pad_feature_group (group->pad->device,
                                                  CLUTTER_PAD_FEATURE_BUTTON,
                                                  button) == n_group)
    return TRUE;

  return FALSE;
}

static void
meta_wayland_tablet_pad_group_send_buttons (MetaWaylandTabletPadGroup *group,
                                            struct wl_resource        *resource)
{
  struct wl_array buttons;
  guint i;

  wl_array_init (&buttons);

  for (i = 0; i < group->pad->n_buttons; i++)
    {
      uint32_t *pos;

      if (!meta_wayland_tablet_pad_group_has_button (group, i))
        continue;

      pos = wl_array_add (&buttons, sizeof (*pos));
      *pos = i;
    }

  zwp_tablet_pad_group_v2_send_buttons (resource, &buttons);
  wl_array_release (&buttons);
}

void
meta_wayland_tablet_pad_group_notify (MetaWaylandTabletPadGroup *group,
                                      struct wl_resource        *resource)
{
  struct wl_client *client = wl_resource_get_client (resource);
  struct wl_array buttons;
  guint n_group, n_modes;
  GList *l;

  wl_array_init (&buttons);

  /* Buttons */
  meta_wayland_tablet_pad_group_send_buttons (group, resource);

  /* Rings */
  for (l = group->rings; l; l = l->next)
    {
      MetaWaylandTabletPadRing *ring = l->data;
      struct wl_resource *ring_resource;

      ring_resource = meta_wayland_tablet_pad_ring_create_new_resource (ring,
                                                                        client,
                                                                        resource,
                                                                        0);
      zwp_tablet_pad_group_v2_send_ring (resource, ring_resource);
    }

  /* Strips */
  for (l = group->strips; l; l = l->next)
    {
      MetaWaylandTabletPadStrip *strip = l->data;
      struct wl_resource *strip_resource;

      strip_resource = meta_wayland_tablet_pad_strip_create_new_resource (strip,
                                                                          client,
                                                                          resource,
                                                                          0);
      zwp_tablet_pad_group_v2_send_strip (resource, strip_resource);
    }

  if (wl_resource_get_version (resource) >= ZWP_TABLET_PAD_GROUP_V2_DIAL_SINCE_VERSION)
    {
      /* Dials */
      for (l = group->dials; l; l = l->next)
        {
          MetaWaylandTabletPadDial *dial = l->data;
          struct wl_resource *dial_resource;

          dial_resource = meta_wayland_tablet_pad_dial_create_new_resource (dial,
                                                                            client,
                                                                            resource,
                                                                            0);
          zwp_tablet_pad_group_v2_send_dial (resource, dial_resource);
        }
    }

  n_group = g_list_index (group->pad->groups, group);
  n_modes = clutter_input_device_get_group_n_modes (group->pad->device,
                                                    n_group);

  zwp_tablet_pad_group_v2_send_modes (resource, n_modes);
  zwp_tablet_pad_group_v2_send_done (resource);
}

void
meta_wayland_tablet_pad_group_update (MetaWaylandTabletPadGroup *group,
                                      const ClutterEvent        *event)
{
  switch (clutter_event_type (event))
    {
    case CLUTTER_PAD_BUTTON_PRESS:
    case CLUTTER_PAD_BUTTON_RELEASE:
      if (meta_wayland_tablet_pad_group_is_mode_switch_button (group,
                                                               clutter_event_get_button (event)))
        {
          clutter_event_get_pad_details (event, NULL,
                                         &group->current_mode,
                                         NULL, NULL);
        }
      break;
    default:
      break;
    }
}

static gboolean
handle_pad_ring_event (MetaWaylandTabletPadGroup *group,
                       const ClutterEvent        *event)
{
  MetaWaylandTabletPadRing *ring;
  uint32_t number;

  if (clutter_event_type (event) != CLUTTER_PAD_RING)
    return FALSE;

  clutter_event_get_pad_details (event, &number, NULL, NULL, NULL);
  ring = g_list_nth_data (group->rings, number);

  if (!ring)
    return FALSE;

  return meta_wayland_tablet_pad_ring_handle_event (ring, event);
}

static gboolean
handle_pad_strip_event (MetaWaylandTabletPadGroup *group,
                        const ClutterEvent        *event)
{
  MetaWaylandTabletPadStrip *strip;
  uint32_t number;

  if (clutter_event_type (event) != CLUTTER_PAD_STRIP)
    return FALSE;

  clutter_event_get_pad_details (event, &number, NULL, NULL, NULL);
  strip = g_list_nth_data (group->strips, number);

  if (!strip)
    return FALSE;

  return meta_wayland_tablet_pad_strip_handle_event (strip, event);
}

static gboolean
handle_pad_dial_event (MetaWaylandTabletPadGroup *group,
                       const ClutterEvent        *event)
{
  MetaWaylandTabletPadDial *dial;
  uint32_t number;

  if (clutter_event_type (event) != CLUTTER_PAD_DIAL)
    return FALSE;

  clutter_event_get_pad_details (event, &number, NULL, NULL, NULL);
  dial = g_list_nth_data (group->dials, number);

  if (!dial)
    return FALSE;

  return meta_wayland_tablet_pad_dial_handle_event (dial, event);
}

static void
broadcast_group_mode (MetaWaylandTabletPadGroup *group,
                      uint32_t                   time)
{
  struct wl_display *display = group->pad->tablet_seat->seat->wl_display;
  struct wl_resource *resource;

  group->mode_switch_serial = wl_display_next_serial (display);

  wl_resource_for_each (resource, &group->focus_resource_list)
    {
      zwp_tablet_pad_group_v2_send_mode_switch (resource, time,
                                                group->mode_switch_serial,
                                                group->current_mode);
    }
}

static void
broadcast_group_buttons (MetaWaylandTabletPadGroup *group)
{
  struct wl_resource *resource;

  wl_resource_for_each (resource, &group->focus_resource_list)
    {
      meta_wayland_tablet_pad_group_send_buttons (group, resource);
    }
}

gboolean
meta_wayland_tablet_pad_group_handle_event (MetaWaylandTabletPadGroup *group,
                                            const ClutterEvent        *event)
{
  switch (clutter_event_type (event))
    {
    case CLUTTER_PAD_BUTTON_PRESS:
    case CLUTTER_PAD_BUTTON_RELEASE:
      if (meta_wayland_tablet_pad_group_is_mode_switch_button (group,
                                                               clutter_event_get_button (event)))
        {
          if (clutter_event_type (event) == CLUTTER_PAD_BUTTON_PRESS)
            broadcast_group_mode (group, clutter_event_get_time (event));
          return TRUE;
        }
      else
        {
          return FALSE;
        }
      break;
    case CLUTTER_PAD_RING:
      return handle_pad_ring_event (group, event);
    case CLUTTER_PAD_STRIP:
      return handle_pad_strip_event (group, event);
    case CLUTTER_PAD_DIAL:
      return handle_pad_dial_event (group, event);
    default:
      return FALSE;
    }
}

static void
meta_wayland_tablet_pad_group_update_rings_focus (MetaWaylandTabletPadGroup *group)
{
  GList *l;

  for (l = group->rings; l; l = l->next)
    meta_wayland_tablet_pad_ring_sync_focus (l->data);
}

static void
meta_wayland_tablet_pad_group_update_strips_focus (MetaWaylandTabletPadGroup *group)
{
  GList *l;

  for (l = group->strips; l; l = l->next)
    meta_wayland_tablet_pad_strip_sync_focus (l->data);
}

static void
meta_wayland_tablet_pad_group_update_dials_focus (MetaWaylandTabletPadGroup *group)
{
  GList *l;

  for (l = group->dials; l; l = l->next)
    meta_wayland_tablet_pad_dial_sync_focus (l->data);
}

static void
move_resources (struct wl_list *destination, struct wl_list *source)
{
  wl_list_insert_list (destination, source);
  wl_list_init (source);
}

static void
move_resources_for_client (struct wl_list *destination,
			   struct wl_list *source,
			   struct wl_client *client)
{
  struct wl_resource *resource, *tmp;

  wl_resource_for_each_safe (resource, tmp, source)
    {
      if (wl_resource_get_client (resource) == client)
        {
          wl_list_remove (wl_resource_get_link (resource));
          wl_list_insert (destination, wl_resource_get_link (resource));
        }
    }
}

void
meta_wayland_tablet_pad_group_sync_focus (MetaWaylandTabletPadGroup *group)
{
  if (!wl_list_empty (&group->focus_resource_list))
    {
      move_resources (&group->resource_list, &group->focus_resource_list);
    }

  if (group->pad->focus_surface != NULL)
    {
      move_resources_for_client (&group->focus_resource_list,
                                 &group->resource_list,
                                 wl_resource_get_client (group->pad->focus_surface->resource));
    }

  meta_wayland_tablet_pad_group_update_rings_focus (group);
  meta_wayland_tablet_pad_group_update_strips_focus (group);
  meta_wayland_tablet_pad_group_update_dials_focus (group);

  if (!wl_list_empty (&group->focus_resource_list))
    {
      broadcast_group_mode (group, clutter_get_current_event_time ());
      broadcast_group_buttons (group);
    }
}

gboolean
meta_wayland_tablet_pad_group_is_mode_switch_button (MetaWaylandTabletPadGroup *group,
                                                     guint                      button)
{
  gint n_group = g_list_index (group->pad->groups, group);

  g_assert (n_group >= 0);

  return clutter_input_device_is_mode_switch_button (group->pad->device,
                                                     n_group, button);
}
