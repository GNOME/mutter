/*
 * Copyright (C) 2020 Endless OS Foundation, LLC
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

#include "clutter-pick-stack-private.h"
#include "clutter-private.h"

typedef struct
{
  graphene_point_t vertices[4];
  ClutterActor *actor;
  int clip_index;
  CoglMatrixEntry *matrix_entry;
} PickRecord;

typedef struct
{
  int prev;
  graphene_point_t vertices[4];
  CoglMatrixEntry *matrix_entry;
} PickClipRecord;

struct _ClutterPickStack
{
  grefcount ref_count;

  CoglMatrixStack *matrix_stack;
  GArray *vertices_stack;
  GArray *clip_stack;
  int current_clip_stack_top;

  gboolean sealed : 1;
};

G_DEFINE_BOXED_TYPE (ClutterPickStack, clutter_pick_stack,
                     clutter_pick_stack_ref, clutter_pick_stack_unref)

static gboolean
is_quadrilateral_axis_aligned_rectangle (const graphene_point_t vertices[4])
{
  int i;

  for (i = 0; i < 4; i++)
    {
      if (!G_APPROX_VALUE (vertices[i].x,
                           vertices[(i + 1) % 4].x,
                           FLT_EPSILON) &&
          !G_APPROX_VALUE (vertices[i].y,
                           vertices[(i + 1) % 4].y,
                           FLT_EPSILON))
        return FALSE;
    }
  return TRUE;
}

static gboolean
is_inside_axis_aligned_rectangle (const graphene_point_t *point,
                                  const graphene_point_t  vertices[4])
{
  float min_x = FLT_MAX;
  float max_x = -FLT_MAX;
  float min_y = FLT_MAX;
  float max_y = -FLT_MAX;
  int i;

  for (i = 0; i < 4; i++)
    {
      min_x = MIN (min_x, vertices[i].x);
      min_y = MIN (min_y, vertices[i].y);
      max_x = MAX (max_x, vertices[i].x);
      max_y = MAX (max_y, vertices[i].y);
    }

  return (point->x >= min_x &&
          point->y >= min_y &&
          point->x < max_x &&
          point->y < max_y);
}

static int
clutter_point_compare_line (const graphene_point_t *p,
                            const graphene_point_t *a,
                            const graphene_point_t *b)
{
  graphene_vec3_t vec_pa;
  graphene_vec3_t vec_pb;
  graphene_vec3_t cross;
  float cross_z;

  graphene_vec3_init (&vec_pa, p->x - a->x, p->y - a->y, 0.f);
  graphene_vec3_init (&vec_pb, p->x - b->x, p->y - b->y, 0.f);
  graphene_vec3_cross (&vec_pa, &vec_pb, &cross);
  cross_z = graphene_vec3_get_z (&cross);

  if (cross_z > 0.f)
    return 1;
  else if (cross_z < 0.f)
    return -1;
  else
    return 0;
}

static gboolean
is_inside_unaligned_rectangle (const graphene_point_t *point,
                               const graphene_point_t  vertices[4])
{
  unsigned int i;
  int first_side;

  first_side = 0;

  for (i = 0; i < 4; i++)
    {
      int side;

      side = clutter_point_compare_line (point,
                                         &vertices[i],
                                         &vertices[(i + 1) % 4]);

      if (side)
        {
          if (first_side == 0)
            first_side = side;
          else if (side != first_side)
            return FALSE;
        }
    }

  if (first_side == 0)
    return FALSE;

  return TRUE;
}

static gboolean
is_inside_input_region (const graphene_point_t *point,
                        const graphene_point_t  vertices[4])
{

  if (is_quadrilateral_axis_aligned_rectangle (vertices))
    return is_inside_axis_aligned_rectangle (point, vertices);
  else
    return is_inside_unaligned_rectangle (point, vertices);
}

static gboolean
pick_record_contains_point (ClutterPickStack *pick_stack,
                            const PickRecord *rec,
                            float             x,
                            float             y)
{
  const graphene_point_t point = GRAPHENE_POINT_INIT (x, y);
  int clip_index;

  if (!is_inside_input_region (&point, rec->vertices))
    return FALSE;

  clip_index = rec->clip_index;
  while (clip_index >= 0)
    {
      const PickClipRecord *clip =
        &g_array_index (pick_stack->clip_stack, PickClipRecord, clip_index);

      if (!is_inside_input_region (&point, clip->vertices))
        return FALSE;

      clip_index = clip->prev;
    }

  return TRUE;
}

static void
add_pick_stack_weak_refs (ClutterPickStack *pick_stack)
{
  int i;

  g_assert (!pick_stack->sealed);

  for (i = 0; i < pick_stack->vertices_stack->len; i++)
    {
      PickRecord *rec =
        &g_array_index (pick_stack->vertices_stack, PickRecord, i);

      if (rec->actor)
        g_object_add_weak_pointer (G_OBJECT (rec->actor),
                                   (gpointer) &rec->actor);
    }
}

static void
remove_pick_stack_weak_refs (ClutterPickStack *pick_stack)
{
  int i;

  for (i = 0; i < pick_stack->vertices_stack->len; i++)
    {
      PickRecord *rec =
        &g_array_index (pick_stack->vertices_stack, PickRecord, i);

      if (rec->actor)
        g_object_remove_weak_pointer (G_OBJECT (rec->actor),
                                      (gpointer) &rec->actor);
    }
}

static void
clutter_pick_stack_dispose (ClutterPickStack *pick_stack)
{
  remove_pick_stack_weak_refs (pick_stack);
  g_clear_pointer (&pick_stack->matrix_stack, cogl_object_unref);
  g_clear_pointer (&pick_stack->vertices_stack, g_array_unref);
  g_clear_pointer (&pick_stack->clip_stack, g_array_unref);
}

static void
clear_pick_record (gpointer data)
{
  PickRecord *rec = data;
  g_clear_pointer (&rec->matrix_entry, cogl_matrix_entry_unref);
}

static void
clear_clip_record (gpointer data)
{
  PickClipRecord *clip = data;
  g_clear_pointer (&clip->matrix_entry, cogl_matrix_entry_unref);
}

/**
 * clutter_pick_stack_new:
 * @context: a #CoglContext
 *
 * Creates a new #ClutterPickStack.
 *
 * Returns: (transfer full): A newly created #ClutterPickStack
 */
ClutterPickStack *
clutter_pick_stack_new (CoglContext *context)
{
  ClutterPickStack *pick_stack;

  pick_stack = g_new0 (ClutterPickStack, 1);
  g_ref_count_init (&pick_stack->ref_count);
  pick_stack->matrix_stack = cogl_matrix_stack_new (context);
  pick_stack->vertices_stack = g_array_new (FALSE, FALSE, sizeof (PickRecord));
  pick_stack->clip_stack = g_array_new (FALSE, FALSE, sizeof (PickClipRecord));
  pick_stack->current_clip_stack_top = -1;

  g_array_set_clear_func (pick_stack->vertices_stack, clear_pick_record);
  g_array_set_clear_func (pick_stack->clip_stack, clear_clip_record);

  return pick_stack;
}

/**
 * clutter_pick_stack_ref:
 * @pick_stack: A #ClutterPickStack
 *
 * Increments the reference count of @pick_stack by one.
 *
 * Returns: (transfer full): @pick_stack
 */
ClutterPickStack *
clutter_pick_stack_ref (ClutterPickStack *pick_stack)
{
  g_ref_count_inc (&pick_stack->ref_count);
  return pick_stack;
}

/**
 * clutter_pick_stack_unref:
 * @pick_stack: A #ClutterPickStack
 *
 * Decrements the reference count of @pick_stack by one, freeing the structure
 * when the reference count reaches zero.
 */
void
clutter_pick_stack_unref (ClutterPickStack *pick_stack)
{
  if (g_ref_count_dec (&pick_stack->ref_count))
    {
      clutter_pick_stack_dispose (pick_stack);
      g_free (pick_stack);
    }
}

void
clutter_pick_stack_seal (ClutterPickStack *pick_stack)
{
  g_assert (!pick_stack->sealed);
  add_pick_stack_weak_refs (pick_stack);
  pick_stack->sealed = TRUE;
}

void
clutter_pick_stack_log_pick (ClutterPickStack       *pick_stack,
                             const graphene_point_t  vertices[4],
                             ClutterActor           *actor)
{
  PickRecord rec;

  g_return_if_fail (actor != NULL);

  g_assert (!pick_stack->sealed);

  memcpy (rec.vertices, vertices, 4 * sizeof (graphene_point_t));
  rec.actor = actor;
  rec.clip_index = pick_stack->current_clip_stack_top;
  rec.matrix_entry = cogl_matrix_stack_get_entry (pick_stack->matrix_stack);
  cogl_matrix_entry_ref (rec.matrix_entry);

  g_array_append_val (pick_stack->vertices_stack, rec);
}

void
clutter_pick_stack_push_clip (ClutterPickStack       *pick_stack,
                              const graphene_point_t  vertices[4])
{
  PickClipRecord clip;

  g_assert (!pick_stack->sealed);

  clip.prev = pick_stack->current_clip_stack_top;
  memcpy (clip.vertices, vertices, 4 * sizeof (graphene_point_t));
  clip.matrix_entry = cogl_matrix_stack_get_entry (pick_stack->matrix_stack);
  cogl_matrix_entry_ref (clip.matrix_entry);

  g_array_append_val (pick_stack->clip_stack, clip);
  pick_stack->current_clip_stack_top = pick_stack->clip_stack->len - 1;
}

void
clutter_pick_stack_pop_clip (ClutterPickStack *pick_stack)
{
  const PickClipRecord *top;

  g_assert (!pick_stack->sealed);
  g_assert (pick_stack->current_clip_stack_top >= 0);

  /* Individual elements of clip_stack are not freed. This is so they can
   * be shared as part of a tree of different stacks used by different
   * actors in the pick_stack. The whole clip_stack does however get
   * freed later in clutter_pick_stack_dispose.
   */

  top = &g_array_index (pick_stack->clip_stack,
                        PickClipRecord,
                        pick_stack->current_clip_stack_top);

  pick_stack->current_clip_stack_top = top->prev;
}

void
clutter_pick_stack_push_transform (ClutterPickStack        *pick_stack,
                                   const graphene_matrix_t *transform)
{
  cogl_matrix_stack_push (pick_stack->matrix_stack);
  cogl_matrix_stack_multiply (pick_stack->matrix_stack, transform);
}

void
clutter_pick_stack_get_transform (ClutterPickStack  *pick_stack,
                                  graphene_matrix_t *out_transform)
{
  cogl_matrix_stack_get (pick_stack->matrix_stack, out_transform);
}

void
clutter_pick_stack_pop_transform (ClutterPickStack *pick_stack)
{
  cogl_matrix_stack_pop (pick_stack->matrix_stack);
}

ClutterActor *
clutter_pick_stack_find_actor_at (ClutterPickStack *pick_stack,
                                  float             x,
                                  float             y)
{
  int i;

  /* Search all "painted" pickable actors from front to back. A linear search
   * is required, and also performs fine since there is typically only
   * on the order of dozens of actors in the list (on screen) at a time.
   */
  for (i = pick_stack->vertices_stack->len - 1; i >= 0; i--)
    {
      const PickRecord *rec =
        &g_array_index (pick_stack->vertices_stack, PickRecord, i);

      if (rec->actor && pick_record_contains_point (pick_stack, rec, x, y))
        return rec->actor;
    }

  return NULL;
}
