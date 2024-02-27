/*
 * Wayland Support
 *
 * Copyright (C) 2013 Intel Corporation
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

#include "wayland/meta-wayland-seat.h"

#include "wayland/meta-wayland-data-device.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-tablet-seat.h"
#include "wayland/meta-wayland-versions.h"

static gboolean meta_wayland_seat_handle_event_internal (MetaWaylandSeat    *seat,
                                                         const ClutterEvent *event);

#define CAPABILITY_ENABLED(prev, cur, capability) ((cur & (capability)) && !(prev & (capability)))
#define CAPABILITY_DISABLED(prev, cur, capability) ((prev & (capability)) && !(cur & (capability)))

static void
unbind_resource (struct wl_resource *resource)
{
  wl_list_remove (wl_resource_get_link (resource));
}

static void
seat_get_pointer (struct wl_client *client,
                  struct wl_resource *resource,
                  uint32_t id)
{
  MetaWaylandSeat *seat = wl_resource_get_user_data (resource);
  MetaWaylandPointer *pointer = seat->pointer;

  meta_wayland_pointer_create_new_resource (pointer, client, resource, id);
}

static void
seat_get_keyboard (struct wl_client *client,
                   struct wl_resource *resource,
                   uint32_t id)
{
  MetaWaylandSeat *seat = wl_resource_get_user_data (resource);
  MetaWaylandKeyboard *keyboard = seat->keyboard;

  meta_wayland_keyboard_create_new_resource (keyboard, client, resource, id);
}

static void
seat_get_touch (struct wl_client *client,
                struct wl_resource *resource,
                uint32_t id)
{
  MetaWaylandSeat *seat = wl_resource_get_user_data (resource);
  MetaWaylandTouch *touch = seat->touch;

  meta_wayland_touch_create_new_resource (touch, client, resource, id);
}

static void
seat_release (struct wl_client   *client,
              struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct wl_seat_interface seat_interface = {
  seat_get_pointer,
  seat_get_keyboard,
  seat_get_touch,
  seat_release
};

static void
bind_seat (struct wl_client *client,
           void *data,
           guint32 version,
           guint32 id)
{
  MetaWaylandSeat *seat = data;
  struct wl_resource *resource;

  resource = wl_resource_create (client, &wl_seat_interface, version, id);
  wl_resource_set_implementation (resource, &seat_interface, seat, unbind_resource);
  wl_list_insert (&seat->base_resource_list, wl_resource_get_link (resource));

  wl_seat_send_capabilities (resource, seat->capabilities);

  if (version >= WL_SEAT_NAME_SINCE_VERSION)
    wl_seat_send_name (resource, "seat0");
}

static uint32_t
lookup_device_capabilities (ClutterSeat *seat)
{
  GList *devices, *l;
  uint32_t capabilities = 0;

  devices = clutter_seat_list_devices (seat);

  for (l = devices; l; l = l->next)
    {
      ClutterInputCapabilities device_capabilities;

      /* Only look for physical devices, logical devices have rather generic
       * keyboard/pointer device types, which is not truly representative of
       * the physical devices connected to them.
       */
      if (clutter_input_device_get_device_mode (l->data) == CLUTTER_INPUT_MODE_LOGICAL)
        continue;

      device_capabilities = clutter_input_device_get_capabilities (l->data);

      if (device_capabilities & CLUTTER_INPUT_CAPABILITY_POINTER)
        capabilities |= WL_SEAT_CAPABILITY_POINTER;
      if (device_capabilities & CLUTTER_INPUT_CAPABILITY_KEYBOARD)
        capabilities |= WL_SEAT_CAPABILITY_KEYBOARD;
      if (device_capabilities & CLUTTER_INPUT_CAPABILITY_TOUCH)
        capabilities |= WL_SEAT_CAPABILITY_TOUCH;
    }

  g_list_free (devices);

  return capabilities;
}

static void
meta_wayland_seat_set_capabilities (MetaWaylandSeat *seat,
                                    uint32_t         flags)
{
  struct wl_resource *resource;
  uint32_t prev_flags;

  prev_flags = seat->capabilities;

  if (prev_flags == flags)
    return;

  seat->capabilities = flags;

  if (CAPABILITY_ENABLED (prev_flags, flags, WL_SEAT_CAPABILITY_POINTER))
    meta_wayland_pointer_enable (seat->pointer);
  else if (CAPABILITY_DISABLED (prev_flags, flags, WL_SEAT_CAPABILITY_POINTER))
    meta_wayland_pointer_disable (seat->pointer);

  if (CAPABILITY_ENABLED (prev_flags, flags, WL_SEAT_CAPABILITY_KEYBOARD))
    meta_wayland_keyboard_enable (seat->keyboard);
  else if (CAPABILITY_DISABLED (prev_flags, flags, WL_SEAT_CAPABILITY_KEYBOARD))
    meta_wayland_keyboard_disable (seat->keyboard);

  if (CAPABILITY_ENABLED (prev_flags, flags, WL_SEAT_CAPABILITY_TOUCH))
    meta_wayland_touch_enable (seat->touch);
  else if (CAPABILITY_DISABLED (prev_flags, flags, WL_SEAT_CAPABILITY_TOUCH))
    meta_wayland_touch_disable (seat->touch);

  /* Broadcast capability changes */
  wl_resource_for_each (resource, &seat->base_resource_list)
    {
      wl_seat_send_capabilities (resource, flags);
    }
}

static void
meta_wayland_seat_update_capabilities (MetaWaylandSeat *seat,
				       ClutterSeat     *clutter_seat)
{
  uint32_t capabilities;

  capabilities = lookup_device_capabilities (clutter_seat);
  meta_wayland_seat_set_capabilities (seat, capabilities);
}

static void
meta_wayland_seat_devices_updated (ClutterSeat        *clutter_seat,
                                   ClutterInputDevice *input_device,
                                   MetaWaylandSeat    *seat)
{
  meta_wayland_seat_update_capabilities (seat, clutter_seat);
}

static MetaWaylandSurface *
default_get_focus_surface (MetaWaylandEventHandler *handler,
                           ClutterInputDevice      *device,
                           ClutterEventSequence    *sequence,
                           gpointer                 user_data)
{
  MetaWaylandSeat *seat = user_data;

  return meta_wayland_seat_get_current_surface (seat,
                                                device,
                                                sequence);
}

static void
default_focus (MetaWaylandEventHandler *handler,
               ClutterInputDevice      *device,
               ClutterEventSequence    *sequence,
               MetaWaylandSurface      *surface,
               gpointer                 user_data)
{
  MetaWaylandSeat *seat = user_data;
  ClutterInputCapabilities caps;

  caps = clutter_input_device_get_capabilities (device);

  if (caps &
      (CLUTTER_INPUT_CAPABILITY_KEYBOARD |
       CLUTTER_INPUT_CAPABILITY_TABLET_PAD))
    {
      if (meta_wayland_seat_has_keyboard (seat))
        meta_wayland_keyboard_set_focus (seat->keyboard, surface);

      meta_wayland_data_device_set_keyboard_focus (&seat->data_device);
      meta_wayland_data_device_primary_set_keyboard_focus (&seat->primary_data_device);
      meta_wayland_tablet_seat_set_pad_focus (seat->tablet_seat, surface);
      meta_wayland_text_input_set_focus (seat->text_input, surface);
    }

  if (caps & CLUTTER_INPUT_CAPABILITY_TABLET_TOOL)
    {
      meta_wayland_tablet_seat_focus_surface (seat->tablet_seat,
                                              device,
                                              surface);
    }

  if (caps &
      (CLUTTER_INPUT_CAPABILITY_POINTER |
       CLUTTER_INPUT_CAPABILITY_TOUCHPAD |
       CLUTTER_INPUT_CAPABILITY_TRACKBALL |
       CLUTTER_INPUT_CAPABILITY_TRACKPOINT))
    meta_wayland_pointer_focus_surface (seat->pointer, surface);
}

static gboolean
default_handle_event (MetaWaylandEventHandler *handler,
                      const ClutterEvent      *event,
                      gpointer                 user_data)
{
  MetaWaylandSeat *seat = user_data;

  return meta_wayland_seat_handle_event_internal (seat, event);
}

static const MetaWaylandEventInterface default_event_interface = {
  .get_focus_surface = default_get_focus_surface,
  .focus = default_focus,
  .motion = default_handle_event,
  .press = default_handle_event,
  .release = default_handle_event,
  .key = default_handle_event,
  .other = default_handle_event,
};

static MetaWaylandSeat *
meta_wayland_seat_new (MetaWaylandCompositor *compositor,
                       struct wl_display     *display)
{
  MetaWaylandSeat *seat;
  ClutterSeat *clutter_seat;

  seat = g_new0 (MetaWaylandSeat, 1);
  seat->compositor = compositor;

  wl_list_init (&seat->base_resource_list);
  seat->wl_display = display;

  seat->pointer = g_object_new (META_TYPE_WAYLAND_POINTER,
                                "seat", seat,
                                NULL);
  seat->keyboard = g_object_new (META_TYPE_WAYLAND_KEYBOARD,
                                 "seat", seat,
                                 NULL);
  seat->touch = g_object_new (META_TYPE_WAYLAND_TOUCH,
                              "seat", seat,
                              NULL);

  seat->text_input = meta_wayland_text_input_new (seat);

  meta_wayland_data_device_init (&seat->data_device, seat);
  meta_wayland_data_device_primary_init (&seat->primary_data_device, seat);

  clutter_seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  meta_wayland_seat_update_capabilities (seat, clutter_seat);
  g_signal_connect (clutter_seat, "device-added",
                    G_CALLBACK (meta_wayland_seat_devices_updated), seat);
  g_signal_connect (clutter_seat, "device-removed",
                    G_CALLBACK (meta_wayland_seat_devices_updated), seat);

  wl_global_create (display, &wl_seat_interface, META_WL_SEAT_VERSION, seat, bind_seat);

  seat->tablet_seat =
    meta_wayland_tablet_manager_ensure_seat (compositor->tablet_manager, seat);

  seat->input_handler = meta_wayland_input_new (seat);
  seat->default_handler =
    meta_wayland_input_attach_event_handler (seat->input_handler,
                                             &default_event_interface,
                                             FALSE, seat);

  return seat;
}

void
meta_wayland_seat_init (MetaWaylandCompositor *compositor)
{
  compositor->seat = meta_wayland_seat_new (compositor,
                                            compositor->wayland_display);
}

void
meta_wayland_seat_free (MetaWaylandSeat *seat)
{
  ClutterSeat *clutter_seat;

  g_clear_object (&seat->input_handler);

  clutter_seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  g_signal_handlers_disconnect_by_data (clutter_seat, seat);
  meta_wayland_seat_set_capabilities (seat, 0);

  g_object_unref (seat->pointer);
  g_object_unref (seat->keyboard);
  g_object_unref (seat->touch);

  meta_wayland_text_input_destroy (seat->text_input);

  g_free (seat);
}

static gboolean
event_is_synthesized_crossing (const ClutterEvent *event)
{
  ClutterInputDevice *device;
  ClutterEventType event_type;

  event_type = clutter_event_type (event);

  if (event_type != CLUTTER_ENTER && event_type != CLUTTER_LEAVE)
    return FALSE;

  device = clutter_event_get_source_device (event);
  return clutter_input_device_get_device_mode (device) == CLUTTER_INPUT_MODE_LOGICAL;
}

static gboolean
event_from_supported_hardware_device (MetaWaylandSeat    *seat,
                                      const ClutterEvent *event)
{
  ClutterInputDevice *input_device;
  ClutterInputMode input_mode;
  ClutterInputCapabilities capabilities;
  gboolean hardware_device = FALSE;
  gboolean supported_device = FALSE;

  input_device = clutter_event_get_source_device (event);

  if (input_device == NULL)
    goto out;

  input_mode = clutter_input_device_get_device_mode (input_device);

  if (input_mode != CLUTTER_INPUT_MODE_PHYSICAL)
    goto out;

  hardware_device = TRUE;

  capabilities = clutter_input_device_get_capabilities (input_device);

  if ((capabilities &
       (CLUTTER_INPUT_CAPABILITY_POINTER |
        CLUTTER_INPUT_CAPABILITY_KEYBOARD |
        CLUTTER_INPUT_CAPABILITY_TOUCH)) != 0)
    supported_device = TRUE;

out:
  return hardware_device && supported_device;
}

static gboolean
is_tablet_event (MetaWaylandSeat    *seat,
                 const ClutterEvent *event)
{
  ClutterInputDevice *device;
  ClutterInputCapabilities capabilities;

  device = clutter_event_get_source_device (event);
  capabilities = clutter_input_device_get_capabilities (device);

  if (capabilities & CLUTTER_INPUT_CAPABILITY_TABLET_TOOL)
    {
      return meta_wayland_tablet_seat_lookup_tablet (seat->tablet_seat,
                                                     device) != NULL;
    }
  if (capabilities & CLUTTER_INPUT_CAPABILITY_TABLET_PAD)
    {
      return meta_wayland_tablet_seat_lookup_pad (seat->tablet_seat,
                                                  device) != NULL;
    }

  return FALSE;
}

void
meta_wayland_seat_update (MetaWaylandSeat    *seat,
                          const ClutterEvent *event)
{
  if (is_tablet_event (seat, event))
    {
      meta_wayland_tablet_seat_update (seat->tablet_seat, event);
      return;
    }

  if (!(clutter_event_get_flags (event) & CLUTTER_EVENT_FLAG_INPUT_METHOD) &&
      !event_from_supported_hardware_device (seat, event) &&
      !event_is_synthesized_crossing (event))
    return;

  switch (clutter_event_type (event))
    {
    case CLUTTER_MOTION:
    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_BUTTON_RELEASE:
    case CLUTTER_SCROLL:
    case CLUTTER_ENTER:
    case CLUTTER_LEAVE:
      if (meta_wayland_seat_has_pointer (seat))
        meta_wayland_pointer_update (seat->pointer, event);
      break;

    case CLUTTER_KEY_PRESS:
    case CLUTTER_KEY_RELEASE:
      if (meta_wayland_seat_has_keyboard (seat))
        meta_wayland_keyboard_update (seat->keyboard, (const ClutterKeyEvent *) event);
      break;

    case CLUTTER_TOUCH_BEGIN:
    case CLUTTER_TOUCH_UPDATE:
    case CLUTTER_TOUCH_END:
      if (meta_wayland_seat_has_touch (seat))
        meta_wayland_touch_update (seat->touch, event);
      break;

    default:
      break;
    }
}

static gboolean
meta_wayland_seat_handle_event_internal (MetaWaylandSeat    *seat,
                                         const ClutterEvent *event)
{
  ClutterEventType event_type;

  if (is_tablet_event (seat, event))
    return meta_wayland_tablet_seat_handle_event (seat->tablet_seat, event);

  if (!(clutter_event_get_flags (event) & CLUTTER_EVENT_FLAG_INPUT_METHOD) &&
      !event_from_supported_hardware_device (seat, event))
    return FALSE;

  event_type = clutter_event_type (event);

  if (event_type == CLUTTER_BUTTON_PRESS ||
      event_type == CLUTTER_TOUCH_BEGIN)
    {
      meta_wayland_text_input_handle_event (seat->text_input, event);
    }

  switch (event_type)
    {
    case CLUTTER_MOTION:
    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_BUTTON_RELEASE:
    case CLUTTER_SCROLL:
    case CLUTTER_TOUCHPAD_SWIPE:
    case CLUTTER_TOUCHPAD_PINCH:
    case CLUTTER_TOUCHPAD_HOLD:
      if (meta_wayland_seat_has_pointer (seat))
        return meta_wayland_pointer_handle_event (seat->pointer, event);

      break;
    case CLUTTER_KEY_PRESS:
    case CLUTTER_KEY_RELEASE:
      if (meta_wayland_seat_has_keyboard (seat))
        return meta_wayland_keyboard_handle_event (seat->keyboard,
                                                   (const ClutterKeyEvent *) event);
      break;
    case CLUTTER_TOUCH_BEGIN:
    case CLUTTER_TOUCH_UPDATE:
    case CLUTTER_TOUCH_END:
      if (meta_wayland_seat_has_touch (seat))
        return meta_wayland_touch_handle_event (seat->touch, event);

      break;
    case CLUTTER_IM_COMMIT:
    case CLUTTER_IM_DELETE:
    case CLUTTER_IM_PREEDIT:
      if (meta_wayland_text_input_handle_event (seat->text_input, event))
        return TRUE;

      break;
    default:
      break;
    }

  return FALSE;
}

static void
input_focus_destroyed (MetaWaylandSurface *surface,
                       MetaWaylandSeat    *seat)
{
  meta_wayland_seat_set_input_focus (seat, NULL);
}

void
meta_wayland_seat_set_input_focus (MetaWaylandSeat    *seat,
                                   MetaWaylandSurface *surface)
{
  if (seat->input_focus == surface)
    return;

  if (seat->input_focus)
    {
      g_clear_signal_handler (&seat->input_focus_destroy_id,
                              seat->input_focus);
      seat->input_focus = NULL;
    }

  seat->input_focus = surface;

  if (surface)
    {
      seat->input_focus_destroy_id =
        g_signal_connect (surface, "destroy",
                          G_CALLBACK (input_focus_destroyed),
                          seat);
    }

  if (meta_wayland_seat_has_keyboard (seat))
    {
      meta_wayland_keyboard_set_focus (seat->keyboard, surface);
      meta_wayland_data_device_set_keyboard_focus (&seat->data_device);
      meta_wayland_data_device_primary_set_keyboard_focus (&seat->primary_data_device);
    }

  meta_wayland_tablet_seat_set_pad_focus (seat->tablet_seat, surface);

  meta_wayland_text_input_set_focus (seat->text_input, surface);
}

MetaWaylandSurface *
meta_wayland_seat_get_input_focus (MetaWaylandSeat *seat)
{
  return seat->input_focus;
}

gboolean
meta_wayland_seat_get_grab_info (MetaWaylandSeat       *seat,
                                 MetaWaylandSurface    *surface,
                                 uint32_t               serial,
                                 gboolean               require_pressed,
                                 ClutterInputDevice   **device_out,
                                 ClutterEventSequence **sequence_out,
                                 float                 *x,
                                 float                 *y)
{
  if (meta_wayland_seat_has_touch (seat))
    {
      ClutterEventSequence *sequence;

      sequence = meta_wayland_touch_find_grab_sequence (seat->touch,
                                                        surface,
                                                        serial);
      if (sequence)
        {
          ClutterSeat *clutter_seat =
            clutter_backend_get_default_seat (clutter_get_default_backend ());

          if (device_out)
            *device_out = clutter_seat_get_pointer (clutter_seat);
          if (sequence_out)
            *sequence_out = sequence;

          meta_wayland_touch_get_press_coords (seat->touch, sequence, x, y);
          return TRUE;
        }
    }

  if (meta_wayland_seat_has_pointer (seat) &&
      meta_wayland_pointer_get_grab_info (seat->pointer,
                                          surface,
                                          serial,
                                          require_pressed,
                                          device_out,
                                          x, y))
    {
      if (sequence_out)
        *sequence_out = NULL;
      return TRUE;
    }

  if (meta_wayland_tablet_seat_get_grab_info (seat->tablet_seat,
                                              surface,
                                              serial,
                                              require_pressed,
                                              device_out,
                                              x, y))
    {
      if (sequence_out)
        *sequence_out = NULL;
      return TRUE;
    }

  return FALSE;
}

gboolean
meta_wayland_seat_can_popup (MetaWaylandSeat *seat,
                             uint32_t         serial)
{
  return (meta_wayland_pointer_can_popup (seat->pointer, serial) ||
          meta_wayland_keyboard_can_popup (seat->keyboard, serial) ||
          meta_wayland_touch_can_popup (seat->touch, serial) ||
          meta_wayland_tablet_seat_can_popup (seat->tablet_seat, serial));
}

gboolean
meta_wayland_seat_has_keyboard (MetaWaylandSeat *seat)
{
  return (seat->capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0;
}

gboolean
meta_wayland_seat_has_pointer (MetaWaylandSeat *seat)
{
  return (seat->capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;
}

gboolean
meta_wayland_seat_has_touch (MetaWaylandSeat *seat)
{
  return (seat->capabilities & WL_SEAT_CAPABILITY_TOUCH) != 0;
}

MetaWaylandCompositor *
meta_wayland_seat_get_compositor (MetaWaylandSeat *seat)
{
  return seat->compositor;
}

gboolean
meta_wayland_seat_handle_event (MetaWaylandSeat *seat,
                                const ClutterEvent *event)
{
  return meta_wayland_input_handle_event (seat->input_handler, event);
}

MetaWaylandInput *
meta_wayland_seat_get_input (MetaWaylandSeat *seat)
{
  return seat->input_handler;
}

MetaWaylandSurface *
meta_wayland_seat_get_current_surface (MetaWaylandSeat      *seat,
                                       ClutterInputDevice   *device,
                                       ClutterEventSequence *sequence)
{
  if (sequence)
    {
      return meta_wayland_touch_get_surface (seat->touch, sequence);
    }
  else
    {
      ClutterInputCapabilities caps;

      caps = clutter_input_device_get_capabilities (device);

      if (caps &
          (CLUTTER_INPUT_CAPABILITY_KEYBOARD |
           CLUTTER_INPUT_CAPABILITY_TABLET_PAD))
        return seat->input_focus;

      if (caps & CLUTTER_INPUT_CAPABILITY_TABLET_TOOL)
        {
          return meta_wayland_tablet_seat_get_current_surface (seat->tablet_seat,
                                                               device);
        }

      if (caps &
          (CLUTTER_INPUT_CAPABILITY_POINTER |
           CLUTTER_INPUT_CAPABILITY_TOUCHPAD |
           CLUTTER_INPUT_CAPABILITY_TRACKBALL |
           CLUTTER_INPUT_CAPABILITY_TRACKPOINT))
        {
          MetaWaylandSurface *implicit_grab_surface;

          implicit_grab_surface =
            meta_wayland_pointer_get_implicit_grab_surface (seat->pointer);

          if (implicit_grab_surface &&
              meta_wayland_input_is_current_handler (seat->input_handler,
                                                     seat->default_handler))
            return implicit_grab_surface;
          else
            return meta_wayland_pointer_get_current_surface (seat->pointer);
        }
    }

  return NULL;
}
