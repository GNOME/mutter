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
basic (WaylandDisplay *display)
{
  TestDisplayState *test_state = display->test_state;
  g_autoptr (WaylandSurface) toplevel1 = NULL;
  g_autoptr (WaylandSurface) toplevel2 = NULL;
  struct xx_session_v1 *session;
  struct xx_toplevel_session_v1 *toplevel_session1;
  struct xx_toplevel_session_v1 *toplevel_session2;
  TestCreateState state = {};
  ToplevelSessionState toplevel_state1 = {};
  ToplevelSessionState toplevel_state2 = {};

  toplevel1 = wayland_surface_new (display, "toplevel1",
                                   100, 100, 0xff50ff50);
  g_signal_connect (toplevel1, "configure",
                    G_CALLBACK (on_toplevel_configured),
                    &toplevel_state1);

  session =
    xx_session_manager_v1_get_session (test_state->session_manager,
                                       XX_SESSION_MANAGER_V1_REASON_LAUNCH,
                                       NULL);
  xx_session_v1_add_listener (session, &test_create_session_listener, &state);

  while (!state.received_created)
    wayland_display_dispatch (display);

  /* Test add before committing initial state. */
  toplevel_session1 = xx_session_v1_add_toplevel (session,
                                                  toplevel1->xdg_toplevel,
                                                  "toplevel1");
  xx_toplevel_session_v1_add_listener (toplevel_session1,
                                       &toplevel_session_listener,
                                       &toplevel_state1);
  wl_surface_commit (toplevel1->wl_surface);

  while (!toplevel_state1.configured)
    wayland_display_dispatch (display);
  g_assert_false (toplevel_state1.restored);

  /* Test add after committing initial state. */
  toplevel2 = wayland_surface_new (display, "toplevel2",
                                   100, 100, 0xff0000ff);
  g_signal_connect (toplevel1, "configure",
                    G_CALLBACK (on_toplevel_configured),
                    &toplevel_state2);
  wl_surface_commit (toplevel1->wl_surface);

  toplevel_session2 = xx_session_v1_add_toplevel (session,
                                                  toplevel2->xdg_toplevel,
                                                  "toplevel2");
  xx_toplevel_session_v1_add_listener (toplevel_session2,
                                       &toplevel_session_listener,
                                       &toplevel_state2);

  while (!toplevel_state2.configured)
    wayland_display_dispatch (display);
  g_assert_false (toplevel_state2.restored);

  xx_toplevel_session_v1_destroy (toplevel_session1);
  xx_toplevel_session_v1_destroy (toplevel_session2);
  xx_session_v1_destroy (session);
}

static void
toplevel_inert (WaylandDisplay *display)
{
  TestDisplayState *test_state = display->test_state;
  g_autoptr (WaylandSurface) toplevel = NULL;
  struct xx_session_v1 *session;
  struct xx_toplevel_session_v1 *toplevel_session;
  TestCreateState state = {};
  ToplevelSessionState toplevel_state = {};

  toplevel = wayland_surface_new (display, "toplevel",
                                 100, 100, 0xff50ff50);
  g_signal_connect (toplevel, "configure",
                    G_CALLBACK (on_toplevel_configured),
                    &toplevel_state);

  session =
    xx_session_manager_v1_get_session (test_state->session_manager,
                                       XX_SESSION_MANAGER_V1_REASON_LAUNCH,
                                       NULL);
  xx_session_v1_add_listener (session, &test_create_session_listener, &state);

  while (!state.received_created)
    wayland_display_dispatch (display);

  /* Test add before committing initial state. */
  toplevel_session = xx_session_v1_add_toplevel (session,
                                                 toplevel->xdg_toplevel,
                                                 "toplevel");
  xx_toplevel_session_v1_add_listener (toplevel_session,
                                       &toplevel_session_listener,
                                       &toplevel_state);
  wl_surface_commit (toplevel->wl_surface);

  while (!toplevel_state.configured)
    wayland_display_dispatch (display);
  g_assert_false (toplevel_state.restored);

  /* destroy the xdg_toplevel */
  g_clear_object (&toplevel);

  /* toplevel_session should be inert now and remove should have no effect */
  xx_toplevel_session_v1_remove (toplevel_session);

  xx_session_v1_destroy (session);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  struct wl_registry *registry;
  TestDisplayState *test_state;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);
  test_state = g_new0 (TestDisplayState, 1);
  display->test_state = test_state;
  display->destroy_test_state = g_free;

  registry = wl_display_get_registry (display->display);
  wl_registry_add_listener (registry, &registry_listener, display);
  wl_display_roundtrip (display->display);

  g_assert_nonnull (test_state->session_manager);

  basic (display);

  toplevel_inert (display);

  return EXIT_SUCCESS;
}
