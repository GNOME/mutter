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
 *
 */

#include "config.h"

#include "backends/meta-backend-private.h"
#include "meta-test/meta-context-test.h"

static MetaContext *test_context;

static void
call_cb (GObject      *source_object,
         GAsyncResult *res,
         gpointer      user_data)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) ret = NULL;
  gboolean *done = user_data;

  ret = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object),
                                  res,
                                  &error);
  g_assert_no_error (error);

  *done = TRUE;
}

static void
set_inhibit_hw_curor_via_dbus (GDBusProxy *proxy,
                               gboolean    inhibit)
{
  gboolean done = FALSE;

  g_dbus_proxy_call (proxy,
                     "Set",
                     g_variant_new ("(ssv)",
                                    "org.gnome.Mutter.DebugControl",
                                    "InhibitHwCursor",
                                    g_variant_new_boolean (inhibit)),
                     G_DBUS_CALL_FLAGS_NO_AUTO_START,
                     -1,
                     NULL,
                     &call_cb,
                     &done);
  while (!done)
    g_main_context_iteration (NULL, TRUE);
}

static void
meta_test_debug_control_inhibit_hw_cursor (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  g_autoptr (GError) error = NULL;
  g_autoptr (GDBusProxy) proxy = NULL;
  g_autoptr (GVariant) ret = NULL;

  g_assert_false (meta_backend_is_hw_cursors_inhibited (backend));

  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                         G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                         NULL,
                                         "org.gnome.Mutter.DebugControl",
                                         "/org/gnome/Mutter/DebugControl",
                                         "org.freedesktop.DBus.Properties",
                                         NULL,
                                         &error);
  g_assert_nonnull (proxy);
  g_assert_no_error (error);

  set_inhibit_hw_curor_via_dbus (proxy, TRUE);
  g_assert_true (meta_backend_is_hw_cursors_inhibited (backend));
  set_inhibit_hw_curor_via_dbus (proxy, FALSE);
  g_assert_false (meta_backend_is_hw_cursors_inhibited (backend));
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;
  MetaDebugControl *debug_control;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_TEST,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  debug_control = meta_context_get_debug_control (context);
  meta_debug_control_set_exported (debug_control, TRUE);

  test_context = context;

  g_test_add_func ("/debug-control/inhibit-hw-cursor",
                   meta_test_debug_control_inhibit_hw_cursor);

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
