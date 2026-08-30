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

#include "backends/native/meta-secondary-gpu-copy-state.h"

#include <stdint.h>
#include <string.h>

#include "clutter/clutter-mutter.h"

struct _MetaSecondaryGpuCopyState
{
  /*
   * frame_sequence advances for every source frame. A destination buffer's
   * sequence advances only when that buffer was successfully copied, so its
   * age still accounts for damage from failed copy attempts.
   */
  unsigned int n_buffers;
  unsigned int next_buffer_index;
  uint64_t frame_sequence;
  uint64_t *buffer_sequences;

  int width;
  int height;
  ClutterDamageHistory *damage_history;
};

static MtkRegion *
create_full_damage (MetaSecondaryGpuCopyState *copy_state)
{
  return mtk_region_create_rectangle (
    &MTK_RECTANGLE_INIT (0, 0, copy_state->width, copy_state->height));
}

static MtkRegion *
copy_damage (MetaSecondaryGpuCopyState *copy_state,
             const MtkRegion           *damage)
{
  if (mtk_region_is_empty (damage))
    return create_full_damage (copy_state);

  return mtk_region_copy (damage);
}

MetaSecondaryGpuCopyState *
meta_secondary_gpu_copy_state_new (unsigned int n_buffers,
                                   int          width,
                                   int          height)
{
  MetaSecondaryGpuCopyState *copy_state;

  g_return_val_if_fail (n_buffers > 0, NULL);
  g_return_val_if_fail (width > 0, NULL);
  g_return_val_if_fail (height > 0, NULL);

  copy_state = g_new0 (MetaSecondaryGpuCopyState, 1);
  copy_state->n_buffers = n_buffers;
  copy_state->width = width;
  copy_state->height = height;
  copy_state->buffer_sequences = g_new0 (uint64_t, n_buffers);
  copy_state->damage_history = clutter_damage_history_new ();
  copy_state->frame_sequence = 1;

  return copy_state;
}

void
meta_secondary_gpu_copy_state_free (MetaSecondaryGpuCopyState *copy_state)
{
  g_clear_pointer (&copy_state->damage_history,
                   clutter_damage_history_free);
  g_free (copy_state->buffer_sequences);
  g_free (copy_state);
}

unsigned int
meta_secondary_gpu_copy_state_get_next_buffer_index (
  MetaSecondaryGpuCopyState *copy_state)
{
  return copy_state->next_buffer_index;
}

MtkRegion *
meta_secondary_gpu_copy_state_get_damage (
  MetaSecondaryGpuCopyState *copy_state,
  const MtkRegion           *damage,
  unsigned int               max_rectangles)
{
  uint64_t buffer_sequence;
  uint64_t buffer_age;
  g_autoptr (MtkRegion) accumulated_damage = NULL;
  int age;

  buffer_sequence =
    copy_state->buffer_sequences[copy_state->next_buffer_index];
  if (buffer_sequence == 0 ||
      buffer_sequence >= copy_state->frame_sequence)
    return create_full_damage (copy_state);

  buffer_age = copy_state->frame_sequence - buffer_sequence;
  if (buffer_age > G_MAXINT)
    return create_full_damage (copy_state);

  age = (int) buffer_age;
  if (age > 1 &&
      !clutter_damage_history_is_age_valid (copy_state->damage_history,
                                            age - 1))
    return create_full_damage (copy_state);

  accumulated_damage = copy_damage (copy_state, damage);
  for (int previous_age = 1; previous_age < age; previous_age++)
    {
      const MtkRegion *previous_damage;

      previous_damage =
        clutter_damage_history_lookup (copy_state->damage_history,
                                       previous_age);
      mtk_region_union (accumulated_damage, previous_damage);
    }

  if (mtk_region_num_rectangles (accumulated_damage) > max_rectangles)
    return create_full_damage (copy_state);

  return g_steal_pointer (&accumulated_damage);
}

void
meta_secondary_gpu_copy_state_finish_frame (
  MetaSecondaryGpuCopyState *copy_state,
  const MtkRegion           *damage,
  gboolean                   buffer_copied)
{
  g_autoptr (MtkRegion) recorded_damage = NULL;

  recorded_damage = copy_damage (copy_state, damage);
  clutter_damage_history_record (copy_state->damage_history, recorded_damage);
  clutter_damage_history_step (copy_state->damage_history);

  if (buffer_copied)
    {
      copy_state->buffer_sequences[copy_state->next_buffer_index] =
        copy_state->frame_sequence;
      copy_state->next_buffer_index =
        (copy_state->next_buffer_index + 1) % copy_state->n_buffers;
    }

  if (G_UNLIKELY (copy_state->frame_sequence == G_MAXUINT64))
    {
      meta_secondary_gpu_copy_state_reset (copy_state);
      return;
    }

  copy_state->frame_sequence++;
}

void
meta_secondary_gpu_copy_state_reset (MetaSecondaryGpuCopyState *copy_state)
{
  memset (copy_state->buffer_sequences,
          0,
          sizeof (*copy_state->buffer_sequences) * copy_state->n_buffers);
  copy_state->next_buffer_index = 0;
  copy_state->frame_sequence = 1;

  g_clear_pointer (&copy_state->damage_history,
                   clutter_damage_history_free);
  copy_state->damage_history = clutter_damage_history_new ();
}
