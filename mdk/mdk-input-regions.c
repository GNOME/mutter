/*
 * Copyright (C) 2025 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "mdk-input-regions.h"

#include "mdk-stream.h"

GHashTable *
mdk_process_regions (struct ei_device *ei_device)
{
  g_autoptr (GHashTable) regions = NULL;
  size_t i = 0;
  struct ei_region *ei_region;

  regions = g_hash_table_new_full (g_str_hash, g_str_equal,
                                   g_free, (GDestroyNotify) ei_region_unref);

  while ((ei_region = ei_device_get_region (ei_device, i++)))
    {
      const char *mapping_id;

      mapping_id = ei_region_get_mapping_id (ei_region);
      if (!mapping_id)
        continue;

      g_debug ("ei: New region: mapping-id: %s, %ux%u (%u, %u)",
               mapping_id,
               ei_region_get_width (ei_region), ei_region_get_height (ei_region),
               ei_region_get_x (ei_region), ei_region_get_y (ei_region));

      g_assert (ei_region_get_width (ei_region) > 0);
      g_assert (ei_region_get_height (ei_region) > 0);

      g_hash_table_insert (regions,
                           g_strdup (mapping_id),
                           ei_region_ref (ei_region));
    }

  return g_steal_pointer (&regions);
}

gboolean
mdk_transform_stream_position (MdkStream  *stream,
                               GHashTable *regions,
                               double     *x,
                               double     *y)
{
  struct ei_region *ei_region;

  ei_region = g_hash_table_lookup (regions, mdk_stream_get_mapping_id (stream));
  if (!ei_region)
    return FALSE;

  *x += ei_region_get_x (ei_region);
  *y += ei_region_get_y (ei_region);

  return TRUE;
}
