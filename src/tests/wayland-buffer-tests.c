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

#include "tests/meta-wayland-test-driver.h"
#include "tests/meta-wayland-test-runner.h"
#include "tests/meta-wayland-test-utils.h"

static void
buffer_transform (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "buffer-transform");
  meta_wayland_test_client_finish (wayland_test_client);
}

static void
buffer_single_pixel_buffer (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "single-pixel-buffer");
  meta_wayland_test_client_finish (wayland_test_client);
}

static void
buffer_ycbcr_basic (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "ycbcr");
  meta_wayland_test_client_finish (wayland_test_client);
}

static void
buffer_shm_destroy_before_release (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "shm-destroy-before-release");

  meta_wayland_test_driver_wait_for_sync_point (test_driver, 0);
  meta_wayland_test_driver_emit_sync_event (test_driver, 0);

  meta_wayland_test_client_finish (wayland_test_client);
  g_test_assert_expected_messages ();
}

static void
init_tests (void)
{
  g_test_add_func ("/wayland/buffer/transform",
                   buffer_transform);
  g_test_add_func ("/wayland/buffer/single-pixel-buffer",
                   buffer_single_pixel_buffer);
  g_test_add_func ("/wayland/buffer/ycbcr-basic",
                   buffer_ycbcr_basic);
  g_test_add_func ("/wayland/buffer/shm-destroy-before-release",
                   buffer_shm_destroy_before_release);
}

int
main (int   argc,
      char *argv[])
{
  return meta_run_wayland_tests (argc, argv, init_tests);
}
