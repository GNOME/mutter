/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Utilities for use with Cogl
 *
 * Copyright 2010 Red Hat, Inc.
 * Copyright 2010 Intel Corporation
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

#include "compositor/clutter-utils.h"

#include <math.h>

#define FIXED_SHIFT 8
#define FIXED_ONE (1 << FIXED_SHIFT)

/* This file uses pixel-aligned region computation to determine what
 * can be clipped out. This only really works if everything is aligned
 * to the pixel grid - not scaled or rotated and at integer offsets.
 *
 * (This could be relaxed - if we turned off filtering for unscaled
 * windows then windows would be, by definition aligned to the pixel
 * grid. And for rectangular windows without a shape, the outline that
 * we draw for an unrotated window is always a rectangle because we
 * don't use antialasing for the window boundary - with or without
 * filtering, with or without a scale. But figuring out exactly
 * what pixels will be drawn by the graphics system in these cases
 * gets tricky, so we just go for the easiest part - no scale,
 * and at integer offsets.)
 *
 * The way we check for pixel-aligned is by looking at the
 * transformation into screen space of the allocation box of an actor
 * and and checking if the corners are "close enough" to integral
 * pixel values.
 */

/* The definition of "close enough" to integral pixel values is
 * equality when we convert to fixed-point.
 */
static inline int
round_to_fixed (float x)
{
  return (int) roundf (x * FIXED_ONE);
}

/* Help macros to scale from OpenGL <-1,1> coordinates system to
 * window coordinates ranging [0,window-size]. Borrowed from clutter-utils.c
 */
#define MTX_GL_SCALE_X(x,w,v1,v2) ((((((x) / (w)) + 1.0f) / 2.0f) * (v1)) + (v2))
#define MTX_GL_SCALE_Y(y,w,v1,v2) ((v1) - (((((y) / (w)) + 1.0f) / 2.0f) * (v1)) + (v2))

/* This helper function checks if (according to our fixed point precision)
 * the vertices @verts form a box of width @widthf and height @heightf
 * located at integral coordinates. These coordinates are returned
 * in @x_origin and @y_origin.
 */
gboolean
meta_actor_vertices_are_untransformed (graphene_point3d_t *verts,
                                       float               widthf,
                                       float               heightf,
                                       MetaTransforms     *out_transforms)
{
  int width, height;
  int v0x, v0y, v1x, v1y, v2x, v2y, v3x, v3y;
  int x, y;

  width = round_to_fixed (widthf);
  height = round_to_fixed (heightf);

  v0x = round_to_fixed (verts[0].x); v0y = round_to_fixed (verts[0].y);
  v1x = round_to_fixed (verts[1].x); v1y = round_to_fixed (verts[1].y);
  v2x = round_to_fixed (verts[2].x); v2y = round_to_fixed (verts[2].y);
  v3x = round_to_fixed (verts[3].x); v3y = round_to_fixed (verts[3].y);

  /* Using shifting for converting fixed => int, gets things right for
   * negative values. / FIXED_ONE wouldn't do the same
   */
  x = v0x >> FIXED_SHIFT;
  y = v0y >> FIXED_SHIFT;

  if (out_transforms)
    {
      out_transforms->x_origin = x;
      out_transforms->y_origin = y;
      out_transforms->x_scale = (v1x - v0x) / (float) width;
      out_transforms->y_scale = (v2y - v0y) / (float) height;
    }

  /* At integral coordinates? */
  if (x * FIXED_ONE != v0x || y * FIXED_ONE != v0y)
    return FALSE;

  /* Not scaled? */
  if (v1x - v0x != width || v2y - v0y != height)
    return FALSE;

  /* Not rotated/skewed? */
  if (v0x != v2x || v0y != v1y ||
      v3x != v1x || v3y != v2y)
    return FALSE;

  return TRUE;
}

/**
 * meta_actor_painting_untransformed:
 * @paint_width: the width of the painted area
 * @paint_height: the height of the painted area
 * @sample_width: the width of the sampled area of the texture
 * @sample_height: the height of the sampled area of the texture
 * @x_origin: if the transform is only an integer translation
 *  then the X coordinate of the location of the origin under the transformation
 *  from drawing space to screen pixel space is returned here.
 * @y_origin: if the transform is only an integer translation
 *  then the X coordinate of the location of the origin under the transformation
 *  from drawing space to screen pixel space is returned here.
 *
 * Determines if the current painting transform is an integer translation.
 * This can differ from the result of meta_actor_is_untransformed() when
 * painting an actor if we're inside a inside a clone paint. @paint_width
 * and @paint_height are used to determine the vertices of the rectangle
 * we check to see if the painted area is "close enough" to the integer
 * transform.
 */
gboolean
meta_actor_painting_untransformed (CoglFramebuffer *fb,
                                   int              paint_width,
                                   int              paint_height,
                                   int              sample_width,
                                   int              sample_height,
                                   MetaTransforms  *out_transforms)
{
  graphene_matrix_t modelview, projection, modelview_projection;
  graphene_point3d_t vertices[4];
  float viewport[4];
  int i;

  cogl_framebuffer_get_modelview_matrix (fb, &modelview);
  cogl_framebuffer_get_projection_matrix (fb, &projection);

  graphene_matrix_multiply (&modelview,
                            &projection,
                            &modelview_projection);

  vertices[0].x = 0;
  vertices[0].y = 0;
  vertices[0].z = 0;
  vertices[1].x = paint_width;
  vertices[1].y = 0;
  vertices[1].z = 0;
  vertices[2].x = 0;
  vertices[2].y = paint_height;
  vertices[2].z = 0;
  vertices[3].x = paint_width;
  vertices[3].y = paint_height;
  vertices[3].z = 0;

  cogl_framebuffer_get_viewport4fv (fb, viewport);

  for (i = 0; i < 4; i++)
    {
      float w = 1;
      cogl_graphene_matrix_project_point (&modelview_projection,
                                          &vertices[i].x,
                                          &vertices[i].y,
                                          &vertices[i].z,
                                          &w);
      vertices[i].x = MTX_GL_SCALE_X (vertices[i].x, w,
                                      viewport[2], viewport[0]);
      vertices[i].y = MTX_GL_SCALE_Y (vertices[i].y, w,
                                      viewport[3], viewport[1]);
    }

  return meta_actor_vertices_are_untransformed (vertices,
                                                sample_width, sample_height,
                                                out_transforms);
}

