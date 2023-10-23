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

#include "compositor/meta-window-actor-private.h"
#include "tests/meta-ref-test.h"
#include "wayland/meta-wayland-actor-surface.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-surface-private.h"

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

  MetaWaylandCompositor *compositor;

  struct wl_global *test_driver;

  GList *resources;

  GHashTable *properties;
};

G_DEFINE_TYPE (MetaWaylandTestDriver, meta_wayland_test_driver,
               G_TYPE_OBJECT)

typedef struct _PendingEffectsData
{
  MetaWaylandSurface *surface;
  struct wl_resource *callback;
} PendingEffectsData;

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
on_effects_completed (ClutterActor       *actor,
                      struct wl_resource *callback)
{
  g_signal_handlers_disconnect_by_data (actor, callback);
  wl_callback_send_done (callback, 0);
  wl_resource_destroy (callback);
}

static void
check_for_pending_effects (ClutterStage       *stage,
                           ClutterStageView   *view,
                           ClutterFrame       *frame,
                           PendingEffectsData *data)
{
  MetaWindow *window;
  MetaWindowActor *window_actor;

  g_signal_handlers_disconnect_by_data (stage, data);

  window = meta_wayland_surface_get_window (data->surface);
  g_assert_nonnull (window);

  window_actor = meta_window_actor_from_window (window);
  g_assert_nonnull (window_actor);

  if (meta_window_actor_effect_in_progress (window_actor))
    {
      g_signal_connect (window_actor, "effects-completed",
                        G_CALLBACK (on_effects_completed), data->callback);
    }
  else
    {
      on_effects_completed (CLUTTER_ACTOR (window_actor), data->callback);
    }

  g_free (data);
}

static void
sync_effects_completed (struct wl_client   *client,
                        struct wl_resource *resource,
                        uint32_t            id,
                        struct wl_resource *surface_resource)
{
  MetaWaylandTestDriver *test_driver = wl_resource_get_user_data (resource);
  MetaContext *context =
    meta_wayland_compositor_get_context (test_driver->compositor);
  MetaBackend *backend = meta_context_get_backend (context);
  ClutterActor *stage = meta_backend_get_stage (backend);
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  PendingEffectsData *data;
  GList *stage_views;

  g_assert_nonnull (surface);

  data = g_new0 (PendingEffectsData, 1);
  data->surface = surface;
  data->callback = wl_resource_create (client, &wl_callback_interface, 1, id);

  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));
  g_assert (g_list_length (stage_views) > 0);

  g_signal_connect (CLUTTER_STAGE (stage), "after-update",
                    G_CALLBACK (check_for_pending_effects), data);

  clutter_stage_schedule_update (CLUTTER_STAGE (stage));
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

static void
on_after_paint (ClutterStage       *stage,
                ClutterStageView   *view,
                ClutterFrame       *frame,
                struct wl_resource *callback)
{
  g_signal_handlers_disconnect_by_data (stage, callback);
  wl_callback_send_done (callback, 0);
  wl_resource_destroy (callback);
}

static void
verify_view (struct wl_client   *client,
             struct wl_resource *resource,
             uint32_t            id,
             uint32_t            sequence)
{
  MetaWaylandTestDriver *test_driver = wl_resource_get_user_data (resource);
  MetaContext *context =
    meta_wayland_compositor_get_context (test_driver->compositor);
  MetaBackend *backend = meta_context_get_backend (context);
  ClutterActor *stage = meta_backend_get_stage (backend);
  GList *stage_views;
  struct wl_resource *callback;

  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));
  g_assert (g_list_length (stage_views) > 0);

  callback = wl_resource_create (client, &wl_callback_interface, 1, id);
  g_signal_connect_after (CLUTTER_STAGE (stage), "after-paint",
                          G_CALLBACK (on_after_paint), callback);

  meta_ref_test_verify_view (CLUTTER_STAGE_VIEW (stage_views->data),
                             g_test_get_path (),
                             sequence,
                             meta_ref_test_determine_ref_test_flag ());
}

static const struct test_driver_interface meta_test_driver_interface = {
  sync_actor_destroy,
  sync_effects_completed,
  sync_point,
  verify_view,
};

static void
test_driver_destructor (struct wl_resource *resource)
{
  MetaWaylandTestDriver *test_driver;

  test_driver = wl_resource_get_user_data (resource);
  if (!test_driver)
    return;

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
  GHashTableIter iter;
  gpointer key, value;

  resource = wl_resource_create (client, &test_driver_interface,
                                 version, id);
  wl_resource_set_implementation (resource, &meta_test_driver_interface,
                                  test_driver, test_driver_destructor);

  test_driver->resources = g_list_prepend (test_driver->resources, resource);

  g_hash_table_iter_init (&iter, test_driver->properties);
  while (g_hash_table_iter_next (&iter, &key, &value))
    test_driver_send_property (resource, key, value);
}

static void
meta_wayland_test_driver_finalize (GObject *object)
{
  MetaWaylandTestDriver *test_driver = META_WAYLAND_TEST_DRIVER (object);
  GList *l;

  for (l = test_driver->resources; l; l = l->next)
    {
      struct wl_resource *resource = l->data;

      wl_resource_set_user_data (resource, NULL);
    }
  g_clear_list (&test_driver->resources, NULL);

  g_clear_pointer (&test_driver->test_driver, wl_global_destroy);
  g_clear_pointer (&test_driver->properties, g_hash_table_unref);

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
  test_driver->properties = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                   g_free, g_free);
}

MetaWaylandTestDriver *
meta_wayland_test_driver_new (MetaWaylandCompositor *compositor)
{
  MetaWaylandTestDriver *test_driver;

  test_driver = g_object_new (META_TYPE_WAYLAND_TEST_DRIVER, NULL);
  test_driver->compositor = compositor;
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

void
meta_wayland_test_driver_set_property (MetaWaylandTestDriver *test_driver,
                                       const char            *name,
                                       const char            *value)
{
  g_hash_table_replace (test_driver->properties,
                        g_strdup (name),
                        g_strdup (value));
}

static void
on_sync_point (MetaWaylandTestDriver *test_driver,
               unsigned int           sequence,
               struct wl_resource    *surface_resource,
               struct wl_client      *wl_client,
               unsigned int          *latest_sequence)
{
  *latest_sequence = sequence;
}

void
meta_wayland_test_driver_wait_for_sync_point (MetaWaylandTestDriver *test_driver,
                                              unsigned int           sync_point)
{
  gulong handler_id;
  unsigned int latest_sequence = sync_point - 1;

  handler_id = g_signal_connect (test_driver, "sync-point",
                                 G_CALLBACK (on_sync_point),
                                 &latest_sequence);
  while (latest_sequence != sync_point)
    g_main_context_iteration (NULL, TRUE);
  g_signal_handler_disconnect (test_driver, handler_id);
}
