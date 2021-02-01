/*
 * Copyright (C) 2021 Red Hat Inc.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#include "config.h"

#include "backends/meta-settings-private.h"
#include "backends/native/meta-backend-native.h"
#include "compositor/meta-plugin-manager.h"
#include "core/main-private.h"
#include "meta/main.h"
#include "meta/meta-backend.h"
#include "tests/native-screen-cast.h"
#include "tests/native-virtual-monitor.h"
#include "tests/test-utils.h"

static void
init_tests (void)
{
  init_virtual_monitor_tests ();
  init_screen_cast_tests ();
}

static gboolean
run_tests (gpointer data)
{
  MetaBackend *backend = meta_get_backend ();
  MetaSettings *settings = meta_backend_get_settings (backend);
  gboolean ret;

  meta_settings_override_experimental_features (settings);
  meta_settings_enable_experimental_feature (
    settings,
    META_EXPERIMENTAL_FEATURE_SCALE_MONITOR_FRAMEBUFFER);

  ret = g_test_run ();

  meta_quit (ret != 0);

  return FALSE;
}

int
main (int    argc,
      char **argv)
{
  test_init (&argc, &argv);
  init_tests ();

  meta_plugin_manager_load (test_get_plugin_name ());

  meta_override_compositor_configuration (META_COMPOSITOR_TYPE_WAYLAND,
                                          META_TYPE_BACKEND_NATIVE,
                                          "headless", TRUE,
                                          NULL);

  meta_init ();
  meta_register_with_session ();

  g_idle_add (run_tests, NULL);

  return meta_run ();
}
