/*
 * Interface for Wayland events
 *
 * Copyright (C) 2023 Red Hat Inc.
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

#include "wayland/meta-wayland-input.h"

#include "wayland/meta-wayland-seat.h"
#include "wayland/meta-wayland-tablet-seat.h"
#include "wayland/meta-wayland.h"

struct _MetaWaylandEventHandler
{
  const MetaWaylandEventInterface *iface;
  MetaWaylandInput *input;
  gpointer user_data;
  gboolean grabbing;
  struct wl_list link;
};

struct _MetaWaylandInput
{
  GObject parent_instance;

  MetaWaylandSeat *seat;
  struct wl_list event_handler_list;
  ClutterStage *stage;
  ClutterGrab *grab;
};

static void meta_wayland_input_sync_focus (MetaWaylandInput *input);

G_DEFINE_FINAL_TYPE (MetaWaylandInput, meta_wayland_input, G_TYPE_OBJECT)

static void
on_stage_is_grabbed_change (MetaWaylandInput *input)
{
  meta_wayland_input_sync_focus (input);
}

static void
meta_wayland_input_init (MetaWaylandInput *input)
{
  wl_list_init (&input->event_handler_list);
}

static void
meta_wayland_input_finalize (GObject *object)
{
  MetaWaylandInput *input = META_WAYLAND_INPUT (object);
  MetaWaylandEventHandler *handler, *next;

  g_signal_handlers_disconnect_by_func (input->stage,
                                        on_stage_is_grabbed_change,
                                        input);

  wl_list_for_each_safe (handler, next, &input->event_handler_list, link)
    meta_wayland_input_detach_event_handler (input, handler);

  G_OBJECT_CLASS (meta_wayland_input_parent_class)->finalize (object);
}

static void
meta_wayland_input_class_init (MetaWaylandInputClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_wayland_input_finalize;
}

MetaWaylandInput *
meta_wayland_input_new (MetaWaylandSeat *seat)
{
  MetaWaylandInput *input;
  MetaWaylandCompositor *compositor = seat->compositor;
  MetaContext *context =
    meta_wayland_compositor_get_context (compositor);
  MetaBackend *backend = meta_context_get_backend (context);

  input = g_object_new (META_TYPE_WAYLAND_INPUT, NULL);
  input->seat = seat;
  input->stage = CLUTTER_STAGE (meta_backend_get_stage (backend));

  g_signal_connect_swapped (input->stage, "notify::is-grabbed",
                            G_CALLBACK (on_stage_is_grabbed_change),
                            input);

  return input;
}

static void
meta_wayland_event_handler_invalidate_focus (MetaWaylandEventHandler *handler,
                                             ClutterInputDevice      *device,
                                             ClutterEventSequence    *sequence)
{
  MetaWaylandInput *input = handler->input;
  MetaWaylandSurface *surface = NULL;

  if (!handler->iface->focus)
    return;

  if (handler->iface->get_focus_surface &&
      /* Only the first handler can focus other than a NULL surface */
      meta_wayland_input_is_current_handler (input, handler) &&
      /* Stage should either be ungrabbed, or grabbed to self */
      (!clutter_stage_get_grab_actor (input->stage) ||
       (input->grab && !clutter_grab_is_revoked (input->grab))))
    {
      surface = handler->iface->get_focus_surface (handler,
                                                   device, sequence,
                                                   handler->user_data);
    }

  handler->iface->focus (handler,
                         device, sequence, surface,
                         handler->user_data);
}

static void
meta_wayland_input_invalidate_all_focus (MetaWaylandInput *input)
{
  MetaWaylandEventHandler *handler;
  MetaWaylandSeat *seat = input->seat;
  ClutterSeat *clutter_seat =
    clutter_backend_get_default_seat (clutter_get_default_backend ());
  ClutterInputDevice *device;
  GHashTableIter iter;

  /* Trigger sync of all known devices */
  if (meta_wayland_seat_has_pointer (seat))
    {
      device = clutter_seat_get_pointer (clutter_seat);
      handler = wl_container_of (input->event_handler_list.next, handler, link);
      meta_wayland_event_handler_invalidate_focus (handler, device, NULL);
    }

  if (meta_wayland_seat_has_keyboard (seat))
    {
      device = clutter_seat_get_keyboard (clutter_seat);
      handler = wl_container_of (input->event_handler_list.next, handler, link);
      meta_wayland_event_handler_invalidate_focus (handler, device, NULL);
    }

  if (meta_wayland_seat_has_touch (seat))
    meta_wayland_touch_cancel (seat->touch);

  g_hash_table_iter_init (&iter, seat->tablet_seat->tablets);
  while (g_hash_table_iter_next (&iter, (gpointer*) &device, NULL))
    {
      handler = wl_container_of (input->event_handler_list.next, handler, link);
      meta_wayland_event_handler_invalidate_focus (handler, device, NULL);
    }

  g_hash_table_iter_init (&iter, seat->tablet_seat->pads);
  while (g_hash_table_iter_next (&iter, (gpointer*) &device, NULL))
    {
      handler = wl_container_of (input->event_handler_list.next, handler, link);
      meta_wayland_event_handler_invalidate_focus (handler, device, NULL);
    }
}

static gboolean
meta_wayland_event_handler_handle_event (MetaWaylandEventHandler *handler,
                                         const ClutterEvent      *event)
{
  ClutterEventType event_type = clutter_event_type (event);

  switch (event_type)
    {
    case CLUTTER_ENTER:
    case CLUTTER_LEAVE:
      {
        ClutterInputDevice *device;
        ClutterEventSequence *sequence;

        device = clutter_event_get_device (event);
        sequence = clutter_event_get_event_sequence (event);
        meta_wayland_event_handler_invalidate_focus (handler,
                                                     device, sequence);
      }

      return CLUTTER_EVENT_PROPAGATE;
    case CLUTTER_TOUCH_BEGIN:
    case CLUTTER_BUTTON_PRESS:
      if (!handler->iface->press)
        return CLUTTER_EVENT_PROPAGATE;
      return handler->iface->press (handler, event, handler->user_data);

    case CLUTTER_TOUCH_UPDATE:
    case CLUTTER_MOTION:
      if (!handler->iface->motion)
        return CLUTTER_EVENT_PROPAGATE;
      return handler->iface->motion (handler, event, handler->user_data);

    case CLUTTER_TOUCH_END:
    case CLUTTER_BUTTON_RELEASE:
      if (!handler->iface->release)
        return CLUTTER_EVENT_PROPAGATE;
      return handler->iface->release (handler, event, handler->user_data);

    case CLUTTER_KEY_PRESS:
    case CLUTTER_KEY_RELEASE:
      if (!handler->iface->key)
        return CLUTTER_EVENT_PROPAGATE;
      return handler->iface->key (handler, event, handler->user_data);

    case CLUTTER_TOUCH_CANCEL:
    case CLUTTER_SCROLL:
    case CLUTTER_PROXIMITY_IN:
    case CLUTTER_PROXIMITY_OUT:
    case CLUTTER_TOUCHPAD_PINCH:
    case CLUTTER_TOUCHPAD_SWIPE:
    case CLUTTER_TOUCHPAD_HOLD:
    case CLUTTER_PAD_BUTTON_PRESS:
    case CLUTTER_PAD_BUTTON_RELEASE:
    case CLUTTER_PAD_STRIP:
    case CLUTTER_PAD_RING:
    case CLUTTER_IM_COMMIT:
    case CLUTTER_IM_DELETE:
    case CLUTTER_IM_PREEDIT:
    case CLUTTER_DEVICE_ADDED:
    case CLUTTER_DEVICE_REMOVED:
    case CLUTTER_NOTHING:
    case CLUTTER_EVENT_LAST:
      if (!handler->iface->other)
        return CLUTTER_EVENT_PROPAGATE;
      return handler->iface->other (handler, event, handler->user_data);
    }

  g_assert_not_reached ();
}

static void
meta_wayland_input_sync_focus (MetaWaylandInput *input)
{
  MetaWaylandEventHandler *handler;

  g_assert (!wl_list_empty (&input->event_handler_list));
  handler = wl_container_of (input->event_handler_list.next, handler, link);
  meta_wayland_input_invalidate_all_focus (input);
}

static void
on_grab_revocation_change (MetaWaylandInput *input)
{
  meta_wayland_input_sync_focus (input);
}

static gboolean
grab_handle_event (const ClutterEvent *event,
                   gpointer            user_data)
{
  MetaWaylandInput *input = user_data;

  return meta_wayland_input_handle_event (input, event);
}

MetaWaylandEventHandler *
meta_wayland_input_attach_event_handler (MetaWaylandInput                *input,
                                         const MetaWaylandEventInterface *iface,
                                         gboolean                         grab,
                                         gpointer                         user_data)
{
  MetaWaylandEventHandler *handler;

  handler = g_new0 (MetaWaylandEventHandler, 1);
  handler->iface = iface;
  handler->input = input;
  handler->grabbing = grab;
  handler->user_data = user_data;
  wl_list_init (&handler->link);
  wl_list_insert (&input->event_handler_list, &handler->link);

  if (grab && !input->grab)
    {
      MetaWaylandCompositor *compositor = input->seat->compositor;
      MetaContext *context =
        meta_wayland_compositor_get_context (compositor);
      MetaBackend *backend = meta_context_get_backend (context);
      ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));

      input->grab = clutter_stage_grab_input_only (stage,
                                                   grab_handle_event,
                                                   input,
                                                   NULL);
      g_signal_connect_swapped (input->grab, "notify::revoked",
                                G_CALLBACK (on_grab_revocation_change),
                                input);
    }

  meta_wayland_input_invalidate_all_focus (input);

  return handler;
}

static gboolean
should_be_grabbed (MetaWaylandInput *input)
{
  MetaWaylandEventHandler *handler;
  gboolean grabbing = FALSE;

  wl_list_for_each (handler, &input->event_handler_list, link)
    grabbing |= handler->grabbing;

  return grabbing;
}

void
meta_wayland_input_detach_event_handler (MetaWaylandInput        *input,
                                         MetaWaylandEventHandler *handler)
{
  gboolean handler_change = FALSE;

  handler_change = meta_wayland_input_is_current_handler (input, handler);
  wl_list_remove (&handler->link);

  if (handler_change && !wl_list_empty (&input->event_handler_list))
    {
      MetaWaylandEventHandler *head =
        wl_container_of (input->event_handler_list.next,
                         head, link);

      meta_wayland_input_invalidate_all_focus (input);
    }

  if (input->grab && !should_be_grabbed (input))
    {
      g_signal_handlers_disconnect_by_func (input->grab,
                                            on_grab_revocation_change,
                                            input);

      clutter_grab_dismiss (input->grab);
      g_clear_object (&input->grab);
    }

  g_free (handler);
}

gboolean
meta_wayland_input_is_current_handler (MetaWaylandInput        *input,
                                       MetaWaylandEventHandler *handler)
{
  return input->event_handler_list.next == &handler->link;
}

gboolean
meta_wayland_input_handle_event (MetaWaylandInput   *input,
                                 const ClutterEvent *event)
{
  MetaWaylandEventHandler *handler, *next;
  gboolean retval = CLUTTER_EVENT_PROPAGATE;
  ClutterEventType event_type = clutter_event_type (event);

  wl_list_for_each_safe (handler, next, &input->event_handler_list, link)
    {
      retval = meta_wayland_event_handler_handle_event (handler, event);
      if (retval == CLUTTER_EVENT_STOP)
        break;
      /* Event handlers propagate focus themselves */
      if (event_type == CLUTTER_ENTER ||
          event_type == CLUTTER_LEAVE)
        break;
    }

  return retval;
}

void
meta_wayland_input_invalidate_focus (MetaWaylandInput     *input,
                                     ClutterInputDevice   *device,
                                     ClutterEventSequence *sequence)
{
  if (!wl_list_empty (&input->event_handler_list))
    {
      MetaWaylandEventHandler *head =
        wl_container_of (input->event_handler_list.next,
                         head, link);

      meta_wayland_event_handler_invalidate_focus (head, device, sequence);
    }
}

MetaWaylandSurface *
meta_wayland_event_handler_chain_up_get_focus_surface (MetaWaylandEventHandler *handler,
                                                       ClutterInputDevice      *device,
                                                       ClutterEventSequence    *sequence)
{
  MetaWaylandEventHandler *next;

  g_assert (!wl_list_empty (&handler->link));

  next = wl_container_of (handler->link.next, next, link);

  return next->iface->get_focus_surface (next, device, sequence, next->user_data);
}

void
meta_wayland_event_handler_chain_up_focus (MetaWaylandEventHandler *handler,
                                           ClutterInputDevice      *device,
                                           ClutterEventSequence    *sequence,
                                           MetaWaylandSurface      *surface)
{
  MetaWaylandEventHandler *next;

  g_assert (!wl_list_empty (&handler->link));

  next = wl_container_of (handler->link.next, next, link);

  return next->iface->focus (next, device, sequence, surface, next->user_data);
}
