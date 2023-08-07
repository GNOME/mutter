/*
 * Copyright 2023 Red Hat
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

#include "wayland/meta-wayland-filter-manager.h"
#include "wayland/meta-wayland.h"

struct _MetaWaylandFilterManager
{
  GHashTable *filters;
};

typedef struct _MetaWaylandFilter
{
  MetaWaylandFilterFunc func;
  gpointer user_data;
} MetaWaylandFilter;

static bool
global_filter_func (const struct wl_client *client,
                    const struct wl_global *global,
                    void                   *user_data)
{
  MetaWaylandFilterManager *filter_manager = user_data;
  MetaWaylandFilter *filter;

  filter = g_hash_table_lookup (filter_manager->filters, global);
  if (!filter)
    return true;

  switch (filter->func (client, global, filter->user_data))
    {
    case META_WAYLAND_ACCESS_ALLOWED:
      return true;
    case META_WAYLAND_ACCESS_DENIED:
      return false;
    }

  g_assert_not_reached ();
}

MetaWaylandFilterManager *
meta_wayland_filter_manager_new (MetaWaylandCompositor *compositor)
{
  struct wl_display *wayland_display =
    meta_wayland_compositor_get_wayland_display (compositor);
  MetaWaylandFilterManager *filter_manager;

  filter_manager = g_new0 (MetaWaylandFilterManager, 1);
  filter_manager->filters = g_hash_table_new_full (NULL, NULL, NULL, g_free);
  wl_display_set_global_filter (wayland_display,
                                global_filter_func, filter_manager);

  return filter_manager;
}

void
meta_wayland_filter_manager_free (MetaWaylandFilterManager *filter_manager)
{
  g_hash_table_unref (filter_manager->filters);
  g_free (filter_manager);
}

void
meta_wayland_filter_manager_add_global (MetaWaylandFilterManager *filter_manager,
                                        struct wl_global         *global,
                                        MetaWaylandFilterFunc     filter_func,
                                        gpointer                  user_data)
{
  MetaWaylandFilter *filter;

  g_return_if_fail (!g_hash_table_lookup (filter_manager->filters, global));

  filter = g_new0 (MetaWaylandFilter, 1);
  filter->func = filter_func;
  filter->user_data = user_data;

  g_hash_table_insert (filter_manager->filters, global, filter);
}

void
meta_wayland_filter_manager_remove_global (MetaWaylandFilterManager *filter_manager,
                                           struct wl_global         *global)
{
  g_hash_table_remove (filter_manager->filters, global);
}
