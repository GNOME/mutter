/*
 * Copyright (C) 2016-2025 Red Hat, Inc.
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

#include "backends/meta-monitor-config-store.h"
#include "tests/monitor-tests-common.h"

static void
meta_test_monitor_config_store_set_current_on_empty (void)
{
  g_autoptr (MetaMonitorsConfig) linear_config = NULL;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaMonitorsConfig *old_current;

  linear_config = meta_monitor_config_manager_create_linear (config_manager);
  old_current = meta_monitor_config_manager_get_current (config_manager);

  g_assert_null (old_current);
  g_assert_nonnull (linear_config);

  meta_monitor_config_manager_set_current (config_manager, linear_config);

  g_assert_true (meta_monitor_config_manager_get_current (config_manager) ==
                 linear_config);
  g_assert_true (meta_monitor_config_manager_get_current (config_manager) !=
                 old_current);
  g_assert_null (meta_monitor_config_manager_get_previous (config_manager));
  g_assert_null (meta_monitor_config_manager_pop_previous (config_manager));
}

static void
meta_test_monitor_config_store_set_current_with_parent_on_empty (void)
{
  g_autoptr (MetaMonitorsConfig) parent_config = NULL;
  g_autoptr (MetaMonitorsConfig) child_config1 = NULL;
  g_autoptr (MetaMonitorsConfig) child_config2 = NULL;
  g_autoptr (MetaMonitorsConfig) child_config3 = NULL;
  g_autoptr (MetaMonitorsConfig) linear_config = NULL;
  g_autoptr (MetaMonitorsConfig) fallback_config = NULL;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaMonitorsConfig *old_current;

  parent_config = meta_monitor_config_manager_create_linear (config_manager);

  child_config1 = meta_monitor_config_manager_create_linear (config_manager);
  meta_monitors_config_set_parent_config (child_config1, parent_config);
  old_current = meta_monitor_config_manager_get_current (config_manager);

  g_assert_null (old_current);
  g_assert_nonnull (child_config1);

  meta_monitor_config_manager_set_current (config_manager, child_config1);

  g_assert_true (meta_monitor_config_manager_get_current (config_manager) ==
                 child_config1);
  g_assert_true (meta_monitor_config_manager_get_current (config_manager) !=
                 old_current);
  g_assert_null (meta_monitor_config_manager_get_previous (config_manager));
  g_assert_null (meta_monitor_config_manager_pop_previous (config_manager));

  child_config2 = meta_monitor_config_manager_create_linear (config_manager);
  meta_monitors_config_set_parent_config (child_config2, parent_config);
  g_assert_true (child_config2->parent_config == parent_config);

  old_current = meta_monitor_config_manager_get_current (config_manager);
  g_assert_nonnull (old_current->parent_config);
  meta_monitor_config_manager_set_current (config_manager, child_config2);

  g_assert_true (meta_monitor_config_manager_get_current (config_manager) ==
                 child_config2);
  g_assert_true (meta_monitor_config_manager_get_current (config_manager) !=
                 old_current);
  g_assert_null (meta_monitor_config_manager_get_previous (config_manager));
  g_assert_null (meta_monitor_config_manager_pop_previous (config_manager));

  child_config3 = meta_monitor_config_manager_create_linear (config_manager);
  meta_monitors_config_set_parent_config (child_config3, child_config2);

  old_current = meta_monitor_config_manager_get_current (config_manager);
  g_assert_nonnull (old_current->parent_config);
  meta_monitor_config_manager_set_current (config_manager, child_config3);

  g_assert_true (meta_monitor_config_manager_get_current (config_manager) ==
                 child_config3);
  g_assert_true (meta_monitor_config_manager_get_current (config_manager) !=
                 old_current);
  g_assert_null (meta_monitor_config_manager_get_previous (config_manager));
  g_assert_null (meta_monitor_config_manager_pop_previous (config_manager));

  linear_config = meta_monitor_config_manager_create_linear (config_manager);
  g_assert_null (linear_config->parent_config);

  old_current = meta_monitor_config_manager_get_current (config_manager);
  g_assert_nonnull (old_current->parent_config);
  meta_monitor_config_manager_set_current (config_manager, linear_config);

  g_assert_true (meta_monitor_config_manager_get_current (config_manager) ==
                 linear_config);
  g_assert_true (meta_monitor_config_manager_get_current (config_manager) !=
                 old_current);
  g_assert_true (meta_monitor_config_manager_get_previous (config_manager) ==
                 child_config3);

  fallback_config =
    meta_monitor_config_manager_create_fallback (config_manager);
  g_assert_null (fallback_config->parent_config);

  old_current = meta_monitor_config_manager_get_current (config_manager);
  g_assert_null (old_current->parent_config);
  meta_monitor_config_manager_set_current (config_manager, fallback_config);

  g_assert_true (meta_monitor_config_manager_get_current (config_manager) ==
                 fallback_config);
  g_assert_true (meta_monitor_config_manager_get_current (config_manager) !=
                 old_current);

  g_assert_true (meta_monitor_config_manager_get_previous (config_manager) ==
                 linear_config);
  g_assert_true (meta_monitor_config_manager_pop_previous (config_manager) ==
                 linear_config);
  g_assert_true (meta_monitor_config_manager_get_previous (config_manager) ==
                 child_config3);
  g_assert_true (meta_monitor_config_manager_pop_previous (config_manager) ==
                 child_config3);
  g_assert_null (meta_monitor_config_manager_get_previous (config_manager));
  g_assert_null (meta_monitor_config_manager_pop_previous (config_manager));
}

static void
meta_test_monitor_config_store_set_current (void)
{
  g_autoptr (MetaMonitorsConfig) linear_config = NULL;
  g_autoptr (MetaMonitorsConfig) fallback_config = NULL;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaMonitorsConfig *old_current;

  fallback_config =
    meta_monitor_config_manager_create_fallback (config_manager);
  linear_config = meta_monitor_config_manager_create_linear (config_manager);

  g_assert_nonnull (linear_config);
  g_assert_nonnull (fallback_config);

  meta_monitor_config_manager_set_current (config_manager, fallback_config);
  g_assert_true (meta_monitor_config_manager_get_current (config_manager) ==
                 fallback_config);

  old_current = meta_monitor_config_manager_get_current (config_manager);
  meta_monitor_config_manager_set_current (config_manager, linear_config);

  g_assert_true (old_current != linear_config);
  g_assert_nonnull (old_current);
  g_assert_true (meta_monitor_config_manager_get_current (config_manager) ==
                 linear_config);
  g_assert_true (meta_monitor_config_manager_get_previous (config_manager) ==
                 old_current);
  g_assert_true (meta_monitor_config_manager_pop_previous (config_manager) ==
                 old_current);

  g_assert_null (meta_monitor_config_manager_get_previous (config_manager));
  g_assert_null (meta_monitor_config_manager_pop_previous (config_manager));
}

static void
meta_test_monitor_config_store_set_current_with_parent (void)
{
  g_autoptr (MetaMonitorsConfig) child_config = NULL;
  g_autoptr (MetaMonitorsConfig) other_child = NULL;
  g_autoptr (MetaMonitorsConfig) linear_config = NULL;
  g_autoptr (MetaMonitorsConfig) fallback_config = NULL;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaMonitorsConfig *old_current;

  linear_config = meta_monitor_config_manager_create_linear (config_manager);
  g_assert_null (linear_config->parent_config);

  old_current = meta_monitor_config_manager_get_current (config_manager);
  g_assert_null (old_current);
  meta_monitor_config_manager_set_current (config_manager, linear_config);

  g_assert_true (meta_monitor_config_manager_get_current (config_manager) ==
                 linear_config);
  g_assert_true (meta_monitor_config_manager_get_current (config_manager) !=
                 old_current);
  g_assert_null (meta_monitor_config_manager_get_previous (config_manager));
  g_assert_null (meta_monitor_config_manager_pop_previous (config_manager));

  fallback_config = meta_monitor_config_manager_create_fallback (config_manager);
  g_assert_nonnull (fallback_config);
  g_assert_null (fallback_config->parent_config);

  old_current = meta_monitor_config_manager_get_current (config_manager);
  g_assert_nonnull (old_current);
  g_assert_null (old_current->parent_config);
  meta_monitor_config_manager_set_current (config_manager, fallback_config);

  g_assert_true (meta_monitor_config_manager_get_current (config_manager) ==
                 fallback_config);
  g_assert_true (meta_monitor_config_manager_get_current (config_manager) !=
                 old_current);
  g_assert_true (meta_monitor_config_manager_get_previous (config_manager) ==
                 old_current);

  child_config = meta_monitor_config_manager_create_linear (config_manager);
  old_current = meta_monitor_config_manager_get_current (config_manager);
  meta_monitors_config_set_parent_config (child_config, old_current);

  g_assert_nonnull (child_config);
  g_assert_nonnull (old_current);
  g_assert_true (old_current == fallback_config);
  g_assert_null (old_current->parent_config);

  meta_monitor_config_manager_set_current (config_manager, child_config);

  g_assert_true (meta_monitor_config_manager_get_current (config_manager) ==
                 child_config);
  g_assert_true (meta_monitor_config_manager_get_current (config_manager) !=
                 old_current);
  g_assert_true (meta_monitor_config_manager_get_previous (config_manager) ==
                 linear_config);

  other_child = meta_monitor_config_manager_create_linear (config_manager);
  meta_monitors_config_set_parent_config (other_child, old_current);

  old_current = meta_monitor_config_manager_get_current (config_manager);
  g_assert_nonnull (old_current->parent_config);
  g_assert_true (old_current == child_config);
  meta_monitor_config_manager_set_current (config_manager, other_child);

  g_assert_true (meta_monitor_config_manager_get_current (config_manager) ==
                 other_child);
  g_assert_true (meta_monitor_config_manager_get_current (config_manager) !=
                 old_current);
  g_assert_true (meta_monitor_config_manager_get_previous (config_manager) ==
                 linear_config);
  g_assert_true (meta_monitor_config_manager_pop_previous (config_manager) ==
                 linear_config);

  g_assert_null (meta_monitor_config_manager_get_previous (config_manager));
  g_assert_null (meta_monitor_config_manager_pop_previous (config_manager));
}

static void
meta_test_monitor_config_store_set_current_max_size (void)
{
  /* Keep this in sync with CONFIG_HISTORY_MAX_SIZE */
  const unsigned int config_history_max_size = 3;
  g_autolist (MetaMonitorsConfig) added = NULL;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaMonitorsConfig *previous = NULL;
  MetaMonitorsConfig *config;
  unsigned int i;

  for (i = 0; i < config_history_max_size; i++)
    {
      g_autoptr (MetaMonitorsConfig) linear_config = NULL;

      linear_config = meta_monitor_config_manager_create_linear (config_manager);
      g_assert_nonnull (linear_config);
      g_assert_false (g_list_find (added, linear_config));

      if (i > 0)
        {
          g_assert_true (previous !=
                         meta_monitor_config_manager_get_current (config_manager));
        }

      previous = meta_monitor_config_manager_get_current (config_manager);
      meta_monitor_config_manager_set_current (config_manager, linear_config);
      added = g_list_prepend (added, g_object_ref (linear_config));

      g_assert_true (meta_monitor_config_manager_get_current (config_manager)
                     == linear_config);

      g_assert_true (meta_monitor_config_manager_get_previous (config_manager)
                     == previous);
    }

  for (i = 0; i < config_history_max_size - 1; i++)
    {
      g_autoptr (MetaMonitorsConfig) fallback = NULL;

      fallback = meta_monitor_config_manager_create_fallback (config_manager);
      g_assert_nonnull (fallback);

      meta_monitor_config_manager_set_current (config_manager, fallback);
      added = g_list_prepend (added, g_steal_pointer (&fallback));
    }

  g_assert_cmpuint (g_list_length (added), >, config_history_max_size);

  config = meta_monitor_config_manager_get_current (config_manager);
  g_assert_true (config == g_list_nth_data (added, 0));

  for (i = 0; i < config_history_max_size; i++)
    {
      config = meta_monitor_config_manager_get_previous (config_manager);
      g_assert_nonnull (config);
      g_assert_true (meta_monitor_config_manager_pop_previous (config_manager)
                     == config);
      g_assert_true (config == g_list_nth_data (added, i + 1));
    }

  config = meta_monitor_config_manager_get_previous (config_manager);
  g_assert_null (config);
  g_assert_null (meta_monitor_config_manager_pop_previous (config_manager));
  g_assert_true (config != g_list_nth_data (added, config_history_max_size));
  g_assert_nonnull (g_list_nth_data (added, config_history_max_size + 1));
}

static void
meta_test_monitor_config_store_set_current_null (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaMonitorsConfig *previous;

  previous = meta_monitor_config_manager_get_current (config_manager);
  g_assert_null (previous);

  meta_monitor_config_manager_set_current (config_manager, NULL);

  g_assert_null (meta_monitor_config_manager_get_current (config_manager));
  g_assert_null (meta_monitor_config_manager_get_previous (config_manager));
  g_assert_null (meta_monitor_config_manager_pop_previous (config_manager));
}

static void
init_storage_tests (void)
{
  meta_add_monitor_test ("/backends/monitor/config-store/set-current-on-empty",
                         meta_test_monitor_config_store_set_current_on_empty);
  meta_add_monitor_test ("/backends/monitor/config-store/set-current-with-parent-on-empty",
                         meta_test_monitor_config_store_set_current_with_parent_on_empty);
  meta_add_monitor_test ("/backends/monitor/config-store/set-current",
                         meta_test_monitor_config_store_set_current);
  meta_add_monitor_test ("/backends/monitor/config-store/set-current-with-parent",
                         meta_test_monitor_config_store_set_current_with_parent);
  meta_add_monitor_test ("/backends/monitor/config-store/set-current-max-size",
                         meta_test_monitor_config_store_set_current_max_size);
  meta_add_monitor_test ("/backends/monitor/config-store/set-current-null",
                         meta_test_monitor_config_store_set_current_null);
}

int
main (int   argc,
      char *argv[])
{
  return meta_monitor_test_main (argc, argv, init_storage_tests);
}
