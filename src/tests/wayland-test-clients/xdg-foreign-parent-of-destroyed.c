/*
 * Copyright (C) 2026 Nathan Saslavsky
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

#include "xdg-foreign-unstable-v2-client-protocol.h"

static struct zxdg_exporter_v2 *exporter_v2;
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
handle_registry_global (void               *user_data,
                        struct wl_registry *registry,
                        uint32_t            id,
                        const char         *interface,
                        uint32_t            version)
{
  if (strcmp (interface, "zxdg_exporter_v2") == 0)
    {
      exporter_v2 = wl_registry_bind (registry, id,
                                      &zxdg_exporter_v2_interface, 1);
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
actor_destroyed (void               *data,
                 struct wl_callback *callback,
                 uint32_t            serial)
{
  gboolean *done = data;

  *done = TRUE;

  wl_callback_destroy (callback);
}

static const struct wl_callback_listener actor_destroy_listener = {
  actor_destroyed,
};

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  g_autoptr (WaylandSurface) parent = NULL;
  g_autoptr (WaylandSurface) child = NULL;
  g_autofree char *handle = NULL;
  struct wl_registry *registry;
  struct wl_callback *callback;
  struct zxdg_exported_v2 *exported;
  struct zxdg_imported_v2 *imported;
  gboolean child_actor_destroyed = FALSE;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);

  registry = wl_display_get_registry (display->display);
  wl_registry_add_listener (registry, &registry_listener, NULL);
  wl_display_roundtrip (display->display);

  g_assert_nonnull (exporter_v2);
  g_assert_nonnull (importer_v2);

  parent = wayland_surface_new (display, "xdg-foreign-parent",
                                100, 100, 0xff50ff50);
  child = wayland_surface_new (display, "xdg-foreign-child",
                               100, 100, 0xff0000ff);

  wl_surface_commit (parent->wl_surface);
  wl_surface_commit (child->wl_surface);
  wl_display_roundtrip (display->display);

  exported = zxdg_exporter_v2_export_toplevel (exporter_v2,
                                               parent->wl_surface);
  zxdg_exported_v2_add_listener (exported, &exported_v2_listener, &handle);

  while (!handle)
    wayland_display_dispatch (display);

  imported = zxdg_importer_v2_import_toplevel (importer_v2, handle);
  zxdg_imported_v2_set_parent_of (imported, child->wl_surface);

  test_driver_sync_point (display->test_driver, 0, NULL);
  wait_for_sync_event (display, 0);

  /*
   * Unmap the child. The compositor drops its reference to the child surface,
   * but the import itself stays alive and can be given a new child later, so
   * it must disconnect from the old one here rather than merely forgetting it.
   */
  callback = test_driver_sync_actor_destroyed (display->test_driver,
                                               child->wl_surface);
  wl_callback_add_listener (callback, &actor_destroy_listener,
                            &child_actor_destroyed);

  wl_surface_attach (child->wl_surface, NULL, 0, 0);
  wl_surface_commit (child->wl_surface);

  while (!child_actor_destroyed)
    wayland_display_dispatch (display);

  /* Destroy the import while it has no current child. */
  zxdg_imported_v2_destroy (imported);
  wl_display_roundtrip (display->display);

  /*
   * Now destroy the child surface. If the compositor still has signal handlers
   * connected to it from the import destroyed above, they fire against freed
   * memory here.
   */
  g_clear_object (&child);
  wl_display_roundtrip (display->display);

  test_driver_sync_point (display->test_driver, 1, NULL);
  wait_for_sync_event (display, 1);

  zxdg_exported_v2_destroy (exported);

  return EXIT_SUCCESS;
}
