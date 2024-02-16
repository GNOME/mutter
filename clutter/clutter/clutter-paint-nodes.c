/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2011  Intel Corporation.
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
 *
 * Author:
 *   Emmanuele Bassi <ebassi@linux.intel.com>
 */


#include "config.h"

#include "clutter/clutter-paint-node-private.h"

#include <pango/pango.h>

#include "cogl/cogl.h"
#include "clutter/clutter-actor-private.h"
#include "clutter/clutter-blur-private.h"
#include "clutter/clutter-color.h"
#include "clutter/clutter-debug.h"
#include "clutter/clutter-private.h"
#include "clutter/clutter-paint-context-private.h"

#include "clutter/clutter-paint-nodes.h"

static CoglPipeline *default_color_pipeline   = NULL;
static CoglPipeline *default_texture_pipeline = NULL;

/*< private >
 * clutter_paint_node_init_types:
 *
 * Initializes the required types for ClutterPaintNode subclasses
 */
void
clutter_paint_node_init_types (ClutterBackend *clutter_backend)
{
  CoglContext *cogl_context;
  CoglColor cogl_color;
  GType node_type G_GNUC_UNUSED;

  if (G_LIKELY (default_color_pipeline != NULL))
    return;

  cogl_context = clutter_backend_get_cogl_context (clutter_backend);

  node_type = clutter_paint_node_get_type ();

  cogl_color_init_from_4f (&cogl_color, 1.0, 1.0, 1.0, 1.0);

  default_color_pipeline = cogl_pipeline_new (cogl_context);
  cogl_pipeline_set_color (default_color_pipeline, &cogl_color);

  default_texture_pipeline = cogl_pipeline_new (cogl_context);
  cogl_pipeline_set_layer_null_texture (default_texture_pipeline, 0);
  cogl_pipeline_set_color (default_texture_pipeline, &cogl_color);
  cogl_pipeline_set_layer_wrap_mode (default_texture_pipeline, 0,
                                     COGL_PIPELINE_WRAP_MODE_AUTOMATIC);
}

/*
 * ClutterRootNode:
 *
 * Any frame can only have a since RootNode instance for each
 * top-level actor.
 */

#define clutter_root_node_get_type      clutter_root_node_get_type

struct _ClutterRootNode
{
  ClutterPaintNode parent_instance;

  CoglFramebuffer *framebuffer;

  CoglBufferBit clear_flags;
  CoglColor clear_color;
};

G_DEFINE_TYPE (ClutterRootNode, clutter_root_node, CLUTTER_TYPE_PAINT_NODE)

static gboolean
clutter_root_node_pre_draw (ClutterPaintNode    *node,
                            ClutterPaintContext *paint_context)
{
  ClutterRootNode *rnode = (ClutterRootNode *) node;

  clutter_paint_context_push_framebuffer (paint_context, rnode->framebuffer);

  cogl_framebuffer_clear (rnode->framebuffer,
                          rnode->clear_flags,
                          &rnode->clear_color);

  return TRUE;
}

static void
clutter_root_node_post_draw (ClutterPaintNode    *node,
                             ClutterPaintContext *paint_context)
{
  clutter_paint_context_pop_framebuffer (paint_context);
}

static void
clutter_root_node_finalize (ClutterPaintNode *node)
{
  ClutterRootNode *rnode = (ClutterRootNode *) node;

  g_object_unref (rnode->framebuffer);

  CLUTTER_PAINT_NODE_CLASS (clutter_root_node_parent_class)->finalize (node);
}

static CoglFramebuffer *
clutter_root_node_get_framebuffer (ClutterPaintNode *node)
{
  ClutterRootNode *rnode = (ClutterRootNode *) node;

  return rnode->framebuffer;
}

static void
clutter_root_node_class_init (ClutterRootNodeClass *klass)
{
  ClutterPaintNodeClass *node_class = CLUTTER_PAINT_NODE_CLASS (klass);

  node_class->pre_draw = clutter_root_node_pre_draw;
  node_class->post_draw = clutter_root_node_post_draw;
  node_class->finalize = clutter_root_node_finalize;
  node_class->get_framebuffer = clutter_root_node_get_framebuffer;
}

static void
clutter_root_node_init (ClutterRootNode *self)
{
}

ClutterPaintNode *
clutter_root_node_new (CoglFramebuffer    *framebuffer,
                       const ClutterColor *clear_color,
                       CoglBufferBit       clear_flags)
{
  ClutterRootNode *res;

  g_return_val_if_fail (framebuffer, NULL);

  res = _clutter_paint_node_create (CLUTTER_TYPE_ROOT_NODE);

  cogl_color_init_from_4f (&res->clear_color,
                           clear_color->red / 255.0,
                           clear_color->green / 255.0,
                           clear_color->blue / 255.0,
                           clear_color->alpha / 255.0);
  cogl_color_premultiply (&res->clear_color);

  res->framebuffer = g_object_ref (framebuffer);
  res->clear_flags = clear_flags;

  return (ClutterPaintNode *) res;
}

/*
 * ClutterTransformNode:
 */

struct _ClutterTransformNode
{
  ClutterPaintNode parent_instance;

  graphene_matrix_t transform;
};

struct _ClutterTransformNodeClass
{
  ClutterPaintNodeClass parent_class;
};

G_DEFINE_TYPE (ClutterTransformNode, clutter_transform_node, CLUTTER_TYPE_PAINT_NODE)

static gboolean
clutter_transform_node_pre_draw (ClutterPaintNode    *node,
                                 ClutterPaintContext *paint_context)
{
  ClutterTransformNode *transform_node = (ClutterTransformNode *) node;
  CoglFramebuffer *fb =
   clutter_paint_context_get_framebuffer (paint_context);

  cogl_framebuffer_push_matrix (fb);
  cogl_framebuffer_transform (fb, &transform_node->transform);

  return TRUE;
}

static void
clutter_transform_node_post_draw (ClutterPaintNode    *node,
                                  ClutterPaintContext *paint_context)
{
  CoglFramebuffer *fb =
   clutter_paint_context_get_framebuffer (paint_context);

  cogl_framebuffer_pop_matrix (fb);
}

static void
clutter_transform_node_class_init (ClutterTransformNodeClass *klass)
{
  ClutterPaintNodeClass *node_class;

  node_class = CLUTTER_PAINT_NODE_CLASS (klass);
  node_class->pre_draw = clutter_transform_node_pre_draw;
  node_class->post_draw = clutter_transform_node_post_draw;
}

static void
clutter_transform_node_init (ClutterTransformNode *self)
{
  graphene_matrix_init_identity (&self->transform);
}

/*
 * clutter_transform_node_new:
 * @transform: (nullable): the transform matrix to apply
 *
 * Return value: (transfer full): the newly created #ClutterTransformNode.
 *   Use clutter_paint_node_unref() when done.
 */
ClutterPaintNode *
clutter_transform_node_new (const graphene_matrix_t *transform)
{
  ClutterTransformNode *res;

  res = _clutter_paint_node_create (CLUTTER_TYPE_TRANSFORM_NODE);
  if (transform)
    graphene_matrix_init_from_matrix (&res->transform, transform);

  return (ClutterPaintNode *) res;
}

/*
 * Dummy node, private
 *
 * an empty node, used temporarily until we can drop API compatibility,
 * and we'll be able to build a full render tree for each frame.
 */

#define clutter_dummy_node_get_type      _clutter_dummy_node_get_type

typedef struct _ClutterDummyNode        ClutterDummyNode;
typedef struct _ClutterPaintNodeClass   ClutterDummyNodeClass;

struct _ClutterDummyNode
{
  ClutterPaintNode parent_instance;

  ClutterActor *actor;
  CoglFramebuffer *framebuffer;
};

G_DEFINE_TYPE (ClutterDummyNode, clutter_dummy_node, CLUTTER_TYPE_PAINT_NODE)

static gboolean
clutter_dummy_node_pre_draw (ClutterPaintNode    *node,
                             ClutterPaintContext *paint_context)
{
  return TRUE;
}

static CoglFramebuffer *
clutter_dummy_node_get_framebuffer (ClutterPaintNode *node)
{
  ClutterDummyNode *dnode = (ClutterDummyNode *) node;

  return dnode->framebuffer;
}

static void
clutter_dummy_node_finalize (ClutterPaintNode *node)
{
  ClutterDummyNode *dnode = (ClutterDummyNode *) node;

  g_clear_object (&dnode->framebuffer);

  CLUTTER_PAINT_NODE_CLASS (clutter_dummy_node_parent_class)->finalize (node);
}

static void
clutter_dummy_node_class_init (ClutterDummyNodeClass *klass)
{
  ClutterPaintNodeClass *node_class = CLUTTER_PAINT_NODE_CLASS (klass);

  node_class->pre_draw = clutter_dummy_node_pre_draw;
  node_class->get_framebuffer = clutter_dummy_node_get_framebuffer;
  node_class->finalize = clutter_dummy_node_finalize;
}

static void
clutter_dummy_node_init (ClutterDummyNode *self)
{
}

ClutterPaintNode *
_clutter_dummy_node_new (ClutterActor    *actor,
                         CoglFramebuffer *framebuffer)
{
  ClutterPaintNode *res;
  ClutterDummyNode *dnode;

  res = _clutter_paint_node_create (_clutter_dummy_node_get_type ());

  dnode = (ClutterDummyNode *) res;
  dnode->actor = actor;
  dnode->framebuffer = g_object_ref (framebuffer);

  return res;
}

/**
 * ClutterPipelineNode:
 */

struct _ClutterPipelineNode
{
  ClutterPaintNode parent_instance;

  CoglPipeline *pipeline;
};

/**
 * ClutterPipelineNodeClass:
 *
 * The `ClutterPipelineNodeClass` structure is an opaque
 * type whose members cannot be directly accessed.
 */
struct _ClutterPipelineNodeClass
{
  ClutterPaintNodeClass parent_class;
};

G_DEFINE_TYPE (ClutterPipelineNode, clutter_pipeline_node, CLUTTER_TYPE_PAINT_NODE)

static void
clutter_pipeline_node_finalize (ClutterPaintNode *node)
{
  ClutterPipelineNode *pnode = CLUTTER_PIPELINE_NODE (node);

  g_clear_object (&pnode->pipeline);

  CLUTTER_PAINT_NODE_CLASS (clutter_pipeline_node_parent_class)->finalize (node);
}

static gboolean
clutter_pipeline_node_pre_draw (ClutterPaintNode    *node,
                                ClutterPaintContext *paint_context)
{
  ClutterPipelineNode *pnode = CLUTTER_PIPELINE_NODE (node);

  if (node->operations != NULL &&
      pnode->pipeline != NULL)
    return TRUE;

  return FALSE;
}

static CoglFramebuffer *
get_target_framebuffer (ClutterPaintNode    *node,
                        ClutterPaintContext *paint_context)
{
  CoglFramebuffer *framebuffer;

  framebuffer = clutter_paint_node_get_framebuffer (node);
  if (framebuffer)
    return framebuffer;

  return clutter_paint_context_get_framebuffer (paint_context);
}

static void
clutter_pipeline_node_draw (ClutterPaintNode    *node,
                            ClutterPaintContext *paint_context)
{
  ClutterPipelineNode *pnode = CLUTTER_PIPELINE_NODE (node);
  CoglFramebuffer *fb;
  guint i;

  if (pnode->pipeline == NULL)
    return;

  if (node->operations == NULL)
    return;

  fb = clutter_paint_context_get_framebuffer (paint_context);

  for (i = 0; i < node->operations->len; i++)
    {
      const ClutterPaintOperation *op;

      op = &g_array_index (node->operations, ClutterPaintOperation, i);

      switch (op->opcode)
        {
        case PAINT_OP_INVALID:
          break;

        case PAINT_OP_TEX_RECT:
          cogl_framebuffer_draw_textured_rectangle (fb,
                                                    pnode->pipeline,
                                                    op->op.texrect[0],
                                                    op->op.texrect[1],
                                                    op->op.texrect[2],
                                                    op->op.texrect[3],
                                                    op->op.texrect[4],
                                                    op->op.texrect[5],
                                                    op->op.texrect[6],
                                                    op->op.texrect[7]);
          break;

        case PAINT_OP_TEX_RECTS:
          cogl_framebuffer_draw_textured_rectangles (fb,
                                                     pnode->pipeline,
                                                     (float *) op->coords->data,
                                                     op->coords->len / 8);
          break;

        case PAINT_OP_MULTITEX_RECT:
          cogl_framebuffer_draw_multitextured_rectangle (fb,
                                                         pnode->pipeline,
                                                         op->op.texrect[0],
                                                         op->op.texrect[1],
                                                         op->op.texrect[2],
                                                         op->op.texrect[3],
                                                         (float *) op->coords->data,
                                                         op->coords->len);
          break;

        case PAINT_OP_PRIMITIVE:
          cogl_primitive_draw (op->op.primitive,
                               fb,
                               pnode->pipeline);
          break;
        }
    }
}

static void
clutter_pipeline_node_post_draw (ClutterPaintNode    *node,
                                 ClutterPaintContext *paint_context)
{
}

static void
clutter_pipeline_node_class_init (ClutterPipelineNodeClass *klass)
{
  ClutterPaintNodeClass *node_class;

  node_class = CLUTTER_PAINT_NODE_CLASS (klass);
  node_class->pre_draw = clutter_pipeline_node_pre_draw;
  node_class->draw = clutter_pipeline_node_draw;
  node_class->post_draw = clutter_pipeline_node_post_draw;
  node_class->finalize = clutter_pipeline_node_finalize;
}

static void
clutter_pipeline_node_init (ClutterPipelineNode *self)
{
}

/**
 * clutter_pipeline_node_new:
 * @pipeline: (allow-none): a Cogl pipeline state object, or %NULL
 *
 * Creates a new #ClutterPaintNode that will use the @pipeline to
 * paint its contents.
 *
 * This function will acquire a reference on the passed @pipeline,
 * so it is safe to call g_object_unref() when it returns.
 *
 * Return value: (transfer full): the newly created #ClutterPaintNode.
 *   Use clutter_paint_node_unref() when done.
 */
ClutterPaintNode *
clutter_pipeline_node_new (CoglPipeline *pipeline)
{
  ClutterPipelineNode *res;

  g_return_val_if_fail (pipeline == NULL || COGL_IS_PIPELINE (pipeline), NULL);

  res = _clutter_paint_node_create (CLUTTER_TYPE_PIPELINE_NODE);

  if (pipeline != NULL)
    res->pipeline = g_object_ref (pipeline);

  return (ClutterPaintNode *) res;
}

/**
 * ClutterColorNode:
 */

struct _ClutterColorNode
{
  ClutterPipelineNode parent_instance;
};

/**
 * ClutterColorNodeClass:
 *
 * The `ClutterColorNodeClass` structure is an
 * opaque type whose members cannot be directly accessed.
 */
struct _ClutterColorNodeClass
{
  ClutterPipelineNodeClass parent_class;
};

G_DEFINE_TYPE (ClutterColorNode, clutter_color_node, CLUTTER_TYPE_PIPELINE_NODE)

static void
clutter_color_node_class_init (ClutterColorNodeClass *klass)
{

}

static void
clutter_color_node_init (ClutterColorNode *cnode)
{
  ClutterPipelineNode *pnode = CLUTTER_PIPELINE_NODE (cnode);

  g_assert (default_color_pipeline != NULL);
  pnode->pipeline = cogl_pipeline_copy (default_color_pipeline);
}

/**
 * clutter_color_node_new:
 * @color: (allow-none): the color to paint, or %NULL
 *
 * Creates a new #ClutterPaintNode that will paint a solid color
 * fill using @color.
 *
 * Return value: (transfer full): the newly created #ClutterPaintNode. Use
 *   clutter_paint_node_unref() when done
 */
ClutterPaintNode *
clutter_color_node_new (const ClutterColor *color)
{
  ClutterPipelineNode *cnode;

  cnode = _clutter_paint_node_create (CLUTTER_TYPE_COLOR_NODE);

  if (color != NULL)
    {
      CoglColor cogl_color;

      cogl_color_init_from_4f (&cogl_color,
                               color->red / 255.0,
                               color->green / 255.0,
                               color->blue / 255.0,
                               color->alpha / 255.0);
      cogl_color_premultiply (&cogl_color);

      cogl_pipeline_set_color (cnode->pipeline, &cogl_color);
    }

  return (ClutterPaintNode *) cnode;
}

/**
 * ClutterTextureNode:
 *
 */

struct _ClutterTextureNode
{
  ClutterPipelineNode parent_instance;
};

/**
 * ClutterTextureNodeClass:
 *
 * The `ClutterTextureNodeClass` structure is an
 * opaque type whose members cannot be directly accessed.
 */
struct _ClutterTextureNodeClass
{
  ClutterPipelineNodeClass parent_class;
};

G_DEFINE_TYPE (ClutterTextureNode, clutter_texture_node, CLUTTER_TYPE_PIPELINE_NODE)

static void
clutter_texture_node_class_init (ClutterTextureNodeClass *klass)
{
}

static void
clutter_texture_node_init (ClutterTextureNode *self)
{
  ClutterPipelineNode *pnode = CLUTTER_PIPELINE_NODE (self);

  g_assert (default_texture_pipeline != NULL);
  pnode->pipeline = cogl_pipeline_copy (default_texture_pipeline);
}

static CoglPipelineFilter
clutter_scaling_filter_to_cogl_pipeline_filter (ClutterScalingFilter filter)
{
  switch (filter)
    {
    case CLUTTER_SCALING_FILTER_NEAREST:
      return COGL_PIPELINE_FILTER_NEAREST;

    case CLUTTER_SCALING_FILTER_LINEAR:
      return COGL_PIPELINE_FILTER_LINEAR;

    case CLUTTER_SCALING_FILTER_TRILINEAR:
      return COGL_PIPELINE_FILTER_LINEAR_MIPMAP_LINEAR;
    }

  return COGL_PIPELINE_FILTER_LINEAR;
}

/**
 * clutter_texture_node_new:
 * @texture: a #CoglTexture
 * @color: (allow-none): a #ClutterColor used for blending, or %NULL
 * @min_filter: the minification filter for the texture
 * @mag_filter: the magnification filter for the texture
 *
 * Creates a new #ClutterPaintNode that will paint the passed @texture.
 *
 * This function will take a reference on @texture, so it is safe to
 * call g_object_unref() on @texture when it returns.
 *
 * The @color must not be pre-multiplied with its #ClutterColor.alpha
 * channel value; if @color is %NULL, a fully opaque white color will
 * be used for blending.
 *
 * Return value: (transfer full): the newly created #ClutterPaintNode.
 *   Use clutter_paint_node_unref() when done
 */
ClutterPaintNode *
clutter_texture_node_new (CoglTexture          *texture,
                          const ClutterColor   *color,
                          ClutterScalingFilter  min_filter,
                          ClutterScalingFilter  mag_filter)
{
  ClutterPipelineNode *tnode;
  CoglColor cogl_color;
  CoglPipelineFilter min_f, mag_f;

  g_return_val_if_fail (COGL_IS_TEXTURE (texture), NULL);

  tnode = _clutter_paint_node_create (CLUTTER_TYPE_TEXTURE_NODE);

  cogl_pipeline_set_layer_texture (tnode->pipeline, 0, texture);

  min_f = clutter_scaling_filter_to_cogl_pipeline_filter (min_filter);
  mag_f = clutter_scaling_filter_to_cogl_pipeline_filter (mag_filter);
  cogl_pipeline_set_layer_filters (tnode->pipeline, 0, min_f, mag_f);

  if (color != NULL)
    {
      cogl_color_init_from_4f (&cogl_color,
                               color->red / 255.0,
                               color->green / 255.0,
                               color->blue / 255.0,
                               color->alpha / 255.0);
      cogl_color_premultiply (&cogl_color);
    }
  else
    cogl_color_init_from_4f (&cogl_color, 1.0, 1.0, 1.0, 1.0);

  cogl_pipeline_set_color (tnode->pipeline, &cogl_color);

  return (ClutterPaintNode *) tnode;
}


/**
 * ClutterTextNode:
 */
struct _ClutterTextNode
{
  ClutterPaintNode parent_instance;

  PangoLayout *layout;
  CoglColor color;
};

/**
 * ClutterTextNodeClass:
 *
 * The `ClutterTextNodeClass` structure is an opaque
 * type whose contents cannot be directly accessed.
 */
struct _ClutterTextNodeClass
{
  ClutterPaintNodeClass parent_class;
};

G_DEFINE_TYPE (ClutterTextNode, clutter_text_node, CLUTTER_TYPE_PAINT_NODE)

static void
clutter_text_node_finalize (ClutterPaintNode *node)
{
  ClutterTextNode *tnode = CLUTTER_TEXT_NODE (node);

  g_clear_object (&tnode->layout);

  CLUTTER_PAINT_NODE_CLASS (clutter_text_node_parent_class)->finalize (node);
}

static gboolean
clutter_text_node_pre_draw (ClutterPaintNode    *node,
                            ClutterPaintContext *paint_context)
{
  ClutterTextNode *tnode = CLUTTER_TEXT_NODE (node);

  return tnode->layout != NULL;
}

static void
clutter_text_node_draw (ClutterPaintNode    *node,
                        ClutterPaintContext *paint_context)
{
  ClutterTextNode *tnode = CLUTTER_TEXT_NODE (node);
  PangoRectangle extents;
  CoglFramebuffer *fb;
  guint i;

  if (node->operations == NULL)
    return;

  fb = get_target_framebuffer (node, paint_context);

  pango_layout_get_pixel_extents (tnode->layout, NULL, &extents);

  for (i = 0; i < node->operations->len; i++)
    {
      const ClutterPaintOperation *op;
      float op_width, op_height;
      gboolean clipped = FALSE;

      op = &g_array_index (node->operations, ClutterPaintOperation, i);

      switch (op->opcode)
        {
        case PAINT_OP_TEX_RECT:
          op_width = op->op.texrect[2] - op->op.texrect[0];
          op_height = op->op.texrect[3] - op->op.texrect[1];

          /* if the primitive size was smaller than the layout,
           * we clip the layout when drawin, to avoid spilling
           * it out
           */
          if (extents.width > op_width ||
              extents.height > op_height)
            {
              cogl_framebuffer_push_rectangle_clip (fb,
                                                    op->op.texrect[0],
                                                    op->op.texrect[1],
                                                    op->op.texrect[2],
                                                    op->op.texrect[3]);
              clipped = TRUE;
            }

          cogl_pango_show_layout (fb,
                                  tnode->layout,
                                  op->op.texrect[0],
                                  op->op.texrect[1],
                                  &tnode->color);

          if (clipped)
            cogl_framebuffer_pop_clip (fb);
          break;

        case PAINT_OP_TEX_RECTS:
        case PAINT_OP_MULTITEX_RECT:
        case PAINT_OP_PRIMITIVE:
        case PAINT_OP_INVALID:
          break;
        }
    }
}

static void
clutter_text_node_class_init (ClutterTextNodeClass *klass)
{
  ClutterPaintNodeClass *node_class = CLUTTER_PAINT_NODE_CLASS (klass);

  node_class->pre_draw = clutter_text_node_pre_draw;
  node_class->draw = clutter_text_node_draw;
  node_class->finalize = clutter_text_node_finalize;
}

static void
clutter_text_node_init (ClutterTextNode *self)
{
  cogl_color_init_from_4f (&self->color, 0.0, 0.0, 0.0, 1.0);
}

/**
 * clutter_text_node_new:
 * @layout: (allow-none): a #PangoLayout, or %NULL
 * @color: (allow-none): the color used to paint the layout,
 *   or %NULL
 *
 * Creates a new #ClutterPaintNode that will paint a #PangoLayout
 * with the given color.
 *
 * This function takes a reference on the passed @layout, so it
 * is safe to call g_object_unref() after it returns.
 *
 * Return value: (transfer full): the newly created #ClutterPaintNode.
 *   Use clutter_paint_node_unref() when done
 */
ClutterPaintNode *
clutter_text_node_new (PangoLayout        *layout,
                       const ClutterColor *color)
{
  ClutterTextNode *res;

  g_return_val_if_fail (layout == NULL || PANGO_IS_LAYOUT (layout), NULL);

  res = _clutter_paint_node_create (CLUTTER_TYPE_TEXT_NODE);

  if (layout != NULL)
    res->layout = g_object_ref (layout);

  if (color != NULL)
    {
      cogl_color_init_from_4f (&res->color,
                               color->red / 255.0,
                               color->green / 255.0,
                               color->blue / 255.0,
                               color->alpha / 255.0);
    }

  return (ClutterPaintNode *) res;
}

/**
 * ClutterClipNode:
 */
struct _ClutterClipNode
{
  ClutterPaintNode parent_instance;
};

/**
 * ClutterClipNodeClass:
 *
 * The `ClutterClipNodeClass` structure is an opaque
 * type whose members cannot be directly accessed.
 */
struct _ClutterClipNodeClass
{
  ClutterPaintNodeClass parent_class;
};

G_DEFINE_TYPE (ClutterClipNode, clutter_clip_node, CLUTTER_TYPE_PAINT_NODE)

static gboolean
clutter_clip_node_pre_draw (ClutterPaintNode    *node,
                            ClutterPaintContext *paint_context)
{
  gboolean retval = FALSE;
  CoglFramebuffer *fb;
  guint i;

  if (node->operations == NULL)
    return FALSE;

  fb = get_target_framebuffer (node, paint_context);

  for (i = 0; i < node->operations->len; i++)
    {
      const ClutterPaintOperation *op;

      op = &g_array_index (node->operations, ClutterPaintOperation, i);

      switch (op->opcode)
        {
        case PAINT_OP_TEX_RECT:
          cogl_framebuffer_push_rectangle_clip (fb,
                                                op->op.texrect[0],
                                                op->op.texrect[1],
                                                op->op.texrect[2],
                                                op->op.texrect[3]);
          retval = TRUE;
          break;

        case PAINT_OP_TEX_RECTS:
        case PAINT_OP_MULTITEX_RECT:
        case PAINT_OP_PRIMITIVE:
        case PAINT_OP_INVALID:
          break;
        }
    }

  return retval;
}

static void
clutter_clip_node_post_draw (ClutterPaintNode    *node,
                             ClutterPaintContext *paint_context)
{
  CoglFramebuffer *fb;
  guint i;

  if (node->operations == NULL)
    return;

  fb = get_target_framebuffer (node, paint_context);

  for (i = 0; i < node->operations->len; i++)
    {
      const ClutterPaintOperation *op;

      op = &g_array_index (node->operations, ClutterPaintOperation, i);

      switch (op->opcode)
        {
        case PAINT_OP_TEX_RECT:
          cogl_framebuffer_pop_clip (fb);
          break;

        case PAINT_OP_TEX_RECTS:
        case PAINT_OP_MULTITEX_RECT:
        case PAINT_OP_PRIMITIVE:
        case PAINT_OP_INVALID:
          break;
        }
    }
}

static void
clutter_clip_node_class_init (ClutterClipNodeClass *klass)
{
  ClutterPaintNodeClass *node_class;

  node_class = CLUTTER_PAINT_NODE_CLASS (klass);
  node_class->pre_draw = clutter_clip_node_pre_draw;
  node_class->post_draw = clutter_clip_node_post_draw;
}

static void
clutter_clip_node_init (ClutterClipNode *self)
{
}

/**
 * clutter_clip_node_new:
 *
 * Creates a new #ClutterPaintNode that will clip its child
 * nodes to the 2D regions added to it.
 *
 * Return value: (transfer full): the newly created #ClutterPaintNode.
 *   Use clutter_paint_node_unref() when done.
 */
ClutterPaintNode *
clutter_clip_node_new (void)
{
  return _clutter_paint_node_create (CLUTTER_TYPE_CLIP_NODE);
}

/**
 * ClutterActorNode:
 */

struct _ClutterActorNode
{
  ClutterPaintNode parent_instance;

  ClutterActor *actor;
  int opacity_override;
  int saved_opacity_override;
};

struct _ClutterActorNodeClass
{
  ClutterPaintNodeClass parent_class;
};

G_DEFINE_TYPE (ClutterActorNode, clutter_actor_node, CLUTTER_TYPE_PAINT_NODE)

static gboolean
clutter_actor_node_pre_draw (ClutterPaintNode    *node,
                             ClutterPaintContext *paint_context)
{
  ClutterActorNode *actor_node = CLUTTER_ACTOR_NODE (node);

  if (actor_node->opacity_override != -1)
    {
      actor_node->saved_opacity_override =
        clutter_actor_get_opacity_override (actor_node->actor);
      clutter_actor_set_opacity_override (actor_node->actor,
                                          actor_node->opacity_override);
    }

  CLUTTER_SET_PRIVATE_FLAGS (actor_node->actor, CLUTTER_IN_PAINT);

  return TRUE;
}

static void
clutter_actor_node_draw (ClutterPaintNode    *node,
                         ClutterPaintContext *paint_context)
{
  ClutterActorNode *actor_node = CLUTTER_ACTOR_NODE (node);

  clutter_actor_continue_paint (actor_node->actor, paint_context);
}

static void
clutter_actor_node_post_draw (ClutterPaintNode    *node,
                              ClutterPaintContext *paint_context)
{
  ClutterActorNode *actor_node = CLUTTER_ACTOR_NODE (node);

  CLUTTER_UNSET_PRIVATE_FLAGS (actor_node->actor, CLUTTER_IN_PAINT);

 if (actor_node->opacity_override != -1)
    {
      clutter_actor_set_opacity_override (actor_node->actor,
                                          actor_node->saved_opacity_override);
    }
}

static void
clutter_actor_node_class_init (ClutterActorNodeClass *klass)
{
  ClutterPaintNodeClass *node_class;

  node_class = CLUTTER_PAINT_NODE_CLASS (klass);
  node_class->pre_draw = clutter_actor_node_pre_draw;
  node_class->draw = clutter_actor_node_draw;
  node_class->post_draw = clutter_actor_node_post_draw;
}

static void
clutter_actor_node_init (ClutterActorNode *self)
{
}

/*
 * clutter_actor_node_new:
 * @actor: the actor to paint
 * @opacity: opacity to draw the actor with, or -1 to use the actor's opacity
 *
 * Creates a new #ClutterActorNode.
 *
 * The actor is painted together with any effects
 * applied to it. Children of this node will draw
 * over the actor contents.
 *
 * Return value: (transfer full): the newly created #ClutterActorNode.
 *   Use clutter_paint_node_unref() when done.
 */
ClutterPaintNode *
clutter_actor_node_new (ClutterActor *actor,
                        int           opacity)
{
  ClutterActorNode *res;

  g_assert (actor != NULL);

  res = _clutter_paint_node_create (CLUTTER_TYPE_ACTOR_NODE);
  res->actor = actor;
  res->opacity_override = CLAMP (opacity, -1, 255);

  return (ClutterPaintNode *) res;
}


/*
 * ClutterEffectNode
 */

struct _ClutterEffectNode
{
  ClutterPaintNode parent_instance;

  ClutterEffect *effect;
};

struct _ClutterEffectNodeClass
{
  ClutterPaintNodeClass parent_class;
};

G_DEFINE_TYPE (ClutterEffectNode, clutter_effect_node, CLUTTER_TYPE_PAINT_NODE)

static void
clutter_effect_node_class_init (ClutterEffectNodeClass *klass)
{
}

static void
clutter_effect_node_init (ClutterEffectNode *self)
{
}

/**
 * clutter_effect_node_new:
 * @effect: the actor to paint
 *
 * Creates a new #ClutterEffectNode.
 *
 * Return value: (transfer full): the newly created #ClutterEffectNode.
 *   Use clutter_paint_node_unref() when done.
 */
ClutterPaintNode *
clutter_effect_node_new (ClutterEffect *effect)
{
  ClutterEffectNode *res;

  g_assert (CLUTTER_IS_EFFECT (effect));

  res = _clutter_paint_node_create (CLUTTER_TYPE_EFFECT_NODE);
  res->effect = effect;

  return (ClutterPaintNode *) res;
}

/*
 * ClutterLayerNode
 */

struct _ClutterLayerNode
{
  ClutterPaintNode parent_instance;

  float fbo_width;
  float fbo_height;

  CoglPipeline *pipeline;
  CoglFramebuffer *offscreen;

  guint8 opacity;
};

struct _ClutterLayerNodeClass
{
  ClutterPaintNodeClass parent_class;
};

G_DEFINE_TYPE (ClutterLayerNode, clutter_layer_node, CLUTTER_TYPE_PAINT_NODE)

static gboolean
clutter_layer_node_pre_draw (ClutterPaintNode *node,
                             ClutterPaintContext *paint_context)
{
  ClutterLayerNode *lnode = (ClutterLayerNode *) node;

  /* if we were unable to create an offscreen buffer for this node, then
   * we simply ignore it
   */
  if (lnode->offscreen == NULL)
    return FALSE;

  clutter_paint_context_push_framebuffer (paint_context, lnode->offscreen);

  /* clear out the target framebuffer */
  cogl_framebuffer_clear4f (lnode->offscreen,
                            COGL_BUFFER_BIT_COLOR | COGL_BUFFER_BIT_DEPTH,
                            0.f, 0.f, 0.f, 0.f);

  cogl_framebuffer_push_matrix (lnode->offscreen);

  /* every draw operation after this point will happen an offscreen
   * framebuffer
   */

  return TRUE;
}

static void
clutter_layer_node_post_draw (ClutterPaintNode    *node,
                              ClutterPaintContext *paint_context)
{
  ClutterLayerNode *lnode = CLUTTER_LAYER_NODE (node);
  CoglFramebuffer *fb;
  guint i;

  /* switch to the previous framebuffer */
  cogl_framebuffer_pop_matrix (lnode->offscreen);
  clutter_paint_context_pop_framebuffer (paint_context);

  if (!node->operations)
    return;

  fb = clutter_paint_context_get_framebuffer (paint_context);

  for (i = 0; i < node->operations->len; i++)
    {
      const ClutterPaintOperation *op;

      op = &g_array_index (node->operations, ClutterPaintOperation, i);
      switch (op->opcode)
        {
        case PAINT_OP_INVALID:
          break;

        case PAINT_OP_TEX_RECT:
          /* now we need to paint the texture */
          cogl_framebuffer_draw_textured_rectangle (fb,
                                                    lnode->pipeline,
                                                    op->op.texrect[0],
                                                    op->op.texrect[1],
                                                    op->op.texrect[2],
                                                    op->op.texrect[3],
                                                    op->op.texrect[4],
                                                    op->op.texrect[5],
                                                    op->op.texrect[6],
                                                    op->op.texrect[7]);
          break;

        case PAINT_OP_TEX_RECTS:
          cogl_framebuffer_draw_textured_rectangles (fb,
                                                     lnode->pipeline,
                                                     (float *) op->coords->data,
                                                     op->coords->len / 8);
          break;

        case PAINT_OP_MULTITEX_RECT:
          cogl_framebuffer_draw_multitextured_rectangle (fb,
                                                         lnode->pipeline,
                                                         op->op.texrect[0],
                                                         op->op.texrect[1],
                                                         op->op.texrect[2],
                                                         op->op.texrect[3],
                                                         (float *) op->coords->data,
                                                         op->coords->len);
          break;

        case PAINT_OP_PRIMITIVE:
          cogl_primitive_draw (op->op.primitive,
                               fb,
                               lnode->pipeline);
          break;
        }
    }
}

static void
clutter_layer_node_finalize (ClutterPaintNode *node)
{
  ClutterLayerNode *lnode = CLUTTER_LAYER_NODE (node);

  g_clear_object (&lnode->pipeline);
  g_clear_object (&lnode->offscreen);

  CLUTTER_PAINT_NODE_CLASS (clutter_layer_node_parent_class)->finalize (node);
}

static void
clutter_layer_node_class_init (ClutterLayerNodeClass *klass)
{
  ClutterPaintNodeClass *node_class;

  node_class = CLUTTER_PAINT_NODE_CLASS (klass);
  node_class->pre_draw = clutter_layer_node_pre_draw;
  node_class->post_draw = clutter_layer_node_post_draw;
  node_class->finalize = clutter_layer_node_finalize;
}

static void
clutter_layer_node_init (ClutterLayerNode *self)
{
}


/**
 * clutter_layer_node_new_to_framebuffer:
 * @framebuffer: a #CoglFramebuffer
 * @pipeline: a #CoglPipeline
 *
 * Creates a new #ClutterLayerNode that will redirect drawing at
 * @framebuffer. It will then use @pipeline to paint the stored
 * operations.
 *
 * When using this constructor, the caller is responsible for setting
 * up @framebuffer, including its modelview and projection matrices,
 * and the viewport, and the @pipeline as well.
 *
 * Return value: (transfer full): the newly created #ClutterLayerNode.
 *   Use clutter_paint_node_unref() when done.
 */
ClutterPaintNode *
clutter_layer_node_new_to_framebuffer (CoglFramebuffer *framebuffer,
                                       CoglPipeline    *pipeline)
{
  ClutterLayerNode *res;

  g_return_val_if_fail (COGL_IS_FRAMEBUFFER (framebuffer), NULL);
  g_return_val_if_fail (COGL_IS_PIPELINE (pipeline), NULL);

  res = _clutter_paint_node_create (CLUTTER_TYPE_LAYER_NODE);

  res->fbo_width = cogl_framebuffer_get_width (framebuffer);
  res->fbo_height = cogl_framebuffer_get_height (framebuffer);
  res->offscreen = g_object_ref (framebuffer);
  res->pipeline = cogl_pipeline_copy (pipeline);

  return (ClutterPaintNode *) res;
}

/*
 * ClutterBlitNode
 */

struct _ClutterBlitNode
{
  ClutterPaintNode parent_instance;

  CoglFramebuffer *src;
};

G_DEFINE_TYPE (ClutterBlitNode, clutter_blit_node, CLUTTER_TYPE_PAINT_NODE)

static gboolean
clutter_blit_node_pre_draw (ClutterPaintNode    *node,
                            ClutterPaintContext *paint_context)
{
  return TRUE;
}

static void
clutter_blit_node_draw (ClutterPaintNode    *node,
                        ClutterPaintContext *paint_context)
{
  ClutterBlitNode *blit_node = CLUTTER_BLIT_NODE (node);
  g_autoptr (GError) error = NULL;
  CoglFramebuffer *framebuffer;
  unsigned int i;

  if (node->operations == NULL)
    return;

  framebuffer = get_target_framebuffer (node, paint_context);

  for (i = 0; i < node->operations->len; i++)
    {
      const ClutterPaintOperation *op;
      float op_width, op_height;

      op = &g_array_index (node->operations, ClutterPaintOperation, i);

      switch (op->opcode)
        {
        case PAINT_OP_INVALID:
          break;

        case PAINT_OP_TEX_RECT:
          op_width = op->op.texrect[6] - op->op.texrect[4];
          op_height = op->op.texrect[7] - op->op.texrect[5];

          cogl_blit_framebuffer (blit_node->src,
                                 framebuffer,
                                 op->op.texrect[0],
                                 op->op.texrect[1],
                                 op->op.texrect[4],
                                 op->op.texrect[5],
                                 op_width,
                                 op_height,
                                 &error);

          if (error)
            {
              g_warning ("Error blitting framebuffers: %s", error->message);
              return;
            }
          break;

        case PAINT_OP_TEX_RECTS:
        case PAINT_OP_MULTITEX_RECT:
        case PAINT_OP_PRIMITIVE:
          break;
        }
    }
}

static void
clutter_blit_node_finalize (ClutterPaintNode *node)
{
  ClutterBlitNode *blit_node = CLUTTER_BLIT_NODE (node);

  g_clear_object (&blit_node->src);

  CLUTTER_PAINT_NODE_CLASS (clutter_blit_node_parent_class)->finalize (node);
}

static void
clutter_blit_node_class_init (ClutterBlitNodeClass *klass)
{
  ClutterPaintNodeClass *node_class;

  node_class = CLUTTER_PAINT_NODE_CLASS (klass);
  node_class->pre_draw = clutter_blit_node_pre_draw;
  node_class->draw = clutter_blit_node_draw;
  node_class->finalize = clutter_blit_node_finalize;
}

static void
clutter_blit_node_init (ClutterBlitNode *self)
{
}

/**
 * clutter_blit_node_new:
 * @src: the source #CoglFramebuffer
 *
 * Creates a new #ClutterBlitNode that blits @src into the current
 * draw framebuffer.
 *
 * You must only add rectangles using [method@BlitNode.add_blit_rectangle].
 *
 * Return value: (transfer full): the newly created #ClutterBlitNode.
 *   Use clutter_paint_node_unref() when done.
 */
ClutterPaintNode *
clutter_blit_node_new (CoglFramebuffer *src)
{
  ClutterBlitNode *res;

  g_return_val_if_fail (COGL_IS_FRAMEBUFFER (src), NULL);

  res = _clutter_paint_node_create (CLUTTER_TYPE_BLIT_NODE);
  res->src = g_object_ref (src);

  return (ClutterPaintNode *) res;
}

/**
 * clutter_blit_node_add_blit_rectangle:
 * @blit_node: a #ClutterBlitNode
 * @src_x: Source x position
 * @src_y: Source y position
 * @dst_x: Destination x position
 * @dst_y: Destination y position
 * @width: Width of region to copy
 * @height: Height of region to copy
 *
 * Adds a new blit rectangle to the stack of rectangles. All the
 * constraints of [func@Cogl.blit_framebuffer] apply here.
 */
void
clutter_blit_node_add_blit_rectangle (ClutterBlitNode *blit_node,
                                      int              src_x,
                                      int              src_y,
                                      int              dst_x,
                                      int              dst_y,
                                      int              width,
                                      int              height)
{
  g_return_if_fail (CLUTTER_IS_BLIT_NODE (blit_node));

  clutter_paint_node_add_texture_rectangle (CLUTTER_PAINT_NODE (blit_node),
                                            &(ClutterActorBox) {
                                              .x1 = src_x,
                                              .y1 = src_y,
                                              .x2 = src_x + width,
                                              .y2 = src_y + height,
                                            },
                                            dst_x,
                                            dst_y,
                                            dst_x + width,
                                            dst_y + height);
}

/**
 * ClutterBlurNode:
 */

struct _ClutterBlurNode
{
  ClutterLayerNode parent_instance;

  ClutterBlur *blur;
  unsigned int radius;
};

G_DEFINE_TYPE (ClutterBlurNode, clutter_blur_node, CLUTTER_TYPE_LAYER_NODE)

static void
clutter_blur_node_post_draw (ClutterPaintNode    *node,
                             ClutterPaintContext *paint_context)
{
  ClutterPaintNodeClass *parent_class =
    CLUTTER_PAINT_NODE_CLASS (clutter_blur_node_parent_class);
  ClutterBlurNode *blur_node = CLUTTER_BLUR_NODE (node);

  clutter_blur_apply (blur_node->blur);

  parent_class->post_draw (node, paint_context);
}

static void
clutter_blur_node_finalize (ClutterPaintNode *node)
{
  ClutterBlurNode *blur_node = CLUTTER_BLUR_NODE (node);

  g_clear_pointer (&blur_node->blur, clutter_blur_free);

  CLUTTER_PAINT_NODE_CLASS (clutter_blur_node_parent_class)->finalize (node);
}

static void
clutter_blur_node_class_init (ClutterBlurNodeClass *klass)
{
  ClutterPaintNodeClass *node_class;

  node_class = CLUTTER_PAINT_NODE_CLASS (klass);
  node_class->post_draw = clutter_blur_node_post_draw;
  node_class->finalize = clutter_blur_node_finalize;
}

static void
clutter_blur_node_init (ClutterBlurNode *blur_node)
{
}

/**
 * clutter_blur_node_new:
 * @width width of the blur layer
 * @height: height of the blur layer
 * @radius: radius (in pixels) of the blur
 *
 * Creates a new #ClutterBlurNode.
 *
 * Children of this node will be painted inside a separate framebuffer,
 * which will be blurred and painted on the current draw framebuffer.
 *
 * Return value: (transfer full): the newly created #ClutterBlurNode.
 *   Use clutter_paint_node_unref() when done.
 */
ClutterPaintNode *
clutter_blur_node_new (unsigned int width,
                       unsigned int height,
                       float        radius)
{
  g_autoptr (CoglOffscreen) offscreen = NULL;
  g_autoptr (GError) error = NULL;
  ClutterLayerNode *layer_node;
  ClutterBlurNode *blur_node;
  CoglContext *context;
  CoglTexture *texture;
  ClutterBlur *blur;

  g_return_val_if_fail (radius >= 0.0, NULL);

  blur_node = _clutter_paint_node_create (CLUTTER_TYPE_BLUR_NODE);
  blur_node->radius = radius;
  context = clutter_backend_get_cogl_context (clutter_get_default_backend ());
  texture = cogl_texture_2d_new_with_size (context, width, height);

  cogl_texture_set_premultiplied (texture, TRUE);

  offscreen = cogl_offscreen_new_with_texture (texture);
  g_object_unref (texture);
  if (!cogl_framebuffer_allocate (COGL_FRAMEBUFFER (offscreen), &error))
    {
      g_warning ("Unable to allocate paint node offscreen: %s",
                 error->message);
      goto out;
    }

  blur = clutter_blur_new (texture, radius);
  blur_node->blur = blur;

  if (!blur)
    {
      g_warning ("Failed to create blur pipeline");
      goto out;
    }

  layer_node = CLUTTER_LAYER_NODE (blur_node);
  layer_node->offscreen = COGL_FRAMEBUFFER (g_steal_pointer (&offscreen));
  layer_node->pipeline = cogl_pipeline_copy (default_texture_pipeline);
  cogl_pipeline_set_layer_filters (layer_node->pipeline, 0,
                                   COGL_PIPELINE_FILTER_LINEAR,
                                   COGL_PIPELINE_FILTER_LINEAR);
  cogl_pipeline_set_layer_texture (layer_node->pipeline,
                                   0,
                                   clutter_blur_get_texture (blur));

  cogl_framebuffer_orthographic (layer_node->offscreen,
                                 0.0, 0.0,
                                 width, height,
                                 0.0, 1.0);

out:
  return (ClutterPaintNode *) blur_node;
}
