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

#include "backends/native/meta-kms-types.h"
#include "mtk/mtk.h"

typedef struct _MetaKmsConstraintsDescription MetaKmsConstraintsDescription;

typedef enum _MetaKmsConstraintsStorage
{
  META_KMS_CONSTRAINTS_STORAGE_NATIVE,
  META_KMS_CONSTRAINTS_STORAGE_IMPORTED,
} MetaKmsConstraintsStorage;

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
  gboolean permits_native;
  gboolean permits_imported;
  uint32_t plane_count;
  uint32_t pitch_alignment;
  uint32_t offset_alignment;
  uint32_t max_pitch;
  MetaKmsConstraintsSize size;
} MetaKmsConstraintsFormat;

typedef struct _MetaKmsConstraintsProperty
{
  uint32_t object_id;
  uint32_t property_id;
  uint32_t type;
  gboolean applies_to_yuv_plane;
  uint64_t minimum;
  uint64_t maximum;
  uint64_t mask;
} MetaKmsConstraintsProperty;

typedef struct _MetaKmsConstraintsPlaneGeometry
{
  uint32_t plane_id;
  gboolean permits_crop;
  gboolean permits_fractional_source;
  gboolean permits_position;
  uint32_t min_scale;
  uint32_t max_scale;
} MetaKmsConstraintsPlaneGeometry;

typedef struct _MetaKmsConstraintsPlaneLimit
{
  uint32_t max_active;
  const uint32_t *plane_ids;
  size_t n_plane_ids;
} MetaKmsConstraintsPlaneLimit;

MetaKmsConstraintsDescription *
meta_kms_constraints_description_new (
  const MetaKmsConstraintsSize     *output,
  const MetaKmsConstraintsFormat   *formats,
  size_t                            n_formats,
  const MetaKmsConstraintsPlaneGeometry *plane_geometries,
  size_t                            n_plane_geometries,
  const MetaKmsConstraintsProperty *properties,
  size_t                            n_properties,
  const MetaKmsConstraintsPlaneLimit *plane_limits,
  size_t                            n_plane_limits,
  GError                          **error);

MetaKmsConstraintsDescription *
meta_kms_constraints_description_ref (
  MetaKmsConstraintsDescription *description);

void meta_kms_constraints_description_unref (
  MetaKmsConstraintsDescription *description);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaKmsConstraintsDescription,
                               meta_kms_constraints_description_unref)

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

const MetaKmsConstraintsPlaneGeometry *
meta_kms_constraints_description_get_plane_geometries (
  const MetaKmsConstraintsDescription *description,
  size_t                              *n_plane_geometries);

const MetaKmsConstraintsPlaneLimit *
meta_kms_constraints_description_get_plane_limits (
  const MetaKmsConstraintsDescription *description,
  size_t                              *n_plane_limits);

gboolean meta_kms_constraints_size_contains (
  const MetaKmsConstraintsSize *size,
  uint32_t                      width,
  uint32_t                      height);

gboolean meta_kms_constraints_description_allows_plane_geometry (
  const MetaKmsConstraintsDescription *description,
  uint32_t                             plane_id,
  uint32_t                             framebuffer_width,
  uint32_t                             framebuffer_height,
  MetaFixed16Rectangle                 source,
  MtkRectangle                         destination);

gboolean meta_kms_constraints_description_allows_explicit_layout (
  const MetaKmsConstraintsDescription *description,
  uint32_t                             plane_id,
  uint32_t                             format,
  uint64_t                             modifier,
  MetaKmsConstraintsStorage            storage,
  uint32_t                             width,
  uint32_t                             height);

gboolean meta_kms_constraints_description_allows_implicit_layout (
  const MetaKmsConstraintsDescription *description,
  uint32_t                             plane_id,
  uint32_t                             format,
  MetaKmsConstraintsStorage            storage,
  uint32_t                             width,
  uint32_t                             height);

gboolean meta_kms_constraints_description_allows_buffer_layout (
  const MetaKmsConstraintsDescription *description,
  uint32_t                             plane_id,
  uint32_t                             format,
  uint64_t                             modifier,
  gboolean                             implicit,
  MetaKmsConstraintsStorage            storage,
  uint32_t                             width,
  uint32_t                             height,
  size_t                               n_planes,
  const uint32_t                      *pitches,
  const uint32_t                      *offsets);

GArray * meta_kms_constraints_description_copy_drm_formats_for_plane (
  const MetaKmsConstraintsDescription *description,
  uint32_t                             plane_id,
  MetaKmsConstraintsStorage            storage,
  uint32_t                             width,
  uint32_t                             height);

GArray * meta_kms_constraints_description_copy_explicit_modifiers_for_format (
  const MetaKmsConstraintsDescription *description,
  uint32_t                             plane_id,
  uint32_t                             format,
  MetaKmsConstraintsStorage            storage,
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
