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

#include "backends/native/meta-kms-constraints-buffer.h"

gboolean
meta_kms_constraints_target_allows_drm_buffer (
  const MetaKmsConstraintsTarget *target,
  MetaKmsPlane                   *plane,
  MetaDrmBuffer                  *buffer,
  MetaKmsConstraintsStorage       storage)
{
  uint32_t pitches[4];
  uint32_t offsets[4];
  int n_planes;
  int i;

  n_planes = meta_drm_buffer_get_n_planes (buffer);
  if (n_planes < 1 || n_planes > G_N_ELEMENTS (pitches))
    return FALSE;

  for (i = 0; i < n_planes; i++)
    {
      int pitch = meta_drm_buffer_get_stride_for_plane (buffer, i);
      int offset = meta_drm_buffer_get_offset_for_plane (buffer, i);

      if (pitch < 0 || offset < 0)
        return FALSE;
      pitches[i] = pitch;
      offsets[i] = offset;
    }

  return meta_kms_constraints_target_allows_buffer_layout (
    target,
    meta_kms_plane_get_id (plane),
    meta_drm_buffer_get_format (buffer),
    meta_drm_buffer_get_modifier (buffer),
    !meta_drm_buffer_uses_explicit_modifiers (buffer),
    storage,
    meta_drm_buffer_get_width (buffer),
    meta_drm_buffer_get_height (buffer),
    n_planes,
    pitches,
    offsets);
}
