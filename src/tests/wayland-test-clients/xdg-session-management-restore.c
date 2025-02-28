/*
 * Copyright (C) 2024 Red Hat Inc.
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

#include <glib.h>

#include "wayland-test-client-utils.h"

#include "session-management-v1-client-protocol.h"

typedef struct _TestDisplayState
{
  struct xx_session_manager_v1 *session_manager;
} TestDisplayState;

static gboolean running = FALSE;

static void
handle_registry_global (void               *user_data,
                        struct wl_registry *registry,
                        uint32_t            id,
                        const char         *interface,
                        uint32_t            version)
{
  WaylandDisplay *display = user_data;
  TestDisplayState *test_state = display->test_state;

  if (strcmp (interface, "xx_session_manager_v1") == 0)
    {
      test_state->session_manager =
        wl_registry_bind (registry, id, &xx_session_manager_v1_interface, 1);
    }
}

static void
handle_registry_global_remove (void               *user_data,
                               struct wl_registry *registry,
                               uint32_t            name)
{
}

static const struct wl_registry_listener registry_listener = {
  handle_registry_global,
  handle_registry_global_remove
};

typedef struct
{
  gboolean received_created;
  gboolean received_restored;
} TestCreateState;

typedef struct
{
  gboolean configured;
  gboolean restored;
} ToplevelSessionState;

static void
test_create_created (void                 *user_data,
                     struct xx_session_v1 *xdg_session_v1,
                     const char           *id)
{
  TestCreateState *state = user_data;

  state->received_created = TRUE;
}

static void
test_create_restored (void                 *user_data,
                      struct xx_session_v1 *xdg_session_v1)
{
  TestCreateState *state = user_data;

  state->received_restored = TRUE;
}

static void
test_create_replaced (void                 *user_data,
                      struct xx_session_v1 *xdg_session_v1)
{
}

static struct xx_session_v1_listener test_create_session_listener = {
  test_create_created,
  test_create_restored,
  test_create_replaced,
};

static void
toplevel_restored (void                          *user_data,
                   struct xx_toplevel_session_v1 *toplevel_session,
                   struct xdg_toplevel           *toplevel)
{
  ToplevelSessionState *toplevel_state = user_data;

  toplevel_state->restored = TRUE;
}

static struct xx_toplevel_session_v1_listener toplevel_session_listener = {
  toplevel_restored,
};

static void
on_toplevel_configured (WaylandSurface       *surface,
                        ToplevelSessionState *toplevel_state)
{
  toplevel_state->configured = TRUE;
}

static void
simple (WaylandDisplay *display,
        const char     *session_id)
{
  TestDisplayState *test_state = display->test_state;
  g_autoptr (WaylandSurface) toplevel1 = NULL;
  struct xx_session_v1 *session;
  struct xx_toplevel_session_v1 *toplevel_session1;
  TestCreateState state = {};
  ToplevelSessionState toplevel_state1 = {};

  toplevel1 = wayland_surface_new (display, "toplevel1",
                                   100, 100, 0xff50ff50);
  g_signal_connect (toplevel1, "configure",
                    G_CALLBACK (on_toplevel_configured),
                    &toplevel_state1);

  session =
    xx_session_manager_v1_get_session (test_state->session_manager,
                                       XX_SESSION_MANAGER_V1_REASON_LAUNCH,
                                       session_id);
  xx_session_v1_add_listener (session, &test_create_session_listener, &state);

  while (!state.received_created && !state.received_restored)
    wayland_display_dispatch (display);

  if (session_id)
    g_assert_true (state.received_restored);
  else
    g_assert_true (state.received_created);

  toplevel_session1 = xx_session_v1_restore_toplevel (session,
                                                      toplevel1->xdg_toplevel,
                                                      "toplevel1");
  xx_toplevel_session_v1_add_listener (toplevel_session1,
                                       &toplevel_session_listener,
                                       &toplevel_state1);

  wl_surface_commit (toplevel1->wl_surface);

  running = TRUE;
  while (running)
    wayland_display_dispatch (display);

  xx_toplevel_session_v1_destroy (toplevel_session1);
  xx_session_v1_destroy (session);
}

static void
on_surface_painted (WaylandDisplay *display,
                    WaylandSurface *surface)
{
  static gboolean first_painted = FALSE;

  if (first_painted)
    return;

  first_painted = TRUE;

  /* Sync point to let parent test do checks */
  test_driver_sync_point (display->test_driver, 0, NULL);
  wait_for_sync_event (display, 0);
  running = FALSE;
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  struct wl_registry *registry;
  TestDisplayState *test_state;
  const char *session_id = NULL;

  if (argc > 1)
    session_id = argv[1];

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);
  test_state = g_new0 (TestDisplayState, 1);
  display->test_state = test_state;
  display->destroy_test_state = g_free;

  g_signal_connect (display, "surface-painted",
                    G_CALLBACK (on_surface_painted), NULL);

  registry = wl_display_get_registry (display->display);
  wl_registry_add_listener (registry, &registry_listener, display);
  wl_display_roundtrip (display->display);

  g_assert_nonnull (test_state->session_manager);

  simple (display, session_id);

  return EXIT_SUCCESS;
}
