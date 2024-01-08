/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2013 Red Hat
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
 *
 * Written by:
 *     Owen Taylor <otaylor@redhat.com>
 *     Ray Strode <rstrode@redhat.com>
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include "config.h"

#include "clutter/clutter-mutter.h"
#include "compositor/clutter-utils.h"
#include "compositor/meta-cullable.h"

G_DEFINE_INTERFACE (MetaCullable, meta_cullable, CLUTTER_TYPE_ACTOR);

static gboolean
has_active_effects (ClutterActor *actor)
{
  g_autoptr (GList) effects = NULL;
  GList *l;

  effects = clutter_actor_get_effects (actor);
  for (l = effects; l != NULL; l = l->next)
    {
      if (clutter_actor_meta_get_enabled (CLUTTER_ACTOR_META (l->data)))
        return TRUE;
    }

  return FALSE;
}

static MtkRegion *
region_apply_transform_expand_maybe_ref (MtkRegion         *region,
                                         graphene_matrix_t *transform)
{
  if (mtk_region_is_empty (region))
    return mtk_region_ref (region);

  return mtk_region_apply_matrix_transform_expand (region, transform);
}

/**
 * MetaCullable:
 *
 * CPU culling operations for efficient drawing
 *
 * When we are painting a stack of 5-10 large actors, the standard
 * bottom-to-top method of drawing every actor results in a tremendous
 * amount of overdraw. If these actors are painting textures like
 * windows, it can easily max out the available memory bandwidth on a
 * low-end graphics chipset. It's even worse if window textures are
 * being accessed over the AGP bus.
 *
 * #MetaCullable is our solution. The basic technique applied here is to
 * do a pre-pass before painting where we walk each actor from top to bottom
 * and ask each actor to "cull itself out". We pass in a region it can copy
 * to clip its drawing to, and the actor can subtract its fully opaque pixels
 * so that actors underneath know not to draw there as well.
 */

typedef void (* ChildCullMethod) (MetaCullable *cullable,
                                  MtkRegion    *region);

static void
cull_out_children_common (MetaCullable    *cullable,
                          MtkRegion       *region,
                          ChildCullMethod  method)
{
  ClutterActor *actor = CLUTTER_ACTOR (cullable);
  ClutterActor *child;
  ClutterActorIter iter;

  clutter_actor_iter_init (&iter, actor);
  while (clutter_actor_iter_prev (&iter, &child))
    {
      gboolean needs_culling;

      if (!META_IS_CULLABLE (child))
        continue;

      needs_culling = (region != NULL);

      if (needs_culling && !clutter_actor_is_visible (child))
        needs_culling = FALSE;

      /* If an actor has effects applied, then that can change the area
       * it paints and the opacity, so we no longer can figure out what
       * portion of the actor is obscured and what portion of the screen
       * it obscures, so we skip the actor.
       *
       * This has a secondary beneficial effect: if a ClutterOffscreenEffect
       * is applied to an actor, then our clipped redraws interfere with the
       * caching of the FBO - even if we only need to draw a small portion
       * of the window right now, ClutterOffscreenEffect may use other portions
       * of the FBO later. So, skipping actors with effects applied also
       * prevents these bugs.
       *
       * Theoretically, we should check clutter_actor_get_offscreen_redirect()
       * as well for the same reason, but omitted for simplicity in the
       * hopes that no-one will do that.
       */
      if (needs_culling && has_active_effects (child))
        needs_culling = FALSE;

      if (needs_culling)
        {
          g_autoptr (MtkRegion) actor_region = NULL;
          g_autoptr (MtkRegion) reduced_region = NULL;
          graphene_matrix_t actor_transform, inverted_actor_transform;

          clutter_actor_get_transform (child, &actor_transform);

          if (graphene_matrix_is_identity (&actor_transform))
            {
              /* No transformation needed, simply pass through to child */
              method (META_CULLABLE (child), region);
              continue;
            }

          if (!graphene_matrix_inverse (&actor_transform,
                                        &inverted_actor_transform) ||
              !graphene_matrix_is_2d (&actor_transform))
            {
              method (META_CULLABLE (child), NULL);
              continue;
            }

          actor_region =
            region_apply_transform_expand_maybe_ref (region,
                                                     &inverted_actor_transform);

          g_assert (actor_region);

          method (META_CULLABLE (child), actor_region);

          reduced_region =
            region_apply_transform_expand_maybe_ref (actor_region,
                                                     &actor_transform);

          g_assert (reduced_region);

          mtk_region_intersect (region, reduced_region);
        }
      else
        {
          method (META_CULLABLE (child), NULL);
        }
    }
}

/**
 * meta_cullable_cull_unobscured_children:
 * @cullable: The #MetaCullable
 * @unobscured_region: The unobscured region, as passed into cull_unobscured()
 *
 * This is a helper method for actors that want to recurse over their
 * child actors, and cull them out.
 *
 * See #MetaCullable and meta_cullable_cull_unobscured() for more details.
 */
void
meta_cullable_cull_unobscured_children (MetaCullable *cullable,
                                        MtkRegion    *unobscured_region)
{
  cull_out_children_common (cullable,
                            unobscured_region,
                            meta_cullable_cull_unobscured);
}

/**
 * meta_cullable_cull_redraw_clip_children:
 * @cullable: The #MetaCullable
 * @clip_region: The clip region, as passed into cull_redraw_clip()
 *
 * This is a helper method for actors that want to recurse over their
 * child actors, and cull them out.
 *
 * See #MetaCullable and meta_cullable_cull_redraw_clip() for more details.
 */
void
meta_cullable_cull_redraw_clip_children (MetaCullable *cullable,
                                         MtkRegion    *clip_region)
{
  cull_out_children_common (cullable,
                            clip_region,
                            meta_cullable_cull_redraw_clip);
}

static void
meta_cullable_default_init (MetaCullableInterface *iface)
{
}

/**
 * meta_cullable_cull_unobscured:
 * @cullable: The #MetaCullable
 * @unobscured_region: The unobscured region, in @cullable's space.
 *
 * When #MetaWindowGroup is painted, we walk over its direct cullable
 * children from top to bottom and ask themselves to "cull out". Cullables
 * can use @unobscured_region record what parts of their window are unobscured
 * for e.g. scheduling repaints.
 *
 * Actors that may have fully opaque parts should also subtract out a region
 * that is fully opaque from @unobscured_region.
 *
 * Actors that have children can also use the meta_cullable_cull_unobscured_children()
 * helper method to do a simple cull across all their children.
 */
void
meta_cullable_cull_unobscured (MetaCullable *cullable,
                               MtkRegion    *unobscured_region)
{
  META_CULLABLE_GET_IFACE (cullable)->cull_unobscured (cullable, unobscured_region);
}

/**
 * meta_cullable_cull_redraw_clip:
 * @cullable: The #MetaCullable
 * @clip_region: The clip region, in @cullable's space.
 *
 * When #MetaWindowGroup is painted, we walk over its direct cullable
 * children from top to bottom and ask themselves to "cull out". Cullables
 * can use @clip_region to clip their drawing. Actors interested in eliminating
 * overdraw should copy the @clip_region and only paint those parts, as
 * everything else has been obscured by actors above it.
 *
 * Actors that may have fully opaque parts should also subtract out a region
 * that is fully opaque from @clip_region.
 *
 * Actors that have children can also use the meta_cullable_cull_redraw_clip_children()
 * helper method to do a simple cull across all their children.
 */
void
meta_cullable_cull_redraw_clip (MetaCullable *cullable,
                                MtkRegion    *clip_region)
{
  META_CULLABLE_GET_IFACE (cullable)->cull_redraw_clip (cullable, clip_region);
}
