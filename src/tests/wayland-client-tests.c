/*
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
 */

#include "config.h"

#include <wayland-client.h>

#include "meta-test/meta-context-test.h"
#include "wayland/meta-wayland-client-private.h"

static MetaContext *test_context;

static gpointer
test_client_destroyed_thread_func (gpointer user_data)
{
  int fd = GPOINTER_TO_INT (user_data);
  struct wl_display *wl_display;

  wl_display = wl_display_connect_to_fd (fd);
  g_assert_nonnull (wl_display);

  wl_display_roundtrip (wl_display);
  wl_display_disconnect (wl_display);

  return GINT_TO_POINTER (TRUE);
}

static void
on_client_destroyed (MetaWaylandClient *client,
                     gboolean          *client_destroyed)
{
  *client_destroyed = TRUE;
}

static void
meta_test_wayland_client_indirect_self_terminate (void)
{
  g_autoptr (MetaWaylandClient) client = NULL;
  g_autoptr (GError) error = NULL;
  GThread *thread;
  int fd;
  gboolean client_destroyed = FALSE;

  client = meta_wayland_client_new_create (test_context, getpid (), &error);
  g_assert_nonnull (client);
  g_assert_null (error);

  fd = meta_wayland_client_take_client_fd (client);
  g_assert_cmpint (fd, >=, 0);

  g_signal_connect (client, "client-destroyed",
                    G_CALLBACK (on_client_destroyed), &client_destroyed);

  thread = g_thread_new ("test client thread (self-terminated)",
                         test_client_destroyed_thread_func,
                         GINT_TO_POINTER (fd));

  g_debug ("Waiting for client to disconnect itself");

  while (!client_destroyed)
    g_main_context_iteration (NULL, TRUE);

  g_debug ("Waiting for thread to terminate");
  g_thread_join (thread);
}

typedef struct
{
  int fd;
  volatile gboolean round_tripped;
} DestroyTestData;

static gpointer
test_client_indefinite_thread_func (gpointer user_data)
{
  DestroyTestData *data = user_data;
  int fd = data->fd;
  struct wl_display *wl_display;

  wl_display = wl_display_connect_to_fd (fd);
  g_assert_nonnull (wl_display);

  wl_display_roundtrip (wl_display);
  g_atomic_int_set (&data->round_tripped, TRUE);

  while (TRUE)
    {
      if (wl_display_dispatch (wl_display) == -1)
        break;
    }

  wl_display_disconnect (wl_display);

  return GINT_TO_POINTER (TRUE);
}

static void
meta_test_wayland_client_indirect_destroy (void)
{
  DestroyTestData data;
  g_autoptr (MetaWaylandClient) client = NULL;
  g_autoptr (GError) error = NULL;
  GThread *thread;
  int fd;
  gboolean client_destroyed = FALSE;

  client = meta_wayland_client_new_create (test_context, getpid (), &error);
  g_assert_nonnull (client);
  g_assert_null (error);

  fd = meta_wayland_client_take_client_fd (client);
  g_assert_cmpint (fd, >=, 0);

  g_signal_connect (client, "client-destroyed",
                    G_CALLBACK (on_client_destroyed), &client_destroyed);

  data = (DestroyTestData) {
    .fd = fd,
    .round_tripped = FALSE,
  };
  thread = g_thread_new ("test client thread (indefinite)",
                         test_client_indefinite_thread_func,
                         &data);

  g_debug ("Waiting for client to round-trip");
  while (!g_atomic_int_get (&data.round_tripped))
    g_main_context_iteration (NULL, FALSE);

  g_debug ("Destroying client");
  meta_wayland_client_destroy (client);

  g_debug ("Waiting for client to terminate");
  while (!client_destroyed)
    g_main_context_iteration (NULL, TRUE);

  g_debug ("Waiting for thread to terminate");
  g_thread_join (thread);
}

static void
init_tests (void)
{
  g_test_add_func ("/wayland/client/indirect/self-terminate",
                   meta_test_wayland_client_indirect_self_terminate);
  g_test_add_func ("/wayland/client/indirect/destroy",
                   meta_test_wayland_client_indirect_destroy);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  test_context = context;

  init_tests ();

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_CAN_SKIP);
}
