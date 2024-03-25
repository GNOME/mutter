/*
 * Copyright (C) 2022 Red Hat Inc.
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

#include "xdg-foreign-unstable-v1-client-protocol.h"
#include "xdg-foreign-unstable-v2-client-protocol.h"

static struct zxdg_exporter_v1 *exporter_v1;
static struct zxdg_exporter_v2 *exporter_v2;
static struct zxdg_importer_v1 *importer_v1;
static struct zxdg_importer_v2 *importer_v2;

static void
handle_xdg_exported_v2_handle (void                    *data,
                               struct zxdg_exported_v2 *zxdg_exported_v2,
                               const char              *handle)
{
  char **handle_ptr = data;

  *handle_ptr = g_strdup (handle);
}

static const struct zxdg_exported_v2_listener exported_v2_listener = {
  handle_xdg_exported_v2_handle,
};

static void
handle_xdg_exported_v1_handle (void                    *data,
                               struct zxdg_exported_v1 *zxdg_exported_v1,
                               const char              *handle)
{
  char **handle_ptr = data;

  *handle_ptr = g_strdup (handle);
}

static const struct zxdg_exported_v1_listener exported_v1_listener = {
  handle_xdg_exported_v1_handle,
};

static void
handle_registry_global (void               *user_data,
                        struct wl_registry *registry,
                        uint32_t            id,
                        const char         *interface,
                        uint32_t            version)
{
  if (strcmp (interface, "zxdg_exporter_v1") == 0)
    {
      exporter_v1 = wl_registry_bind (registry, id,
                                      &zxdg_exporter_v1_interface, 1);
    }
  else if (strcmp (interface, "zxdg_exporter_v2") == 0)
    {
      exporter_v2 = wl_registry_bind (registry, id,
                                      &zxdg_exporter_v2_interface, 1);
    }
  else if (strcmp (interface, "zxdg_importer_v1") == 0)
    {
      importer_v1 = wl_registry_bind (registry, id,
                                      &zxdg_importer_v1_interface, 1);
    }
  else if (strcmp (interface, "zxdg_importer_v2") == 0)
    {
      importer_v2 = wl_registry_bind (registry, id,
                                      &zxdg_importer_v2_interface, 1);
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

static void
xdg_imported_v1_destroyed (void                    *data,
                           struct zxdg_imported_v1 *zxdg_imported_v1)
{
  gboolean *destroyed = data;

  *destroyed = TRUE;
}

static const struct zxdg_imported_v1_listener xdg_imported_v1_listener = {
  xdg_imported_v1_destroyed,
};

static void
xdg_imported_v2_destroyed (void                    *data,
                           struct zxdg_imported_v2 *zxdg_imported_v2)
{
  gboolean *destroyed = data;

  *destroyed = TRUE;
}

static const struct zxdg_imported_v2_listener xdg_imported_v2_listener = {
  xdg_imported_v2_destroyed,
};

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  g_autoptr (WaylandSurface) window1 = NULL;
  g_autoptr (WaylandSurface) window2 = NULL;
  g_autoptr (WaylandSurface) window3 = NULL;
  g_autoptr (WaylandSurface) window4 = NULL;
  g_autofree char *handle1 = NULL;
  g_autofree char *handle3 = NULL;
  struct wl_registry *registry;
  struct zxdg_exported_v1 *exported1; /* for window1 */
  struct zxdg_exported_v2 *exported3; /* for window2 */
  struct zxdg_imported_v2 *imported1; /* for window1 */
  struct zxdg_imported_v1 *imported3; /* for window2 */
  gboolean imported1_destroyed = FALSE;
  gboolean imported3_destroyed = FALSE;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);

  registry = wl_display_get_registry (display->display);
  wl_registry_add_listener (registry, &registry_listener, NULL);
  wl_display_roundtrip (display->display);

  g_assert_nonnull (exporter_v1);
  g_assert_nonnull (exporter_v2);
  g_assert_nonnull (importer_v1);
  g_assert_nonnull (importer_v2);

  window1 = wayland_surface_new (display, "xdg-foreign-window1",
                                 100, 100, 0xff50ff50);
  window2 = wayland_surface_new (display, "xdg-foreign-window2",
                                 100, 100, 0xff0000ff);
  window3 = wayland_surface_new (display, "xdg-foreign-window3",
                                 100, 100, 0xff2020ff);
  window4 = wayland_surface_new (display, "xdg-foreign-window4",
                                 100, 100, 0xff40ffff);

  exported1 = zxdg_exporter_v1_export (exporter_v1, window1->wl_surface);
  zxdg_exported_v1_add_listener (exported1, &exported_v1_listener, &handle1);

  exported3 = zxdg_exporter_v2_export_toplevel (exporter_v2,
                                                window3->wl_surface);
  zxdg_exported_v2_add_listener (exported3, &exported_v2_listener, &handle3);

  while (!handle1 && !handle3)
    wayland_display_dispatch (display);

  zxdg_importer_v2_import_toplevel (importer_v2, "don't crash on bogus handle");
  zxdg_importer_v1_import (importer_v1, "don't crash on bogus handle");

  imported1 = zxdg_importer_v2_import_toplevel (importer_v2, handle1);
  zxdg_imported_v2_add_listener (imported1, &xdg_imported_v2_listener,
                                 &imported1_destroyed);
  imported3 = zxdg_importer_v1_import (importer_v1, handle3);
  zxdg_imported_v1_add_listener (imported3, &xdg_imported_v1_listener,
                                 &imported3_destroyed);

  /*
   *  +------+
   *  | W1 +------+
   *  |    | W2 +------+
   *  |    |    | W3 +----+
   *  |    |    |    | W4 |
   *  +----+----+----+----+
   *    ^         ^
   *    |_ exported with v1, imported with v2
   *              |__ exported with v2, imported with v1
   */

  zxdg_imported_v2_set_parent_of (imported1, window2->wl_surface);
  xdg_toplevel_set_parent (window3->xdg_toplevel,
                           window2->xdg_toplevel);
  zxdg_imported_v1_set_parent_of (imported3, window4->wl_surface);

  wl_surface_commit (window1->wl_surface);
  wl_surface_commit (window2->wl_surface);
  wl_surface_commit (window3->wl_surface);
  wl_surface_commit (window4->wl_surface);

  test_driver_sync_point (display->test_driver, 0, NULL);

  wait_for_sync_event (display, 0);

  zxdg_exported_v1_destroy (exported1);
  zxdg_exported_v2_destroy (exported3);

  while (!imported1_destroyed || !imported3_destroyed)
    wayland_display_dispatch (display);

  return EXIT_SUCCESS;
}
