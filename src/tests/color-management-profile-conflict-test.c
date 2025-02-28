/*
 * Copyright (C) 2022 Red Hat
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

#include <fcntl.h>

#include "backends/meta-color-manager-private.h"
#include "backends/meta-color-profile.h"
#include "backends/meta-color-store.h"
#include "meta-test/meta-context-test.h"
#include "tests/meta-monitor-test-utils.h"

#define VX239_ICC_PROFILE_ID "icc-c5e479355c02452dd30c1256a154a8f4"

static MetaContext *test_context;

static GDBusProxy *
get_colord_mock_proxy (void)
{
  GDBusProxy *proxy;
  g_autoptr (GError) error = NULL;

  proxy =
    g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                   G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START |
                                   G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                   NULL,
                                   "org.freedesktop.ColorManager",
                                   "/org/freedesktop/ColorManager",
                                   "org.freedesktop.DBus.Mock",
                                   NULL, &error);
  if (!proxy)
    {
      g_error ("Failed to find mocked color manager system service, %s",
               error->message);
    }

  return proxy;
}

static void
add_colord_system_profile (const char *cd_profile_id,
                           const char *file_path)
{
  GDBusProxy *proxy;
  g_autoptr (GError) error = NULL;
  GVariantBuilder params_builder;
  g_autoptr (GVariant) ret = NULL;

  proxy = get_colord_mock_proxy ();

  g_variant_builder_init (&params_builder, G_VARIANT_TYPE ("(ss)"));
  g_variant_builder_add (&params_builder, "s", cd_profile_id);
  g_variant_builder_add (&params_builder, "s", file_path);

  ret = g_dbus_proxy_call_sync (proxy,
                                "AddSystemProfile",
                                g_variant_builder_end (&params_builder),
                                G_DBUS_CALL_FLAGS_NO_AUTO_START, -1, NULL,
                                &error);
  if (ret == NULL)
    g_error ("Failed to add system profile: %s", error->message);
}

static void
meta_test_color_profile_conflicts (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaColorManager *color_manager =
    meta_backend_get_color_manager (backend);
  MetaColorStore *color_store;

  while (!meta_color_manager_is_ready (color_manager))
    g_main_context_iteration (NULL, TRUE);

  color_store = meta_color_manager_get_color_store (color_manager);
  while (meta_color_store_has_pending_profiles (color_store))
    g_main_context_iteration (NULL, TRUE);

  g_assert_null (meta_color_store_get_profile (color_store,
                                               VX239_ICC_PROFILE_ID));
}

static void
init_tests (void)
{
  g_test_add_func ("/color-manegement/profile/conflict",
                   meta_test_color_profile_conflicts);
}

int
main (int argc, char **argv)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GFile) source_file = NULL;
  g_autoptr (GFile) dest_file = NULL;
  g_autoptr (MetaContext) context = NULL;
  g_autofree char *system_profile_path = NULL;
  g_autofree char *dest_dir = NULL;
  g_autofree char *dest_path = NULL;
  const char *data_home_path = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_NONE);

  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  system_profile_path = g_test_build_filename (G_TEST_DIST,
                                               "icc-profiles",
                                               "vx239-calibrated.icc",
                                               NULL);
  add_colord_system_profile (VX239_ICC_PROFILE_ID, system_profile_path);

  data_home_path = g_getenv ("XDG_DATA_HOME");
  g_assert_nonnull (data_home_path);

  dest_dir = g_build_filename (data_home_path,
                               "icc",
                               NULL);
  g_assert_no_errno (g_mkdir_with_parents (dest_dir, 0755));
  dest_path = g_build_filename (dest_dir, "vx239-calibrated.icc", NULL);
  source_file = g_file_new_for_path (system_profile_path);
  dest_file = g_file_new_for_path (dest_path);
  g_file_copy (source_file, dest_file, G_FILE_COPY_NONE, NULL, NULL, NULL, &error);
  g_assert_no_error (error);

  test_context = context;

  init_tests ();

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
