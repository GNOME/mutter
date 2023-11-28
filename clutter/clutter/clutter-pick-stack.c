/*
 * Copyright (C) 2020 Endless OS Foundation, LLC
 * Copyright (C) 2018 Canonical Ltd.
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

#include "clutter/clutter-pick-stack-private.h"
#include "clutter/clutter-private.h"

typedef struct
{
  graphene_point3d_t vertices[4];
  CoglMatrixEntry *matrix_entry;
  ClutterActorBox rect;
  gboolean projected;
} Record;

typedef struct
{
  Record base;
  ClutterActor *actor;
  int clip_index;
  gboolean is_overlap;
} PickRecord;

typedef struct
{
  Record base;
  int prev;
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

static void
project_vertices (CoglMatrixEntry       *matrix_entry,
                  const ClutterActorBox *box,
                  graphene_point3d_t     vertices[4])
{
  graphene_matrix_t m;
  int i;

  cogl_matrix_entry_get (matrix_entry, &m);

  graphene_point3d_init (&vertices[0], box->x1, box->y1, 0.f);
  graphene_point3d_init (&vertices[1], box->x2, box->y1, 0.f);
  graphene_point3d_init (&vertices[2], box->x2, box->y2, 0.f);
  graphene_point3d_init (&vertices[3], box->x1, box->y2, 0.f);

  for (i = 0; i < 4; i++)
    {
      float w = 1.f;

      cogl_graphene_matrix_project_point (&m,
                                          &vertices[i].x,
                                          &vertices[i].y,
                                          &vertices[i].z,
                                          &w);
    }
}

static void
maybe_project_record (Record *rec)
{
  if (!rec->projected)
    {
      project_vertices (rec->matrix_entry, &rec->rect, rec->vertices);
      rec->projected = TRUE;
    }
}

static inline gboolean
is_axis_aligned_2d_rectangle (const graphene_point3d_t vertices[4])
{
  int i;

  for (i = 0; i < 4; i++)
    {
      if (!G_APPROX_VALUE (vertices[i].z,
                           vertices[(i + 1) % 4].z,
                           FLT_EPSILON))
        return FALSE;

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
ray_intersects_input_region (Record                   *rec,
                             const graphene_ray_t     *ray,
                             const graphene_point3d_t *point)
{
  maybe_project_record (rec);

  if (G_LIKELY (is_axis_aligned_2d_rectangle (rec->vertices)))
    {
      graphene_box_t box;
      graphene_box_t right_border;
      graphene_box_t bottom_border;

      /* Graphene considers both the start and end coordinates of boxes to be
       * inclusive, while the vertices of a clutter actor are exclusive. So we
       * need to manually exclude hits on these borders
       */

      graphene_box_init_from_points (&box, 4, rec->vertices);
      graphene_box_init_from_points (&right_border, 2, rec->vertices + 1);
      graphene_box_init_from_points (&bottom_border, 2, rec->vertices + 2);

      /* Fast path for actors without 3D transforms */
      if (graphene_box_contains_point (&box, point))
        {
          return !graphene_box_contains_point (&right_border, point) &&
                 !graphene_box_contains_point (&bottom_border, point);
        }

      return graphene_ray_intersects_box (ray, &box) &&
             !graphene_ray_intersects_box (ray, &right_border) &&
             !graphene_ray_intersects_box (ray, &bottom_border);
    }
  else
    {
      graphene_triangle_t t0, t1;

      /*
       * Degrade the projected quad into the following triangles:
       *
       * 0 -------------- 1
       * |  •             |
       * |     •     t0   |
       * |        •       |
       * |   t1      •    |
       * |              • |
       * 3 -------------- 2
       */

      graphene_triangle_init_from_point3d (&t0,
                                           &rec->vertices[0],
                                           &rec->vertices[1],
                                           &rec->vertices[2]);

      graphene_triangle_init_from_point3d (&t1,
                                           &rec->vertices[0],
                                           &rec->vertices[2],
                                           &rec->vertices[3]);

      return graphene_triangle_contains_point (&t0, point) ||
             graphene_triangle_contains_point (&t1, point) ||
             graphene_ray_intersects_triangle (ray, &t0) ||
             graphene_ray_intersects_triangle (ray, &t1);
    }
}

static gboolean
ray_intersects_record (ClutterPickStack         *pick_stack,
                       PickRecord               *rec,
                       const graphene_point3d_t *point,
                       const graphene_ray_t     *ray)
{
  int clip_index;

  if (!ray_intersects_input_region (&rec->base, ray, point))
    return FALSE;

  clip_index = rec->clip_index;
  while (clip_index >= 0)
    {
      PickClipRecord *clip =
        &g_array_index (pick_stack->clip_stack, PickClipRecord, clip_index);

      if (!ray_intersects_input_region (&clip->base, ray, point))
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
  g_clear_object (&pick_stack->matrix_stack);
  g_clear_pointer (&pick_stack->vertices_stack, g_array_unref);
  g_clear_pointer (&pick_stack->clip_stack, g_array_unref);
}

static void
clear_pick_record (gpointer data)
{
  PickRecord *rec = data;
  g_clear_pointer (&rec->base.matrix_entry, cogl_matrix_entry_unref);
}

static void
clear_clip_record (gpointer data)
{
  PickClipRecord *clip = data;
  g_clear_pointer (&clip->base.matrix_entry, cogl_matrix_entry_unref);
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
                             const ClutterActorBox  *box,
                             ClutterActor           *actor)
{
  PickRecord rec;

  g_return_if_fail (actor != NULL);

  g_assert (!pick_stack->sealed);

  rec.is_overlap = FALSE;
  rec.actor = actor;
  rec.clip_index = pick_stack->current_clip_stack_top;
  rec.base.rect = *box;
  rec.base.projected = FALSE;
  rec.base.matrix_entry = cogl_matrix_stack_get_entry (pick_stack->matrix_stack);
  cogl_matrix_entry_ref (rec.base.matrix_entry);

  g_array_append_val (pick_stack->vertices_stack, rec);
}

void
clutter_pick_stack_log_overlap (ClutterPickStack *pick_stack,
                                ClutterActor     *actor)
{
  PickRecord rec = { 0 };

  g_assert (!pick_stack->sealed);

  rec.is_overlap = TRUE;
  rec.actor = actor;
  rec.clip_index = pick_stack->current_clip_stack_top;

  g_array_append_val (pick_stack->vertices_stack, rec);
}

void
clutter_pick_stack_push_clip (ClutterPickStack      *pick_stack,
                              const ClutterActorBox *box)
{
  PickClipRecord clip;

  g_assert (!pick_stack->sealed);

  clip.prev = pick_stack->current_clip_stack_top;
  clip.base.rect = *box;
  clip.base.projected = FALSE;
  clip.base.matrix_entry = cogl_matrix_stack_get_entry (pick_stack->matrix_stack);
  cogl_matrix_entry_ref (clip.base.matrix_entry);

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

static gboolean
get_verts_rectangle (graphene_point3d_t  verts[4],
                     MtkRectangle       *rect)
{
  if (verts[0].x != verts[2].x ||
      verts[0].y != verts[1].y ||
      verts[3].x != verts[1].x ||
      verts[3].y != verts[2].y ||
      verts[0].x > verts[3].x ||
      verts[0].y > verts[3].y)
    return FALSE;

  *rect = (MtkRectangle) {
    .x = ceilf (verts[0].x),
    .y = ceilf (verts[0].y),
    .width = floor (verts[1].x - ceilf (verts[0].x)),
    .height = floor (verts[2].y - ceilf (verts[0].y)),
  };

  return TRUE;
}

static void
calculate_clear_area (ClutterPickStack  *pick_stack,
                      PickRecord        *pick_rec,
                      int                elem,
                      MtkRegion        **clear_area)
{
  MtkRegion *area = NULL;
  graphene_point3d_t verts[4];
  MtkRectangle rect;
  int i;

  if (!clutter_actor_has_allocation (pick_rec->actor))
    {
      if (clear_area)
        *clear_area = NULL;
      return;
    }

  clutter_actor_get_abs_allocation_vertices (pick_rec->actor,
                                             (graphene_point3d_t *) &verts);
  if (!get_verts_rectangle (verts, &rect))
    {
      if (clear_area)
        *clear_area = NULL;
      return;
    }

  rect.x += ceil (pick_rec->base.rect.x1);
  rect.y += ceil (pick_rec->base.rect.y1);
  rect.width =
    MIN (rect.width, floor (pick_rec->base.rect.x2 - pick_rec->base.rect.x1));
  rect.height =
    MIN (rect.height, floor (pick_rec->base.rect.y2 - pick_rec->base.rect.y1));

  area = mtk_region_create_rectangle (&rect);

  for (i = elem + 1; i < pick_stack->vertices_stack->len; i++)
    {
      PickRecord *rec =
        &g_array_index (pick_stack->vertices_stack, PickRecord, i);
      ClutterActorBox paint_box;

      if (!rec->is_overlap &&
          (rec->base.rect.x1 == rec->base.rect.x2 ||
           rec->base.rect.y1 == rec->base.rect.y2))
        continue;

      if (!clutter_actor_get_paint_box (rec->actor, &paint_box))
        continue;

      mtk_region_subtract_rectangle (area,
                                     &MTK_RECTANGLE_INIT (paint_box.x1, paint_box.y1,
                                                          paint_box.x2 - paint_box.x1,
                                                          paint_box.y2 - paint_box.y1)
      );
    }

  if (clear_area)
    *clear_area = g_steal_pointer (&area);

  g_clear_pointer (&area, mtk_region_unref);
}

ClutterActor *
clutter_pick_stack_search_actor (ClutterPickStack          *pick_stack,
                                 const graphene_point3d_t  *point,
                                 const graphene_ray_t      *ray,
                                 MtkRegion                **clear_area)
{
  int i;

  /* Search all "painted" pickable actors from front to back. A linear search
   * is required, and also performs fine since there is typically only
   * on the order of dozens of actors in the list (on screen) at a time.
   */
  for (i = pick_stack->vertices_stack->len - 1; i >= 0; i--)
    {
      PickRecord *rec =
        &g_array_index (pick_stack->vertices_stack, PickRecord, i);

      if (!rec->is_overlap && rec->actor &&
          ray_intersects_record (pick_stack, rec, point, ray))
        {
          if (clear_area)
            calculate_clear_area (pick_stack, rec, i, clear_area);
          return rec->actor;
        }
    }

  return NULL;
}
