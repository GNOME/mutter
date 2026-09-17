/*
 * Copyright (C) 2026 Red Hat
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

#include "backends/native/meta-kms-constraints-list.h"

#include <gio/gio.h>

struct _MetaKmsConstraintsListEntry
{
  uint64_t id;
  gboolean selectable;
  MetaKmsConstraintsDescription *description;
};

struct _MetaKmsConstraintsList
{
  uint64_t generation;
  uint64_t selected_id;
  uint64_t suggested_id;
  MetaKmsConstraintsListEntry *entries;
  size_t n_entries;
};

static void
clear_entries (MetaKmsConstraintsListEntry *entries,
               size_t                       n_entries)
{
  size_t i;

  for (i = 0; i < n_entries; i++)
    meta_kms_constraints_description_free (entries[i].description);
  g_free (entries);
}

static gboolean
has_duplicate_id (const MetaKmsConstraintsListEntrySpec *entries,
                  size_t                                 index)
{
  size_t i;

  for (i = 0; i < index; i++)
    {
      if (entries[i].id == entries[index].id)
        return TRUE;
    }

  return FALSE;
}

static const MetaKmsConstraintsListEntrySpec *
find_entry_spec (const MetaKmsConstraintsListEntrySpec *entries,
                 size_t                                 n_entries,
                 uint64_t                               id)
{
  size_t i;

  for (i = 0; i < n_entries; i++)
    {
      if (entries[i].id == id)
        return &entries[i];
    }

  return NULL;
}

MetaKmsConstraintsList *
meta_kms_constraints_list_new (
  uint64_t                               generation,
  uint64_t                               selected_id,
  uint64_t                               suggested_id,
  const MetaKmsConstraintsListEntrySpec *entries,
  size_t                                 n_entries,
  GError                               **error)
{
  g_autofree MetaKmsConstraintsListEntry *entries_copy = NULL;
  MetaKmsConstraintsList *list;
  const MetaKmsConstraintsListEntrySpec *suggested_entry;
  size_t n_copied = 0;
  size_t i;

  if (generation == 0 ||
      selected_id == 0 ||
      !entries ||
      n_entries == 0)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Invalid KMS constraints list header");
      return NULL;
    }

  for (i = 0; i < n_entries; i++)
    {
      if (entries[i].id == 0 ||
          !entries[i].description ||
          has_duplicate_id (entries, i))
        {
          g_set_error_literal (error,
                               G_IO_ERROR,
                               G_IO_ERROR_INVALID_DATA,
                               "Invalid KMS constraints list entry");
          return NULL;
        }
    }

  if (!find_entry_spec (entries, n_entries, selected_id))
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Selected KMS constraints are not listed");
      return NULL;
    }

  suggested_entry = find_entry_spec (entries, n_entries, suggested_id);
  if (suggested_id != 0 &&
      (!suggested_entry || !suggested_entry->selectable))
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Suggested KMS constraints are not selectable");
      return NULL;
    }

  entries_copy = g_try_new0 (MetaKmsConstraintsListEntry, n_entries);
  if (!entries_copy)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NO_SPACE,
                           "Allocate KMS constraints list entries");
      return NULL;
    }

  for (i = 0; i < n_entries; i++)
    {
      entries_copy[i].id = entries[i].id;
      entries_copy[i].selectable = entries[i].selectable;
      entries_copy[i].description =
        meta_kms_constraints_description_copy (entries[i].description, error);
      if (!entries_copy[i].description)
        {
          clear_entries (g_steal_pointer (&entries_copy), n_copied);
          return NULL;
        }
      n_copied++;
    }

  list = g_try_new0 (MetaKmsConstraintsList, 1);
  if (!list)
    {
      clear_entries (g_steal_pointer (&entries_copy), n_copied);
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NO_SPACE,
                           "Allocate KMS constraints list");
      return NULL;
    }

  list->generation = generation;
  list->selected_id = selected_id;
  list->suggested_id = suggested_id;
  list->entries = g_steal_pointer (&entries_copy);
  list->n_entries = n_entries;

  return list;
}

void
meta_kms_constraints_list_free (MetaKmsConstraintsList *list)
{
  if (!list)
    return;

  clear_entries (list->entries, list->n_entries);
  g_free (list);
}

uint64_t
meta_kms_constraints_list_get_generation (const MetaKmsConstraintsList *list)
{
  return list->generation;
}

uint64_t
meta_kms_constraints_list_get_selected_id (const MetaKmsConstraintsList *list)
{
  return list->selected_id;
}

uint64_t
meta_kms_constraints_list_get_suggested_id (const MetaKmsConstraintsList *list)
{
  return list->suggested_id;
}

size_t
meta_kms_constraints_list_get_n_entries (const MetaKmsConstraintsList *list)
{
  return list->n_entries;
}

const MetaKmsConstraintsListEntry *
meta_kms_constraints_list_get_entry (const MetaKmsConstraintsList *list,
                                     size_t                        index)
{
  g_return_val_if_fail (index < list->n_entries, NULL);

  return &list->entries[index];
}

const MetaKmsConstraintsListEntry *
meta_kms_constraints_list_find_entry (const MetaKmsConstraintsList *list,
                                      uint64_t                      id)
{
  size_t i;

  for (i = 0; i < list->n_entries; i++)
    {
      if (list->entries[i].id == id)
        return &list->entries[i];
    }

  return NULL;
}

uint64_t
meta_kms_constraints_list_entry_get_id (
  const MetaKmsConstraintsListEntry *entry)
{
  return entry->id;
}

gboolean
meta_kms_constraints_list_entry_is_selectable (
  const MetaKmsConstraintsListEntry *entry)
{
  return entry->selectable;
}

const MetaKmsConstraintsDescription *
meta_kms_constraints_list_entry_get_description (
  const MetaKmsConstraintsListEntry *entry)
{
  return entry->description;
}
