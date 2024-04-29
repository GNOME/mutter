/*
 * Wayland Support
 *
 * Copyright (C) 2024 Red Hat
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
 */

#include "config.h"

#include "wayland/meta-wayland-tablet-pad-dial.h"

#include <glib.h>
#include <wayland-server.h>

#include "compositor/meta-surface-actor-wayland.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-tablet-pad.h"
#include "wayland/meta-wayland-tablet-pad-group.h"

#include "tablet-v2-server-protocol.h"

static void
unbind_resource (struct wl_resource *resource)
{
  wl_list_remove (wl_resource_get_link (resource));
}

MetaWaylandTabletPadDial *
meta_wayland_tablet_pad_dial_new (MetaWaylandTabletPad *pad)
{
  MetaWaylandTabletPadDial *dial;

  dial = g_new0 (MetaWaylandTabletPadDial, 1);
  wl_list_init (&dial->resource_list);
  wl_list_init (&dial->focus_resource_list);
  dial->pad = pad;

  return dial;
}

void
meta_wayland_tablet_pad_dial_free (MetaWaylandTabletPadDial *dial)
{
  struct wl_resource *resource, *next;

  wl_resource_for_each_safe (resource, next, &dial->resource_list)
    {
      wl_list_remove (wl_resource_get_link (resource));
      wl_list_init (wl_resource_get_link (resource));
    }

  g_free (dial->feedback);
  g_free (dial);
}

static void
tablet_pad_dial_set_feedback (struct wl_client   *client,
                              struct wl_resource *resource,
                              const char         *str,
                              uint32_t            serial)
{
  MetaWaylandTabletPadDial *dial = wl_resource_get_user_data (resource);

  if (dial->group->mode_switch_serial != serial)
    return;

  dial->feedback = g_strdup (str);
}

static void
tablet_pad_dial_destroy (struct wl_client   *client,
                         struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct zwp_tablet_pad_dial_v2_interface dial_interface = {
  tablet_pad_dial_set_feedback,
  tablet_pad_dial_destroy,
};

struct wl_resource *
meta_wayland_tablet_pad_dial_create_new_resource (MetaWaylandTabletPadDial *dial,
                                                  struct wl_client         *client,
                                                  struct wl_resource       *group_resource,
                                                  uint32_t                  id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client, &zwp_tablet_pad_dial_v2_interface,
                                 wl_resource_get_version (group_resource), id);
  wl_resource_set_implementation (resource, &dial_interface,
                                  dial, unbind_resource);
  wl_resource_set_user_data (resource, dial);
  wl_list_insert (&dial->resource_list, wl_resource_get_link (resource));

  return resource;
}

gboolean
meta_wayland_tablet_pad_dial_handle_event (MetaWaylandTabletPadDial *dial,
                                           const ClutterEvent       *event)
{
  struct wl_list *focus_resources = &dial->focus_resource_list;
  struct wl_resource *resource;
  double value;

  if (wl_list_empty (focus_resources))
    return FALSE;
  if (clutter_event_type (event) != CLUTTER_PAD_DIAL)
    return FALSE;

  clutter_event_get_pad_details (event, NULL, NULL, NULL, &value);

  wl_resource_for_each (resource, focus_resources)
    {
      if (value != 0)
        zwp_tablet_pad_dial_v2_send_delta (resource, (int32_t) value);

      zwp_tablet_pad_dial_v2_send_frame (resource,
                                          clutter_event_get_time (event));
    }

  return TRUE;
}

static void
move_resources (struct wl_list *destination,
                struct wl_list *source)
{
  wl_list_insert_list (destination, source);
  wl_list_init (source);
}

static void
move_resources_for_client (struct wl_list   *destination,
                           struct wl_list   *source,
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
meta_wayland_tablet_pad_dial_sync_focus (MetaWaylandTabletPadDial *dial)
{
  g_clear_pointer (&dial->feedback, g_free);

  if (!wl_list_empty (&dial->focus_resource_list))
    {
      move_resources (&dial->resource_list, &dial->focus_resource_list);
    }

  if (dial->pad->focus_surface != NULL)
    {
      move_resources_for_client (&dial->focus_resource_list,
                                 &dial->resource_list,
                                 wl_resource_get_client (dial->pad->focus_surface->resource));
    }
}

void
meta_wayland_tablet_pad_dial_set_group (MetaWaylandTabletPadDial  *dial,
                                        MetaWaylandTabletPadGroup *group)
{
  /* Group is static, can only be set once */
  g_assert (dial->group == NULL);

  dial->group = group;
  group->dials = g_list_append (group->dials, dial);
}
