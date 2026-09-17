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

#include "backends/native/meta-kms-constraints-list.h"

typedef struct _MetaKmsConstraintsTarget MetaKmsConstraintsTarget;

MetaKmsConstraintsTarget *
meta_kms_constraints_target_new (MetaKmsConstraintsList  *list,
                                 uint64_t                 id,
                                 GError                 **error);

void meta_kms_constraints_target_free (MetaKmsConstraintsTarget *target);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaKmsConstraintsTarget,
                               meta_kms_constraints_target_free)

uint64_t meta_kms_constraints_target_get_generation (
  const MetaKmsConstraintsTarget *target);

uint64_t meta_kms_constraints_target_get_id (
  const MetaKmsConstraintsTarget *target);

const MetaKmsConstraintsDescription *
meta_kms_constraints_target_get_description (
  const MetaKmsConstraintsTarget *target);

gboolean meta_kms_constraints_target_allows_format (
  const MetaKmsConstraintsTarget *target,
  uint32_t                        plane_id,
  uint32_t                        format,
  MetaKmsConstraintsStorage       storage,
  uint32_t                        width,
  uint32_t                        height);

gboolean meta_kms_constraints_target_allows_implicit_layout (
  const MetaKmsConstraintsTarget *target,
  uint32_t                        plane_id,
  uint32_t                        format,
  MetaKmsConstraintsStorage       storage,
  uint32_t                        width,
  uint32_t                        height);

gboolean meta_kms_constraints_target_allows_buffer_layout (
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
  const uint32_t                 *offsets);

GArray * meta_kms_constraints_target_filter_explicit_modifiers (
  const MetaKmsConstraintsTarget *target,
  uint32_t                        plane_id,
  uint32_t                        format,
  MetaKmsConstraintsStorage       storage,
  uint32_t                        width,
  uint32_t                        height,
  const GArray                   *candidates);

gboolean meta_kms_constraints_target_allows_property (
  const MetaKmsConstraintsTarget *target,
  uint32_t                        object_id,
  uint32_t                        property_id,
  uint64_t                        value);

gboolean meta_kms_constraints_target_allows_active_planes (
  const MetaKmsConstraintsTarget *target,
  const uint32_t                 *plane_ids,
  size_t                          n_plane_ids);
