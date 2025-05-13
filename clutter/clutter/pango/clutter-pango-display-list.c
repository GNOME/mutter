/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2009 Intel Corporation.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "config.h"

#include <glib.h>
#include <string.h>

#include "clutter/pango/clutter-pango-display-list.h"
#include "clutter/pango/clutter-pango-pipeline-cache.h"
#include "cogl/cogl.h"

typedef enum
{
  PANGO_DISPLAY_LIST_TEXTURE,
  PANGO_DISPLAY_LIST_RECTANGLE,
  PANGO_DISPLAY_LIST_TRAPEZOID
} PangoDisplayListNodeType;

struct _ClutterPangoDisplayList
{
  gboolean color_override;
  CoglColor color;
  GSList                 *nodes;
  GSList                 *last_node;
  ClutterPangoPipelineCache *pipeline_cache;
};

/* This matches the format expected by cogl_rectangles_with_texture_coords */
typedef struct _PangoDisplayListRectangle
{
  float x_1, y_1, x_2, y_2;
  float s_1, t_1, s_2, t_2;
} PangoDisplayListRectangle;

typedef struct _PangoDisplayListNode
{
  PangoDisplayListNodeType type;

  gboolean color_override;
  CoglColor color;

  CoglPipeline *pipeline;

  union
  {
    struct
    {
      /* The texture to render these coords from */
      CoglTexture *texture;
      /* Array of rectangles in the format expected by
         cogl_rectangles_with_texture_coords */
      GArray *rectangles;
      /* A primitive representing those vertices */
      CoglPrimitive *primitive;
      guint has_color : 1;
    } texture;

    struct
    {
      float x_1, y_1;
      float x_2, y_2;
    } rectangle;

    struct
    {
      CoglPrimitive *primitive;
    } trapezoid;
  } d;
} PangoDisplayListNode;

ClutterPangoDisplayList *
clutter_pango_display_list_new (ClutterPangoPipelineCache *pipeline_cache)
{
  ClutterPangoDisplayList *dl = g_new0 (ClutterPangoDisplayList, 1);

  dl->pipeline_cache = pipeline_cache;

  return dl;
}

static void
clutter_pango_display_list_append_node (ClutterPangoDisplayList *dl,
                                        PangoDisplayListNode    *node)
{
  if (dl->last_node)
    dl->last_node = dl->last_node->next = g_slist_prepend (NULL, node);
  else
    dl->last_node = dl->nodes = g_slist_prepend (NULL, node);
}

void
clutter_pango_display_list_set_color_override (ClutterPangoDisplayList *dl,
                                               const CoglColor         *color)
{
  dl->color_override = TRUE;
  dl->color = *color;
}

void
clutter_pango_display_list_remove_color_override (ClutterPangoDisplayList *dl)
{
  dl->color_override = FALSE;
}

void
clutter_pango_display_list_add_texture (ClutterPangoDisplayList *dl,
                                        CoglTexture             *texture,
                                        float                    x_1,
                                        float                    y_1,
                                        float                    x_2,
                                        float                    y_2,
                                        float                    tx_1,
                                        float                    ty_1,
                                        float                    tx_2,
                                        float                    ty_2)
{
  PangoDisplayListNode *node;
  PangoDisplayListRectangle *rectangle;

  /* Add to the last node if it is a texture node with the same
     target texture */
  if (dl->last_node
      && (node = dl->last_node->data)->type == PANGO_DISPLAY_LIST_TEXTURE
      && node->d.texture.texture == texture
      && (dl->color_override
          ? (node->color_override && cogl_color_equal (&dl->color, &node->color))
          : !node->color_override))
    {
      /* Get rid of the vertex buffer so that it will be recreated */
      g_clear_object (&node->d.texture.primitive);
    }
  else
    {
      /* Otherwise create a new node */
      node = g_new0 (PangoDisplayListNode, 1);

      node->type = PANGO_DISPLAY_LIST_TEXTURE;
      node->color_override = dl->color_override;
      node->color = dl->color;
      node->pipeline = NULL;
      node->d.texture.texture = g_object_ref (texture);
      node->d.texture.rectangles
        = g_array_new (FALSE, FALSE, sizeof (PangoDisplayListRectangle));
      node->d.texture.primitive = NULL;

      clutter_pango_display_list_append_node (dl, node);
    }

  g_array_set_size (node->d.texture.rectangles,
                    node->d.texture.rectangles->len + 1);
  rectangle = &g_array_index (node->d.texture.rectangles,
                              PangoDisplayListRectangle,
                              node->d.texture.rectangles->len - 1);
  rectangle->x_1 = x_1;
  rectangle->y_1 = y_1;
  rectangle->x_2 = x_2;
  rectangle->y_2 = y_2;
  rectangle->s_1 = tx_1;
  rectangle->t_1 = ty_1;
  rectangle->s_2 = tx_2;
  rectangle->t_2 = ty_2;
}

void
clutter_pango_display_list_add_rectangle (ClutterPangoDisplayList *dl,
                                          float                    x_1,
                                          float                    y_1,
                                          float                    x_2,
                                          float                    y_2)
{
  PangoDisplayListNode *node = g_new0 (PangoDisplayListNode, 1);

  node->type = PANGO_DISPLAY_LIST_RECTANGLE;
  node->color_override = dl->color_override;
  node->color = dl->color;
  node->d.rectangle.x_1 = x_1;
  node->d.rectangle.y_1 = y_1;
  node->d.rectangle.x_2 = x_2;
  node->d.rectangle.y_2 = y_2;
  node->pipeline = NULL;

  clutter_pango_display_list_append_node (dl, node);
}

void
clutter_pango_display_list_add_trapezoid (ClutterPangoDisplayList *dl,
                                          float                    y_1,
                                          float                    x_11,
                                          float                    x_21,
                                          float                    y_2,
                                          float                    x_12,
                                          float                    x_22)
{
  CoglContext *ctx = dl->pipeline_cache->ctx;
  PangoDisplayListNode *node = g_new0 (PangoDisplayListNode, 1);
  CoglVertexP2 vertices[4] = {
        { x_11, y_1 },
        { x_12, y_2 },
        { x_22, y_2 },
        { x_21, y_1 }
  };

  node->type = PANGO_DISPLAY_LIST_TRAPEZOID;
  node->color_override = dl->color_override;
  node->color = dl->color;
  node->pipeline = NULL;

  node->d.trapezoid.primitive =
    cogl_primitive_new_p2 (ctx,
                           COGL_VERTICES_MODE_TRIANGLE_FAN,
                           4,
                           vertices);

  clutter_pango_display_list_append_node (dl, node);
}

static void
emit_rectangles_through_journal (CoglFramebuffer *fb,
                                 CoglPipeline *pipeline,
                                 PangoDisplayListNode *node)
{
  const float *rectangles = (const float *)node->d.texture.rectangles->data;

  cogl_framebuffer_draw_textured_rectangles (fb,
                                             pipeline,
                                             rectangles,
                                             node->d.texture.rectangles->len);
}

static void
emit_vertex_buffer_geometry (CoglFramebuffer *fb,
                             CoglPipeline *pipeline,
                             PangoDisplayListNode *node)
{
  CoglContext *ctx = cogl_framebuffer_get_context (fb);

  /* It's expensive to go through the Cogl journal for large runs
   * of text in part because the journal transforms the quads in software
   * to avoid changing the modelview matrix. So for larger runs of text
   * we load the vertices into a VBO, and this has the added advantage
   * that if the text doesn't change from frame to frame the VBO can
   * be re-used avoiding the repeated cost of validating the data and
   * mapping it into the GPU... */

  if (node->d.texture.primitive == NULL)
    {
      CoglAttributeBuffer *buffer;
      CoglVertexP2T2 *verts, *v;
      int n_verts;
      gboolean allocated = FALSE;
      CoglAttribute *attributes[2];
      CoglPrimitive *prim;
      CoglIndices *indices;
      int i;

      n_verts = node->d.texture.rectangles->len * 4;

      buffer
        = cogl_attribute_buffer_new_with_size (ctx,
                                               n_verts *
                                               sizeof (CoglVertexP2T2));

      if ((verts = cogl_buffer_map (COGL_BUFFER (buffer),
                                    COGL_BUFFER_ACCESS_WRITE,
                                    COGL_BUFFER_MAP_HINT_DISCARD)) == NULL)
        {
          verts = g_new (CoglVertexP2T2, n_verts);
          allocated = TRUE;
        }

      v = verts;

      /* Copy the rectangles into the buffer and expand into four
         vertices instead of just two */
      for (i = 0; i < node->d.texture.rectangles->len; i++)
        {
          const PangoDisplayListRectangle *rectangle
            = &g_array_index (node->d.texture.rectangles,
                              PangoDisplayListRectangle, i);

          v->x = rectangle->x_1;
          v->y = rectangle->y_1;
          v->s = rectangle->s_1;
          v->t = rectangle->t_1;
          v++;
          v->x = rectangle->x_1;
          v->y = rectangle->y_2;
          v->s = rectangle->s_1;
          v->t = rectangle->t_2;
          v++;
          v->x = rectangle->x_2;
          v->y = rectangle->y_2;
          v->s = rectangle->s_2;
          v->t = rectangle->t_2;
          v++;
          v->x = rectangle->x_2;
          v->y = rectangle->y_1;
          v->s = rectangle->s_2;
          v->t = rectangle->t_1;
          v++;
        }

      if (allocated)
        {
          cogl_buffer_set_data (COGL_BUFFER (buffer),
                                0, /* offset */
                                verts,
                                sizeof (CoglVertexP2T2) * n_verts);
          g_free (verts);
        }
      else
        cogl_buffer_unmap (COGL_BUFFER (buffer));

      attributes[0] = cogl_attribute_new (buffer,
                                          "cogl_position_in",
                                          sizeof (CoglVertexP2T2),
                                          G_STRUCT_OFFSET (CoglVertexP2T2, x),
                                          2, /* n_components */
                                          COGL_ATTRIBUTE_TYPE_FLOAT);
      attributes[1] = cogl_attribute_new (buffer,
                                          "cogl_tex_coord0_in",
                                          sizeof (CoglVertexP2T2),
                                          G_STRUCT_OFFSET (CoglVertexP2T2, s),
                                          2, /* n_components */
                                          COGL_ATTRIBUTE_TYPE_FLOAT);

      prim = cogl_primitive_new_with_attributes (COGL_VERTICES_MODE_TRIANGLES,
                                                 n_verts,
                                                 attributes,
                                                 2 /* n_attributes */);

      indices =
        cogl_context_get_rectangle_indices (ctx, node->d.texture.rectangles->len);

      cogl_primitive_set_indices (prim, indices,
                                  node->d.texture.rectangles->len * 6);

      node->d.texture.primitive = prim;

      g_object_unref (buffer);
      g_object_unref (attributes[0]);
      g_object_unref (attributes[1]);
    }

  cogl_primitive_draw (node->d.texture.primitive,
                       fb,
                       pipeline);
}

static void
_cogl_framebuffer_draw_display_list_texture (CoglFramebuffer *fb,
                                             CoglPipeline *pipeline,
                                             PangoDisplayListNode *node)
{
  /* For small runs of text like icon labels, we can get better performance
   * going through the Cogl journal since text may then be batched together
   * with other geometry. */
  /* FIXME: 25 is a number I plucked out of thin air; it would be good
   * to determine this empirically! */
  if (node->d.texture.rectangles->len < 25)
    emit_rectangles_through_journal (fb, pipeline, node);
  else
    emit_vertex_buffer_geometry (fb, pipeline, node);
}

void
clutter_pango_display_list_render (CoglFramebuffer          *fb,
                                   ClutterPangoDisplayList  *dl,
                                   ClutterColorState        *color_state,
                                   ClutterColorState        *target_color_state,
                                   const CoglColor          *color)
{
  GSList *l;

  for (l = dl->nodes; l; l = l->next)
    {
      PangoDisplayListNode *node = l->data;
      CoglColor draw_color;
      g_autoptr (CoglPipeline) pipeline = NULL;

      if (node->pipeline == NULL)
        {
          if (node->type == PANGO_DISPLAY_LIST_TEXTURE)
            node->pipeline =
              clutter_pango_pipeline_cache_get (dl->pipeline_cache,
                                                node->d.texture.texture);
          else
            node->pipeline =
              clutter_pango_pipeline_cache_get (dl->pipeline_cache,
                                                NULL);
        }

      pipeline = cogl_pipeline_copy (node->pipeline);

      if (node->color_override)
        /* Use the override color but preserve the alpha from the
           draw color */
        cogl_color_init_from_4f (&draw_color,
                                 cogl_color_get_red (&node->color),
                                 cogl_color_get_green (&node->color),
                                 cogl_color_get_blue (&node->color),
                                 (cogl_color_get_alpha (&node->color) *
                                  cogl_color_get_alpha (color)));
      else
        draw_color = *color;
      cogl_color_premultiply (&draw_color);

      cogl_pipeline_set_color (pipeline, &draw_color);

      clutter_color_state_add_pipeline_transform (color_state,
                                                  target_color_state,
                                                  pipeline,
                                                  0);

      switch (node->type)
        {
        case PANGO_DISPLAY_LIST_TEXTURE:
          _cogl_framebuffer_draw_display_list_texture (fb, pipeline, node);
          break;

        case PANGO_DISPLAY_LIST_RECTANGLE:
          cogl_framebuffer_draw_rectangle (fb,
                                           pipeline,
                                           node->d.rectangle.x_1,
                                           node->d.rectangle.y_1,
                                           node->d.rectangle.x_2,
                                           node->d.rectangle.y_2);
          break;

        case PANGO_DISPLAY_LIST_TRAPEZOID:
          cogl_primitive_draw (node->d.trapezoid.primitive,
                               fb,
                               pipeline);
          break;
        }
    }
}

static void
clutter_pango_display_list_node_free (PangoDisplayListNode *node)
{
  if (node->type == PANGO_DISPLAY_LIST_TEXTURE)
    {
      g_array_free (node->d.texture.rectangles, TRUE);
      g_clear_object (&node->d.texture.texture);
      g_clear_object (&node->d.texture.primitive);
    }
  else if (node->type == PANGO_DISPLAY_LIST_TRAPEZOID)
    g_clear_object (&node->d.trapezoid.primitive);

  g_clear_object (&node->pipeline);

  g_free (node);
}

void
clutter_pango_display_list_free (ClutterPangoDisplayList *dl)
{
  g_slist_free_full (dl->nodes, (GDestroyNotify)
                     clutter_pango_display_list_node_free);
  dl->nodes = NULL;
  dl->last_node = NULL;
  g_free (dl);
}
