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

typedef struct _MetaKmsConstraintsDescription MetaKmsConstraintsDescription;

typedef struct _MetaKmsConstraintsSize
{
  uint32_t min_width;
  uint32_t min_height;
  uint32_t max_width;
  uint32_t max_height;
} MetaKmsConstraintsSize;

typedef struct _MetaKmsConstraintsFormat
{
  uint32_t plane_id;
  uint32_t format;
  uint64_t modifier;
  gboolean implicit;
  MetaKmsConstraintsSize size;
} MetaKmsConstraintsFormat;

typedef struct _MetaKmsConstraintsProperty
{
  uint32_t object_id;
  uint32_t property_id;
  uint32_t type;
  uint64_t minimum;
  uint64_t maximum;
  uint64_t mask;
} MetaKmsConstraintsProperty;

MetaKmsConstraintsDescription *
meta_kms_constraints_description_new (
  const MetaKmsConstraintsSize     *output,
  const MetaKmsConstraintsFormat   *formats,
  size_t                            n_formats,
  const MetaKmsConstraintsProperty *properties,
  size_t                            n_properties,
  GError                          **error);

MetaKmsConstraintsDescription *
meta_kms_constraints_description_copy (
  const MetaKmsConstraintsDescription *description,
  GError                             **error);

void meta_kms_constraints_description_free (
  MetaKmsConstraintsDescription *description);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaKmsConstraintsDescription,
                               meta_kms_constraints_description_free)

const MetaKmsConstraintsSize *
meta_kms_constraints_description_get_output (
  const MetaKmsConstraintsDescription *description);

const MetaKmsConstraintsFormat *
meta_kms_constraints_description_get_formats (
  const MetaKmsConstraintsDescription *description,
  size_t                              *n_formats);

const MetaKmsConstraintsProperty *
meta_kms_constraints_description_get_properties (
  const MetaKmsConstraintsDescription *description,
  size_t                              *n_properties);

gboolean meta_kms_constraints_size_contains (
  const MetaKmsConstraintsSize *size,
  uint32_t                      width,
  uint32_t                      height);

gboolean meta_kms_constraints_description_allows_explicit_layout (
  const MetaKmsConstraintsDescription *description,
  uint32_t                             plane_id,
  uint32_t                             format,
  uint64_t                             modifier,
  uint32_t                             width,
  uint32_t                             height);

gboolean meta_kms_constraints_description_allows_implicit_layout (
  const MetaKmsConstraintsDescription *description,
  uint32_t                             plane_id,
  uint32_t                             format,
  uint32_t                             width,
  uint32_t                             height);

const MetaKmsConstraintsProperty *
meta_kms_constraints_description_find_property (
  const MetaKmsConstraintsDescription *description,
  uint32_t                             object_id,
  uint32_t                             property_id);

gboolean meta_kms_constraints_property_matches (
  const MetaKmsConstraintsProperty *property,
  uint64_t                          value);
