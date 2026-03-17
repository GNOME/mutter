/*
 * Copyright (C) 2019-2026 Red Hat, Inc.
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

#include "tests/meta-wayland-test-runner.h"
#include "tests/meta-wayland-test-utils.h"

static gboolean
set_true (gpointer user_data)
{
  gboolean *done = user_data;

  *done = TRUE;

  return G_SOURCE_REMOVE;
}

static void
idle_inhibit_instant_destroy (void)
{
  MetaWaylandTestClient *wayland_test_client;
  gboolean done;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "idle-inhibit");
  meta_wayland_test_client_finish (wayland_test_client);

  done = FALSE;
  g_timeout_add_seconds (1, set_true, &done);
  while (!done)
    g_main_context_iteration (NULL, TRUE);
}

static void
init_tests (void)
{
  g_test_add_func ("/wayland/idle-inhibit/instant-destroy",
                   idle_inhibit_instant_destroy);
}

int
main (int   argc,
      char *argv[])
{
  meta_run_wayland_tests (argc, argv, init_tests);
}
