/*
 * Wayland Support
 *
 * Copyright (C) 2013 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright © 2008 Kristian Høgsberg
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

/* The file is based on src/input.c from Weston */

#include "config.h"

#include <linux/input.h>
#include <string.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-cursor-renderer.h"
#include "backends/meta-cursor-tracker-private.h"
#include "backends/meta-cursor.h"
#include "clutter/clutter.h"
#include "cogl/cogl.h"
#include "compositor/meta-surface-actor-wayland.h"
#include "meta/meta-cursor-tracker.h"
#include "wayland/meta-cursor-sprite-wayland.h"
#include "wayland/meta-wayland-buffer.h"
#include "wayland/meta-wayland-cursor-surface.h"
#include "wayland/meta-wayland-pointer.h"
#include "wayland/meta-wayland-popup.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-seat.h"
#include "wayland/meta-wayland-surface-private.h"

#ifdef HAVE_XWAYLAND
#include "wayland/meta-xwayland.h"
#endif

#ifdef HAVE_NATIVE_BACKEND
#include "backends/native/meta-backend-native.h"
#endif

#include "relative-pointer-unstable-v1-server-protocol.h"

#define DEFAULT_AXIS_STEP_DISTANCE wl_fixed_from_int (10)

enum
{
  FOCUS_SURFACE_CHANGED,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

struct _MetaWaylandPointer
{
  MetaWaylandInputDevice parent;

  MetaWaylandPointerClient *focus_client;
  GHashTable *pointer_clients;

  MetaWaylandSurface *focus_surface;
  gulong focus_surface_destroyed_handler_id;
  gulong focus_surface_alive_notify_id;
  guint32 focus_serial;
  guint32 click_serial;

  MetaWaylandSurface *cursor_surface;
  gulong cursor_surface_destroy_id;

  guint32 grab_button;
  guint32 grab_serial;
  guint32 grab_time;
  float grab_x, grab_y;
  float last_rel_x, last_rel_y;

  ClutterInputDevice *device;
  MetaWaylandSurface *current;
  gulong current_surface_destroyed_handler_id;

  guint32 button_count;
};

G_DEFINE_TYPE (MetaWaylandPointer, meta_wayland_pointer,
               META_TYPE_WAYLAND_INPUT_DEVICE)

static void
meta_wayland_pointer_set_current (MetaWaylandPointer *pointer,
                                  MetaWaylandSurface *surface);

static void meta_wayland_pointer_set_focus (MetaWaylandPointer *pointer,
                                            MetaWaylandSurface *surface);

static MetaBackend *
backend_from_pointer (MetaWaylandPointer *pointer)
{
  MetaWaylandInputDevice *input_device = META_WAYLAND_INPUT_DEVICE (pointer);
  MetaWaylandSeat *seat = meta_wayland_input_device_get_seat (input_device);
  MetaWaylandCompositor *compositor = meta_wayland_seat_get_compositor (seat);
  MetaContext *context = meta_wayland_compositor_get_context (compositor);

  return meta_context_get_backend (context);
}

static MetaWaylandPointerClient *
meta_wayland_pointer_client_new (void)
{
  MetaWaylandPointerClient *pointer_client;

  pointer_client = g_new0 (MetaWaylandPointerClient, 1);
  wl_list_init (&pointer_client->pointer_resources);
  wl_list_init (&pointer_client->swipe_gesture_resources);
  wl_list_init (&pointer_client->pinch_gesture_resources);
  wl_list_init (&pointer_client->hold_gesture_resources);
  wl_list_init (&pointer_client->relative_pointer_resources);

  return pointer_client;
}

static void
meta_wayland_pointer_make_resources_inert (MetaWaylandPointerClient *pointer_client)
{
  struct wl_resource *resource, *next;

  /* Since we make every wl_pointer resource defunct when we stop advertising
   * the pointer capability on the wl_seat, we need to make sure all the
   * resources in the pointer client instance gets removed.
   */
  wl_resource_for_each_safe (resource, next, &pointer_client->pointer_resources)
    {
      wl_list_remove (wl_resource_get_link (resource));
      wl_list_init (wl_resource_get_link (resource));
      wl_resource_set_user_data (resource, NULL);
    }
  wl_resource_for_each_safe (resource, next, &pointer_client->swipe_gesture_resources)
    {
      wl_list_remove (wl_resource_get_link (resource));
      wl_list_init (wl_resource_get_link (resource));
      wl_resource_set_user_data (resource, NULL);
    }
  wl_resource_for_each_safe (resource, next, &pointer_client->pinch_gesture_resources)
    {
      wl_list_remove (wl_resource_get_link (resource));
      wl_list_init (wl_resource_get_link (resource));
      wl_resource_set_user_data (resource, NULL);
    }
  wl_resource_for_each_safe (resource, next, &pointer_client->hold_gesture_resources)
    {
      wl_list_remove (wl_resource_get_link (resource));
      wl_list_init (wl_resource_get_link (resource));
    }
  wl_resource_for_each_safe (resource, next, &pointer_client->relative_pointer_resources)
    {
      wl_list_remove (wl_resource_get_link (resource));
      wl_list_init (wl_resource_get_link (resource));
      wl_resource_set_user_data (resource, NULL);
    }
}

static void
meta_wayland_pointer_client_free (MetaWaylandPointerClient *pointer_client)
{
  meta_wayland_pointer_make_resources_inert (pointer_client);
  g_free (pointer_client);
}

static void
make_resources_inert_foreach (gpointer key,
                              gpointer value,
                              gpointer data)
{
  MetaWaylandPointerClient *pointer_client = value;

  meta_wayland_pointer_make_resources_inert (pointer_client);
}

static gboolean
meta_wayland_pointer_client_is_empty (MetaWaylandPointerClient *pointer_client)
{
  return (wl_list_empty (&pointer_client->pointer_resources) &&
          wl_list_empty (&pointer_client->swipe_gesture_resources) &&
          wl_list_empty (&pointer_client->pinch_gesture_resources) &&
          wl_list_empty (&pointer_client->hold_gesture_resources) &&
          wl_list_empty (&pointer_client->relative_pointer_resources));
}

static void
meta_wayland_pointer_client_maybe_cancel_gesture (MetaWaylandPointer       *pointer,
                                                  MetaWaylandPointerClient *pointer_client,
                                                  uint32_t                  serial)
{
  switch (pointer_client->active_touchpad_gesture)
    {
    case CLUTTER_TOUCHPAD_SWIPE:
      meta_wayland_pointer_gesture_swipe_cancel (pointer, serial);
      break;

    case CLUTTER_TOUCHPAD_PINCH:
      meta_wayland_pointer_gesture_pinch_cancel (pointer, serial);
      break;

    case CLUTTER_TOUCHPAD_HOLD:
      meta_wayland_pointer_gesture_hold_cancel (pointer, serial);
      break;

    default:
      break;
    }
}

MetaWaylandPointerClient *
meta_wayland_pointer_get_pointer_client (MetaWaylandPointer *pointer,
                                         struct wl_client   *client)
{
  return g_hash_table_lookup (pointer->pointer_clients, client);
}

static MetaWaylandPointerClient *
meta_wayland_pointer_ensure_pointer_client (MetaWaylandPointer *pointer,
                                            struct wl_client   *client)
{
  MetaWaylandPointerClient *pointer_client;

  pointer_client = meta_wayland_pointer_get_pointer_client (pointer, client);
  if (pointer_client)
    return pointer_client;

  pointer_client = meta_wayland_pointer_client_new ();
  g_hash_table_insert (pointer->pointer_clients, client, pointer_client);

  if (!pointer->focus_client &&
      pointer->focus_surface &&
      wl_resource_get_client (pointer->focus_surface->resource) == client)
    pointer->focus_client = pointer_client;

  return pointer_client;
}

static void
meta_wayland_pointer_cleanup_pointer_client (MetaWaylandPointer       *pointer,
                                             MetaWaylandPointerClient *pointer_client,
                                             struct wl_client         *client)
{
  if (meta_wayland_pointer_client_is_empty (pointer_client))
    {
      if (pointer->focus_client == pointer_client)
        pointer->focus_client = NULL;
      g_hash_table_remove (pointer->pointer_clients, client);
    }
}

void
meta_wayland_pointer_unbind_pointer_client_resource (struct wl_resource *resource)
{
  MetaWaylandPointer *pointer = wl_resource_get_user_data (resource);
  MetaWaylandPointerClient *pointer_client;
  struct wl_client *client = wl_resource_get_client (resource);

  pointer = wl_resource_get_user_data (resource);
  if (!pointer)
    return;

  wl_list_remove (wl_resource_get_link (resource));

  pointer_client = meta_wayland_pointer_get_pointer_client (pointer, client);
  if (!pointer_client)
    {
      /* This happens if all pointer devices were unplugged and no new resources
       * were created by the client.
       *
       * If this is a resource that was previously made defunct, pointer_client
       * be non-NULL but it is harmless since the below cleanup call will be
       * prevented from removing the pointer client because of valid resources.
       */
      return;
    }

  meta_wayland_pointer_cleanup_pointer_client (pointer,
                                               pointer_client,
                                               client);
}

static MetaWindow *
surface_get_effective_window (MetaWaylandSurface *surface)
{
  MetaWaylandSurface *toplevel;

  toplevel = meta_wayland_surface_get_toplevel (surface);
  if (!toplevel)
    return NULL;

  return meta_wayland_surface_get_window (toplevel);
}

static void
sync_focus_surface (MetaWaylandPointer *pointer)
{
  MetaWaylandInputDevice *input_device = META_WAYLAND_INPUT_DEVICE (pointer);
  MetaWaylandSeat *seat = meta_wayland_input_device_get_seat (input_device);
  MetaWaylandInput *input;

  input = meta_wayland_seat_get_input (seat);
  meta_wayland_input_invalidate_focus (input, pointer->device, NULL);
}

static void
meta_wayland_pointer_send_frame (MetaWaylandPointer *pointer,
				 struct wl_resource *resource)
{
  if (wl_resource_get_version (resource) >= WL_POINTER_AXIS_SOURCE_SINCE_VERSION)
    wl_pointer_send_frame (resource);
}

void
meta_wayland_pointer_broadcast_frame (MetaWaylandPointer *pointer)
{
  struct wl_resource *resource;

  if (!pointer->focus_client)
    return;

  wl_resource_for_each (resource, &pointer->focus_client->pointer_resources)
    {
      meta_wayland_pointer_send_frame (pointer, resource);
    }
}

void
meta_wayland_pointer_send_relative_motion (MetaWaylandPointer *pointer,
                                           const ClutterEvent *event)
{
  struct wl_resource *resource;
  double dx, dy;
  double dx_unaccel, dy_unaccel;
  uint64_t time_us;
  uint32_t time_us_hi;
  uint32_t time_us_lo;
  wl_fixed_t dxf, dyf;
  wl_fixed_t dx_unaccelf, dy_unaccelf;

  if (!pointer->focus_client)
    return;

  if (!clutter_event_get_relative_motion (event,
                                          &dx, &dy,
                                          &dx_unaccel, &dy_unaccel,
                                          NULL, NULL))
    return;

  time_us = clutter_event_get_time_us (event);
  if (time_us == 0)
    time_us = clutter_event_get_time (event) * 1000ULL;
  time_us_hi = (uint32_t) (time_us >> 32);
  time_us_lo = (uint32_t) time_us;
  dxf = wl_fixed_from_double (dx);
  dyf = wl_fixed_from_double (dy);
  dx_unaccelf = wl_fixed_from_double (dx_unaccel);
  dy_unaccelf = wl_fixed_from_double (dy_unaccel);

  wl_resource_for_each (resource,
                        &pointer->focus_client->relative_pointer_resources)
    {
      zwp_relative_pointer_v1_send_relative_motion (resource,
                                                    time_us_hi,
                                                    time_us_lo,
                                                    dxf,
                                                    dyf,
                                                    dx_unaccelf,
                                                    dy_unaccelf);
    }
}

static void
meta_wayland_pointer_send_motion (MetaWaylandPointer *pointer,
                                  const ClutterEvent *event)
{
  struct wl_resource *resource;
  uint32_t time;
  float x, y, sx, sy;

  if (!pointer->focus_client)
    return;

  time = clutter_event_get_time (event);
  clutter_event_get_coords (event, &x, &y);
  meta_wayland_surface_get_relative_coordinates (pointer->focus_surface,
                                                 x, y, &sx, &sy);

  if (pointer->last_rel_x != sx ||
      pointer->last_rel_y != sy)
    {
      wl_resource_for_each (resource, &pointer->focus_client->pointer_resources)
        {
          wl_pointer_send_motion (resource, time,
                                  wl_fixed_from_double (sx),
                                  wl_fixed_from_double (sy));
        }

      pointer->last_rel_x = sx;
      pointer->last_rel_y = sy;
    }

  meta_wayland_pointer_send_relative_motion (pointer, event);

  meta_wayland_pointer_broadcast_frame (pointer);
}

static void
meta_wayland_pointer_send_button (MetaWaylandPointer *pointer,
                                  const ClutterEvent *event)
{
  struct wl_resource *resource;
  ClutterEventType event_type;

  event_type = clutter_event_type (event);

  if (pointer->focus_client &&
      !wl_list_empty (&pointer->focus_client->pointer_resources))
    {
      MetaWaylandInputDevice *input_device =
        META_WAYLAND_INPUT_DEVICE (pointer);
      uint32_t time;
      uint32_t button;
      uint32_t serial;

      button = clutter_event_get_event_code (event);
      time = clutter_event_get_time (event);
      serial = meta_wayland_input_device_next_serial (input_device);

      wl_resource_for_each (resource, &pointer->focus_client->pointer_resources)
        {
          wl_pointer_send_button (resource, serial,
                                  time, button,
                                  event_type == CLUTTER_BUTTON_PRESS ? 1 : 0);
        }

      meta_wayland_pointer_broadcast_frame (pointer);
    }

  if (pointer->button_count == 0 && event_type == CLUTTER_BUTTON_RELEASE)
    sync_focus_surface (pointer);
}

static void
meta_wayland_pointer_on_cursor_changed (MetaCursorTracker *cursor_tracker,
                                        MetaWaylandPointer *pointer)
{
  if (pointer->cursor_surface)
    meta_wayland_surface_update_outputs (pointer->cursor_surface);
}

void
meta_wayland_pointer_enable (MetaWaylandPointer *pointer)
{
  MetaBackend *backend = backend_from_pointer (pointer);
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);
  ClutterSeat *clutter_seat;

  pointer->cursor_surface = NULL;

  clutter_seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  pointer->device = clutter_seat_get_pointer (clutter_seat);

  g_signal_connect (cursor_tracker,
                    "cursor-changed",
                    G_CALLBACK (meta_wayland_pointer_on_cursor_changed),
                    pointer);

  pointer->last_rel_x = -FLT_MAX;
  pointer->last_rel_y = -FLT_MAX;
}

void
meta_wayland_pointer_disable (MetaWaylandPointer *pointer)
{
  MetaBackend *backend = backend_from_pointer (pointer);
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);

  g_hash_table_foreach (pointer->pointer_clients,
                        make_resources_inert_foreach,
                        NULL);

  g_signal_handlers_disconnect_by_func (cursor_tracker,
                                        (gpointer) meta_wayland_pointer_on_cursor_changed,
                                        pointer);

  if (pointer->cursor_surface)
    {
      g_clear_signal_handler (&pointer->cursor_surface_destroy_id,
                              pointer->cursor_surface);
    }

  meta_wayland_pointer_set_focus (pointer, NULL);
  meta_wayland_pointer_set_current (pointer, NULL);

  pointer->cursor_surface = NULL;
}

static int
count_buttons (const ClutterEvent *event)
{
  static gint maskmap[5] =
    {
      CLUTTER_BUTTON1_MASK, CLUTTER_BUTTON2_MASK, CLUTTER_BUTTON3_MASK,
      CLUTTER_BUTTON4_MASK, CLUTTER_BUTTON5_MASK
    };
  ClutterModifierType mod_mask;
  int i, count;

  mod_mask = clutter_event_get_state (event);
  count = 0;
  for (i = 0; i < 5; i++)
    {
      if (mod_mask & maskmap[i])
	count++;
    }

  return count;
}

static void
current_surface_destroyed (MetaWaylandSurface *surface,
                           MetaWaylandPointer *pointer)
{
  meta_wayland_pointer_set_current (pointer, NULL);
}

static void
meta_wayland_pointer_set_current (MetaWaylandPointer *pointer,
                                  MetaWaylandSurface *surface)
{
  if (pointer->current == surface)
    return;

  if (pointer->current)
    {
      g_clear_signal_handler (&pointer->current_surface_destroyed_handler_id,
                              pointer->current);
      pointer->current = NULL;
    }

  if (surface)
    {
      pointer->current = surface;
      pointer->current_surface_destroyed_handler_id =
        g_signal_connect (surface, "destroy",
                          G_CALLBACK (current_surface_destroyed),
                          pointer);
    }

  meta_wayland_pointer_update_cursor_surface (pointer);
}

static void
repick_for_event (MetaWaylandPointer *pointer,
                  const ClutterEvent *for_event)
{
  ClutterActor *actor;
  MetaWaylandSurface *surface;
  MetaBackend *backend = backend_from_pointer (pointer);
  ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));

  actor = clutter_stage_get_device_actor (stage,
                                          clutter_event_get_device (for_event),
                                          clutter_event_get_event_sequence (for_event));

  if (META_IS_SURFACE_ACTOR_WAYLAND (actor))
    {
      MetaSurfaceActorWayland *actor_wayland =
        META_SURFACE_ACTOR_WAYLAND (actor);

      surface = meta_surface_actor_wayland_get_surface (actor_wayland);

      if (meta_window_has_modals (meta_wayland_surface_get_window (surface)))
        surface = NULL;
    }
  else
    {
      surface = NULL;
    }

  meta_wayland_pointer_set_current (pointer, surface);

  sync_focus_surface (pointer);
  meta_wayland_pointer_update_cursor_surface (pointer);
}

void
meta_wayland_pointer_update (MetaWaylandPointer *pointer,
                             const ClutterEvent *event)
{
  MetaWaylandInputDevice *input_device = META_WAYLAND_INPUT_DEVICE (pointer);
  MetaWaylandSeat *seat = meta_wayland_input_device_get_seat (input_device);
  MetaWaylandCompositor *compositor = meta_wayland_seat_get_compositor (seat);
  MetaContext *context =
    meta_wayland_compositor_get_context (compositor);
  MetaDisplay *display = meta_context_get_display (context);
  ClutterEventType event_type;

  event_type = clutter_event_type (event);

  if ((event_type == CLUTTER_MOTION ||
       event_type == CLUTTER_ENTER ||
       event_type == CLUTTER_LEAVE) &&
      !clutter_event_get_event_sequence (event))
    {
      repick_for_event (pointer, event);

      if (event_type == CLUTTER_ENTER || event_type == CLUTTER_LEAVE)
        {
          ClutterInputDevice *device;
          graphene_point_t pos;
          MetaWindow *focus_window = NULL;

          device = clutter_event_get_source_device (event);
          clutter_event_get_coords (event, &pos.x, &pos.y);

          if (clutter_input_device_get_device_mode (device) != CLUTTER_INPUT_MODE_LOGICAL)
            {
              if (pointer->focus_surface)
                focus_window = meta_wayland_surface_get_window (pointer->focus_surface);

              meta_display_handle_window_enter (display,
                                                focus_window,
                                                clutter_event_get_time (event),
                                                pos.x, pos.y);
            }
        }
    }

  if (event_type == CLUTTER_MOTION ||
      event_type == CLUTTER_BUTTON_PRESS ||
      event_type == CLUTTER_BUTTON_RELEASE)
    {
      pointer->button_count = count_buttons (event);
    }
}

static void
notify_motion (MetaWaylandPointer *pointer,
               const ClutterEvent *event)
{
  meta_wayland_pointer_send_motion (pointer, event);
}

static void
handle_motion_event (MetaWaylandPointer *pointer,
                     const ClutterEvent *event)
{
  notify_motion (pointer, event);
}

static void
handle_button_event (MetaWaylandPointer *pointer,
                     const ClutterEvent *event)
{
  gboolean implicit_grab;

  implicit_grab = (clutter_event_type (event) == CLUTTER_BUTTON_PRESS) && (pointer->button_count == 1);
  if (implicit_grab)
    {
      pointer->grab_button = clutter_event_get_button (event);
      pointer->grab_time = clutter_event_get_time (event);
      clutter_event_get_coords (event, &pointer->grab_x, &pointer->grab_y);
    }

  meta_wayland_pointer_send_button (pointer, event);

  if (implicit_grab)
    {
      MetaWaylandSeat *seat = meta_wayland_pointer_get_seat (pointer);

      pointer->grab_serial = wl_display_get_serial (seat->wl_display);
    }
}

static void
handle_scroll_event (MetaWaylandPointer *pointer,
                     const ClutterEvent *event)
{
  struct wl_resource *resource;
  wl_fixed_t x_value = 0, y_value = 0;
  int x_discrete = 0, y_discrete = 0;
  int32_t x_value120 = 0, y_value120 = 0;
  enum wl_pointer_axis_source source = -1;
  MetaWaylandPointerClient *client;
  gboolean is_discrete_event = FALSE, is_value120_event = FALSE;
  ClutterScrollFinishFlags finish_flags;

  if (clutter_event_get_flags (event) & CLUTTER_EVENT_FLAG_POINTER_EMULATED)
    return;

  client = pointer->focus_client;
  if (!client)
    return;

  switch (clutter_event_get_scroll_source (event))
    {
    case CLUTTER_SCROLL_SOURCE_WHEEL:
      source = WL_POINTER_AXIS_SOURCE_WHEEL;
      break;
    case CLUTTER_SCROLL_SOURCE_FINGER:
      source = WL_POINTER_AXIS_SOURCE_FINGER;
      break;
    case CLUTTER_SCROLL_SOURCE_CONTINUOUS:
      source = WL_POINTER_AXIS_SOURCE_CONTINUOUS;
      break;
    default:
      source = WL_POINTER_AXIS_SOURCE_WHEEL;
      break;
    }

  switch (clutter_event_get_scroll_direction (event))
    {
    case CLUTTER_SCROLL_UP:
      is_discrete_event = TRUE;
      y_value = -DEFAULT_AXIS_STEP_DISTANCE;
      y_discrete = -1;
      break;

    case CLUTTER_SCROLL_DOWN:
      is_discrete_event = TRUE;
      y_value = DEFAULT_AXIS_STEP_DISTANCE;
      y_discrete = 1;
      break;

    case CLUTTER_SCROLL_LEFT:
      is_discrete_event = TRUE;
      x_value = -DEFAULT_AXIS_STEP_DISTANCE;
      x_discrete = -1;
      break;

    case CLUTTER_SCROLL_RIGHT:
      is_discrete_event = TRUE;
      x_value = DEFAULT_AXIS_STEP_DISTANCE;
      x_discrete = 1;
      break;

    case CLUTTER_SCROLL_SMOOTH:
      {
        double dx, dy;
        /* Clutter smooth scroll events are in discrete steps (1 step = 1.0 long
         * vector along one axis). To convert to smooth scroll events that are
         * in pointer motion event space, multiply the vector with the 10. */
        const double factor = 10.0;
        clutter_event_get_scroll_delta (event, &dx, &dy);
        x_value = wl_fixed_from_double (dx) * factor;
        y_value = wl_fixed_from_double (dy) * factor;

        is_value120_event = (source == WL_POINTER_AXIS_SOURCE_WHEEL);
        if (is_value120_event)
          {
            x_value120 = (int32_t) (dx * 120);
            y_value120 = (int32_t) (dy * 120);
          }
      }
      break;

    default:
      return;
    }

  finish_flags = clutter_event_get_scroll_finish_flags (event);

  wl_resource_for_each (resource, &client->pointer_resources)
    {
      int client_version = wl_resource_get_version (resource);
      gboolean send_axis_x = TRUE, send_axis_y = TRUE;

      if (client_version >= WL_POINTER_AXIS_SOURCE_SINCE_VERSION)
        wl_pointer_send_axis_source (resource, source);

      /* X axis */
      if (client_version >= WL_POINTER_AXIS_VALUE120_SINCE_VERSION)
        {
          if (is_value120_event && x_value120 != 0)
            wl_pointer_send_axis_value120 (resource,
                                           WL_POINTER_AXIS_HORIZONTAL_SCROLL,
                                           x_value120);

          send_axis_x = !is_discrete_event;
        }
      else if (client_version >= WL_POINTER_AXIS_DISCRETE_SINCE_VERSION)
        {
          if (is_discrete_event && x_discrete != 0)
            wl_pointer_send_axis_discrete (resource,
                                           WL_POINTER_AXIS_HORIZONTAL_SCROLL,
                                           x_discrete);

          send_axis_x = !is_value120_event;
        }

      if (x_value && send_axis_x)
        wl_pointer_send_axis (resource, clutter_event_get_time (event),
                              WL_POINTER_AXIS_HORIZONTAL_SCROLL, x_value);

      if ((finish_flags & CLUTTER_SCROLL_FINISHED_HORIZONTAL) &&
          client_version >= WL_POINTER_AXIS_STOP_SINCE_VERSION)
        wl_pointer_send_axis_stop (resource,
                                   clutter_event_get_time (event),
                                   WL_POINTER_AXIS_HORIZONTAL_SCROLL);
      /* Y axis */
      if (client_version >= WL_POINTER_AXIS_VALUE120_SINCE_VERSION)
        {
          if (is_value120_event && y_value120 != 0)
            wl_pointer_send_axis_value120 (resource,
                                           WL_POINTER_AXIS_VERTICAL_SCROLL,
                                           y_value120);

          send_axis_y = !is_discrete_event;
        }
      else if (client_version >= WL_POINTER_AXIS_DISCRETE_SINCE_VERSION)
        {
          if (is_discrete_event && y_discrete != 0)
            wl_pointer_send_axis_discrete (resource,
                                           WL_POINTER_AXIS_VERTICAL_SCROLL,
                                           y_discrete);

          send_axis_y = !is_value120_event;
        }

      if (y_value && send_axis_y)
        wl_pointer_send_axis (resource, clutter_event_get_time (event),
                              WL_POINTER_AXIS_VERTICAL_SCROLL, y_value);

      if ((finish_flags & CLUTTER_SCROLL_FINISHED_VERTICAL) &&
          client_version >= WL_POINTER_AXIS_STOP_SINCE_VERSION)
        wl_pointer_send_axis_stop (resource,
                                   clutter_event_get_time (event),
                                   WL_POINTER_AXIS_VERTICAL_SCROLL);
    }

  meta_wayland_pointer_broadcast_frame (pointer);
}

gboolean
meta_wayland_pointer_handle_event (MetaWaylandPointer *pointer,
                                   const ClutterEvent *event)
{
  switch (clutter_event_type (event))
    {
    case CLUTTER_MOTION:
      handle_motion_event (pointer, event);
      return pointer->focus_surface ?
        CLUTTER_EVENT_STOP : CLUTTER_EVENT_PROPAGATE;

    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_BUTTON_RELEASE:
      handle_button_event (pointer, event);
      return pointer->focus_surface ?
        CLUTTER_EVENT_STOP : CLUTTER_EVENT_PROPAGATE;

    case CLUTTER_SCROLL:
      handle_scroll_event (pointer, event);
      return pointer->focus_surface ?
        CLUTTER_EVENT_STOP : CLUTTER_EVENT_PROPAGATE;

    case CLUTTER_TOUCHPAD_SWIPE:
      meta_wayland_pointer_gesture_swipe_handle_event (pointer, event);
      return CLUTTER_EVENT_PROPAGATE;

    case CLUTTER_TOUCHPAD_PINCH:
      return meta_wayland_pointer_gesture_pinch_handle_event (pointer, event);

    case CLUTTER_TOUCHPAD_HOLD:
      return meta_wayland_pointer_gesture_hold_handle_event (pointer, event);

    default:
      return CLUTTER_EVENT_PROPAGATE;
    }
}

static void
meta_wayland_pointer_send_enter (MetaWaylandPointer *pointer,
                                 struct wl_resource *pointer_resource,
                                 uint32_t            serial,
                                 MetaWaylandSurface *surface)
{
  wl_fixed_t sx, sy;

  meta_wayland_pointer_get_relative_coordinates (pointer, surface, &sx, &sy);
  wl_pointer_send_enter (pointer_resource,
                         serial,
                         surface->resource,
                         sx, sy);
}

static void
meta_wayland_pointer_send_leave (MetaWaylandPointer *pointer,
                                 struct wl_resource *pointer_resource,
                                 uint32_t            serial,
                                 MetaWaylandSurface *surface)
{
  wl_pointer_send_leave (pointer_resource, serial, surface->resource);
}

static void
meta_wayland_pointer_broadcast_enter (MetaWaylandPointer *pointer,
                                      uint32_t            serial,
                                      MetaWaylandSurface *surface)
{
  struct wl_resource *pointer_resource;

  wl_resource_for_each (pointer_resource,
                        &pointer->focus_client->pointer_resources)
    meta_wayland_pointer_send_enter (pointer, pointer_resource,
                                     serial, surface);

  meta_wayland_pointer_broadcast_frame (pointer);
}

static void
meta_wayland_pointer_broadcast_leave (MetaWaylandPointer *pointer,
                                      uint32_t            serial,
                                      MetaWaylandSurface *surface)
{
  struct wl_resource *pointer_resource;

  wl_resource_for_each (pointer_resource,
                        &pointer->focus_client->pointer_resources)
    meta_wayland_pointer_send_leave (pointer, pointer_resource,
                                     serial, surface);

  meta_wayland_pointer_broadcast_frame (pointer);
}

static void
focus_surface_destroyed (MetaWaylandSurface *surface,
                         MetaWaylandPointer *pointer)
{
  meta_wayland_pointer_set_focus (pointer, NULL);
}

static void
focus_surface_alive_notify (MetaWindow         *window,
                            GParamSpec         *pspec,
                            MetaWaylandPointer *pointer)
{
  if (!meta_window_get_alive (window))
    meta_wayland_pointer_set_focus (pointer, NULL);
  sync_focus_surface (pointer);
}

static void
meta_wayland_pointer_set_focus (MetaWaylandPointer *pointer,
                                MetaWaylandSurface *surface)
{
  MetaWaylandInputDevice *input_device = META_WAYLAND_INPUT_DEVICE (pointer);
  MetaBackend *backend = backend_from_pointer (pointer);
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);
  ClutterBackend *clutter_backend = clutter_get_default_backend ();
  ClutterSeat *clutter_seat = clutter_backend_get_default_seat (clutter_backend);
  MetaWindow *toplevel_window;

  g_return_if_fail (meta_cursor_tracker_get_pointer_visible (cursor_tracker) ||
                    clutter_seat_is_unfocus_inhibited (clutter_seat) ||
                    surface == NULL);

  if (pointer->focus_surface == surface)
    return;

  pointer->last_rel_x = -FLT_MAX;
  pointer->last_rel_y = -FLT_MAX;

  if (pointer->focus_surface != NULL)
    {
      uint32_t serial;

      serial = meta_wayland_input_device_next_serial (input_device);

      if (pointer->focus_client)
        {
          meta_wayland_pointer_client_maybe_cancel_gesture (pointer,
                                                            pointer->focus_client,
                                                            serial);

          meta_wayland_pointer_broadcast_leave (pointer,
                                                serial,
                                                pointer->focus_surface);
          pointer->focus_client = NULL;
        }

      toplevel_window = surface_get_effective_window (pointer->focus_surface);
      if (toplevel_window)
        {
          g_clear_signal_handler (&pointer->focus_surface_alive_notify_id,
                                  toplevel_window);
        }

      g_clear_signal_handler (&pointer->focus_surface_destroyed_handler_id,
                              pointer->focus_surface);
      pointer->focus_surface = NULL;
    }

  if (surface != NULL && surface->resource != NULL)
    {
      struct wl_client *client = wl_resource_get_client (surface->resource);

      pointer->focus_surface = surface;

      pointer->focus_surface_destroyed_handler_id =
        g_signal_connect_after (pointer->focus_surface, "destroy",
                                G_CALLBACK (focus_surface_destroyed),
                                pointer);

      toplevel_window = surface_get_effective_window (pointer->focus_surface);
      if (toplevel_window)
        {
          pointer->focus_surface_alive_notify_id =
            g_signal_connect (toplevel_window, "notify::is-alive",
                              G_CALLBACK (focus_surface_alive_notify),
                              pointer);
        }

      pointer->focus_client =
        meta_wayland_pointer_get_pointer_client (pointer, client);
      if (pointer->focus_client)
        {
          pointer->focus_serial =
            meta_wayland_input_device_next_serial (input_device);
          meta_wayland_pointer_broadcast_enter (pointer,
                                                pointer->focus_serial,
                                                pointer->focus_surface);
        }
    }

  meta_wayland_pointer_update_cursor_surface (pointer);

  g_signal_emit (pointer, signals[FOCUS_SURFACE_CHANGED], 0);
}

void
meta_wayland_pointer_focus_surface (MetaWaylandPointer *pointer,
                                    MetaWaylandSurface *surface)
{
  MetaWaylandSeat *seat = meta_wayland_pointer_get_seat (pointer);

  if (!meta_wayland_seat_has_pointer (seat))
    return;

  if (surface)
    {
      MetaWindow *window = NULL;

      window = surface_get_effective_window (surface);

      /* Avoid focusing a non-alive surface */
      if (!window || !meta_window_get_alive (window))
        surface = NULL;
    }

  meta_wayland_pointer_set_focus (pointer, surface);
}

void
meta_wayland_pointer_get_relative_coordinates (MetaWaylandPointer *pointer,
					       MetaWaylandSurface *surface,
					       wl_fixed_t         *sx,
					       wl_fixed_t         *sy)
{
  MetaBackend *backend = backend_from_pointer (pointer);
  ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
  float xf = 0.0f, yf = 0.0f;
  graphene_point_t pos;

  clutter_stage_get_device_coords (stage, pointer->device, NULL, &pos);
  meta_wayland_surface_get_relative_coordinates (surface, pos.x, pos.y, &xf, &yf);

  *sx = wl_fixed_from_double (xf);
  *sy = wl_fixed_from_double (yf);
}

void
meta_wayland_pointer_update_cursor_surface (MetaWaylandPointer *pointer)
{
  MetaBackend *backend = backend_from_pointer (pointer);
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);

  if (pointer->current)
    {
      MetaCursorSprite *cursor_sprite = NULL;

      if (pointer->cursor_surface)
        {
          MetaWaylandCursorSurface *cursor_surface =
            META_WAYLAND_CURSOR_SURFACE (pointer->cursor_surface->role);

          cursor_sprite = meta_wayland_cursor_surface_get_sprite (cursor_surface);
        }

      meta_cursor_tracker_set_window_cursor (cursor_tracker, cursor_sprite);
    }
  else
    {
      meta_cursor_tracker_unset_window_cursor (cursor_tracker);
    }
}

static void
ensure_update_cursor_surface (MetaWaylandPointer *pointer,
                              MetaWaylandSurface *surface)
{
  if (pointer->cursor_surface != surface)
    return;

  pointer->cursor_surface = NULL;
  meta_wayland_pointer_update_cursor_surface (pointer);
}

static void
meta_wayland_pointer_set_cursor_surface (MetaWaylandPointer *pointer,
                                         MetaWaylandSurface *cursor_surface)
{
  MetaWaylandSurface *prev_cursor_surface;

  prev_cursor_surface = pointer->cursor_surface;

  if (prev_cursor_surface == cursor_surface)
    return;

  pointer->cursor_surface = cursor_surface;

  if (prev_cursor_surface)
    {
      meta_wayland_surface_update_outputs (prev_cursor_surface);
      g_clear_signal_handler (&pointer->cursor_surface_destroy_id,
                              prev_cursor_surface);
    }

  if (cursor_surface)
    {
      pointer->cursor_surface_destroy_id =
        g_signal_connect_swapped (cursor_surface, "destroy",
                                  G_CALLBACK (ensure_update_cursor_surface),
                                  pointer);
    }

  meta_wayland_pointer_update_cursor_surface (pointer);
}

static void
pointer_set_cursor (struct wl_client *client,
                    struct wl_resource *resource,
                    uint32_t serial,
                    struct wl_resource *surface_resource,
                    int32_t hot_x, int32_t hot_y)
{
  MetaWaylandPointer *pointer;
  MetaWaylandSurface *surface;

  pointer = wl_resource_get_user_data (resource);
  if (!pointer)
    return;

  surface = (surface_resource ? wl_resource_get_user_data (surface_resource) : NULL);

  if (pointer->focus_surface == NULL)
    return;
  if (wl_resource_get_client (pointer->focus_surface->resource) != client)
    return;
  if (pointer->focus_serial - serial > G_MAXUINT32 / 2)
    return;

  if (surface &&
      !meta_wayland_surface_assign_role (surface,
                                         META_TYPE_WAYLAND_CURSOR_SURFACE,
                                         NULL))
    {
      wl_resource_post_error (resource, WL_POINTER_ERROR_ROLE,
                              "wl_surface@%d already has a different role",
                              wl_resource_get_id (surface_resource));
      return;
    }

  if (surface)
    {
      ClutterBackend *clutter_backend = clutter_get_default_backend ();
      ClutterSeat *clutter_seat =
        clutter_backend_get_default_seat (clutter_backend);
      ClutterInputDevice *device = clutter_seat_get_pointer (clutter_seat);
      MetaCursorRenderer *cursor_renderer =
        meta_backend_get_cursor_renderer_for_device (backend_from_pointer (pointer),
                                                     device);
      MetaWaylandCursorSurface *cursor_surface;
      MetaCursorSprite *cursor_sprite;

      cursor_surface = META_WAYLAND_CURSOR_SURFACE (surface->role);
      meta_wayland_cursor_surface_set_renderer (cursor_surface,
                                                cursor_renderer);
      meta_wayland_cursor_surface_set_hotspot (cursor_surface,
                                               hot_x, hot_y);

      cursor_sprite = meta_wayland_cursor_surface_get_sprite (cursor_surface);
      meta_cursor_sprite_invalidate (cursor_sprite);
    }

  meta_wayland_pointer_set_cursor_surface (pointer, surface);
}

static void
pointer_release (struct wl_client *client,
                 struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct wl_pointer_interface pointer_interface = {
  pointer_set_cursor,
  pointer_release,
};

void
meta_wayland_pointer_create_new_resource (MetaWaylandPointer *pointer,
                                          struct wl_client   *client,
                                          struct wl_resource *seat_resource,
                                          uint32_t id)
{
  struct wl_resource *resource;
  MetaWaylandPointerClient *pointer_client;

  resource = wl_resource_create (client, &wl_pointer_interface,
                                 wl_resource_get_version (seat_resource), id);
  wl_resource_set_implementation (resource, &pointer_interface, pointer,
                                  meta_wayland_pointer_unbind_pointer_client_resource);

  pointer_client = meta_wayland_pointer_ensure_pointer_client (pointer, client);

  wl_list_insert (&pointer_client->pointer_resources,
                  wl_resource_get_link (resource));

  if (pointer->focus_client == pointer_client)
    {
      meta_wayland_pointer_send_enter (pointer, resource,
                                       pointer->focus_serial,
                                       pointer->focus_surface);
      meta_wayland_pointer_send_frame (pointer, resource);
    }
}

static gboolean
pointer_can_grab_surface (MetaWaylandPointer *pointer,
                          MetaWaylandSurface *surface)
{
  MetaWaylandSurface *subsurface;

  if (pointer->focus_surface == surface)
    return TRUE;

  META_WAYLAND_SURFACE_FOREACH_SUBSURFACE (&surface->applied_state,
                                           subsurface)
    {
      if (pointer_can_grab_surface (pointer, subsurface))
        return TRUE;
    }

  return FALSE;
}

static gboolean
meta_wayland_pointer_can_grab_surface (MetaWaylandPointer *pointer,
                                       MetaWaylandSurface *surface,
                                       uint32_t            serial)
{
  return (pointer->grab_serial == serial &&
          pointer_can_grab_surface (pointer, surface));
}

gboolean
meta_wayland_pointer_get_grab_info (MetaWaylandPointer    *pointer,
                                    MetaWaylandSurface    *surface,
                                    uint32_t               serial,
                                    gboolean               require_pressed,
                                    ClutterInputDevice   **device_out,
                                    float                 *x,
                                    float                 *y)
{
  if ((!require_pressed || pointer->button_count > 0) &&
      meta_wayland_pointer_can_grab_surface (pointer, surface, serial))
    {
      if (device_out)
        *device_out = pointer->device;

      if (x)
        *x = pointer->grab_x;
      if (y)
        *y = pointer->grab_y;

      return TRUE;
    }

  return FALSE;
}

gboolean
meta_wayland_pointer_can_popup (MetaWaylandPointer *pointer, uint32_t serial)
{
  return pointer->grab_serial == serial;
}

static void
relative_pointer_destroy (struct wl_client *client,
                          struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct zwp_relative_pointer_v1_interface relative_pointer_interface = {
  relative_pointer_destroy
};

static void
relative_pointer_manager_destroy (struct wl_client *client,
                                  struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
relative_pointer_manager_get_relative_pointer (struct wl_client   *client,
                                               struct wl_resource *manager_resource,
                                               uint32_t            id,
                                               struct wl_resource *pointer_resource)
{
  MetaWaylandPointer *pointer = wl_resource_get_user_data (pointer_resource);
  struct wl_resource *resource;
  MetaWaylandPointerClient *pointer_client;

  resource = wl_resource_create (client, &zwp_relative_pointer_v1_interface,
                                 wl_resource_get_version (manager_resource),
                                 id);
  if (!resource)
    {
      wl_client_post_no_memory (client);
      return;
    }

  wl_resource_set_implementation (resource, &relative_pointer_interface,
                                  pointer,
                                  meta_wayland_pointer_unbind_pointer_client_resource);

  pointer_client = meta_wayland_pointer_ensure_pointer_client (pointer, client);

  wl_list_insert (&pointer_client->relative_pointer_resources,
                  wl_resource_get_link (resource));
}

static const struct zwp_relative_pointer_manager_v1_interface relative_pointer_manager = {
  relative_pointer_manager_destroy,
  relative_pointer_manager_get_relative_pointer,
};

static void
bind_relative_pointer_manager (struct wl_client *client,
                               void             *data,
                               uint32_t          version,
                               uint32_t          id)
{
  MetaWaylandCompositor *compositor = data;
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &zwp_relative_pointer_manager_v1_interface,
                                 1, id);

  if (version != 1)
    wl_resource_post_error (resource,
                            WL_DISPLAY_ERROR_INVALID_OBJECT,
                            "bound invalid version %u of "
                            "wp_relative_pointer_manager",
                            version);

  wl_resource_set_implementation (resource, &relative_pointer_manager,
                                  compositor,
                                  NULL);
}

void
meta_wayland_relative_pointer_init (MetaWaylandCompositor *compositor)
{
  /* Relative pointer events are currently only supported by the native backend
   * so lets just advertise the extension when the native backend is used.
   */
#ifdef HAVE_NATIVE_BACKEND
  MetaContext *context = meta_wayland_compositor_get_context (compositor);
  MetaBackend *backend = meta_context_get_backend (context);

  if (!META_IS_BACKEND_NATIVE (backend))
    return;
#else
  return;
#endif

  if (!wl_global_create (compositor->wayland_display,
                         &zwp_relative_pointer_manager_v1_interface, 1,
                         compositor, bind_relative_pointer_manager))
    g_error ("Could not create relative pointer manager global");
}

MetaWaylandSeat *
meta_wayland_pointer_get_seat (MetaWaylandPointer *pointer)
{
  MetaWaylandInputDevice *input_device = META_WAYLAND_INPUT_DEVICE (pointer);

  return meta_wayland_input_device_get_seat (input_device);
}

static void
meta_wayland_pointer_init (MetaWaylandPointer *pointer)
{
  pointer->pointer_clients =
    g_hash_table_new_full (NULL, NULL, NULL,
                           (GDestroyNotify) meta_wayland_pointer_client_free);
}

static void
meta_wayland_pointer_finalize (GObject *object)
{
  MetaWaylandPointer *pointer = META_WAYLAND_POINTER (object);

  g_clear_pointer (&pointer->pointer_clients, g_hash_table_unref);

  G_OBJECT_CLASS (meta_wayland_pointer_parent_class)->finalize (object);
}

static void
meta_wayland_pointer_class_init (MetaWaylandPointerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_wayland_pointer_finalize;

  signals[FOCUS_SURFACE_CHANGED] = g_signal_new ("focus-surface-changed",
                                                 G_TYPE_FROM_CLASS (klass),
                                                 G_SIGNAL_RUN_LAST,
                                                 0,
                                                 NULL, NULL, NULL,
                                                 G_TYPE_NONE, 0);
}

MetaWaylandSurface *
meta_wayland_pointer_get_current_surface (MetaWaylandPointer *pointer)
{
  return pointer->current;
}

MetaWaylandSurface *
meta_wayland_pointer_get_focus_surface (MetaWaylandPointer *pointer)
{
  return pointer->focus_surface;
}

MetaWaylandSurface *
meta_wayland_pointer_get_implicit_grab_surface (MetaWaylandPointer *pointer)
{
  if (pointer->button_count > 0)
    return pointer->focus_surface;

  return NULL;
}

MetaWaylandPointerClient *
meta_wayland_pointer_get_focus_client (MetaWaylandPointer *pointer)
{
  return pointer->focus_client;
}
