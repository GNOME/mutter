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

#pragma once

#include <glib.h>
#include <stddef.h>
#include <stdint.h>

#include "backends/native/meta-kms-constraints.h"

typedef struct _MetaKmsConstraintsList MetaKmsConstraintsList;
typedef struct _MetaKmsConstraintsListEntry MetaKmsConstraintsListEntry;

typedef struct _MetaKmsConstraintsListEntrySpec
{
  uint64_t id;
  gboolean selectable;
  MetaKmsConstraintsDescription *description;
} MetaKmsConstraintsListEntrySpec;

MetaKmsConstraintsList *
meta_kms_constraints_list_new (
  uint64_t                               generation,
  uint64_t                               selected_id,
  uint64_t                               suggested_id,
  const MetaKmsConstraintsListEntrySpec *entries,
  size_t                                 n_entries,
  GError                               **error);

MetaKmsConstraintsList *
meta_kms_constraints_list_ref (MetaKmsConstraintsList *list);

void meta_kms_constraints_list_unref (MetaKmsConstraintsList *list);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaKmsConstraintsList,
                               meta_kms_constraints_list_unref)

uint64_t meta_kms_constraints_list_get_generation (
  const MetaKmsConstraintsList *list);

uint64_t meta_kms_constraints_list_get_selected_id (
  const MetaKmsConstraintsList *list);

uint64_t meta_kms_constraints_list_get_suggested_id (
  const MetaKmsConstraintsList *list);

size_t meta_kms_constraints_list_get_n_entries (
  const MetaKmsConstraintsList *list);

const MetaKmsConstraintsListEntry *
meta_kms_constraints_list_get_entry (const MetaKmsConstraintsList *list,
                                     size_t                        index);

const MetaKmsConstraintsListEntry *
meta_kms_constraints_list_find_entry (const MetaKmsConstraintsList *list,
                                      uint64_t                      id);

uint64_t meta_kms_constraints_list_entry_get_id (
  const MetaKmsConstraintsListEntry *entry);

gboolean meta_kms_constraints_list_entry_is_selectable (
  const MetaKmsConstraintsListEntry *entry);

const MetaKmsConstraintsDescription *
meta_kms_constraints_list_entry_get_description (
  const MetaKmsConstraintsListEntry *entry);
