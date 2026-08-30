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

#include "mtk/mtk.h"

typedef struct _MetaSecondaryGpuCopyState MetaSecondaryGpuCopyState;

MetaSecondaryGpuCopyState *
meta_secondary_gpu_copy_state_new (unsigned int n_buffers,
                                   int          width,
                                   int          height);

void
meta_secondary_gpu_copy_state_free (MetaSecondaryGpuCopyState *copy_state);

unsigned int
meta_secondary_gpu_copy_state_get_next_buffer_index (
  MetaSecondaryGpuCopyState *copy_state);

/*
 * Returns all damage accumulated since the next buffer was last copied. An
 * empty damage region means that the whole frame is damaged.
 */
MtkRegion *
meta_secondary_gpu_copy_state_get_damage (
  MetaSecondaryGpuCopyState *copy_state,
  const MtkRegion           *damage,
  unsigned int               max_rectangles);

/* Call exactly once for every source frame, including failed copies. */
void
meta_secondary_gpu_copy_state_finish_frame (
  MetaSecondaryGpuCopyState *copy_state,
  const MtkRegion           *damage,
  gboolean                   buffer_copied);

void
meta_secondary_gpu_copy_state_reset (MetaSecondaryGpuCopyState *copy_state);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaSecondaryGpuCopyState,
                               meta_secondary_gpu_copy_state_free)
