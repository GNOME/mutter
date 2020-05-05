/*
 * Copyright (C) 2016 Red Hat Inc.
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

#include "clutter-build-config.h"

#include "clutter/clutter-stage-view.h"
#include "clutter/clutter-stage-view-private.h"

#include <cairo-gobject.h>
#include <math.h>

#include "clutter/clutter-private.h"
#include "clutter/clutter-mutter.h"
#include "cogl/cogl.h"

enum
{
  PROP_0,

  PROP_NAME,
  PROP_LAYOUT,
  PROP_FRAMEBUFFER,
  PROP_OFFSCREEN,
  PROP_USE_SHADOWFB,
  PROP_SCALE,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

typedef struct _ClutterStageViewPrivate
{
  char *name;

  cairo_rectangle_int_t layout;
  float scale;
  CoglFramebuffer *framebuffer;

  CoglOffscreen *offscreen;
  CoglPipeline *offscreen_pipeline;

  gboolean use_shadowfb;
  struct {
    CoglOffscreen *framebuffer;
  } shadow;

  CoglScanout *next_scanout;

  gboolean has_redraw_clip;
  cairo_region_t *redraw_clip;

  guint dirty_viewport   : 1;
  guint dirty_projection : 1;
} ClutterStageViewPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (ClutterStageView, clutter_stage_view, G_TYPE_OBJECT)

void
clutter_stage_view_get_layout (ClutterStageView      *view,
                               cairo_rectangle_int_t *rect)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  *rect = priv->layout;
}

/**
 * clutter_stage_view_get_framebuffer:
 * @view: a #ClutterStageView
 *
 * Retrieves the framebuffer of @view to draw to.
 *
 * Returns: (transfer none): a #CoglFramebuffer
 */
CoglFramebuffer *
clutter_stage_view_get_framebuffer (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  if (priv->offscreen)
    return priv->offscreen;
  else if (priv->shadow.framebuffer)
    return priv->shadow.framebuffer;
  else
    return priv->framebuffer;
}

/**
 * clutter_stage_view_get_onscreen:
 * @view: a #ClutterStageView
 *
 * Retrieves the onscreen framebuffer of @view if available.
 *
 * Returns: (transfer none): a #CoglFramebuffer
 */
CoglFramebuffer *
clutter_stage_view_get_onscreen (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->framebuffer;
}

static CoglPipeline *
clutter_stage_view_create_framebuffer_pipeline (CoglFramebuffer *framebuffer)
{
  CoglPipeline *pipeline;

  pipeline = cogl_pipeline_new (cogl_framebuffer_get_context (framebuffer));

  cogl_pipeline_set_layer_filters (pipeline, 0,
                                   COGL_PIPELINE_FILTER_NEAREST,
                                   COGL_PIPELINE_FILTER_NEAREST);
  cogl_pipeline_set_layer_texture (pipeline, 0,
                                   cogl_offscreen_get_texture (framebuffer));
  cogl_pipeline_set_layer_wrap_mode (pipeline, 0,
                                     COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);

  return pipeline;
}

static void
clutter_stage_view_ensure_offscreen_blit_pipeline (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);
  ClutterStageViewClass *view_class =
    CLUTTER_STAGE_VIEW_GET_CLASS (view);

  g_assert (priv->offscreen != NULL);

  if (priv->offscreen_pipeline)
    return;

  priv->offscreen_pipeline =
    clutter_stage_view_create_framebuffer_pipeline (priv->offscreen);

  if (view_class->setup_offscreen_blit_pipeline)
    view_class->setup_offscreen_blit_pipeline (view, priv->offscreen_pipeline);
}

void
clutter_stage_view_invalidate_offscreen_blit_pipeline (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  g_clear_pointer (&priv->offscreen_pipeline, cogl_object_unref);
}

static void
paint_transformed_framebuffer (ClutterStageView *view,
                               CoglPipeline     *pipeline,
                               CoglFramebuffer  *src_framebuffer,
                               CoglFramebuffer  *dst_framebuffer)
{
  CoglMatrix matrix;

  cogl_framebuffer_push_matrix (dst_framebuffer);

  cogl_matrix_init_identity (&matrix);
  cogl_matrix_translate (&matrix, -1, 1, 0);
  cogl_matrix_scale (&matrix, 2, -2, 0);
  cogl_framebuffer_set_projection_matrix (dst_framebuffer, &matrix);

  cogl_framebuffer_draw_rectangle (dst_framebuffer,
                                   pipeline,
                                   0, 0, 1, 1);

  cogl_framebuffer_pop_matrix (dst_framebuffer);
}

static CoglOffscreen *
create_offscreen_framebuffer (CoglContext  *context,
                              int           width,
                              int           height,
                              GError      **error)
{
  CoglOffscreen *framebuffer;
  CoglTexture2D *texture;

  texture = cogl_texture_2d_new_with_size (context, width, height);
  cogl_primitive_texture_set_auto_mipmap (COGL_PRIMITIVE_TEXTURE (texture),
                                          FALSE);

  if (!cogl_texture_allocate (COGL_TEXTURE (texture), error))
    {
      cogl_object_unref (texture);
      return FALSE;
    }

  framebuffer = cogl_offscreen_new_with_texture (COGL_TEXTURE (texture));
  cogl_object_unref (texture);
  if (!cogl_framebuffer_allocate (COGL_FRAMEBUFFER (framebuffer), error))
    {
      cogl_object_unref (framebuffer);
      return FALSE;
    }

  return framebuffer;
}

static gboolean
init_offscreen_shadowfb (ClutterStageView  *view,
                         CoglContext       *cogl_context,
                         int                width,
                         int                height,
                         GError           **error)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);
  CoglOffscreen *offscreen;

  offscreen = create_offscreen_framebuffer (cogl_context, width, height, error);
  if (!offscreen)
    return FALSE;

  priv->shadow.framebuffer = offscreen;
  return TRUE;
}

static void
init_shadowfb (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);
  g_autoptr (GError) error = NULL;
  int width;
  int height;
  CoglContext *cogl_context;

  width = cogl_framebuffer_get_width (priv->framebuffer);
  height = cogl_framebuffer_get_height (priv->framebuffer);
  cogl_context = cogl_framebuffer_get_context (priv->framebuffer);

  if (!init_offscreen_shadowfb (view, cogl_context, width, height, &error))
    {
      g_warning ("Failed to initialize single buffered shadow fb for %s: %s",
                 priv->name, error->message);
    }
  else
    {
      g_message ("Initialized single buffered shadow fb for %s", priv->name);
    }
}

void
clutter_stage_view_after_paint (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  if (priv->offscreen)
    {
      clutter_stage_view_ensure_offscreen_blit_pipeline (view);

      if (priv->shadow.framebuffer)
        {
          paint_transformed_framebuffer (view,
                                         priv->offscreen_pipeline,
                                         priv->offscreen,
                                         priv->shadow.framebuffer);
        }
      else
        {
          paint_transformed_framebuffer (view,
                                         priv->offscreen_pipeline,
                                         priv->offscreen,
                                         priv->framebuffer);
        }
    }

  if (priv->shadow.framebuffer)
    {
      int width, height;
      g_autoptr (GError) error = NULL;

      width = cogl_framebuffer_get_width (priv->framebuffer);
      height = cogl_framebuffer_get_height (priv->framebuffer);
      if (!cogl_blit_framebuffer (priv->shadow.framebuffer,
                                  priv->framebuffer,
                                  0, 0,
                                  0, 0,
                                  width, height,
                                  &error))
        {
          g_warning ("Failed to blit shadow buffer: %s", error->message);
          return;
        }
    }
}

float
clutter_stage_view_get_scale (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->scale;
}

gboolean
clutter_stage_view_is_dirty_viewport (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->dirty_viewport;
}

void
clutter_stage_view_invalidate_viewport (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  priv->dirty_viewport = TRUE;
}

void
clutter_stage_view_set_viewport (ClutterStageView *view,
                                 float             x,
                                 float             y,
                                 float             width,
                                 float             height)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);
  CoglFramebuffer *framebuffer;

  priv->dirty_viewport = FALSE;
  framebuffer = clutter_stage_view_get_framebuffer (view);
  cogl_framebuffer_set_viewport (framebuffer, x, y, width, height);
}

gboolean
clutter_stage_view_is_dirty_projection (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->dirty_projection;
}

void
clutter_stage_view_invalidate_projection (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  priv->dirty_projection = TRUE;
}

void
clutter_stage_view_set_projection (ClutterStageView *view,
                                   const CoglMatrix *matrix)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);
  CoglFramebuffer *framebuffer;

  priv->dirty_projection = FALSE;
  framebuffer = clutter_stage_view_get_framebuffer (view);
  cogl_framebuffer_set_projection_matrix (framebuffer, matrix);
}

void
clutter_stage_view_get_offscreen_transformation_matrix (ClutterStageView *view,
                                                        CoglMatrix       *matrix)
{
  ClutterStageViewClass *view_class = CLUTTER_STAGE_VIEW_GET_CLASS (view);

  view_class->get_offscreen_transformation_matrix (view, matrix);
}

void
clutter_stage_view_add_redraw_clip (ClutterStageView            *view,
                                    const cairo_rectangle_int_t *clip)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  if (priv->has_redraw_clip && !priv->redraw_clip)
    return;

  if (!clip)
    {
      g_clear_pointer (&priv->redraw_clip, cairo_region_destroy);
      priv->has_redraw_clip = TRUE;
      return;
    }

  if (clip->width == 0 || clip->height == 0)
    return;

  if (!priv->redraw_clip)
    {
      if (!clutter_util_rectangle_equal (&priv->layout, clip))
        priv->redraw_clip = cairo_region_create_rectangle (clip);
    }
  else
    {
      cairo_region_union_rectangle (priv->redraw_clip, clip);

      if (cairo_region_num_rectangles (priv->redraw_clip) == 1)
        {
          cairo_rectangle_int_t redraw_clip_extents;

          cairo_region_get_extents (priv->redraw_clip, &redraw_clip_extents);
          if (clutter_util_rectangle_equal (&priv->layout, &redraw_clip_extents))
            g_clear_pointer (&priv->redraw_clip, cairo_region_destroy);
        }
    }

  priv->has_redraw_clip = TRUE;
}

gboolean
clutter_stage_view_has_redraw_clip (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->has_redraw_clip;
}

gboolean
clutter_stage_view_has_full_redraw_clip (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->has_redraw_clip && !priv->redraw_clip;
}

const cairo_region_t *
clutter_stage_view_peek_redraw_clip (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->redraw_clip;
}

cairo_region_t *
clutter_stage_view_take_redraw_clip (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  priv->has_redraw_clip = FALSE;

  return g_steal_pointer (&priv->redraw_clip);
}

void
clutter_stage_view_transform_to_onscreen (ClutterStageView *view,
                                          gfloat           *x,
                                          gfloat           *y)
{
  gfloat z = 0, w = 1;
  CoglMatrix matrix;

  clutter_stage_view_get_offscreen_transformation_matrix (view, &matrix);
  cogl_matrix_get_inverse (&matrix, &matrix);
  cogl_matrix_transform_point (&matrix, x, y, &z, &w);
}

static void
clutter_stage_default_get_offscreen_transformation_matrix (ClutterStageView *view,
                                                           CoglMatrix       *matrix)
{
  cogl_matrix_init_identity (matrix);
}

void
clutter_stage_view_assign_next_scanout (ClutterStageView *view,
                                        CoglScanout      *scanout)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  g_set_object (&priv->next_scanout, scanout);
}

CoglScanout *
clutter_stage_view_take_scanout (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return g_steal_pointer (&priv->next_scanout);
}

static void
clutter_stage_view_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  ClutterStageView *view = CLUTTER_STAGE_VIEW (object);
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  switch (prop_id)
    {
    case PROP_NAME:
      g_value_set_string (value, priv->name);
      break;
    case PROP_LAYOUT:
      g_value_set_boxed (value, &priv->layout);
      break;
    case PROP_FRAMEBUFFER:
      g_value_set_boxed (value, priv->framebuffer);
      break;
    case PROP_OFFSCREEN:
      g_value_set_boxed (value, priv->offscreen);
      break;
    case PROP_USE_SHADOWFB:
      g_value_set_boolean (value, priv->use_shadowfb);
      break;
    case PROP_SCALE:
      g_value_set_float (value, priv->scale);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
clutter_stage_view_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  ClutterStageView *view = CLUTTER_STAGE_VIEW (object);
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);
  cairo_rectangle_int_t *layout;

  switch (prop_id)
    {
    case PROP_NAME:
      priv->name = g_value_dup_string (value);
      break;
    case PROP_LAYOUT:
      layout = g_value_get_boxed (value);
      priv->layout = *layout;
      break;
    case PROP_FRAMEBUFFER:
      priv->framebuffer = g_value_dup_boxed (value);
#ifndef G_DISABLE_CHECKS
      if (priv->framebuffer)
        {
          int fb_width, fb_height;

          fb_width = cogl_framebuffer_get_width (priv->framebuffer);
          fb_height = cogl_framebuffer_get_height (priv->framebuffer);

          g_warn_if_fail (fabsf (roundf (fb_width / priv->scale) -
                                 fb_width / priv->scale) < FLT_EPSILON);
          g_warn_if_fail (fabsf (roundf (fb_height / priv->scale) -
                                 fb_height / priv->scale) < FLT_EPSILON);
        }
#endif
      break;
    case PROP_OFFSCREEN:
      priv->offscreen = g_value_dup_boxed (value);
      break;
    case PROP_USE_SHADOWFB:
      priv->use_shadowfb = g_value_get_boolean (value);
      break;
    case PROP_SCALE:
      priv->scale = g_value_get_float (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
clutter_stage_view_constructed (GObject *object)
{
  ClutterStageView *view = CLUTTER_STAGE_VIEW (object);
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  if (priv->use_shadowfb)
    init_shadowfb (view);

  G_OBJECT_CLASS (clutter_stage_view_parent_class)->constructed (object);
}

static void
clutter_stage_view_dispose (GObject *object)
{
  ClutterStageView *view = CLUTTER_STAGE_VIEW (object);
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  g_clear_pointer (&priv->name, g_free);
  g_clear_pointer (&priv->framebuffer, cogl_object_unref);
  g_clear_pointer (&priv->shadow.framebuffer, cogl_object_unref);
  g_clear_pointer (&priv->offscreen, cogl_object_unref);
  g_clear_pointer (&priv->offscreen_pipeline, cogl_object_unref);
  g_clear_pointer (&priv->redraw_clip, cairo_region_destroy);

  G_OBJECT_CLASS (clutter_stage_view_parent_class)->dispose (object);
}

static void
clutter_stage_view_init (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  priv->dirty_viewport = TRUE;
  priv->dirty_projection = TRUE;
  priv->scale = 1.0;
}

static void
clutter_stage_view_class_init (ClutterStageViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  klass->get_offscreen_transformation_matrix =
    clutter_stage_default_get_offscreen_transformation_matrix;

  object_class->get_property = clutter_stage_view_get_property;
  object_class->set_property = clutter_stage_view_set_property;
  object_class->constructed = clutter_stage_view_constructed;
  object_class->dispose = clutter_stage_view_dispose;

  obj_props[PROP_NAME] =
    g_param_spec_string ("name",
                         "Name",
                         "Name of view",
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_LAYOUT] =
    g_param_spec_boxed ("layout",
                        "View layout",
                        "The view layout on the screen",
                        CAIRO_GOBJECT_TYPE_RECTANGLE_INT,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT |
                        G_PARAM_STATIC_STRINGS);

  obj_props[PROP_FRAMEBUFFER] =
    g_param_spec_boxed ("framebuffer",
                        "View framebuffer",
                        "The front buffer of the view",
                        COGL_TYPE_HANDLE,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT |
                        G_PARAM_STATIC_STRINGS);

  obj_props[PROP_OFFSCREEN] =
    g_param_spec_boxed ("offscreen",
                        "Offscreen buffer",
                        "Framebuffer used as intermediate buffer",
                        COGL_TYPE_HANDLE,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  obj_props[PROP_USE_SHADOWFB] =
    g_param_spec_boolean ("use-shadowfb",
                          "Use shadowfb",
                          "Whether to use one or more shadow framebuffers",
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

  obj_props[PROP_SCALE] =
    g_param_spec_float ("scale",
                        "View scale",
                        "The view scale",
                        0.5, G_MAXFLOAT, 1.0,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT |
                        G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST, obj_props);
}
