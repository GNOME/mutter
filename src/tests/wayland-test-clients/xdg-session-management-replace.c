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


typedef enum _TestState
{
  TEST_STATE_INIT = 0,
  TEST_STATE_RECEIVE_REPLACED = 1,
  TEST_STATE_ASSERT_RESTORED = 2,
} TestState;

typedef struct _TestDisplayState
{
  struct xx_session_manager_v1 *session_manager;
  TestState state;
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
  gboolean received_restored;
  gboolean received_replaced;
  char *id;
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
  state->id = g_strdup (id);
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
  TestCreateState *state = user_data;

  state->received_replaced = TRUE;
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

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display1 = NULL, display2 = NULL;
  struct wl_registry *registry1, *registry2;
  TestDisplayState *test_state1, *test_state2;
  g_autoptr (WaylandSurface) toplevel1 = NULL, toplevel2 = NULL;
  struct xx_session_v1 *session1, *session2;
  struct xx_toplevel_session_v1 *toplevel_session1, *toplevel_session2;
  TestCreateState state1 = {};
  TestCreateState state2 = {};
  ToplevelSessionState toplevel_state1 = {};
  ToplevelSessionState toplevel_state2 = {};

  display1 = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);
  test_state1 = g_new0 (TestDisplayState, 1);
  display1->test_state = test_state1;
  display1->destroy_test_state = g_free;

  registry1 = wl_display_get_registry (display1->display);
  wl_registry_add_listener (registry1, &registry_listener, display1);
  wl_display_roundtrip (display1->display);

  g_assert_nonnull (test_state1->session_manager);

  toplevel1 = wayland_surface_new (display1, "toplevel",
                                   100, 100, 0xff50ff50);
  g_signal_connect (toplevel1, "configure",
                    G_CALLBACK (on_toplevel_configured),
                    &toplevel_state1);

  session1 =
    xx_session_manager_v1_get_session (test_state1->session_manager,
                                       XX_SESSION_MANAGER_V1_REASON_LAUNCH,
                                       NULL);
  xx_session_v1_add_listener (session1, &test_create_session_listener, &state1);

  while (!state1.received_created)
    wayland_display_dispatch (display1);
  g_assert_nonnull (state1.id);

  /* Test add before committing initial state. */
  toplevel_session1 = xx_session_v1_add_toplevel (session1,
                                                  toplevel1->xdg_toplevel,
                                                  "toplevel");
  xx_toplevel_session_v1_add_listener (toplevel_session1,
                                       &toplevel_session_listener,
                                       &toplevel_state1);
  wl_surface_commit (toplevel1->wl_surface);

  while (!toplevel_state1.configured)
    wayland_display_dispatch (display1);
  g_assert_false (toplevel_state1.restored);

  display2 = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);
  test_state2 = g_new0 (TestDisplayState, 1);
  display2->test_state = test_state2;
  display2->destroy_test_state = g_free;

  registry2 = wl_display_get_registry (display2->display);
  wl_registry_add_listener (registry2, &registry_listener, display2);
  wl_display_roundtrip (display2->display);

  g_assert_nonnull (test_state2->session_manager);

  toplevel2 = wayland_surface_new (display2, "toplevel",
                                   100, 100, 0xff50ff50);
  g_signal_connect (toplevel2, "configure",
                    G_CALLBACK (on_toplevel_configured),
                    &toplevel_state2);

  session2 =
    xx_session_manager_v1_get_session (test_state2->session_manager,
                                       XX_SESSION_MANAGER_V1_REASON_LAUNCH,
                                       state1.id);
  xx_session_v1_add_listener (session2, &test_create_session_listener, &state2);

  while (!state2.received_restored)
    wayland_display_dispatch (display2);

  /* Test add before committing initial state. */
  toplevel_session2 = xx_session_v1_restore_toplevel (session2,
                                                      toplevel2->xdg_toplevel,
                                                      "toplevel");
  xx_toplevel_session_v1_add_listener (toplevel_session2,
                                       &toplevel_session_listener,
                                       &toplevel_state2);
  wl_surface_commit (toplevel2->wl_surface);

  while (!toplevel_state2.configured)
    wayland_display_dispatch (display2);

  /* check that the first client received the replaced event */
  while (!state1.received_replaced)
    wayland_display_dispatch (display1);

  /* TODO: check that client1 is now inert */

  return EXIT_SUCCESS;
}
