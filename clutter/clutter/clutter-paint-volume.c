/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2010  Intel Corporation.
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
 * License along with this library. If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *      Robert Bragg <robert@linux.intel.com>
 *      Emmanuele Bassi <ebassi@linux.intel.com>
 */

#include "config.h"

#include <string.h>

#include <glib-object.h>
#include <math.h>

#include "clutter/clutter-actor-private.h"
#include "clutter/clutter-paint-volume-private.h"
#include "clutter/clutter-private.h"
#include "clutter/clutter-stage-private.h"
#include "clutter/clutter-actor-box-private.h"

static void _clutter_paint_volume_axis_align (ClutterPaintVolume *pv);

G_DEFINE_BOXED_TYPE (ClutterPaintVolume, clutter_paint_volume,
                     clutter_paint_volume_copy,
                     clutter_paint_volume_free);

/* Since paint volumes are used so heavily in a typical paint
 * traversal of a Clutter scene graph and since paint volumes often
 * have a very short life cycle that maps well to stack allocation we
 * allow initializing a static ClutterPaintVolume variable to avoid
 * hammering the memory allocator.
 *
 * We were seeing slice allocation take about 1% cumulative CPU time
 * for some very simple clutter tests which although it isn't a *lot*
 * this is an easy way to basically drop that to 0%.
 */
void
clutter_paint_volume_init_from_actor (ClutterPaintVolume *pv,
                                      ClutterActor       *actor)
{
  pv->actor = actor;

  memset (pv->vertices, 0, 8 * sizeof (graphene_point3d_t));

  pv->is_empty = TRUE;
  pv->is_axis_aligned = TRUE;
  pv->is_complete = TRUE;
  pv->is_2d = TRUE;
}

void
clutter_paint_volume_init_from_paint_volume (ClutterPaintVolume       *dst_pv,
                                             const ClutterPaintVolume *src_pv)
{

  g_return_if_fail (src_pv != NULL && dst_pv != NULL);

  memcpy (dst_pv, src_pv, sizeof (ClutterPaintVolume));
}

/**
 * clutter_paint_volume_copy:
 * @pv: a #ClutterPaintVolume
 *
 * Copies @pv into a new #ClutterPaintVolume
 *
 * Return value: a newly allocated copy of a #ClutterPaintVolume
 */
ClutterPaintVolume *
clutter_paint_volume_copy (const ClutterPaintVolume *pv)
{
  ClutterPaintVolume *copy;

  g_return_val_if_fail (pv != NULL, NULL);

  copy = g_memdup2 (pv, sizeof (ClutterPaintVolume));

  return copy;
}

/**
 * clutter_paint_volume_free:
 * @pv: a #ClutterPaintVolume
 *
 * Frees the resources allocated by @pv
 */
void
clutter_paint_volume_free (ClutterPaintVolume *pv)
{
  g_return_if_fail (pv != NULL);

  g_free (pv);
}

/**
 * clutter_paint_volume_set_origin:
 * @pv: a #ClutterPaintVolume
 * @origin: a #graphene_point3d_t
 *
 * Sets the origin of the paint volume.
 *
 * The origin is defined as the X, Y and Z coordinates of the top-left
 * corner of an actor's paint volume, in actor coordinates.
 *
 * The default is origin is assumed at: (0, 0, 0)
 */
void
clutter_paint_volume_set_origin (ClutterPaintVolume       *pv,
                                 const graphene_point3d_t *origin)
{
  static const int key_vertices[4] = { 0, 1, 3, 4 };
  float dx, dy, dz;
  int i;

  g_return_if_fail (pv != NULL);

  dx = origin->x - pv->vertices[0].x;
  dy = origin->y - pv->vertices[0].y;
  dz = origin->z - pv->vertices[0].z;

  /* If we change the origin then all the key vertices of the paint
   * volume need to be shifted too... */
  for (i = 0; i < 4; i++)
    {
      pv->vertices[key_vertices[i]].x += dx;
      pv->vertices[key_vertices[i]].y += dy;
      pv->vertices[key_vertices[i]].z += dz;
    }

  pv->is_complete = FALSE;
}

/**
 * clutter_paint_volume_get_origin:
 * @pv: a #ClutterPaintVolume
 * @vertex: (out): the return location for a #graphene_point3d_t
 *
 * Retrieves the origin of the #ClutterPaintVolume.
 */
void
clutter_paint_volume_get_origin (const ClutterPaintVolume *pv,
                                 graphene_point3d_t       *vertex)
{
  g_return_if_fail (pv != NULL);
  g_return_if_fail (vertex != NULL);

  *vertex = pv->vertices[0];
}

static void
_clutter_paint_volume_update_is_empty (ClutterPaintVolume *pv)
{
  if (pv->vertices[0].x == pv->vertices[1].x &&
      pv->vertices[0].y == pv->vertices[3].y &&
      pv->vertices[0].z == pv->vertices[4].z)
    pv->is_empty = TRUE;
  else
    pv->is_empty = FALSE;
}

/**
 * clutter_paint_volume_set_width:
 * @pv: a #ClutterPaintVolume
 * @width: the width of the paint volume, in pixels
 *
 * Sets the width of the paint volume. The width is measured along
 * the x axis in the actor coordinates that @pv is associated with.
 */
void
clutter_paint_volume_set_width (ClutterPaintVolume *pv,
                                gfloat              width)
{
  gfloat right_xpos;

  g_return_if_fail (pv != NULL);
  g_return_if_fail (width >= 0.0f);

  /* If the volume is currently empty then only the origin is
   * currently valid */
  if (pv->is_empty)
    pv->vertices[1] = pv->vertices[3] = pv->vertices[4] = pv->vertices[0];

  if (!pv->is_axis_aligned)
    _clutter_paint_volume_axis_align (pv);

  right_xpos = pv->vertices[0].x + width;

  /* Move the right vertices of the paint box relative to the
   * origin... */
  pv->vertices[1].x = right_xpos;
  /* pv->vertices[2].x = right_xpos; NB: updated lazily */
  /* pv->vertices[5].x = right_xpos; NB: updated lazily */
  /* pv->vertices[6].x = right_xpos; NB: updated lazily */

  pv->is_complete = FALSE;

  _clutter_paint_volume_update_is_empty (pv);
}

/**
 * clutter_paint_volume_get_width:
 * @pv: a #ClutterPaintVolume
 *
 * Retrieves the width of the volume's, axis aligned, bounding box.
 *
 * In other words; this takes into account what actor's coordinate
 * space @pv belongs too and conceptually fits an axis aligned box
 * around the volume. It returns the size of that bounding box as
 * measured along the x-axis.
 *
 * If, for example, [method@Actor.get_transformed_paint_volume]
 * is used to transform a 2D child actor that is 100px wide, 100px
 * high and 0px deep into container coordinates then the width might
 * not simply be 100px if the child actor has a 3D rotation applied to
 * it.
 * 
 * Remember: if [method@Actor.get_transformed_paint_volume] is
 * used then a transformed child volume will be defined relative to the
 * ancestor container actor and so a 2D child actor can have a 3D
 * bounding volume.
 *
 * There are no accuracy guarantees for the reported width,
 * except that it must always be greater than, or equal to, the
 * actor's width. This is because actors may report simple, loose
 * fitting paint volumes for efficiency.

 * Return value: the width, in units of @pv's local coordinate system.
 */
gfloat
clutter_paint_volume_get_width (const ClutterPaintVolume *pv)
{
  g_return_val_if_fail (pv != NULL, 0.0);

  if (pv->is_empty)
    return 0;
  else if (!pv->is_axis_aligned)
    {
      ClutterPaintVolume tmp;
      float width;
      clutter_paint_volume_init_from_paint_volume (&tmp, pv);
      _clutter_paint_volume_axis_align (&tmp);
      width = tmp.vertices[1].x - tmp.vertices[0].x;
      return width;
    }
  else
    return pv->vertices[1].x - pv->vertices[0].x;
}

/**
 * clutter_paint_volume_set_height:
 * @pv: a #ClutterPaintVolume
 * @height: the height of the paint volume, in pixels
 *
 * Sets the height of the paint volume. The height is measured along
 * the y axis in the actor coordinates that @pv is associated with.
 */
void
clutter_paint_volume_set_height (ClutterPaintVolume *pv,
                                 gfloat              height)
{
  gfloat height_ypos;

  g_return_if_fail (pv != NULL);
  g_return_if_fail (height >= 0.0f);

  /* If the volume is currently empty then only the origin is
   * currently valid */
  if (pv->is_empty)
    pv->vertices[1] = pv->vertices[3] = pv->vertices[4] = pv->vertices[0];

  if (!pv->is_axis_aligned)
    _clutter_paint_volume_axis_align (pv);

  height_ypos = pv->vertices[0].y + height;

  /* Move the bottom vertices of the paint box relative to the
   * origin... */
  /* pv->vertices[2].y = height_ypos; NB: updated lazily */
  pv->vertices[3].y = height_ypos;
  /* pv->vertices[6].y = height_ypos; NB: updated lazily */
  /* pv->vertices[7].y = height_ypos; NB: updated lazily */
  pv->is_complete = FALSE;

  _clutter_paint_volume_update_is_empty (pv);
}

/**
 * clutter_paint_volume_get_height:
 * @pv: a #ClutterPaintVolume
 *
 * Retrieves the height of the volume's, axis aligned, bounding box.
 *
 * In other words; this takes into account what actor's coordinate
 * space @pv belongs too and conceptually fits an axis aligned box
 * around the volume. It returns the size of that bounding box as
 * measured along the y-axis.
 *
 * If, for example, [method@Actor.get_transformed_paint_volume]
 * is used to transform a 2D child actor that is 100px wide, 100px
 * high and 0px deep into container coordinates then the height might
 * not simply be 100px if the child actor has a 3D rotation applied to
 * it.
 *
 * Remember: if [method@Actor.get_transformed_paint_volume] is
 * used then a transformed child volume will be defined relative to the
 * ancestor container actor and so a 2D child actor
 * can have a 3D bounding volume.
 *
 * There are no accuracy guarantees for the reported height,
 * except that it must always be greater than, or equal to, the actor's
 * height. This is because actors may report simple, loose fitting paint
 * volumes for efficiency.
 *
 * Return value: the height, in units of @pv's local coordinate system.
 */
gfloat
clutter_paint_volume_get_height (const ClutterPaintVolume *pv)
{
  g_return_val_if_fail (pv != NULL, 0.0);

  if (pv->is_empty)
    return 0;
  else if (!pv->is_axis_aligned)
    {
      ClutterPaintVolume tmp;
      float height;
      clutter_paint_volume_init_from_paint_volume (&tmp, pv);
      _clutter_paint_volume_axis_align (&tmp);
      height = tmp.vertices[3].y - tmp.vertices[0].y;
      return height;
    }
  else
    return pv->vertices[3].y - pv->vertices[0].y;
}

/**
 * clutter_paint_volume_set_depth:
 * @pv: a #ClutterPaintVolume
 * @depth: the depth of the paint volume, in pixels
 *
 * Sets the depth of the paint volume. The depth is measured along
 * the z axis in the actor coordinates that @pv is associated with.
 */
void
clutter_paint_volume_set_depth (ClutterPaintVolume *pv,
                                gfloat              depth)
{
  gfloat depth_zpos;

  g_return_if_fail (pv != NULL);
  g_return_if_fail (depth >= 0.0f);

  /* If the volume is currently empty then only the origin is
   * currently valid */
  if (pv->is_empty)
    pv->vertices[1] = pv->vertices[3] = pv->vertices[4] = pv->vertices[0];

  if (!pv->is_axis_aligned)
    _clutter_paint_volume_axis_align (pv);

  depth_zpos = pv->vertices[0].z + depth;

  /* Move the back vertices of the paint box relative to the
   * origin... */
  pv->vertices[4].z = depth_zpos;
  /* pv->vertices[5].z = depth_zpos; NB: updated lazily */
  /* pv->vertices[6].z = depth_zpos; NB: updated lazily */
  /* pv->vertices[7].z = depth_zpos; NB: updated lazily */

  pv->is_complete = FALSE;
  pv->is_2d = depth ? FALSE : TRUE;
  _clutter_paint_volume_update_is_empty (pv);
}

/**
 * clutter_paint_volume_get_depth:
 * @pv: a #ClutterPaintVolume
 *
 * Retrieves the depth of the volume's, axis aligned, bounding box.
 *
 * In other words; this takes into account what actor's coordinate
 * space @pv belongs too and conceptually fits an axis aligned box
 * around the volume. It returns the size of that bounding box as
 * measured along the z-axis.
 *
 * If, for example, [method@Actor.get_transformed_paint_volume]
 * is used to transform a 2D child actor that is 100px wide, 100px
 * high and 0px deep into container coordinates then the depth might
 * not simply be 0px if the child actor has a 3D rotation applied to
 * it.
 *
 * Remember: if [method@Actor.get_transformed_paint_volume] is
 * used then the transformed volume will be defined relative to the
 * container actor and in container coordinates a 2D child actor
 * can have a 3D bounding volume.
 *
 * There are no accuracy guarantees for the reported depth,
 * except that it must always be greater than, or equal to, the actor's
 * depth. This is because actors may report simple, loose fitting paint
 * volumes for efficiency.
 *
 * Return value: the depth, in units of @pv's local coordinate system.
 */
gfloat
clutter_paint_volume_get_depth (const ClutterPaintVolume *pv)
{
  g_return_val_if_fail (pv != NULL, 0.0);

  if (pv->is_empty)
    return 0;
  else if (!pv->is_axis_aligned)
    {
      ClutterPaintVolume tmp;
      float depth;
      clutter_paint_volume_init_from_paint_volume (&tmp, pv);
      _clutter_paint_volume_axis_align (&tmp);
      depth = tmp.vertices[4].z - tmp.vertices[0].z;
      return depth;
    }
  else
    return pv->vertices[4].z - pv->vertices[0].z;
}

/**
 * clutter_paint_volume_union:
 * @pv: The first #ClutterPaintVolume and destination for resulting
 *      union
 * @another_pv: A second #ClutterPaintVolume to union with @pv
 *
 * Updates the geometry of @pv to encompass @pv and @another_pv.
 *
 * There are no guarantees about how precisely the two volumes
 * will be unioned.
 */
void
clutter_paint_volume_union (ClutterPaintVolume *pv,
                            const ClutterPaintVolume *another_pv)
{
  ClutterPaintVolume aligned_pv;
  graphene_point3d_t min;
  graphene_point3d_t max;
  graphene_box_t another_box;
  graphene_box_t union_box;
  graphene_box_t box;

  g_return_if_fail (pv != NULL);
  g_return_if_fail (another_pv != NULL);

  /* Both volumes have to belong to the same local coordinate space */
  g_return_if_fail (pv->actor == another_pv->actor);

  /* We special case empty volumes because otherwise we'd end up
   * calculating a bounding box that would enclose the origin of
   * the empty volume which isn't desired.
   */
  if (another_pv->is_empty)
    return;

  if (pv->is_empty)
    {
      clutter_paint_volume_init_from_paint_volume (pv, another_pv);
      goto done;
    }

  if (!pv->is_axis_aligned)
    _clutter_paint_volume_axis_align (pv);

  _clutter_paint_volume_complete (pv);

  if (!another_pv->is_axis_aligned || !another_pv->is_complete)
    {
      clutter_paint_volume_init_from_paint_volume (&aligned_pv, another_pv);
      _clutter_paint_volume_axis_align (&aligned_pv);
      _clutter_paint_volume_complete (&aligned_pv);
      another_pv = &aligned_pv;
    }

  if (G_LIKELY (pv->is_2d))
    graphene_box_init_from_points (&box, 4, pv->vertices);
  else
    graphene_box_init_from_points (&box, 8, pv->vertices);

  if (G_LIKELY (another_pv->is_2d))
    graphene_box_init_from_points (&another_box, 4, another_pv->vertices);
  else
    graphene_box_init_from_points (&another_box, 8, another_pv->vertices);

  graphene_box_union (&box, &another_box, &union_box);

  graphene_box_get_min (&union_box, &min);
  graphene_box_get_max (&union_box, &max);
  graphene_point3d_init (&pv->vertices[0], min.x, min.y, min.z);
  graphene_point3d_init (&pv->vertices[1], max.x, min.y, min.z);
  graphene_point3d_init (&pv->vertices[3], min.x, max.y, min.z);
  graphene_point3d_init (&pv->vertices[4], min.x, min.y, max.z);

  if (pv->vertices[4].z == pv->vertices[0].z)
    pv->is_2d = TRUE;
  else
    pv->is_2d = FALSE;

done:
  pv->is_empty = FALSE;
  pv->is_complete = FALSE;
}

/**
 * clutter_paint_volume_union_box:
 * @pv: a #ClutterPaintVolume
 * @box: a #ClutterActorBox to union to @pv
 *
 * Unions the 2D region represented by @box to a #ClutterPaintVolume.
 *
 * This function is similar to [method@PaintVolume.union], but it is
 * specific for 2D regions.
 */
void
clutter_paint_volume_union_box (ClutterPaintVolume    *pv,
                                const ClutterActorBox *box)
{
  ClutterPaintVolume volume;
  graphene_point3d_t origin;

  g_return_if_fail (pv != NULL);
  g_return_if_fail (box != NULL);

  clutter_paint_volume_init_from_actor (&volume, pv->actor);

  origin.x = box->x1;
  origin.y = box->y1;
  origin.z = 0.f;
  clutter_paint_volume_set_origin (&volume, &origin);
  clutter_paint_volume_set_width (&volume, box->x2 - box->x1);
  clutter_paint_volume_set_height (&volume, box->y2 - box->y1);

  clutter_paint_volume_union (pv, &volume);
}

/* The paint_volume setters only update vertices 0, 1, 3 and
 * 4 since the others can be drived from them.
 *
 * This will set pv->completed = TRUE;
 */
void
_clutter_paint_volume_complete (ClutterPaintVolume *pv)
{
  float dx_l2r, dy_l2r, dz_l2r;
  float dx_t2b, dy_t2b, dz_t2b;

  if (pv->is_empty)
    return;

  if (pv->is_complete)
    return;

  /* Find the vector that takes us from any vertex on the left face to
   * the corresponding vertex on the right face. */
  dx_l2r = pv->vertices[1].x - pv->vertices[0].x;
  dy_l2r = pv->vertices[1].y - pv->vertices[0].y;
  dz_l2r = pv->vertices[1].z - pv->vertices[0].z;

  /* Find the vector that takes us from any vertex on the top face to
   * the corresponding vertex on the bottom face. */
  dx_t2b = pv->vertices[3].x - pv->vertices[0].x;
  dy_t2b = pv->vertices[3].y - pv->vertices[0].y;
  dz_t2b = pv->vertices[3].z - pv->vertices[0].z;

  /* front-bottom-right */
  pv->vertices[2].x = pv->vertices[3].x + dx_l2r;
  pv->vertices[2].y = pv->vertices[3].y + dy_l2r;
  pv->vertices[2].z = pv->vertices[3].z + dz_l2r;

  if (G_UNLIKELY (!pv->is_2d))
    {
      /* back-top-right */
      pv->vertices[5].x = pv->vertices[4].x + dx_l2r;
      pv->vertices[5].y = pv->vertices[4].y + dy_l2r;
      pv->vertices[5].z = pv->vertices[4].z + dz_l2r;

      /* back-bottom-right */
      pv->vertices[6].x = pv->vertices[5].x + dx_t2b;
      pv->vertices[6].y = pv->vertices[5].y + dy_t2b;
      pv->vertices[6].z = pv->vertices[5].z + dz_t2b;

      /* back-bottom-left */
      pv->vertices[7].x = pv->vertices[4].x + dx_t2b;
      pv->vertices[7].y = pv->vertices[4].y + dy_t2b;
      pv->vertices[7].z = pv->vertices[4].z + dz_t2b;
    }

  pv->is_complete = TRUE;
}

/*<private>
 * _clutter_paint_volume_get_box:
 * @pv: a #ClutterPaintVolume
 * @box: a pixel aligned #ClutterActorBox
 *
 * Transforms a 3D paint volume into a 2D bounding box in the
 * same coordinate space as the 3D paint volume.
 *
 * To get an actors "paint box" you should first project
 * the paint volume into window coordinates before getting
 * the 2D bounding box.
 *
 * The coordinates of the returned box are not clamped to
 * integer pixel values; if you need them to be rounded to the
 * nearest integer pixel values, you can use the
 * clutter_actor_box_clamp_to_pixel() function.
 */
void
_clutter_paint_volume_get_bounding_box (ClutterPaintVolume *pv,
                                        ClutterActorBox *box)
{
  gfloat x_min, y_min, x_max, y_max;
  graphene_point3d_t *vertices;
  int count;
  gint i;

  g_return_if_fail (pv != NULL);
  g_return_if_fail (box != NULL);

  if (pv->is_empty)
    {
      box->x1 = box->x2 = pv->vertices[0].x;
      box->y1 = box->y2 = pv->vertices[0].y;
      return;
    }

  /* Updates the vertices we calculate lazily
   * (See ClutterPaintVolume typedef for more details) */
  _clutter_paint_volume_complete (pv);

  vertices = pv->vertices;

  x_min = x_max = vertices[0].x;
  y_min = y_max = vertices[0].y;

  /* Most actors are 2D so we only have to look at the front 4
   * vertices of the paint volume... */
  if (G_LIKELY (pv->is_2d))
    count = 4;
  else
    count = 8;

  for (i = 1; i < count; i++)
    {
      if (vertices[i].x < x_min)
        x_min = vertices[i].x;
      else if (vertices[i].x > x_max)
        x_max = vertices[i].x;

      if (vertices[i].y < y_min)
        y_min = vertices[i].y;
      else if (vertices[i].y > y_max)
        y_max = vertices[i].y;
    }

  box->x1 = x_min;
  box->y1 = y_min;
  box->x2 = x_max;
  box->y2 = y_max;
}

static void
_clutter_paint_volume_project (ClutterPaintVolume *pv,
                               const graphene_matrix_t *modelview,
                               const graphene_matrix_t *projection,
                               const float *viewport)
{
  int transform_count;

  if (pv->is_empty)
    {
      /* Just transform the origin... */
      _clutter_util_fully_transform_vertices (modelview,
                                              projection,
                                              viewport,
                                              pv->vertices,
                                              pv->vertices,
                                              1);
      return;
    }

  /* All the vertices must be up to date, since after the projection
   * it won't be trivial to derive the other vertices. */
  _clutter_paint_volume_complete (pv);

  /* Most actors are 2D so we only have to transform the front 4
   * vertices of the paint volume... */
  if (G_LIKELY (pv->is_2d))
    transform_count = 4;
  else
    transform_count = 8;

  _clutter_util_fully_transform_vertices (modelview,
                                          projection,
                                          viewport,
                                          pv->vertices,
                                          pv->vertices,
                                          transform_count);

  pv->is_axis_aligned = FALSE;
}

void
_clutter_paint_volume_transform (ClutterPaintVolume *pv,
                                 const graphene_matrix_t *matrix)
{
  int transform_count;

  if (pv->is_empty)
    {
      gfloat w = 1;
      /* Just transform the origin */
      cogl_graphene_matrix_project_point (matrix,
                                          &pv->vertices[0].x,
                                          &pv->vertices[0].y,
                                          &pv->vertices[0].z,
                                          &w);
      return;
    }

  /* All the vertices must be up to date, since after the transform
   * it won't be trivial to derive the other vertices. */
  _clutter_paint_volume_complete (pv);

  /* Most actors are 2D so we only have to transform the front 4
   * vertices of the paint volume... */
  if (G_LIKELY (pv->is_2d))
    transform_count = 4;
  else
    transform_count = 8;

  cogl_graphene_matrix_transform_points (matrix,
                                         3,
                                         sizeof (graphene_point3d_t),
                                         pv->vertices,
                                         sizeof (graphene_point3d_t),
                                         pv->vertices,
                                         transform_count);

  pv->is_axis_aligned = FALSE;
}


/* Given a paint volume that has been transformed by an arbitrary
 * modelview and is no longer axis aligned, this derives a replacement
 * that is axis aligned. */
static void
_clutter_paint_volume_axis_align (ClutterPaintVolume *pv)
{
  int count;
  int i;
  graphene_point3d_t origin;
  float max_x;
  float max_y;
  float max_z;

  g_return_if_fail (pv != NULL);

  if (pv->is_empty)
    return;

  if (G_LIKELY (pv->is_axis_aligned))
    return;

  if (G_LIKELY (pv->vertices[0].x == pv->vertices[1].x &&
                pv->vertices[0].y == pv->vertices[3].y &&
                pv->vertices[0].z == pv->vertices[4].z))
    {
      pv->is_axis_aligned = TRUE;
      return;
    }

  if (!pv->is_complete)
    _clutter_paint_volume_complete (pv);

  origin = pv->vertices[0];
  max_x = pv->vertices[0].x;
  max_y = pv->vertices[0].y;
  max_z = pv->vertices[0].z;

  count = pv->is_2d ? 4 : 8;
  for (i = 1; i < count; i++)
    {
      if (pv->vertices[i].x < origin.x)
        origin.x = pv->vertices[i].x;
      else if (pv->vertices[i].x > max_x)
        max_x = pv->vertices[i].x;

      if (pv->vertices[i].y < origin.y)
        origin.y = pv->vertices[i].y;
      else if (pv->vertices[i].y > max_y)
        max_y = pv->vertices[i].y;

      if (pv->vertices[i].z < origin.z)
        origin.z = pv->vertices[i].z;
      else if (pv->vertices[i].z > max_z)
        max_z = pv->vertices[i].z;
    }

  pv->vertices[0] = origin;

  pv->vertices[1].x = max_x;
  pv->vertices[1].y = origin.y;
  pv->vertices[1].z = origin.z;

  pv->vertices[3].x = origin.x;
  pv->vertices[3].y = max_y;
  pv->vertices[3].z = origin.z;

  pv->vertices[4].x = origin.x;
  pv->vertices[4].y = origin.y;
  pv->vertices[4].z = max_z;

  pv->is_complete = FALSE;
  pv->is_axis_aligned = TRUE;

  if (pv->vertices[4].z == pv->vertices[0].z)
    pv->is_2d = TRUE;
  else
    pv->is_2d = FALSE;
}

/*<private>
 * _clutter_actor_set_default_paint_volume:
 * @self: a #ClutterActor
 * @check_gtype: if not %G_TYPE_INVALID, match the type of @self against
 *   this type
 * @volume: the #ClutterPaintVolume to set
 *
 * Sets the default paint volume for @self.
 *
 * This function should be called by #ClutterActor sub-classes that follow
 * the default assumption that their paint volume is defined by their
 * allocation.
 *
 * If @check_gtype is not %G_TYPE_INVALID, this function will check the
 * type of @self and only compute the paint volume if the type matches;
 * this can be used to avoid computing the paint volume for sub-classes
 * of an actor class
 *
 * Return value: %TRUE if the paint volume was set, and %FALSE otherwise
 */
gboolean
_clutter_actor_set_default_paint_volume (ClutterActor       *self,
                                         GType               check_gtype,
                                         ClutterPaintVolume *volume)
{
  ClutterActorBox box;

  if (check_gtype != G_TYPE_INVALID)
    {
      if (G_OBJECT_TYPE (self) != check_gtype)
        return FALSE;
    }

  /* calling clutter_actor_get_allocation_* can potentially be very
   * expensive, as it can result in a synchronous full stage relayout
   * and redraw
   */
  if (!clutter_actor_has_allocation (self))
    return FALSE;

  clutter_actor_get_allocation_box (self, &box);

  /* we only set the width and height, as the paint volume is defined
   * to be relative to the actor's modelview, which means that the
   * allocation's origin has already been applied
   */
  clutter_paint_volume_set_width (volume, box.x2 - box.x1);
  clutter_paint_volume_set_height (volume, box.y2 - box.y1);

  return TRUE;
}

/**
 * clutter_paint_volume_set_from_allocation:
 * @pv: a #ClutterPaintVolume
 * @actor: a #ClutterActor
 *
 * Sets the #ClutterPaintVolume from the allocation of @actor.
 *
 * This function should be used when overriding the
 * [vfunc@Actor.get_paint_volume] by [class@Actor] sub-classes
 * that do not paint outside their allocation.
 *
 * A typical example is:
 *
 * ```c
 * static gboolean
 * my_actor_get_paint_volume (ClutterActor       *self,
 *                            ClutterPaintVolume *volume)
 * {
 *   return clutter_paint_volume_set_from_allocation (volume, self);
 * }
 * ```
 *
 * Return value: %TRUE if the paint volume was successfully set, and %FALSE
 *   otherwise
 */
gboolean
clutter_paint_volume_set_from_allocation (ClutterPaintVolume *pv,
                                          ClutterActor       *actor)
{
  g_return_val_if_fail (pv != NULL, FALSE);
  g_return_val_if_fail (CLUTTER_IS_ACTOR (actor), FALSE);

  return _clutter_actor_set_default_paint_volume (actor, G_TYPE_INVALID, pv);
}

/* Currently paint volumes are defined relative to a given actor, but
 * in some cases it is desirable to be able to change the actor that
 * a volume relates too (For instance for ClutterClone actors where we
 * need to masquerade the source actors volume as the volume for the
 * clone). */
void
_clutter_paint_volume_set_reference_actor (ClutterPaintVolume *pv,
                                           ClutterActor       *actor)
{
  g_return_if_fail (pv != NULL);

  pv->actor = actor;
}

ClutterCullResult
_clutter_paint_volume_cull (ClutterPaintVolume       *pv,
                            const graphene_frustum_t *frustum)
{
  int vertex_count;
  graphene_box_t box;

  if (pv->is_empty)
    return CLUTTER_CULL_RESULT_OUT;

  /* We expect the volume to already be transformed into eye coordinates
   */
  g_return_val_if_fail (pv->is_complete == TRUE, CLUTTER_CULL_RESULT_IN);
  g_return_val_if_fail (pv->actor == NULL, CLUTTER_CULL_RESULT_IN);

  /* Most actors are 2D so we only have to transform the front 4
   * vertices of the paint volume... */
  if (G_LIKELY (pv->is_2d))
    vertex_count = 4;
  else
    vertex_count = 8;

  graphene_box_init_from_points (&box, vertex_count, pv->vertices);

  if (graphene_frustum_intersects_box (frustum, &box))
    return CLUTTER_CULL_RESULT_IN;
  else
    return CLUTTER_CULL_RESULT_OUT;
}

void
_clutter_paint_volume_get_stage_paint_box (const ClutterPaintVolume *pv,
                                           ClutterStage             *stage,
                                           ClutterActorBox          *box)
{
  ClutterPaintVolume projected_pv;
  graphene_matrix_t modelview;
  graphene_matrix_t projection;
  float viewport[4];

  clutter_paint_volume_init_from_paint_volume (&projected_pv, pv);

  graphene_matrix_init_identity (&modelview);

  /* If the paint volume isn't already in eye coordinates... */
  if (pv->actor)
    _clutter_actor_apply_relative_transformation_matrix (pv->actor, NULL,
                                                         &modelview);

  _clutter_stage_get_projection_matrix (stage, &projection);
  _clutter_stage_get_viewport (stage,
                               &viewport[0],
                               &viewport[1],
                               &viewport[2],
                               &viewport[3]);

  _clutter_paint_volume_project (&projected_pv,
                                 &modelview,
                                 &projection,
                                 viewport);

  _clutter_paint_volume_get_bounding_box (&projected_pv, box);

  if (pv->is_2d &&
      (!pv->actor || clutter_actor_get_z_position (pv->actor) == 0))
    {
      /* If the volume/actor are perfectly 2D, take the bounding box as
       * good. We won't need to add any extra room for sub-pixel positioning
       * in this case.
       */
      clutter_round_to_256ths (&box->x1);
      clutter_round_to_256ths (&box->y1);
      clutter_round_to_256ths (&box->x2);
      clutter_round_to_256ths (&box->y2);
      box->x1 = floorf (box->x1);
      box->y1 = floorf (box->y1);
      box->x2 = ceilf (box->x2);
      box->y2 = ceilf (box->y2);
      return;
    }

  _clutter_actor_box_enlarge_for_effects (box);
}

void
_clutter_paint_volume_transform_relative (ClutterPaintVolume *pv,
                                          ClutterActor *relative_to_ancestor)
{
  graphene_matrix_t matrix;
  ClutterActor *actor;

  actor = pv->actor;

  g_return_if_fail (actor != NULL);

  _clutter_paint_volume_set_reference_actor (pv, relative_to_ancestor);

  graphene_matrix_init_identity (&matrix);
  _clutter_actor_apply_relative_transformation_matrix (actor,
                                                       relative_to_ancestor,
                                                      &matrix);

  _clutter_paint_volume_transform (pv, &matrix);
}

void
clutter_paint_volume_to_box (ClutterPaintVolume *pv,
                             graphene_box_t     *box)
{
  int vertex_count;

  if (pv->is_empty)
    {
      graphene_box_init_from_box (box, graphene_box_empty ());
      return;
    }

  _clutter_paint_volume_complete (pv);

  if (G_LIKELY (pv->is_2d))
    vertex_count = 4;
  else
    vertex_count = 8;

  graphene_box_init_from_points (box, vertex_count, pv->vertices);
}
