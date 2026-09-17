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

#include "backends/native/meta-kms-constraints-target.h"

#include <gio/gio.h>

struct _MetaKmsConstraintsTarget
{
  MetaKmsConstraintsList *list;
  uint64_t id;
};

MetaKmsConstraintsTarget *
meta_kms_constraints_target_new (MetaKmsConstraintsList  *list,
                                 uint64_t                 id,
                                 GError                 **error)
{
  const MetaKmsConstraintsListEntry *entry;
  MetaKmsConstraintsTarget *target;

  g_return_val_if_fail (list != NULL, NULL);

  entry = meta_kms_constraints_list_find_entry (list, id);
  if (!entry ||
      (id != meta_kms_constraints_list_get_selected_id (list) &&
       !meta_kms_constraints_list_entry_is_selectable (entry)))
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_SUPPORTED,
                           "KMS constraints target is not selectable");
      return NULL;
    }

  target = g_try_new0 (MetaKmsConstraintsTarget, 1);
  if (!target)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NO_SPACE,
                           "Allocate KMS constraints target");
      return NULL;
    }

  target->list = meta_kms_constraints_list_ref (list);
  target->id = id;

  return target;
}

void
meta_kms_constraints_target_free (MetaKmsConstraintsTarget *target)
{
  if (!target)
    return;

  meta_kms_constraints_list_unref (target->list);
  g_free (target);
}

uint64_t
meta_kms_constraints_target_get_generation (
  const MetaKmsConstraintsTarget *target)
{
  return meta_kms_constraints_list_get_generation (target->list);
}

uint64_t
meta_kms_constraints_target_get_id (const MetaKmsConstraintsTarget *target)
{
  return target->id;
}

const MetaKmsConstraintsDescription *
meta_kms_constraints_target_get_description (
  const MetaKmsConstraintsTarget *target)
{
  const MetaKmsConstraintsListEntry *entry;

  entry = meta_kms_constraints_list_find_entry (target->list, target->id);
  g_assert (entry != NULL);

  return meta_kms_constraints_list_entry_get_description (entry);
}

gboolean
meta_kms_constraints_target_allows_format (
  const MetaKmsConstraintsTarget *target,
  uint32_t                        plane_id,
  uint32_t                        format,
  MetaKmsConstraintsStorage       storage,
  uint32_t                        width,
  uint32_t                        height)
{
  const MetaKmsConstraintsDescription *description =
    meta_kms_constraints_target_get_description (target);
  const MetaKmsConstraintsFormat *formats;
  size_t n_formats;
  size_t i;

  formats = meta_kms_constraints_description_get_formats (description,
                                                           &n_formats);
  for (i = 0; i < n_formats; i++)
    {
      if (formats[i].plane_id == plane_id &&
          formats[i].format == format &&
          ((storage == META_KMS_CONSTRAINTS_STORAGE_NATIVE &&
            formats[i].permits_native) ||
           (storage == META_KMS_CONSTRAINTS_STORAGE_IMPORTED &&
            formats[i].permits_imported)) &&
          meta_kms_constraints_size_contains (&formats[i].size,
                                               width,
                                               height))
        return TRUE;
    }

  return FALSE;
}

gboolean
meta_kms_constraints_target_allows_implicit_layout (
  const MetaKmsConstraintsTarget *target,
  uint32_t                        plane_id,
  uint32_t                        format,
  MetaKmsConstraintsStorage       storage,
  uint32_t                        width,
  uint32_t                        height)
{
  return meta_kms_constraints_description_allows_implicit_layout (
    meta_kms_constraints_target_get_description (target),
    plane_id,
    format,
    storage,
    width,
    height);
}

gboolean
meta_kms_constraints_target_allows_buffer_layout (
  const MetaKmsConstraintsTarget *target,
  uint32_t                        plane_id,
  uint32_t                        format,
  uint64_t                        modifier,
  gboolean                        implicit,
  MetaKmsConstraintsStorage       storage,
  uint32_t                        width,
  uint32_t                        height,
  size_t                          n_planes,
  const uint32_t                 *pitches,
  const uint32_t                 *offsets)
{
  return meta_kms_constraints_description_allows_buffer_layout (
    meta_kms_constraints_target_get_description (target),
    plane_id,
    format,
    modifier,
    implicit,
    storage,
    width,
    height,
    n_planes,
    pitches,
    offsets);
}

static gboolean
array_contains_uint64 (const GArray *values,
                       uint64_t      value)
{
  unsigned int i;

  for (i = 0; i < values->len; i++)
    {
      if (g_array_index (values, uint64_t, i) == value)
        return TRUE;
    }

  return FALSE;
}

GArray *
meta_kms_constraints_target_filter_explicit_modifiers (
  const MetaKmsConstraintsTarget *target,
  uint32_t                        plane_id,
  uint32_t                        format,
  MetaKmsConstraintsStorage       storage,
  uint32_t                        width,
  uint32_t                        height,
  const GArray                   *candidates)
{
  const MetaKmsConstraintsDescription *description =
    meta_kms_constraints_target_get_description (target);
  g_autoptr (GArray) allowed_modifiers = NULL;
  GArray *filtered_modifiers;
  unsigned int i;

  allowed_modifiers =
    meta_kms_constraints_description_copy_explicit_modifiers_for_format (
      description,
      plane_id,
      format,
      storage,
      width,
      height);
  filtered_modifiers = g_array_new (FALSE, FALSE, sizeof (uint64_t));
  for (i = 0; i < candidates->len; i++)
    {
      uint64_t modifier = g_array_index (candidates, uint64_t, i);

      if (array_contains_uint64 (allowed_modifiers, modifier))
        g_array_append_val (filtered_modifiers, modifier);
    }

  return filtered_modifiers;
}

gboolean
meta_kms_constraints_target_allows_property (
  const MetaKmsConstraintsTarget *target,
  uint32_t                        object_id,
  uint32_t                        property_id,
  uint64_t                        value)
{
  const MetaKmsConstraintsDescription *description =
    meta_kms_constraints_target_get_description (target);
  const MetaKmsConstraintsProperty *property;

  property = meta_kms_constraints_description_find_property (description,
                                                              object_id,
                                                              property_id);
  return !property || meta_kms_constraints_property_matches (property, value);
}
