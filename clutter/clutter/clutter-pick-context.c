/*
 * Copyright (C) 2019 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "clutter/clutter-backend.h"
#include "clutter/clutter-pick-context-private.h"

struct _ClutterPickContext
{
  grefcount ref_count;

  ClutterPickMode mode;
  ClutterPickStack *pick_stack;

  graphene_ray_t ray;
  graphene_point3d_t point;
};

G_DEFINE_BOXED_TYPE (ClutterPickContext, clutter_pick_context,
                     clutter_pick_context_ref,
                     clutter_pick_context_unref)

ClutterPickContext *
clutter_pick_context_new_for_view (ClutterStageView         *view,
                                   ClutterPickMode           mode,
                                   const graphene_point3d_t *point,
                                   const graphene_ray_t     *ray)
{
  ClutterPickContext *pick_context;
  CoglContext *context;

  pick_context = g_new0 (ClutterPickContext, 1);
  g_ref_count_init (&pick_context->ref_count);
  pick_context->mode = mode;
  graphene_ray_init_from_ray (&pick_context->ray, ray);
  graphene_point3d_init_from_point (&pick_context->point, point);

  context = clutter_backend_get_cogl_context (clutter_get_default_backend ());
  pick_context->pick_stack = clutter_pick_stack_new (context);

  return pick_context;
}

ClutterPickContext *
clutter_pick_context_ref (ClutterPickContext *pick_context)
{
  g_ref_count_inc (&pick_context->ref_count);
  return pick_context;
}

static void
clutter_pick_context_dispose (ClutterPickContext *pick_context)
{
  g_clear_pointer (&pick_context->pick_stack, clutter_pick_stack_unref);
}

void
clutter_pick_context_unref (ClutterPickContext *pick_context)
{
  if (g_ref_count_dec (&pick_context->ref_count))
    {
      clutter_pick_context_dispose (pick_context);
      g_free (pick_context);
    }
}

void
clutter_pick_context_destroy (ClutterPickContext *pick_context)
{
  clutter_pick_context_dispose (pick_context);
  clutter_pick_context_unref (pick_context);
}

/**
 * clutter_pick_context_get_mode: (skip)
 */
ClutterPickMode
clutter_pick_context_get_mode (ClutterPickContext *pick_context)
{
  return pick_context->mode;
}

ClutterPickStack *
clutter_pick_context_steal_stack (ClutterPickContext *pick_context)
{
  clutter_pick_stack_seal (pick_context->pick_stack);
  return g_steal_pointer (&pick_context->pick_stack);
}

/**
 * clutter_pick_context_log_pick:
 * @pick_context: a #ClutterPickContext
 * @box: a #ClutterActorBox
 * @actor: a #ClutterActor
 *
 * Logs a pick rectangle into the pick stack.
 */
void
clutter_pick_context_log_pick (ClutterPickContext    *pick_context,
                               const ClutterActorBox *box,
                               ClutterActor          *actor)
{
  clutter_pick_stack_log_pick (pick_context->pick_stack, box, actor);
}

/**
 * clutter_pick_context_log_overlap:
 * @pick_context: a #ClutterPickContext
 * @actor: a #ClutterActor
 *
 * Logs an overlapping actor into the pick stack.
 */
void
clutter_pick_context_log_overlap (ClutterPickContext *pick_context,
                                  ClutterActor       *actor)
{
  clutter_pick_stack_log_overlap (pick_context->pick_stack, actor);
}

/**
 * clutter_pick_context_push_clip:
 * @pick_context: a #ClutterPickContext
 * @box: a #ClutterActorBox
 *
 * Pushes a clip rectangle defined by @box into the pick stack. Pop with
 * [method@PickContext.pop_clip] when done.
 */
void
clutter_pick_context_push_clip (ClutterPickContext    *pick_context,
                                const ClutterActorBox *box)
{
  clutter_pick_stack_push_clip (pick_context->pick_stack, box);
}

/**
 * clutter_pick_context_pop_clip:
 * @pick_context: a #ClutterPickContext
 *
 * Pops the current clip rectangle from the clip stack. It is a programming
 * error to call this without a corresponding [method@PickContext.push_clip]
 * call first.
 */
void
clutter_pick_context_pop_clip (ClutterPickContext *pick_context)
{
  clutter_pick_stack_pop_clip (pick_context->pick_stack);
}

/**
 * clutter_pick_context_push_transform:
 * @pick_context: a #ClutterPickContext
 * @transform: a #graphene_matrix_t
 *
 * Pushes @transform into the pick stack. Pop with
 * [method@PickContext.pop_transform] when done.
 */
void
clutter_pick_context_push_transform (ClutterPickContext      *pick_context,
                                     const graphene_matrix_t *transform)
{
  clutter_pick_stack_push_transform (pick_context->pick_stack, transform);
}

/**
 * clutter_pick_context_get_transform:
 * @pick_context: a #ClutterPickContext
 * @out_matrix: (out): a #graphene_matrix_t
 *
 * Retrieves the current transform of the pick stack.
 */
void
clutter_pick_context_get_transform (ClutterPickContext *pick_context,
                                    graphene_matrix_t  *out_transform)
{
  clutter_pick_stack_get_transform (pick_context->pick_stack, out_transform);
}

/**
 * clutter_pick_context_pop_transform:
 * @pick_context: a #ClutterPickContext
 *
 * Pops the current transform from the clip stack. It is a programming error
 * to call this without a corresponding [method@PickContext.push_transform]
 * call first.
 */
void
clutter_pick_context_pop_transform (ClutterPickContext *pick_context)
{
  clutter_pick_stack_pop_transform (pick_context->pick_stack);
}

gboolean
clutter_pick_context_intersects_box (ClutterPickContext   *pick_context,
                                     const graphene_box_t *box)
{
  return graphene_box_contains_point (box, &pick_context->point) ||
         graphene_ray_intersects_box (&pick_context->ray, box);
}
