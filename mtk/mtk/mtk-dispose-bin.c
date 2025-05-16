/*
 * Mtk
 *
 * A low-level base library.
 *
 * Copyright (C) 2025 Red Hat
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

#include "mtk/mtk-dispose-bin.h"

typedef struct _DisposeEntry
{
  GDestroyNotify notify;
  gpointer user_data;
} DisposeEntry;

struct _MtkDisposeBin
{
  GArray *entries;
};

void
mtk_dispose_bin_add (MtkDisposeBin  *bin,
                     gpointer        user_data,
                     GDestroyNotify  notify)
{
  DisposeEntry entry = { .user_data = user_data, .notify = notify };

  g_return_if_fail (bin);

  g_array_append_val (bin->entries, entry);
}

MtkDisposeBin *
mtk_dispose_bin_new (void)
{
  MtkDisposeBin *bin;

  bin = g_new0 (MtkDisposeBin, 1);
  bin->entries = g_array_new (FALSE, FALSE, sizeof (DisposeEntry));

  return bin;
}

void
mtk_dispose_bin_dispose (MtkDisposeBin *bin)
{
  size_t i;

  for (i = 0; i < bin->entries->len; i++)
    {
      DisposeEntry *entry = &g_array_index (bin->entries, DisposeEntry, i);

      entry->notify (entry->user_data);
    }

  g_array_unref (bin->entries);
  g_free (bin);
}
