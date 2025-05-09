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
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "clutter/clutter-types.h"
#include "clutter/clutter-private.h"

G_BEGIN_DECLS

struct _ClutterPaintVolume
{
  /* A paint volume represents a volume in a given actors private
   * coordinate system. */
  ClutterActor *actor;

  /* cuboid for the volume:
   *
   *       4━━━━━━━┓5
   *    ┏━━━━━━━━┓╱┃
   *    ┃0 ┊7   1┃ ┃
   *    ┃   ┄┄┄┄┄┃┄┃6
   *    ┃3      2┃╱
   *    ┗━━━━━━━━┛
   *
   *   0: top, left (origin)  : always valid
   *   1: top, right          : always valid
   *   2: bottom, right       :  updated lazily
   *   3: bottom, left        : always valid
   *
   *   4: top, left, back     : always valid
   *   5: top, right, back    :  updated lazily
   *   6: bottom, right, back :  updated lazily
   *   7: bottom, left, back  :  updated lazily
   *
   * Elements 0, 1, 3 and 4 are filled in by the PaintVolume setters
   *
   * Note: the reason for this ordering is that we can simply ignore
   * elements 4, 5, 6 and 7 most of the time for 2D actors when
   * calculating the projected paint box.
   */
  graphene_point3d_t vertices[8];

  /* A newly initialized PaintVolume is considered empty as it is
   * degenerate on all three axis.
   *
   * We consider this carefully when we union an empty volume with
   * another so that the union simply results in a copy of the other
   * volume instead of also bounding the origin of the empty volume.
   *
   * For example this is a convenient property when calculating the
   * volume of a container as the union of the volume of its children
   * where the initial volume passed to the containers
   * ->get_paint_volume method will be empty. */
  guint is_empty:1;

  /* TRUE when we've updated the values we calculate lazily */
  guint is_complete:1;

  /* TRUE if vertices 4-7 can be ignored. (Only valid if complete is
   * TRUE) */
  guint is_2d:1;

  /* Set to TRUE initially but cleared if the paint volume is
   * transformed by a matrix. */
  guint is_axis_aligned:1;


  /* Note: There is a precedence to the above bitfields that should be
   * considered whenever we implement code that manipulates
   * PaintVolumes...
   *
   * Firstly if ->is_empty == TRUE then the values for ->is_complete
   * and ->is_2d are undefined, so you should typically check
   * ->is_empty as the first priority.
   *
   * XXX: document other invariables...
   */
};

void clutter_paint_volume_init_from_actor (ClutterPaintVolume *pv,
                                           ClutterActor       *actor);
void clutter_paint_volume_init_from_paint_volume (ClutterPaintVolume       *dst_pv,
                                                  const ClutterPaintVolume *src_pv);

void                _clutter_paint_volume_complete             (ClutterPaintVolume *pv);
void                _clutter_paint_volume_transform            (ClutterPaintVolume      *pv,
                                                                const graphene_matrix_t *matrix);
void                _clutter_paint_volume_get_bounding_box     (ClutterPaintVolume *pv,
                                                                ClutterActorBox    *box);
void                _clutter_paint_volume_set_reference_actor  (ClutterPaintVolume *pv,
                                                                ClutterActor *actor);

ClutterCullResult   _clutter_paint_volume_cull                 (ClutterPaintVolume       *pv,
                                                                const graphene_frustum_t *frustum);

void                _clutter_paint_volume_get_stage_paint_box  (const ClutterPaintVolume *pv,
                                                                ClutterStage             *stage,
                                                                ClutterActorBox          *box);

void                _clutter_paint_volume_transform_relative   (ClutterPaintVolume *pv,
                                                                ClutterActor *relative_to_ancestor);

void                clutter_paint_volume_to_box                (ClutterPaintVolume *pv,
                                                                graphene_box_t     *box);

G_END_DECLS
