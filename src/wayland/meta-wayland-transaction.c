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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "config.h"

#include "wayland/meta-wayland-transaction.h"

#include "wayland/meta-wayland-subsurface.h"

struct _MetaWaylandTransaction
{
  GHashTable *entries;
};

typedef struct _MetaWaylandTransactionEntry
{
  MetaWaylandSurfaceState *state;
} MetaWaylandTransactionEntry;

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

  META_WAYLAND_SURFACE_FOREACH_SUBSURFACE (surface, subsurface_surface)
    {
      MetaWaylandSubsurface *subsurface;
      MetaWaylandActorSurface *actor_surface;

      subsurface = META_WAYLAND_SUBSURFACE (subsurface_surface->role);
      actor_surface = META_WAYLAND_ACTOR_SURFACE (subsurface);
      meta_wayland_actor_surface_sync_actor_state (actor_surface);
    }
}

static gboolean
is_ancestor (MetaWaylandSurface *candidate,
             MetaWaylandSurface *reference)
{
  MetaWaylandSurface *ancestor;

  for (ancestor = reference->sub.parent; ancestor; ancestor = ancestor->sub.parent)
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
  if (surface1->sub.parent == surface2->sub.parent)
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

void
meta_wayland_transaction_commit (MetaWaylandTransaction *transaction)
{
  g_autofree MetaWaylandSurface **surfaces = NULL;
  unsigned int num_surfaces;
  int i;

  surfaces = (MetaWaylandSurface **)
    g_hash_table_get_keys_as_array (transaction->entries, &num_surfaces);

  /* Sort surfaces from ancestors to descendants */
  qsort (surfaces, num_surfaces, sizeof (MetaWaylandSurface *),
         meta_wayland_transaction_compare);

  /* Apply states from ancestors to descendants */
  for (i = 0; i < num_surfaces; i++)
    {
      MetaWaylandSurface *surface = surfaces[i];
      MetaWaylandTransactionEntry *entry;

      entry = meta_wayland_transaction_get_entry (transaction, surface);
      meta_wayland_surface_apply_state (surface, entry->state);
    }

  /* Synchronize child states from descendants to ancestors */
  for (i = num_surfaces - 1; i >= 0; i--)
    meta_wayland_transaction_sync_child_states (surfaces[i]);

  meta_wayland_transaction_free (transaction);
}

static MetaWaylandTransactionEntry *
meta_wayland_transaction_ensure_entry (MetaWaylandTransaction *transaction,
                                       MetaWaylandSurface     *surface)
{
  MetaWaylandTransactionEntry *entry;

  entry = meta_wayland_transaction_get_entry (transaction, surface);
  if (entry)
    return entry;

  entry = g_new0 (MetaWaylandTransactionEntry, 1);
  g_hash_table_insert (transaction->entries, surface, entry);

  return entry;
}

void
meta_wayland_transaction_add_state (MetaWaylandTransaction  *transaction,
                                    MetaWaylandSurface      *surface,
                                    MetaWaylandSurfaceState *state)
{
  MetaWaylandTransactionEntry *entry;

  entry = meta_wayland_transaction_ensure_entry (transaction, surface);
  g_assert (!entry->state);
  entry->state = state;
}

static void
meta_wayland_transaction_entry_free (MetaWaylandTransactionEntry *entry)
{
  g_clear_object (&entry->state);
  g_free (entry);
}

MetaWaylandTransaction *
meta_wayland_transaction_new (void)
{
  MetaWaylandTransaction *transaction;

  transaction = g_new0 (MetaWaylandTransaction, 1);

  transaction->entries = g_hash_table_new_full (NULL, NULL, NULL,
                                                (GDestroyNotify) meta_wayland_transaction_entry_free);

  return transaction;
}

void
meta_wayland_transaction_free (MetaWaylandTransaction *transaction)
{
  g_hash_table_destroy (transaction->entries);
  g_free (transaction);
}
