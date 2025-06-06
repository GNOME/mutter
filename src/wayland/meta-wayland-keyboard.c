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

/*
 * Copyright © 2010-2011 Intel Corporation
 * Copyright © 2008-2011 Kristian Høgsberg
 * Copyright © 2012 Collabora, Ltd.
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* The file is based on src/input.c from Weston */

#include "config.h"

#include <errno.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "backends/meta-backend-private.h"
#include "core/display-private.h"
#include "core/meta-anonymous-file.h"
#include "wayland/meta-wayland-private.h"

typedef struct
{
  struct xkb_keymap *keymap;
  struct xkb_state *state;
  MetaAnonymousFile *keymap_rofile;
} MetaWaylandXkbInfo;

struct _MetaWaylandKeyboard
{
  MetaWaylandInputDevice parent;

  struct wl_list resource_list;
  struct wl_list focus_resource_list;

  MetaWaylandSurface *focus_surface;
  struct wl_listener focus_surface_listener;
  uint32_t focus_serial;

  struct wl_array pressed_keys;
  GHashTable *key_down_serials;
  uint32_t last_key_up_serial;
  uint32_t last_key_up;

  MetaWaylandXkbInfo xkb_info;
  enum xkb_state_component mods_changed;
  xkb_mod_mask_t kbd_a11y_latched_mods;
  xkb_mod_mask_t kbd_a11y_locked_mods;

  GSettings *settings;
};

G_DEFINE_TYPE (MetaWaylandKeyboard, meta_wayland_keyboard,
               META_TYPE_WAYLAND_INPUT_DEVICE)

static void meta_wayland_keyboard_update_xkb_state (MetaWaylandKeyboard *keyboard);
static void notify_modifiers (MetaWaylandKeyboard *keyboard);

static MetaBackend *
backend_from_keyboard (MetaWaylandKeyboard *keyboard)
{
  MetaWaylandInputDevice *input_device = META_WAYLAND_INPUT_DEVICE (keyboard);
  MetaWaylandSeat *seat = meta_wayland_input_device_get_seat (input_device);
  MetaWaylandCompositor *compositor = meta_wayland_seat_get_compositor (seat);
  MetaContext *context = meta_wayland_compositor_get_context (compositor);

  return meta_context_get_backend (context);
}

static void
unbind_resource (struct wl_resource *resource)
{
  wl_list_remove (wl_resource_get_link (resource));
}

static void
send_keymap (MetaWaylandKeyboard *keyboard,
             struct wl_resource  *resource)
{
  MetaWaylandXkbInfo *xkb_info = &keyboard->xkb_info;
  int fd;
  size_t size;
  MetaAnonymousFileMapmode mapmode;

  if (wl_resource_get_version (resource) < 7)
    mapmode = META_ANONYMOUS_FILE_MAPMODE_SHARED;
  else
    mapmode = META_ANONYMOUS_FILE_MAPMODE_PRIVATE;

  fd = meta_anonymous_file_open_fd (xkb_info->keymap_rofile, mapmode);
  size = meta_anonymous_file_size (xkb_info->keymap_rofile);

  if (fd == -1)
    {
      g_warning ("Creating a keymap file failed: %s", strerror (errno));
      return;
    }

  wl_keyboard_send_keymap (resource,
                           WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                           fd, size);

  meta_anonymous_file_close_fd (fd);
}

static void
inform_clients_of_new_keymap (MetaWaylandKeyboard *keyboard)
{
  struct wl_resource *keyboard_resource;

  wl_resource_for_each (keyboard_resource, &keyboard->resource_list)
    send_keymap (keyboard, keyboard_resource);
  wl_resource_for_each (keyboard_resource, &keyboard->focus_resource_list)
    send_keymap (keyboard, keyboard_resource);
}

static void
meta_wayland_keyboard_take_keymap (MetaWaylandKeyboard *keyboard,
				   struct xkb_keymap   *keymap)
{
  MetaWaylandXkbInfo *xkb_info = &keyboard->xkb_info;
  char *keymap_string;
  size_t keymap_size;

  if (keymap == NULL)
    {
      g_warning ("Attempting to set null keymap (compilation probably failed)");
      return;
    }

  xkb_keymap_unref (xkb_info->keymap);
  xkb_info->keymap = xkb_keymap_ref (keymap);

  meta_wayland_keyboard_update_xkb_state (keyboard);

  keymap_string =
    xkb_keymap_get_as_string (xkb_info->keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
  if (!keymap_string)
    {
      g_warning ("Failed to get string version of keymap");
      return;
    }
  keymap_size = strlen (keymap_string) + 1;

  g_clear_pointer (&xkb_info->keymap_rofile, meta_anonymous_file_free);
  xkb_info->keymap_rofile =
    meta_anonymous_file_new ("wayland-keymap",
                             keymap_size, (const uint8_t *) keymap_string);

  free (keymap_string);

  if (!xkb_info->keymap_rofile)
    {
      g_warning ("Failed to create anonymous file for keymap");
      return;
    }

  inform_clients_of_new_keymap (keyboard);

  notify_modifiers (keyboard);
}

static xkb_mod_mask_t
kbd_a11y_apply_mask (MetaWaylandKeyboard *keyboard)
{
  xkb_mod_mask_t latched, locked, depressed, group;
  xkb_mod_mask_t update_mask = 0;

  depressed = xkb_state_serialize_mods(keyboard->xkb_info.state, XKB_STATE_DEPRESSED);
  latched = xkb_state_serialize_mods (keyboard->xkb_info.state, XKB_STATE_MODS_LATCHED);
  locked = xkb_state_serialize_mods (keyboard->xkb_info.state, XKB_STATE_MODS_LOCKED);
  group = xkb_state_serialize_layout (keyboard->xkb_info.state, XKB_STATE_LAYOUT_EFFECTIVE);

  if ((latched & keyboard->kbd_a11y_latched_mods) != keyboard->kbd_a11y_latched_mods)
    update_mask |= XKB_STATE_MODS_LATCHED;

  if ((locked & keyboard->kbd_a11y_locked_mods) != keyboard->kbd_a11y_locked_mods)
    update_mask |= XKB_STATE_MODS_LOCKED;

  if (update_mask)
    {
      latched |= keyboard->kbd_a11y_latched_mods;
      locked |= keyboard->kbd_a11y_locked_mods;
      xkb_state_update_mask (keyboard->xkb_info.state, depressed, latched, locked, 0, 0, group);
    }

  return update_mask;
}

static void
on_keymap_changed (MetaBackend *backend,
                   gpointer     data)
{
  MetaWaylandKeyboard *keyboard = data;

  meta_wayland_keyboard_take_keymap (keyboard, meta_backend_get_keymap (backend));
}

static void
on_keymap_layout_group_changed (MetaBackend *backend,
                                guint        idx,
                                gpointer     data)
{
  MetaWaylandKeyboard *keyboard = data;
  xkb_mod_mask_t depressed_mods;
  xkb_mod_mask_t latched_mods;
  xkb_mod_mask_t locked_mods;
  struct xkb_state *state;

  state = keyboard->xkb_info.state;

  depressed_mods = xkb_state_serialize_mods (state, XKB_STATE_MODS_DEPRESSED);
  latched_mods = xkb_state_serialize_mods (state, XKB_STATE_MODS_LATCHED);
  locked_mods = xkb_state_serialize_mods (state, XKB_STATE_MODS_LOCKED);

  xkb_state_update_mask (state, depressed_mods, latched_mods, locked_mods, 0, 0, idx);
  kbd_a11y_apply_mask (keyboard);

  notify_modifiers (keyboard);
}

static void
keyboard_handle_focus_surface_destroy (struct wl_listener *listener, void *data)
{
  MetaWaylandKeyboard *keyboard =
    wl_container_of (listener, keyboard, focus_surface_listener);

  meta_wayland_keyboard_set_focus (keyboard, NULL);
}

static gboolean
meta_wayland_keyboard_broadcast_key (MetaWaylandKeyboard *keyboard,
                                     uint32_t             time,
                                     uint32_t             key,
                                     uint32_t             state)
{
  struct wl_resource *resource;

  if (!wl_list_empty (&keyboard->focus_resource_list))
    {
      MetaWaylandInputDevice *input_device =
        META_WAYLAND_INPUT_DEVICE (keyboard);
      uint32_t serial;

      serial = meta_wayland_input_device_next_serial (input_device);

      if (keyboard->last_key_up)
        {
          g_hash_table_remove (keyboard->key_down_serials,
                               GUINT_TO_POINTER (keyboard->last_key_up));
          keyboard->last_key_up = 0;
        }

      if (state)
        {
          g_hash_table_insert (keyboard->key_down_serials,
                               GUINT_TO_POINTER (key),
                               GUINT_TO_POINTER (serial));
          keyboard->last_key_up_serial = 0;
        }
      else
        {
          keyboard->last_key_up_serial = serial;
          keyboard->last_key_up = key;
        }

      wl_resource_for_each (resource, &keyboard->focus_resource_list)
        wl_keyboard_send_key (resource, serial, time, key, state);
    }

  /* Eat the key events if we have a focused surface. */
  return (keyboard->focus_surface != NULL);
}

static gboolean
notify_key (MetaWaylandKeyboard *keyboard,
            const ClutterEvent  *event)
{
  gboolean is_press = clutter_event_type (event) == CLUTTER_KEY_PRESS;
  guint32 code = 0;

  /* Ignore autorepeat events, as autorepeat in Wayland is done on the client
   * side. */
  if (clutter_event_get_flags (event) & CLUTTER_EVENT_FLAG_REPEATED)
    return FALSE;

  code = clutter_event_get_event_code (event);

  return meta_wayland_keyboard_broadcast_key (keyboard,
                                              clutter_event_get_time (event),
                                              code, is_press);
}

static xkb_mod_mask_t
add_vmod (xkb_mod_mask_t mask,
          xkb_mod_mask_t mod,
          xkb_mod_mask_t vmod,
          xkb_mod_mask_t *added)
{
  if ((mask & mod) && !(mod & *added))
    {
      mask |= vmod;
      *added |= mod;
    }
  return mask;
}

static xkb_mod_mask_t
add_virtual_mods (MetaDisplay    *display,
                  xkb_mod_mask_t  mask)
{
  MetaKeyBindingManager *keys = &display->key_binding_manager;
  xkb_mod_mask_t added;
  guint i;
  /* Order is important here: if multiple vmods share the same real
     modifier we only want to add the first. */
  struct {
    xkb_mod_mask_t mod;
    xkb_mod_mask_t vmod;
  } mods[] = {
    { keys->super_mask, keys->virtual_super_mask },
    { keys->hyper_mask, keys->virtual_hyper_mask },
    { keys->meta_mask,  keys->virtual_meta_mask },
  };

  added = 0;
  for (i = 0; i < G_N_ELEMENTS (mods); ++i)
    mask = add_vmod (mask, mods[i].mod, mods[i].vmod, &added);

  return mask;
}

static void
keyboard_send_modifiers (MetaWaylandKeyboard *keyboard,
                         struct wl_resource  *resource,
                         uint32_t             serial)
{
  MetaWaylandInputDevice *input_device = META_WAYLAND_INPUT_DEVICE (keyboard);
  MetaWaylandSeat *seat = meta_wayland_input_device_get_seat (input_device);
  MetaWaylandCompositor *compositor = meta_wayland_seat_get_compositor (seat);
  MetaContext *context = meta_wayland_compositor_get_context (compositor);
  MetaDisplay *display = meta_context_get_display (context);
  struct xkb_state *state = keyboard->xkb_info.state;
  xkb_mod_mask_t depressed, latched, locked;

  depressed = add_virtual_mods (display,
                                xkb_state_serialize_mods (state, XKB_STATE_MODS_DEPRESSED));
  latched = add_virtual_mods (display,
                              xkb_state_serialize_mods (state, XKB_STATE_MODS_LATCHED));
  locked = add_virtual_mods (display,
                             xkb_state_serialize_mods (state, XKB_STATE_MODS_LOCKED));

  wl_keyboard_send_modifiers (resource, serial, depressed, latched, locked,
                              xkb_state_serialize_layout (state, XKB_STATE_LAYOUT_EFFECTIVE));
}

static void
meta_wayland_keyboard_broadcast_modifiers (MetaWaylandKeyboard *keyboard)
{
  struct wl_resource *resource;

  if (!wl_list_empty (&keyboard->focus_resource_list))
    {
      MetaWaylandInputDevice *input_device =
        META_WAYLAND_INPUT_DEVICE (keyboard);
      uint32_t serial;

      serial = meta_wayland_input_device_next_serial (input_device);

      wl_resource_for_each (resource, &keyboard->focus_resource_list)
        keyboard_send_modifiers (keyboard, resource, serial);
    }
}

static void
notify_modifiers (MetaWaylandKeyboard *keyboard)
{
  meta_wayland_keyboard_broadcast_modifiers (keyboard);
}

static void
meta_wayland_keyboard_update_xkb_state (MetaWaylandKeyboard *keyboard)
{
  MetaWaylandXkbInfo *xkb_info = &keyboard->xkb_info;
  xkb_mod_mask_t latched, locked, numlock;
  MetaBackend *backend = backend_from_keyboard (keyboard);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  xkb_layout_index_t layout_idx;
  ClutterKeymap *keymap;
  ClutterSeat *seat;

  /* Preserve latched/locked modifiers state */
  if (xkb_info->state)
    {
      latched = xkb_state_serialize_mods (xkb_info->state, XKB_STATE_MODS_LATCHED);
      locked = xkb_state_serialize_mods (xkb_info->state, XKB_STATE_MODS_LOCKED);
      xkb_state_unref (xkb_info->state);
    }
  else
    {
      latched = locked = 0;
    }

  seat = clutter_backend_get_default_seat (clutter_backend);
  keymap = clutter_seat_get_keymap (seat);
  numlock = (1 <<  xkb_keymap_mod_get_index (xkb_info->keymap, "Mod2"));

  if (clutter_keymap_get_num_lock_state (keymap))
    locked |= numlock;
  else
    locked &= ~numlock;

  xkb_info->state = xkb_state_new (xkb_info->keymap);

  layout_idx = meta_backend_get_keymap_layout_group (backend);
  xkb_state_update_mask (xkb_info->state, 0, latched, locked, 0, 0, layout_idx);

  kbd_a11y_apply_mask (keyboard);
}

static void
on_kbd_a11y_mask_changed (ClutterSeat         *seat,
                          xkb_mod_mask_t       new_latched_mods,
                          xkb_mod_mask_t       new_locked_mods,
                          MetaWaylandKeyboard *keyboard)
{
  xkb_mod_mask_t latched, locked, depressed, group;

  if (keyboard->xkb_info.state == NULL)
    return;

  depressed = xkb_state_serialize_mods(keyboard->xkb_info.state, XKB_STATE_DEPRESSED);
  latched = xkb_state_serialize_mods (keyboard->xkb_info.state, XKB_STATE_MODS_LATCHED);
  locked = xkb_state_serialize_mods (keyboard->xkb_info.state, XKB_STATE_MODS_LOCKED);
  group = xkb_state_serialize_layout (keyboard->xkb_info.state, XKB_STATE_LAYOUT_EFFECTIVE);

  /* Clear previous masks */
  latched &= ~keyboard->kbd_a11y_latched_mods;
  locked &= ~keyboard->kbd_a11y_locked_mods;
  xkb_state_update_mask (keyboard->xkb_info.state, depressed, latched, locked, 0, 0, group);

  /* Apply new masks */
  keyboard->kbd_a11y_latched_mods = new_latched_mods;
  keyboard->kbd_a11y_locked_mods = new_locked_mods;
  kbd_a11y_apply_mask (keyboard);

  notify_modifiers (keyboard);
}

static void
notify_key_repeat_for_resource (MetaWaylandKeyboard *keyboard,
                                struct wl_resource  *keyboard_resource)
{
  if (wl_resource_get_version (keyboard_resource) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION)
    {
      gboolean repeat;
      unsigned int delay, rate;

      repeat = g_settings_get_boolean (keyboard->settings, "repeat");

      if (repeat)
        {
          unsigned int interval;
          interval = g_settings_get_uint (keyboard->settings, "repeat-interval");
          /* Our setting is in the milliseconds between keys. "rate" is the number
           * of keys per second. */
          if (interval > 0)
            rate = (1000 / interval);
          else
            rate = 0;

          delay = g_settings_get_uint (keyboard->settings, "delay");
        }
      else
        {
          rate = 0;
          delay = 0;
        }

      wl_keyboard_send_repeat_info (keyboard_resource, rate, delay);
    }
}

static void
notify_key_repeat (MetaWaylandKeyboard *keyboard)
{
  struct wl_resource *keyboard_resource;

  wl_resource_for_each (keyboard_resource, &keyboard->resource_list)
    {
      notify_key_repeat_for_resource (keyboard, keyboard_resource);
    }

  wl_resource_for_each (keyboard_resource, &keyboard->focus_resource_list)
    {
      notify_key_repeat_for_resource (keyboard, keyboard_resource);
    }
}

static void
settings_changed (GSettings           *settings,
                  const char          *key,
                  gpointer             data)
{
  MetaWaylandKeyboard *keyboard = data;

  notify_key_repeat (keyboard);
}

void
meta_wayland_keyboard_enable (MetaWaylandKeyboard *keyboard)
{
  MetaWaylandInputDevice *input_device = META_WAYLAND_INPUT_DEVICE (keyboard);
  MetaWaylandSeat *seat = meta_wayland_input_device_get_seat (input_device);
  MetaBackend *backend = backend_from_keyboard (keyboard);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);

  keyboard->settings = g_settings_new ("org.gnome.desktop.peripherals.keyboard");

  wl_array_init (&keyboard->pressed_keys);

  keyboard->key_down_serials = g_hash_table_new (NULL, NULL);

  g_signal_connect (keyboard->settings, "changed",
                    G_CALLBACK (settings_changed), keyboard);

  g_signal_connect (backend, "keymap-changed",
                    G_CALLBACK (on_keymap_changed), keyboard);
  g_signal_connect (backend, "keymap-layout-group-changed",
                    G_CALLBACK (on_keymap_layout_group_changed), keyboard);

  g_signal_connect (clutter_backend_get_default_seat (clutter_backend),
		    "kbd-a11y-mods-state-changed",
                    G_CALLBACK (on_kbd_a11y_mask_changed), keyboard);

  meta_wayland_keyboard_take_keymap (keyboard, meta_backend_get_keymap (backend));

  meta_wayland_keyboard_set_focus (keyboard, seat->input_focus);
}

static void
meta_wayland_xkb_info_destroy (MetaWaylandXkbInfo *xkb_info)
{
  g_clear_pointer (&xkb_info->keymap, xkb_keymap_unref);
  g_clear_pointer (&xkb_info->state, xkb_state_unref);
  g_clear_pointer (&xkb_info->keymap_rofile, meta_anonymous_file_free);
}

void
meta_wayland_keyboard_disable (MetaWaylandKeyboard *keyboard)
{
  MetaBackend *backend = backend_from_keyboard (keyboard);

  g_signal_handlers_disconnect_by_func (backend, on_keymap_changed, keyboard);
  g_signal_handlers_disconnect_by_func (backend, on_keymap_layout_group_changed, keyboard);

  meta_wayland_keyboard_set_focus (keyboard, NULL);

  wl_list_remove (&keyboard->resource_list);
  wl_list_init (&keyboard->resource_list);
  wl_list_remove (&keyboard->focus_resource_list);
  wl_list_init (&keyboard->focus_resource_list);

  g_clear_pointer (&keyboard->key_down_serials, g_hash_table_unref);
  keyboard->last_key_up_serial = 0;

  wl_array_release (&keyboard->pressed_keys);

  g_clear_object (&keyboard->settings);
}

static gboolean
update_pressed_keys (struct wl_array *keys,
                     uint32_t         evdev_code,
                     gboolean         is_press)
{
  uint32_t *end = (void *) ((char *) keys->data + keys->size);
  uint32_t *k;

  if (is_press)
    {
      /* Make sure we don't already have this key. */
      for (k = keys->data; k < end; k++)
        {
          if (*k == evdev_code)
            return FALSE;
        }

      /* Otherwise add the key to the list of pressed keys */
      k = wl_array_add (keys, sizeof (*k));
      *k = evdev_code;
      return TRUE;
    }
  else
    {
      /* Remove the key from the array */
      for (k = keys->data; k < end; k++)
        {
          if (*k == evdev_code)
            {
              *k = *(end - 1);
              keys->size -= sizeof (*k);
              return TRUE;
            }
        }

      return FALSE;
    }
}

void
meta_wayland_keyboard_update (MetaWaylandKeyboard *keyboard,
                              const ClutterKeyEvent *event)
{
  gboolean is_press = clutter_event_type ((ClutterEvent *) event) == CLUTTER_KEY_PRESS;
  uint32_t evdev_code, hardware_keycode;

  evdev_code = clutter_event_get_event_code ((ClutterEvent *) event);
  hardware_keycode = clutter_event_get_key_code ((ClutterEvent *) event);

  if (!update_pressed_keys (&keyboard->pressed_keys, evdev_code, is_press))
    return;

  /* If we get a key event but still have pending modifier state
   * changes from a previous event that didn't get cleared, we need to
   * send that state right away so that the new key event can be
   * interpreted by clients correctly modified. */
  if (keyboard->mods_changed)
    notify_modifiers (keyboard);

  keyboard->mods_changed = xkb_state_update_key (keyboard->xkb_info.state,
                                                 hardware_keycode,
                                                 is_press ? XKB_KEY_DOWN : XKB_KEY_UP);
  keyboard->mods_changed |= kbd_a11y_apply_mask (keyboard);
}

gboolean
meta_wayland_keyboard_handle_event (MetaWaylandKeyboard   *keyboard,
                                    const ClutterKeyEvent *event)
{
#ifdef WITH_VERBOSE_MODE
  gboolean is_press =
    clutter_event_type ((ClutterEvent *) event) == CLUTTER_KEY_PRESS;
#endif
  gboolean handled;
  ClutterEventFlags flags;
  uint32_t hardware_keycode;

  flags = clutter_event_get_flags ((ClutterEvent *) event);
  hardware_keycode = clutter_event_get_key_code ((ClutterEvent *) event);

  /* Synthetic key events are for autorepeat. Ignore those, as
   * autorepeat in Wayland is done on the client side. */
  if ((flags & CLUTTER_EVENT_FLAG_SYNTHETIC) &&
      !(flags & CLUTTER_EVENT_FLAG_INPUT_METHOD))
    return FALSE;

  meta_topic (META_DEBUG_WAYLAND,
              "Handling key %s event code %d",
              is_press ? "press" : "release",
              hardware_keycode);

  handled = notify_key (keyboard, (const ClutterEvent *) event);

  if (handled)
    {
      meta_topic (META_DEBUG_WAYLAND, "Sent event to wayland client");
    }
  else
    {
      meta_topic (META_DEBUG_WAYLAND,
                  "No wayland surface is focused, continuing normal operation");
    }

  if (keyboard->mods_changed != 0)
    {
      notify_modifiers (keyboard);
      keyboard->mods_changed = 0;
    }

  return handled;
}

void
meta_wayland_keyboard_update_key_state (MetaWaylandKeyboard *keyboard,
                                        char                *key_vector,
                                        int                  key_vector_len,
                                        int                  offset)
{
  gboolean mods_changed = FALSE;

  int i;
  for (i = offset; i < key_vector_len * 8; i++)
    {
      gboolean set = (key_vector[i/8] & (1 << (i % 8))) != 0;

      /* The 'offset' parameter allows the caller to have the indices
       * into key_vector to either be X-style (base 8) or evdev (base 0), or
       * something else (unlikely). We subtract 'offset' to convert to evdev
       * style, then add 8 to convert the "evdev" style keycode back to
       * the X-style that xkbcommon expects.
       */
      mods_changed |= xkb_state_update_key (keyboard->xkb_info.state,
                                            i - offset + 8,
                                            set ? XKB_KEY_DOWN : XKB_KEY_UP);
    }

  mods_changed |= kbd_a11y_apply_mask (keyboard);
  if (mods_changed)
    notify_modifiers (keyboard);
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

static void
broadcast_focus (MetaWaylandKeyboard *keyboard,
                 struct wl_resource  *resource)
{
  wl_keyboard_send_enter (resource, keyboard->focus_serial,
                          keyboard->focus_surface->resource,
                          &keyboard->pressed_keys);
  keyboard_send_modifiers (keyboard, resource, keyboard->focus_serial);
}

void
meta_wayland_keyboard_set_focus (MetaWaylandKeyboard *keyboard,
                                 MetaWaylandSurface *surface)
{
  MetaWaylandInputDevice *input_device = META_WAYLAND_INPUT_DEVICE (keyboard);

  if (keyboard->focus_surface == surface)
    return;

  if (keyboard->focus_surface != NULL)
    {
      if (!wl_list_empty (&keyboard->focus_resource_list))
        {
          struct wl_resource *resource;
          uint32_t serial;

          serial = meta_wayland_input_device_next_serial (input_device);

          wl_resource_for_each (resource, &keyboard->focus_resource_list)
            {
              wl_keyboard_send_leave (resource, serial,
                                      keyboard->focus_surface->resource);
            }

          move_resources (&keyboard->resource_list,
                          &keyboard->focus_resource_list);
        }

      if (!surface ||
          wl_resource_get_client (keyboard->focus_surface->resource) !=
          wl_resource_get_client (surface->resource))
        {
          g_hash_table_remove_all (keyboard->key_down_serials);
          keyboard->last_key_up_serial = 0;
        }

      wl_list_remove (&keyboard->focus_surface_listener.link);
      keyboard->focus_surface = NULL;
    }

  if (surface != NULL)
    {
      struct wl_resource *focus_surface_resource;

      keyboard->focus_surface = surface;
      focus_surface_resource = keyboard->focus_surface->resource;
      wl_resource_add_destroy_listener (focus_surface_resource,
                                        &keyboard->focus_surface_listener);

      move_resources_for_client (&keyboard->focus_resource_list,
                                 &keyboard->resource_list,
                                 wl_resource_get_client (focus_surface_resource));

      /* Make sure a11y masks are applied before broadcasting modifiers */
      kbd_a11y_apply_mask (keyboard);

      if (!wl_list_empty (&keyboard->focus_resource_list))
        {
          struct wl_resource *resource;

          keyboard->focus_serial =
            meta_wayland_input_device_next_serial (input_device);

          wl_resource_for_each (resource, &keyboard->focus_resource_list)
            {
              broadcast_focus (keyboard, resource);
            }
        }
    }
}

static void
keyboard_release (struct wl_client *client,
                  struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct wl_keyboard_interface keyboard_interface = {
  keyboard_release,
};

void
meta_wayland_keyboard_create_new_resource (MetaWaylandKeyboard *keyboard,
                                           struct wl_client    *client,
                                           struct wl_resource  *seat_resource,
                                           uint32_t id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client, &wl_keyboard_interface,
                                 wl_resource_get_version (seat_resource), id);
  wl_resource_set_implementation (resource, &keyboard_interface,
                                  keyboard, unbind_resource);

  send_keymap (keyboard, resource);

  notify_key_repeat_for_resource (keyboard, resource);

  if (keyboard->focus_surface &&
      wl_resource_get_client (keyboard->focus_surface->resource) == client)
    {
      wl_list_insert (&keyboard->focus_resource_list,
                      wl_resource_get_link (resource));
      broadcast_focus (keyboard, resource);
    }
  else
    {
      wl_list_insert (&keyboard->resource_list,
                      wl_resource_get_link (resource));
    }
}

gboolean
meta_wayland_keyboard_can_grab_surface (MetaWaylandKeyboard *keyboard,
                                        MetaWaylandSurface  *surface,
                                        uint32_t             serial)
{
  if (keyboard->focus_surface != surface)
    return FALSE;

  return (keyboard->focus_serial == serial ||
          meta_wayland_keyboard_can_popup (keyboard, serial));
}

gboolean
meta_wayland_keyboard_can_popup (MetaWaylandKeyboard *keyboard,
                                 uint32_t             serial)
{
  GHashTableIter iter;
  gpointer value;

  if (keyboard->last_key_up_serial == serial)
    return TRUE;

  g_hash_table_iter_init (&iter, keyboard->key_down_serials);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      if (GPOINTER_TO_UINT (value) == serial)
        return TRUE;
    }

  return FALSE;
}

static void
meta_wayland_keyboard_init (MetaWaylandKeyboard *keyboard)
{
  wl_list_init (&keyboard->resource_list);
  wl_list_init (&keyboard->focus_resource_list);

  keyboard->focus_surface_listener.notify =
    keyboard_handle_focus_surface_destroy;
}

static void
meta_wayland_keyboard_finalize (GObject *object)
{
  MetaWaylandKeyboard *keyboard = META_WAYLAND_KEYBOARD (object);

  meta_wayland_xkb_info_destroy (&keyboard->xkb_info);

  G_OBJECT_CLASS (meta_wayland_keyboard_parent_class)->finalize (object);
}

static void
meta_wayland_keyboard_class_init (MetaWaylandKeyboardClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_wayland_keyboard_finalize;
}

MetaWaylandSurface *
meta_wayland_keyboard_get_focus_surface (MetaWaylandKeyboard *keyboard)
{
  return keyboard->focus_surface;
}
