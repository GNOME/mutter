/*
 * Copyright (C) 2026 Kristof Imeri
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

#include "compositor/meta-background-effect.h"

#include <float.h>
#include <math.h>

#include "backends/meta-stage-private.h"
#include "clutter/clutter-mutter.h"
#include "clutter/clutter-paint-node-private.h"

struct _MetaBackgroundBlur
{
  ClutterActor *actor;
  MtkRegion *blur_region;
  float radius;
  MetaStageRedrawClipFilter *redraw_clip_filter;
};

typedef struct
{
  graphene_matrix_t actor_to_framebuffer;
  graphene_matrix_t framebuffer_to_actor;
  float scale;
  gboolean flip_x;
  gboolean flip_y;
} FramebufferTransform;

static int
calculate_blur_sample_padding (float radius)
{
  return (int) ceilf (radius * 2.0f);
}

static float
get_2d_transform_scale (const graphene_matrix_t *transform)
{
  double xx;
  double yx;
  double xy;
  double yy;

  graphene_matrix_to_2d (transform, &xx, &yx, &xy, &yy, NULL, NULL);

  return (float) MAX (hypot (xx, yx), hypot (xy, yy));
}

static MtkRegion *
transform_region_to_stage (ClutterActor    *actor,
                           MetaStage       *stage,
                           const MtkRegion *region,
                           float           *scale)
{
  graphene_matrix_t actor_to_stage;

  clutter_actor_get_relative_transformation_matrix (actor,
                                                    CLUTTER_ACTOR (stage),
                                                    &actor_to_stage);
  if (!graphene_matrix_is_2d (&actor_to_stage))
    return NULL;

  *scale = get_2d_transform_scale (&actor_to_stage);

  return mtk_region_apply_matrix_transform_expand (region, &actor_to_stage);
}

static MtkRegion *
transform_region_to_clone_stage (ClutterActor    *actor,
                                 ClutterActor    *source,
                                 ClutterActor    *clone,
                                 MetaStage       *stage,
                                 const MtkRegion *region,
                                 float           *scale)
{
  graphene_matrix_t actor_to_source;
  graphene_matrix_t source_to_clone;
  graphene_matrix_t clone_to_stage;
  graphene_matrix_t actor_to_clone;
  graphene_matrix_t actor_to_stage;

  clutter_actor_get_relative_transformation_matrix (actor,
                                                    source,
                                                    &actor_to_source);
  clutter_actor_get_relative_transformation_matrix (clone,
                                                    CLUTTER_ACTOR (stage),
                                                    &clone_to_stage);
  if (!graphene_matrix_is_2d (&actor_to_source) ||
      !graphene_matrix_is_2d (&clone_to_stage))
    return NULL;

  clutter_clone_get_source_transform (CLUTTER_CLONE (clone),
                                      &source_to_clone);

  graphene_matrix_multiply (&actor_to_source,
                            &source_to_clone,
                            &actor_to_clone);
  graphene_matrix_multiply (&actor_to_clone,
                            &clone_to_stage,
                            &actor_to_stage);

  *scale = get_2d_transform_scale (&actor_to_stage);

  return mtk_region_apply_matrix_transform_expand (region, &actor_to_stage);
}

typedef struct
{
  MetaBackgroundBlur *blur;
  MetaStage *stage;
  ClutterStageView *stage_view;
  MtkRegion *redraw_clip;
  MtkRectangle view_layout;
  gboolean changed;
  gboolean intersects;
} ExpandRedrawClipData;

static void
expand_redraw_clip (ExpandRedrawClipData *data,
                    const MtkRegion      *blur_region,
                    float                 scale)
{
  g_autoptr (MtkRegion) sample_region = NULL;
  gboolean region_intersects = FALSE;
  gboolean expands = FALSE;
  int n_rects;

  sample_region =
    meta_background_effect_create_blur_sample_region (blur_region,
                                                      data->blur->radius * scale);
  mtk_region_intersect_rectangle (sample_region, &data->view_layout);

  n_rects = mtk_region_num_rectangles (sample_region);
  for (int i = 0; i < n_rects; i++)
    {
      MtkRectangle rect;
      MtkRegionOverlap overlap;

      rect = mtk_region_get_rectangle (sample_region, i);
      overlap = mtk_region_contains_rectangle (data->redraw_clip, &rect);
      region_intersects |= overlap != MTK_REGION_OVERLAP_OUT;
      expands |= overlap != MTK_REGION_OVERLAP_IN;

      if (region_intersects && expands)
        break;
    }

  data->intersects |= region_intersects;

  if (!region_intersects || !expands)
    return;

  mtk_region_union (data->redraw_clip, sample_region);
  data->changed = TRUE;
}

static void
expand_redraw_clip_for_clone (ClutterActor *source,
                              ClutterActor *clone,
                              gpointer      user_data)
{
  ExpandRedrawClipData *data = user_data;
  g_autoptr (MtkRegion) blur_region = NULL;
  float scale;

  if (!g_list_find (clutter_actor_peek_stage_views (clone), data->stage_view))
    return;

  if (!data->redraw_clip)
    {
      data->intersects = TRUE;
      return;
    }

  blur_region = transform_region_to_clone_stage (data->blur->actor,
                                                 source,
                                                 clone,
                                                 data->stage,
                                                 data->blur->blur_region,
                                                 &scale);
  if (!blur_region)
    return;

  expand_redraw_clip (data, blur_region, scale);
}

static gboolean
meta_background_blur_expand_redraw_clip (MetaStage        *stage,
                                         ClutterStageView *stage_view,
                                         MtkRegion        *redraw_clip,
                                         gpointer          user_data)
{
  MetaBackgroundBlur *blur = user_data;
  ExpandRedrawClipData data = {
    .blur = blur,
    .stage = stage,
    .stage_view = stage_view,
    .redraw_clip = redraw_clip,
  };
  g_autoptr (MtkRegion) blur_region = NULL;
  float scale;

  if (clutter_actor_get_stage (blur->actor) != CLUTTER_ACTOR (stage))
    return FALSE;

  if (redraw_clip)
    clutter_stage_view_get_layout (stage_view, &data.view_layout);

  if (clutter_actor_is_mapped (blur->actor) &&
      g_list_find (clutter_actor_peek_stage_views (blur->actor), stage_view))
    {
      if (!redraw_clip)
        data.intersects = TRUE;
      else
        {
          blur_region = transform_region_to_stage (blur->actor,
                                                   stage,
                                                   blur->blur_region,
                                                   &scale);
          if (blur_region)
            expand_redraw_clip (&data, blur_region, scale);
        }
    }

  clutter_actor_foreach_mapped_clone (blur->actor,
                                      expand_redraw_clip_for_clone,
                                      &data);

  if (data.intersects)
    clutter_actor_invalidate_paint_cache (blur->actor);

  return data.changed;
}

MetaBackgroundBlur *
meta_background_blur_new (ClutterActor    *actor,
                          const MtkRegion *blur_region,
                          float            radius)
{
  ClutterActor *stage_actor;
  MetaBackgroundBlur *blur;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (actor), NULL);
  g_return_val_if_fail (blur_region != NULL, NULL);
  g_return_val_if_fail (radius >= 0.0f, NULL);

  stage_actor = clutter_actor_get_stage (actor);
  if (!stage_actor)
    return NULL;

  g_return_val_if_fail (META_IS_STAGE (stage_actor), NULL);

  blur = g_new0 (MetaBackgroundBlur, 1);
  blur->actor = actor;
  blur->blur_region = mtk_region_copy (blur_region);
  blur->radius = radius;
  blur->redraw_clip_filter =
    meta_stage_add_redraw_clip_filter (META_STAGE (stage_actor),
                                       meta_background_blur_expand_redraw_clip,
                                       blur,
                                       NULL);

  return blur;
}

void
meta_background_blur_destroy (MetaBackgroundBlur *blur)
{
  g_clear_pointer (&blur->redraw_clip_filter,
                   meta_stage_remove_redraw_clip_filter);
  g_clear_pointer (&blur->blur_region, mtk_region_unref);
  g_free (blur);
}

MtkRegion *
meta_background_effect_create_blur_sample_region (const MtkRegion *blur_region,
                                                  float            radius)
{
  g_autoptr (MtkRegion) sample_region = NULL;
  int padding;
  int n_rects;

  sample_region = mtk_region_create ();
  padding = calculate_blur_sample_padding (radius);
  n_rects = mtk_region_num_rectangles (blur_region);

  for (int i = 0; i < n_rects; i++)
    {
      MtkRectangle rect;
      MtkRectangle sample_rect;

      rect = mtk_region_get_rectangle (blur_region, i);
      sample_rect = (MtkRectangle) {
        .x = rect.x - padding,
        .y = rect.y - padding,
        .width = rect.width + 2 * padding,
        .height = rect.height + 2 * padding,
      };

      mtk_region_union_rectangle (sample_region, &sample_rect);
    }

  return g_steal_pointer (&sample_region);
}

static gboolean
project_actor_point (const graphene_matrix_t *modelview_projection,
                     const float             *viewport,
                     float                    actor_x,
                     float                    actor_y,
                     graphene_point_t        *framebuffer_point)
{
  float x = actor_x;
  float y = actor_y;
  float z = 0.0f;
  float w = 1.0f;

  cogl_graphene_matrix_project_point (modelview_projection, &x, &y, &z, &w);
  if (G_APPROX_VALUE (w, 0.0f, FLT_EPSILON))
    return FALSE;

  graphene_point_init (framebuffer_point,
                       viewport[0] + ((x / w + 1.0f) * viewport[2] / 2.0f),
                       viewport[1] + ((1.0f - y / w) * viewport[3] / 2.0f));

  return TRUE;
}

static gboolean
get_framebuffer_transform (ClutterActor          *actor,
                           ClutterPaintContext   *paint_context,
                           CoglFramebuffer       *framebuffer,
                           const ClutterActorBox *actor_box,
                           FramebufferTransform  *transform)
{
  graphene_matrix_t actor_to_eye;
  graphene_matrix_t projection;
  graphene_matrix_t modelview_projection;
  graphene_point_t origin;
  graphene_point_t x_point;
  graphene_point_t y_point;
  graphene_point_t opposite_point;
  graphene_point_t expected_opposite;
  float xx;
  float yy;
  float width;
  float height;
  float x0;
  float y0;
  float viewport[4];

  width = actor_box->x2 - actor_box->x1;
  height = actor_box->y2 - actor_box->y1;
  if (width <= 0.0f || height <= 0.0f)
    return FALSE;

  clutter_actor_get_effective_eye_transformation_matrix (actor,
                                                         paint_context,
                                                         &actor_to_eye);

  cogl_framebuffer_get_projection_matrix (framebuffer, &projection);
  cogl_framebuffer_get_viewport4fv (framebuffer, viewport);
  graphene_matrix_multiply (&actor_to_eye,
                            &projection,
                            &modelview_projection);

  if (!project_actor_point (&modelview_projection, viewport,
                            actor_box->x1, actor_box->y1, &origin) ||
      !project_actor_point (&modelview_projection, viewport,
                            actor_box->x2, actor_box->y1, &x_point) ||
      !project_actor_point (&modelview_projection, viewport,
                            actor_box->x1, actor_box->y2, &y_point) ||
      !project_actor_point (&modelview_projection, viewport,
                            actor_box->x2, actor_box->y2, &opposite_point))
    return FALSE;

  graphene_point_init (&expected_opposite,
                       x_point.x + y_point.x - origin.x,
                       x_point.y + y_point.y - origin.y);
  if (!graphene_point_near (&opposite_point,
                            &expected_opposite,
                            CLUTTER_COORDINATE_EPSILON))
    return FALSE;

  if (!G_APPROX_VALUE (x_point.y, origin.y, CLUTTER_COORDINATE_EPSILON) ||
      !G_APPROX_VALUE (y_point.x, origin.x, CLUTTER_COORDINATE_EPSILON))
    return FALSE;

  xx = (x_point.x - origin.x) / width;
  yy = (y_point.y - origin.y) / height;
  x0 = origin.x - actor_box->x1 * xx;
  y0 = origin.y - actor_box->y1 * yy;
  graphene_matrix_init_from_2d (&transform->actor_to_framebuffer,
                                xx, 0.0f, 0.0f, yy,
                                x0, y0);
  if (!graphene_matrix_inverse (&transform->actor_to_framebuffer,
                                &transform->framebuffer_to_actor))
    return FALSE;

  transform->scale = MAX (fabsf (xx), fabsf (yy));
  transform->flip_x = xx < 0.0f;
  transform->flip_y = yy < 0.0f;
  return TRUE;
}

static gboolean
actor_box_to_framebuffer_rect (const ClutterActorBox      *actor_box,
                               const FramebufferTransform *transform,
                               graphene_rect_t            *framebuffer_rect)
{
  graphene_rect_t actor_rect;

  graphene_rect_init (&actor_rect,
                      actor_box->x1,
                      actor_box->y1,
                      actor_box->x2 - actor_box->x1,
                      actor_box->y2 - actor_box->y1);
  graphene_matrix_transform_bounds (&transform->actor_to_framebuffer,
                                    &actor_rect,
                                    framebuffer_rect);

  return framebuffer_rect->size.width > 0.0f &&
         framebuffer_rect->size.height > 0.0f;
}

static gboolean
framebuffer_rect_to_actor_box (const graphene_rect_t      *framebuffer_rect,
                               const FramebufferTransform *transform,
                               ClutterActorBox            *actor_box)
{
  graphene_rect_t actor_rect;

  graphene_matrix_transform_bounds (&transform->framebuffer_to_actor,
                                    framebuffer_rect,
                                    &actor_rect);

  *actor_box = (ClutterActorBox) {
    .x1 = actor_rect.origin.x,
    .y1 = actor_rect.origin.y,
    .x2 = actor_rect.origin.x + actor_rect.size.width,
    .y2 = actor_rect.origin.y + actor_rect.size.height,
  };

  return actor_box->x2 > actor_box->x1 && actor_box->y2 > actor_box->y1;
}

static gboolean
region_rect_to_actor_box (const MtkRectangle    *region_rect,
                          const ClutterActorBox *content_box,
                          int                    content_width,
                          int                    content_height,
                          ClutterActorBox       *actor_box)
{
  float x_scale;
  float y_scale;

  if (content_width <= 0 || content_height <= 0)
    return FALSE;

  x_scale = (content_box->x2 - content_box->x1) / content_width;
  y_scale = (content_box->y2 - content_box->y1) / content_height;

  *actor_box = (ClutterActorBox) {
    .x1 = content_box->x1 + region_rect->x * x_scale,
    .y1 = content_box->y1 + region_rect->y * y_scale,
    .x2 = content_box->x1 + (region_rect->x + region_rect->width) * x_scale,
    .y2 = content_box->y1 + (region_rect->y + region_rect->height) * y_scale,
  };

  return actor_box->x2 > actor_box->x1 && actor_box->y2 > actor_box->y1;
}

static gboolean
expand_framebuffer_rect_for_blur (const MtkRectangle *framebuffer_rect,
                                  const MtkRectangle *framebuffer_bounds,
                                  float               radius,
                                  MtkRectangle       *sample_rect)
{
  int padding;

  padding = calculate_blur_sample_padding (radius);

  *sample_rect = (MtkRectangle) {
    .x = framebuffer_rect->x - padding,
    .y = framebuffer_rect->y - padding,
    .width = framebuffer_rect->width + 2 * padding,
    .height = framebuffer_rect->height + 2 * padding,
  };

  return mtk_rectangle_intersect (sample_rect,
                                  framebuffer_bounds,
                                  sample_rect);
}

static void
add_blur_rectangle (ClutterPaintNode           *paint_node,
                    const MtkRectangle         *source_rect,
                    const graphene_rect_t      *framebuffer_rect,
                    const ClutterActorBox      *actor_box,
                    const FramebufferTransform *transform)
{
  float texture_x1;
  float texture_y1;
  float texture_x2;
  float texture_y2;

  texture_x1 =
    CLAMP ((framebuffer_rect->origin.x - source_rect->x) /
           (float) source_rect->width,
           0.0f,
           1.0f);
  texture_y1 =
    CLAMP ((framebuffer_rect->origin.y - source_rect->y) /
           (float) source_rect->height,
           0.0f,
           1.0f);
  texture_x2 =
    CLAMP ((framebuffer_rect->origin.x + framebuffer_rect->size.width -
            source_rect->x) /
           (float) source_rect->width,
           0.0f,
           1.0f);
  texture_y2 =
    CLAMP ((framebuffer_rect->origin.y + framebuffer_rect->size.height -
            source_rect->y) /
           (float) source_rect->height,
           0.0f,
           1.0f);

  if (transform->flip_x)
    {
      float tmp = texture_x1;

      texture_x1 = texture_x2;
      texture_x2 = tmp;
    }
  if (transform->flip_y)
    {
      float tmp = texture_y1;

      texture_y1 = texture_y2;
      texture_y2 = tmp;
    }

  clutter_paint_node_add_texture_rectangle (paint_node,
                                            actor_box,
                                            texture_x1,
                                            texture_y1,
                                            texture_x2,
                                            texture_y2);
}

void
meta_background_effect_paint_blur_region (ClutterPaintNode       *root_node,
                                          ClutterActor           *actor,
                                          ClutterPaintContext    *paint_context,
                                          const ClutterActorBox  *content_box,
                                          int                     content_width,
                                          int                     content_height,
                                          const MtkRegion        *blur_region,
                                          const MtkRegion        *clip_region,
                                          float                   radius,
                                          float                   saturation,
                                          float                   noise,
                                          uint8_t                 opacity)
{
  ClutterActor *stage;
  CoglFramebuffer *source_framebuffer;
  FramebufferTransform framebuffer_transform;
  ClutterActorBox extents_actor_box;
  MtkRectangle content_rect;
  MtkRectangle effect_extents;
  graphene_rect_t extents_framebuffer_rect;
  graphene_rect_t framebuffer_bounds;
  MtkRectangle framebuffer_bounds_int;
  MtkRectangle effect_framebuffer_rect;
  MtkRectangle source_rect;
  g_autoptr (MtkRegion) effect_region = NULL;
  g_autoptr (MtkRegion) paint_region = NULL;
  g_autoptr (ClutterPaintNode) blur_node = NULL;
  float blur_radius;
  int n_rects;
  gboolean has_blur_rects = FALSE;

  if (opacity == 0)
    return;

  stage = clutter_actor_get_stage (actor);
  if (!stage)
    return;

  source_framebuffer = clutter_paint_node_get_framebuffer (root_node);
  if (!source_framebuffer)
    source_framebuffer = clutter_paint_context_get_framebuffer (paint_context);
  if (!source_framebuffer)
    return;

  content_rect = (MtkRectangle) {
    .width = content_width,
    .height = content_height,
  };

  effect_region = mtk_region_copy (blur_region);
  mtk_region_intersect_rectangle (effect_region, &content_rect);

  if (mtk_region_is_empty (effect_region))
    return;

  paint_region = mtk_region_copy (effect_region);
  if (clip_region)
    mtk_region_intersect (paint_region, clip_region);

  if (mtk_region_is_empty (paint_region))
    return;

  framebuffer_bounds_int = (MtkRectangle) {
    .width = cogl_framebuffer_get_width (source_framebuffer),
    .height = cogl_framebuffer_get_height (source_framebuffer),
  };
  framebuffer_bounds = mtk_rectangle_to_graphene_rect (&framebuffer_bounds_int);
  effect_extents = mtk_region_get_extents (effect_region);

  if (!region_rect_to_actor_box (&effect_extents,
                                 content_box,
                                 content_width,
                                 content_height,
                                 &extents_actor_box))
    return;

  if (!get_framebuffer_transform (actor,
                                  paint_context,
                                  source_framebuffer,
                                  &extents_actor_box,
                                  &framebuffer_transform))
    return;

  blur_radius = radius * framebuffer_transform.scale;

  if (!actor_box_to_framebuffer_rect (&extents_actor_box,
                                      &framebuffer_transform,
                                      &extents_framebuffer_rect))
    return;

  if (!graphene_rect_intersection (&extents_framebuffer_rect,
                                   &framebuffer_bounds,
                                   &extents_framebuffer_rect))
    return;

  mtk_rectangle_from_graphene_rect (&extents_framebuffer_rect,
                                    MTK_ROUNDING_STRATEGY_GROW,
                                    &effect_framebuffer_rect);

  if (!expand_framebuffer_rect_for_blur (&effect_framebuffer_rect,
                                         &framebuffer_bounds_int,
                                         blur_radius,
                                         &source_rect))
    return;

  blur_node =
    clutter_blur_node_new_from_framebuffer (source_framebuffer,
                                            source_rect.x,
                                            source_rect.y,
                                            source_rect.width,
                                            source_rect.height,
                                            blur_radius,
                                            saturation,
                                            noise,
                                            opacity);
  if (!blur_node)
    return;

  n_rects = mtk_region_num_rectangles (paint_region);
  for (int i = 0; i < n_rects; i++)
    {
      MtkRectangle region_rect;
      graphene_rect_t framebuffer_rect;
      ClutterActorBox actor_box;

      region_rect = mtk_region_get_rectangle (paint_region, i);
      if (!region_rect_to_actor_box (&region_rect,
                                     content_box,
                                     content_width,
                                     content_height,
                                     &actor_box))
        continue;

      if (!actor_box_to_framebuffer_rect (&actor_box,
                                          &framebuffer_transform,
                                          &framebuffer_rect))
        continue;

      if (!graphene_rect_intersection (&framebuffer_rect,
                                       &framebuffer_bounds,
                                       &framebuffer_rect))
        continue;

      if (!framebuffer_rect_to_actor_box (&framebuffer_rect,
                                          &framebuffer_transform,
                                          &actor_box))
        continue;

      add_blur_rectangle (blur_node,
                          &source_rect,
                          &framebuffer_rect,
                          &actor_box,
                          &framebuffer_transform);
      has_blur_rects = TRUE;
    }

  if (has_blur_rects)
    clutter_paint_node_add_child (root_node, blur_node);
}
