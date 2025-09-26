/*
 * Copyright (C) 2025 Red Hat, Inc.
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

#include <glib.h>
#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include "wayland-test-client-utils.h"

static gboolean running;
static struct wl_seat *wl_seat;
static struct wl_keyboard *wl_keyboard;
static int key_event_count;
static int sync_point_count;
static int enter_event_count;
static uint32_t pressed_mods;
static char *test_name;

#define SHIFT_MASK (1 << 0)
#define SUPER_MASK (1 << 6)

static void
sync_point (WaylandDisplay *display)
{
  test_driver_sync_point (display->test_driver, sync_point_count++, NULL);
}

static void
keyboard_handle_keymap (void               *data,
                        struct wl_keyboard *keyboard,
                        uint32_t            format,
                        int                 fd,
                        uint32_t            size)
{
}

static void
keyboard_handle_enter (void               *data,
                       struct wl_keyboard *keyboard,
                       uint32_t            serial,
                       struct wl_surface  *surface,
                       struct wl_array    *keys)
{
  WaylandDisplay *display = data;

  enter_event_count++;
  sync_point (display);
}

static void
keyboard_handle_leave (void               *data,
                       struct wl_keyboard *keyboard,
                       uint32_t            serial,
                       struct wl_surface  *surface)
{
  if (g_strcmp0 (test_name, "focus-switch-source") == 0)
    running = FALSE;
}

static void
keyboard_handle_key (void               *data,
                     struct wl_keyboard *keyboard,
                     uint32_t            serial,
                     uint32_t            time,
                     uint32_t            key,
                     uint32_t            state)
{
  WaylandDisplay *display = data;

  key_event_count++;

  if (g_strcmp0 (test_name, "event-order") == 0)
    {
      g_assert_cmpint (key, ==, KEY_LEFTSHIFT);
      if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
        g_assert_cmpint ((pressed_mods & SHIFT_MASK), ==, 0);
      else
        g_assert_cmpint ((pressed_mods & SHIFT_MASK), ==, SHIFT_MASK);
    }
  else if (g_strcmp0 (test_name, "event-order2") == 0)
    {
      if (key == KEY_LEFTSHIFT)
        {
          g_assert_cmpint (state, ==, WL_KEYBOARD_KEY_STATE_PRESSED);
          g_assert_cmpint ((pressed_mods & SHIFT_MASK), ==, 0);
        }
      else if (key == KEY_F)
        {
          g_assert_cmpint (state, ==, WL_KEYBOARD_KEY_STATE_PRESSED);
          g_assert_cmpint ((pressed_mods & SHIFT_MASK), ==, SHIFT_MASK);
          sync_point (display);
          running = FALSE;
        }
    }
  else if (g_strcmp0 (test_name, "client-shortcut") == 0)
    {
      if (key == KEY_F)
        {
          g_assert_cmpint ((pressed_mods & SUPER_MASK), ==, SUPER_MASK);
          sync_point (display);
        }
      else
        {
          g_assert_cmpint (key, ==, KEY_LEFTMETA);
          if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
            g_assert_cmpint ((pressed_mods & SUPER_MASK), ==, 0);
          else
            g_assert_cmpint ((pressed_mods & SUPER_MASK), ==, SUPER_MASK);
        }
    }
  else if (g_strcmp0 (test_name, "focus-switch-source") == 0 ||
           g_strcmp0 (test_name, "focus-switch-dest") == 0)
    {
      if (key_event_count == 1 && state == WL_KEYBOARD_KEY_STATE_PRESSED)
        {
          g_assert_cmpint (key, ==, KEY_LEFTMETA);
          g_assert_cmpint ((pressed_mods & SUPER_MASK), ==, 0);
        }
    }
}

static void
keyboard_handle_modifiers (void               *data,
                           struct wl_keyboard *keyboard,
                           uint32_t            serial,
                           uint32_t            mods_pressed,
                           uint32_t            mods_latched,
                           uint32_t            mods_locked,
                           uint32_t            group)
{
  WaylandDisplay *display = data;

  pressed_mods = mods_pressed;

  if (g_strcmp0 (test_name, "event-order") == 0)
    {
      if (mods_pressed)
        {
          g_assert_cmpint (key_event_count, ==, 1);
          sync_point (display);
        }
      else if (key_event_count > 0)
        {
          g_assert_cmpint (key_event_count, ==, 2);
          sync_point (display);
          running = FALSE;
        }
    }
  else if (g_strcmp0 (test_name, "event-order2") == 0)
    {
      if (key_event_count > 0)
        g_assert_cmpint (key_event_count, ==, 1);
    }
  else if (g_strcmp0 (test_name, "client-shortcut") == 0)
    {
      if (mods_pressed)
        {
          g_assert_cmpint (key_event_count, ==, 1);
          sync_point (display);
        }
      else if (key_event_count > 0)
        {
          g_assert_cmpint (key_event_count, ==, 4);
          sync_point (display);
          running = FALSE;
        }
    }
  else if (g_strcmp0 (test_name, "focus-switch-source") == 0)
    {
      if (mods_pressed && key_event_count > 0)
        {
          g_assert_cmpint (key_event_count, ==, 1);
          sync_point (display);
        }
    }
  else if (g_strcmp0 (test_name, "focus-switch-dest") == 0)
    {
      if (enter_event_count == 2 && (pressed_mods & SUPER_MASK) == 0)
        running = FALSE;
    }
}

static const struct wl_keyboard_listener keyboard_listener = {
  keyboard_handle_keymap,
  keyboard_handle_enter,
  keyboard_handle_leave,
  keyboard_handle_key,
  keyboard_handle_modifiers,
};

static void
seat_handle_capabilities (void                    *data,
                          struct wl_seat          *seat,
                          enum wl_seat_capability  caps)
{
  WaylandDisplay *display = data;

  if (caps & WL_SEAT_CAPABILITY_KEYBOARD)
    {
      wl_keyboard = wl_seat_get_keyboard (seat);
      wl_keyboard_add_listener (wl_keyboard, &keyboard_listener, display);
    }
}

static void
seat_handle_name (void           *data,
                  struct wl_seat *seat,
                  const char     *name)
{
}

static const struct wl_seat_listener seat_listener = {
  seat_handle_capabilities,
  seat_handle_name,
};

static void
handle_registry_global (void               *data,
                        struct wl_registry *registry,
                        uint32_t            id,
                        const char         *interface,
                        uint32_t            version)
{
  WaylandDisplay *display = data;

  if (strcmp (interface, "wl_seat") == 0)
    {
      wl_seat = wl_registry_bind (registry, id, &wl_seat_interface, 1);
      wl_seat_add_listener (wl_seat, &seat_listener, display);
    }
}

static void
handle_registry_global_remove (void               *data,
                               struct wl_registry *registry,
                               uint32_t            name)
{
}

static const struct wl_registry_listener registry_listener = {
  handle_registry_global,
  handle_registry_global_remove
};

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  g_autoptr (WaylandSurface) surface = NULL;
  struct wl_registry *registry;

  /* Use a sync counter in a different range */
  if (g_strcmp0 (argv[1], "focus-switch-source") == 0)
    sync_point_count = 100;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);

  registry = wl_display_get_registry (display->display);
  wl_registry_add_listener (registry, &registry_listener, display);
  wl_display_roundtrip (display->display);

  surface = wayland_surface_new (display,
                                 argv[1],
                                 100, 100, 0xffffffff);
  wl_surface_commit (surface->wl_surface);

  test_name = argv[1];

  running = TRUE;
  while (running)
    wayland_display_dispatch (display);

  wl_display_roundtrip (display->display);

  return EXIT_SUCCESS;
}
