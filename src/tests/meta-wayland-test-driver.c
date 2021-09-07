/*
 * Copyright (C) 2019 Red Hat, Inc.
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

#include <wayland-server.h>

#include "wayland/meta-wayland-actor-surface.h"
#include "wayland/meta-wayland-private.h"

#include "test-driver-server-protocol.h"

enum
{
  SYNC_POINT,

  N_SIGNALS
};

static int signals[N_SIGNALS];

struct _MetaWaylandTestDriver
{
  GObject parent;

  struct wl_global *test_driver;

  GList *resources;
};

G_DEFINE_TYPE (MetaWaylandTestDriver, meta_wayland_test_driver,
               G_TYPE_OBJECT)

static void
on_actor_destroyed (ClutterActor       *actor,
                    struct wl_resource *callback)
{
  wl_callback_send_done (callback, 0);
  wl_resource_destroy (callback);
}

static void
sync_actor_destroy (struct wl_client   *client,
                    struct wl_resource *resource,
                    uint32_t            id,
                    struct wl_resource *surface_resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandActorSurface *actor_surface;
  MetaSurfaceActor *actor;
  struct wl_resource *callback;

  g_assert_nonnull (surface);

  actor_surface = (MetaWaylandActorSurface *) surface->role;
  g_assert_nonnull (actor_surface);

  actor = meta_wayland_actor_surface_get_actor (actor_surface);
  g_assert_nonnull (actor);

  callback = wl_resource_create (client, &wl_callback_interface, 1, id);

  g_signal_connect (actor, "destroy", G_CALLBACK (on_actor_destroyed),
                    callback);
}

static void
sync_point (struct wl_client   *client,
            struct wl_resource *resource,
            uint32_t            sequence,
            struct wl_resource *surface_resource)
{
  MetaWaylandTestDriver *test_driver = wl_resource_get_user_data (resource);

  g_signal_emit (test_driver, signals[SYNC_POINT], 0,
                 sequence,
                 surface_resource,
                 client);
}

static const struct test_driver_interface meta_test_driver_interface = {
  sync_actor_destroy,
  sync_point,
};

static void
test_driver_destructor (struct wl_resource *resource)
{
  MetaWaylandTestDriver *test_driver = wl_resource_get_user_data (resource);

  test_driver->resources = g_list_remove (test_driver->resources, resource);
}

static void
bind_test_driver (struct wl_client *client,
                  void             *user_data,
                  uint32_t          version,
                  uint32_t          id)
{
  MetaWaylandTestDriver *test_driver = user_data;
  struct wl_resource *resource;

  resource = wl_resource_create (client, &test_driver_interface,
                                 version, id);
  wl_resource_set_implementation (resource, &meta_test_driver_interface,
                                  test_driver, test_driver_destructor);

  test_driver->resources = g_list_prepend (test_driver->resources, resource);
}

static void
meta_wayland_test_driver_finalize (GObject *object)
{
  MetaWaylandTestDriver *test_driver = META_WAYLAND_TEST_DRIVER (object);

  g_clear_pointer (&test_driver->test_driver, wl_global_destroy);

  G_OBJECT_CLASS (meta_wayland_test_driver_parent_class)->finalize (object);
}

static void
meta_wayland_test_driver_class_init (MetaWaylandTestDriverClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_wayland_test_driver_finalize;

  signals[SYNC_POINT] =
    g_signal_new ("sync-point",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 3,
                  G_TYPE_UINT,
                  G_TYPE_POINTER,
                  G_TYPE_POINTER);
}

static void
meta_wayland_test_driver_init (MetaWaylandTestDriver *test_driver)
{
}

MetaWaylandTestDriver *
meta_wayland_test_driver_new (MetaWaylandCompositor *compositor)
{
  MetaWaylandTestDriver *test_driver;

  test_driver = g_object_new (META_TYPE_WAYLAND_TEST_DRIVER, NULL);
  test_driver->test_driver = wl_global_create (compositor->wayland_display,
                                               &test_driver_interface,
                                               1,
                                               test_driver, bind_test_driver);
  if (!test_driver->test_driver)
    g_error ("Failed to register a global wl-subcompositor object");

  return test_driver;
}

void
meta_wayland_test_driver_emit_sync_event (MetaWaylandTestDriver *test_driver,
                                          uint32_t               serial)
{
  GList *l;

  for (l = test_driver->resources; l; l = l->next)
    {
      struct wl_resource *resource = l->data;

      test_driver_send_sync_event (resource, serial);
    }
}
