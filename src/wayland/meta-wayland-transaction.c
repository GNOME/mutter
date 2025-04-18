/*
 * Wayland Transaction Support
 *
 * Copyright (C) 2021 Red Hat, Inc.
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

#include "wayland/meta-wayland-transaction.h"

#include <glib-unix.h>

#include "wayland/meta-wayland.h"
#include "wayland/meta-wayland-buffer.h"
#include "wayland/meta-wayland-dma-buf.h"
#include "wayland/meta-wayland-linux-drm-syncobj.h"

#define META_WAYLAND_TRANSACTION_NONE ((void *)(uintptr_t) G_MAXSIZE)

struct _MetaWaylandTransaction
{
  GList node;
  MetaWaylandCompositor *compositor;
  MetaWaylandTransaction *next_candidate;
  uint64_t committed_sequence;

  /*
   * Keys:   All surfaces referenced in the transaction
   * Values: Pointer to MetaWaylandTransactionEntry for the surface
   */
  GHashTable *entries;

  /* Sources for buffers which are not ready yet */
  GHashTable *buf_sources;

  int64_t target_presentation_time_us;
};

struct _MetaWaylandTransactionEntry
{
  /* Next committed transaction with entry for the same surface */
  MetaWaylandTransaction *next_transaction;

  MetaWaylandSurfaceState *state;

  /* Sub-surface position */
  gboolean has_sub_pos;
  int x;
  int y;
};

int64_t
meta_wayland_transaction_get_target_presentation_time_us (const MetaWaylandTransaction *transaction)
{
  return transaction->target_presentation_time_us;
}

static MetaWaylandTransactionEntry *
meta_wayland_transaction_get_entry (MetaWaylandTransaction *transaction,
                                    MetaWaylandSurface     *surface)
{
  return g_hash_table_lookup (transaction->entries, surface);
}

static void
meta_wayland_transaction_sync_child_states (MetaWaylandSurface *surface)
{
  MetaWaylandSurface *subsurface_surface;
  MetaWaylandSubsurface *subsurface;
  MetaWaylandActorSurface *actor_surface;

  META_WAYLAND_SURFACE_FOREACH_SUBSURFACE (&surface->applied_state, subsurface_surface)
    {
      subsurface = META_WAYLAND_SUBSURFACE (subsurface_surface->role);
      actor_surface = META_WAYLAND_ACTOR_SURFACE (subsurface);
      meta_wayland_actor_surface_sync_actor_state (actor_surface);
    }

  if (!surface->applied_state.parent &&
      surface->role && META_IS_WAYLAND_SUBSURFACE (surface->role))
    {
      /* Unmapped sub-surface */
      subsurface = META_WAYLAND_SUBSURFACE (surface->role);
      actor_surface = META_WAYLAND_ACTOR_SURFACE (subsurface);
      meta_wayland_actor_surface_sync_actor_state (actor_surface);
    }
}

static void
meta_wayland_transaction_apply_subsurface_position (MetaWaylandSurface          *surface,
                                                    MetaWaylandTransactionEntry *entry)
{
  if (!entry->has_sub_pos)
    return;

  surface->sub.x = entry->x;
  surface->sub.y = entry->y;
}

static gboolean
is_ancestor (MetaWaylandSurface *candidate,
             MetaWaylandSurface *reference)
{
  MetaWaylandSurface *ancestor;

  for (ancestor = reference->applied_state.parent;
       ancestor;
       ancestor = ancestor->applied_state.parent)
    {
      if (ancestor == candidate)
        return TRUE;
    }

  return FALSE;
}

static int
meta_wayland_transaction_compare (const void *key1,
                                  const void *key2)
{
  MetaWaylandSurface *surface1 = *(MetaWaylandSurface **) key1;
  MetaWaylandSurface *surface2 = *(MetaWaylandSurface **) key2;

  /* Order of siblings doesn't matter */
  if (surface1->applied_state.parent == surface2->applied_state.parent)
    return 0;

  /* Ancestor surfaces come before descendant surfaces */
  if (is_ancestor (surface1, surface2))
    return 1;

  if (is_ancestor (surface2, surface1))
    return -1;

  /*
   * Order unrelated surfaces by their toplevel surface pointer values, to
   * prevent unrelated surfaces from getting mixed between siblings
   */
  return (meta_wayland_surface_get_toplevel (surface1) <
          meta_wayland_surface_get_toplevel (surface2)) ? -1 : 1;
}

static void
ensure_next_candidate (MetaWaylandTransaction  *transaction,
                       MetaWaylandTransaction **first_candidate)
{
  MetaWaylandTransaction **candidate;

  if (transaction->next_candidate)
    return;

  candidate = first_candidate;
  while (*candidate != META_WAYLAND_TRANSACTION_NONE &&
         (*candidate)->committed_sequence <
         transaction->committed_sequence)
    candidate = &(*candidate)->next_candidate;

  transaction->next_candidate = *candidate;
  *candidate = transaction;
}

static void
meta_wayland_transaction_apply (MetaWaylandTransaction  *transaction,
                                MetaWaylandTransaction **first_candidate)
{
  g_autofree MetaWaylandSurface **surfaces = NULL;
  g_autofree MetaWaylandSurfaceState **states = NULL;
  unsigned int num_surfaces;
  MetaWaylandSurface *surface;
  MetaWaylandTransactionEntry *entry;
  int i;

  if (g_hash_table_size (transaction->entries) == 0)
    goto free;

  surfaces = (MetaWaylandSurface **)
    g_hash_table_get_keys_as_array (transaction->entries, &num_surfaces);
  states = g_new (MetaWaylandSurfaceState *, num_surfaces);

  /* Apply sub-surface states to ensure output surface hierarchy is up to date */
  for (i = 0; i < num_surfaces; i++)
    {
      surface = surfaces[i];
      entry = meta_wayland_transaction_get_entry (transaction, surface);
      meta_wayland_transaction_apply_subsurface_position (surface, entry);

      if (entry->state && entry->state->subsurface_placement_ops)
        meta_wayland_surface_apply_placement_ops (surface, entry->state);
    }

  /* Sort surfaces from ancestors to descendants */
  qsort (surfaces, num_surfaces, sizeof (MetaWaylandSurface *),
         meta_wayland_transaction_compare);

  /* Apply states from ancestors to descendants */
  for (i = 0; i < num_surfaces; i++)
    {
      surface = surfaces[i];
      entry = meta_wayland_transaction_get_entry (transaction, surface);

      states[i] = entry->state;
      if (entry->state)
        meta_wayland_surface_apply_state (surface, entry->state);

      if (surface->transaction.last_committed == transaction)
        {
          surface->transaction.first_committed = NULL;
          surface->transaction.last_committed = NULL;
        }
      else
        {
          MetaWaylandTransaction *next_transaction = entry->next_transaction;

          if (next_transaction)
            {
              surface->transaction.first_committed = next_transaction;
              ensure_next_candidate (next_transaction, first_candidate);
            }
        }
    }

  /* Synchronize child states from descendants to ancestors */
  for (i = num_surfaces - 1; i >= 0; i--)
    {
      if (states[i])
        meta_wayland_transaction_sync_child_states (surfaces[i]);
    }

free:
  meta_wayland_transaction_free (transaction);
}

static gboolean
has_dependencies (MetaWaylandTransaction *transaction)
{
  GHashTableIter iter;
  MetaWaylandSurface *surface;
  MetaWaylandTransactionEntry *entry;

  if (transaction->target_presentation_time_us)
    return TRUE;

  if (transaction->buf_sources &&
      g_hash_table_size (transaction->buf_sources) > 0)
    return TRUE;

  g_hash_table_iter_init (&iter, transaction->entries);
  while (g_hash_table_iter_next (&iter, (gpointer *) &surface,
                                        (gpointer *) &entry))
    {
      MetaSurfaceActor *actor;

      if (surface->transaction.first_committed != transaction)
        return TRUE;

      if (!entry || !entry->state)
        continue;

      actor = meta_wayland_surface_get_actor (surface);
      if (!actor || meta_surface_actor_is_effectively_obscured (actor) ||
          !clutter_actor_is_mapped (CLUTTER_ACTOR (actor)))
        continue;

      if (entry->state->fifo_wait && surface->fifo_barrier)
        return TRUE;
    }

  return FALSE;
}

static void
meta_wayland_transaction_maybe_apply_one (MetaWaylandTransaction  *transaction,
                                          MetaWaylandTransaction **first_candidate)
{
  if (has_dependencies (transaction))
    return;

  meta_wayland_transaction_apply (transaction, first_candidate);
}

static void
meta_wayland_transaction_maybe_apply (MetaWaylandTransaction *transaction)
{
  MetaWaylandTransaction *first_candidate = META_WAYLAND_TRANSACTION_NONE;

  while (TRUE)
    {
      meta_wayland_transaction_maybe_apply_one (transaction, &first_candidate);

      if (first_candidate == META_WAYLAND_TRANSACTION_NONE)
        return;

      transaction = first_candidate;
      first_candidate = transaction->next_candidate;
      transaction->next_candidate = NULL;
    }
}

gboolean
meta_wayland_transaction_unblock_timed (MetaWaylandTransaction *transaction,
                                        int64_t                 target_time_us)
{
  if (target_time_us < transaction->target_presentation_time_us)
    return FALSE;

  transaction->target_presentation_time_us = 0;

  meta_wayland_transaction_maybe_apply (transaction);

  return TRUE;
}

void
meta_wayland_transaction_consider_surface (MetaWaylandSurface *surface)
{
  MetaWaylandTransaction *transaction;

  transaction = surface->transaction.first_committed;
  if (transaction)
    meta_wayland_transaction_maybe_apply (transaction);
}

void
meta_wayland_transaction_unblock_surface (MetaWaylandSurface *surface)
{
  if (!surface->fifo_barrier)
    {
      g_warning ("Attempting to unblock a surface with no fifo_barrier");
      return;
    }

  surface->fifo_barrier = FALSE;

  meta_wayland_transaction_consider_surface (surface);
}

static void
meta_wayland_transaction_dma_buf_dispatch (MetaWaylandBuffer *buffer,
                                           gpointer           user_data)
{
  MetaWaylandTransaction *transaction = user_data;

  if (!transaction->buf_sources ||
      !g_hash_table_remove (transaction->buf_sources, buffer))
    return;

  meta_wayland_transaction_maybe_apply (transaction);
}

static void
ensure_buf_sources (MetaWaylandTransaction *transaction)
{
  if (!transaction->buf_sources)
    {
      transaction->buf_sources =
        g_hash_table_new_full (NULL, NULL, NULL,
                               (GDestroyNotify) g_source_destroy);
    }
}

static gboolean
meta_wayland_transaction_add_dma_buf_source (MetaWaylandTransaction *transaction,
                                             MetaWaylandBuffer      *buffer)
{
  GSource *source;

  if (transaction->buf_sources &&
      g_hash_table_contains (transaction->buf_sources, buffer))
    return FALSE;

  source = meta_wayland_dma_buf_create_source (buffer,
                                               meta_wayland_transaction_dma_buf_dispatch,
                                               transaction);
  if (!source)
    return FALSE;

  ensure_buf_sources (transaction);

  g_hash_table_insert (transaction->buf_sources, buffer, source);
  g_source_attach (source, NULL);
  g_source_unref (source);

  return TRUE;
}

static gboolean
meta_wayland_transaction_add_drm_syncobj_source (MetaWaylandTransaction *transaction,
                                                 MetaWaylandBuffer      *buffer,
                                                 MetaWaylandSyncPoint   *acquire)
{
  GSource *source;

  if (transaction->buf_sources &&
      g_hash_table_contains (transaction->buf_sources, buffer))
    return FALSE;

  source = meta_wayland_drm_syncobj_create_source (buffer,
                                                   acquire->timeline,
                                                   acquire->sync_point,
                                                   meta_wayland_transaction_dma_buf_dispatch,
                                                   transaction);
  if (!source)
    return FALSE;

  ensure_buf_sources (transaction);

  g_hash_table_insert (transaction->buf_sources, buffer, source);
  g_source_attach (source, NULL);
  g_source_unref (source);

  return TRUE;
}

static void
meta_wayland_transaction_add_placement_surfaces (MetaWaylandTransaction  *transaction,
                                                 MetaWaylandSurfaceState *state)
{
  GSList *l;

  for (l = state->subsurface_placement_ops; l; l = l->next)
    {
      MetaWaylandSubsurfacePlacementOp *op = l->data;

      meta_wayland_transaction_ensure_entry (transaction, op->surface);

      if (op->sibling)
        meta_wayland_transaction_ensure_entry (transaction, op->sibling);
    }
}

void
meta_wayland_transaction_commit (MetaWaylandTransaction *transaction)
{
  static uint64_t committed_sequence;
  GQueue *committed_queue;
  gboolean maybe_apply = TRUE;
  GHashTableIter iter;
  MetaWaylandSurface *surface;
  MetaWaylandTransactionEntry *entry;
  g_autoptr (GPtrArray) placement_states = NULL;
  unsigned int num_placement_states = 0;
  int i;
  gint64 max_time_us = 0;
  MetaWaylandSurface *max_time_surface = NULL;

  g_hash_table_iter_init (&iter, transaction->entries);
  while (g_hash_table_iter_next (&iter,
                                 (gpointer *) &surface, (gpointer *) &entry))
    {
      if (entry && entry->state)
        {
          MetaWaylandBuffer *buffer = entry->state->buffer;

          if ((entry->state->drm_syncobj.acquire &&
               meta_wayland_transaction_add_drm_syncobj_source (transaction, buffer,
                                                                entry->state->drm_syncobj.acquire))
              || (buffer &&
                  meta_wayland_transaction_add_dma_buf_source (transaction, buffer)))
            maybe_apply = FALSE;

          if (entry->state->subsurface_placement_ops)
            {
              if (!placement_states)
                placement_states = g_ptr_array_new ();

              g_ptr_array_add (placement_states, entry->state);
              num_placement_states++;
            }

          if (entry->state->has_target_time &&
              entry->state->target_time_us > max_time_us)
            {
              max_time_us = entry->state->target_time_us;
              max_time_surface = surface;
            }
        }
    }

  for (i = 0; i < num_placement_states; i++)
    {
      MetaWaylandSurfaceState *placement_state;

      placement_state = g_ptr_array_index (placement_states, i);
      meta_wayland_transaction_add_placement_surfaces (transaction,
                                                       placement_state);
    }

  /* If we have a time constraint, we always defer application until just before the
   * appropriate frame clock tick.
   */
  if (max_time_us)
    {
      MetaSurfaceActor *actor = meta_wayland_surface_get_actor (max_time_surface);
      ClutterFrameClock *frame_clock =
        clutter_actor_pick_frame_clock (CLUTTER_ACTOR (actor), NULL);

      if (frame_clock)
        {
          maybe_apply = FALSE;
          transaction->target_presentation_time_us = max_time_us;
          meta_wayland_compositor_add_timed_transaction (transaction->compositor, transaction);
          clutter_frame_clock_add_future_time (frame_clock, max_time_us);
        }
    }
  transaction->committed_sequence = ++committed_sequence;
  transaction->node.data = transaction;

  committed_queue =
    meta_wayland_compositor_get_committed_transactions (transaction->compositor);
  g_queue_push_tail_link (committed_queue, &transaction->node);

  g_hash_table_iter_init (&iter, transaction->entries);
  while (g_hash_table_iter_next (&iter, (gpointer *) &surface, NULL))
    {
      if (surface->transaction.first_committed)
        {
          entry = g_hash_table_lookup (surface->transaction.last_committed->entries,
                                       surface);
          entry->next_transaction = transaction;
          maybe_apply = FALSE;
        }
      else
        {
          surface->transaction.first_committed = transaction;
        }

      surface->transaction.last_committed = transaction;
    }

  if (maybe_apply)
    meta_wayland_transaction_maybe_apply (transaction);
}

MetaWaylandTransactionEntry *
meta_wayland_transaction_ensure_entry (MetaWaylandTransaction *transaction,
                                       MetaWaylandSurface     *surface)
{
  MetaWaylandTransactionEntry *entry;

  entry = meta_wayland_transaction_get_entry (transaction, surface);
  if (entry)
    return entry;

  g_return_val_if_fail (surface, NULL);
  surface = g_object_ref (surface);
  g_return_val_if_fail (surface, NULL);

  entry = g_new0 (MetaWaylandTransactionEntry, 1);
  g_hash_table_insert (transaction->entries, surface, entry);

  return entry;
}

static void
meta_wayland_transaction_entry_free (MetaWaylandTransactionEntry *entry)
{
  if (entry->state)
    {
      if (entry->state->buffer)
        meta_wayland_buffer_dec_use_count (entry->state->buffer);

      g_clear_object (&entry->state);
    }

  g_free (entry);
}

void
meta_wayland_transaction_add_placement_op (MetaWaylandTransaction           *transaction,
                                           MetaWaylandSurface               *surface,
                                           MetaWaylandSubsurfacePlacementOp *op)
{
  MetaWaylandTransactionEntry *entry;
  MetaWaylandSurfaceState *state;

  entry = meta_wayland_transaction_ensure_entry (transaction, surface);

  if (!entry->state)
    entry->state = meta_wayland_surface_state_new ();

  state = entry->state;
  state->subsurface_placement_ops =
    g_slist_append (state->subsurface_placement_ops, op);
}

void
meta_wayland_transaction_add_subsurface_position (MetaWaylandTransaction *transaction,
                                                  MetaWaylandSurface     *surface,
                                                  int                     x,
                                                  int                     y)
{
  MetaWaylandTransactionEntry *entry;

  entry = meta_wayland_transaction_ensure_entry (transaction, surface);
  entry->x = x;
  entry->y = y;
  entry->has_sub_pos = TRUE;
}

void
meta_wayland_transaction_add_xdg_popup_reposition (MetaWaylandTransaction *transaction,
                                                   MetaWaylandSurface     *surface,
                                                   void                   *xdg_positioner,
                                                   uint32_t               token)
{
  MetaWaylandTransactionEntry *entry;
  MetaWaylandSurfaceState *state;

  entry = meta_wayland_transaction_ensure_entry (transaction, surface);

  if (entry->state)
    g_clear_pointer (&entry->state->xdg_positioner, g_free);
  else
    entry->state = meta_wayland_surface_state_new ();

  state = entry->state;
  state->xdg_positioner = xdg_positioner;
  state->xdg_popup_reposition_token = token;
}

static void
meta_wayland_transaction_entry_merge_into (MetaWaylandTransactionEntry *from,
                                           MetaWaylandTransactionEntry *to)
{
  if (from->has_sub_pos)
    {
      to->x = from->x;
      to->y = from->y;
      to->has_sub_pos = TRUE;
    }

  if (from->state)
    {
      if (to->state)
        {
          meta_wayland_surface_state_merge_into (from->state, to->state);
          g_clear_object (&from->state);
        }
      else
        {
          to->state = g_steal_pointer (&from->state);
        }
    }
}

void
meta_wayland_transaction_merge_into (MetaWaylandTransaction *from,
                                     MetaWaylandTransaction *to)
{
  GHashTableIter iter;
  MetaWaylandSurface *surface;
  MetaWaylandTransactionEntry *from_entry, *to_entry;

  g_hash_table_iter_init (&iter, from->entries);
  while (g_hash_table_iter_next (&iter, (gpointer *) &surface,
                                 (gpointer *) &from_entry))
    {
      to_entry = meta_wayland_transaction_get_entry (to, surface);
      if (!to_entry)
        {
          g_hash_table_iter_steal (&iter);
          g_hash_table_insert (to->entries, surface, from_entry);
          continue;
        }

      meta_wayland_transaction_entry_merge_into (from_entry, to_entry);
      g_hash_table_iter_remove (&iter);
    }

  meta_wayland_transaction_free (from);
}

void
meta_wayland_transaction_merge_pending_state (MetaWaylandTransaction *transaction,
                                              MetaWaylandSurface     *surface)
{
  MetaWaylandSurfaceState *pending = surface->pending_state;
  MetaWaylandTransactionEntry *entry;

  entry = meta_wayland_transaction_ensure_entry (transaction, surface);

  if (!entry->state)
    {
      entry->state = pending;
      surface->pending_state = meta_wayland_surface_state_new ();
      return;
    }

  meta_wayland_surface_state_merge_into (pending, entry->state);
  meta_wayland_surface_state_reset (pending);
}

MetaWaylandTransaction *
meta_wayland_transaction_new (MetaWaylandCompositor *compositor)
{
  MetaWaylandTransaction *transaction;

  transaction = g_new0 (MetaWaylandTransaction, 1);

  transaction->compositor = compositor;
  transaction->entries = g_hash_table_new_full (NULL, NULL, g_object_unref,
                                                (GDestroyNotify) meta_wayland_transaction_entry_free);

  return transaction;
}

void
meta_wayland_transaction_free (MetaWaylandTransaction *transaction)
{
  if (transaction->node.data)
    {
      GQueue *committed_queue =
        meta_wayland_compositor_get_committed_transactions (transaction->compositor);

      g_queue_unlink (committed_queue, &transaction->node);
    }

  g_clear_pointer (&transaction->buf_sources, g_hash_table_destroy);
  g_hash_table_destroy (transaction->entries);
  g_free (transaction);
}

void
meta_wayland_transaction_finalize (MetaWaylandCompositor *compositor)
{
  GQueue *transactions;
  GList *node;

  transactions = meta_wayland_compositor_get_committed_transactions (compositor);

  while ((node = g_queue_pop_head_link (transactions)))
    {
      MetaWaylandTransaction *transaction = node->data;

      g_assert (node == &transaction->node);

      meta_wayland_transaction_free (transaction);
    }
}

void
meta_wayland_transaction_init (MetaWaylandCompositor *compositor)
{
  GQueue *transactions;

  transactions = meta_wayland_compositor_get_committed_transactions (compositor);
  g_queue_init (transactions);
}
