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

/**
 * ClutterPaintNode:(ref-func clutter_paint_node_ref) (unref-func clutter_paint_node_unref) (set-value-func clutter_value_set_paint_node) (get-value-func clutter_value_get_paint_node)
 * 
 * Paint objects
 *
 * #ClutterPaintNode is an element in the render graph.
 *
 * The render graph contains all the elements that need to be painted by
 * Clutter when submitting a frame to the graphics system.
 *
 * The render graph is distinct from the scene graph: the scene graph is
 * composed by actors, which can be visible or invisible; the scene graph
 * elements also respond to events. The render graph, instead, is only
 * composed by nodes that will be painted.
 *
 * Each #ClutterActor can submit multiple `ClutterPaintNode`s to
 * the render graph.
 */

/**
 * ClutterPaintNodeClass:
 *
 * The `ClutterPaintNodeClass` structure contains only private data.
 */

#include "config.h"

#include <gobject/gvaluecollector.h>

#include "cogl/cogl.h"
#include "clutter/clutter-paint-node-private.h"
#include "clutter/clutter-debug.h"
#include "clutter/clutter-private.h"


static inline void      clutter_paint_operation_clear   (ClutterPaintOperation *op);

static void clutter_paint_node_remove_child (ClutterPaintNode *node,
                                             ClutterPaintNode *child);

static void
value_paint_node_init (GValue *value)
{
  value->data[0].v_pointer = NULL;
}

static void
value_paint_node_free_value (GValue *value)
{
  if (value->data[0].v_pointer != NULL)
    clutter_paint_node_unref (value->data[0].v_pointer);
}

static void
value_paint_node_copy_value (const GValue *src,
                             GValue       *dst)
{
  if (src->data[0].v_pointer != NULL)
    dst->data[0].v_pointer = clutter_paint_node_ref (src->data[0].v_pointer);
  else
    dst->data[0].v_pointer = NULL;
}

static gpointer
value_paint_node_peek_pointer (const GValue *value)
{
  return value->data[0].v_pointer;
}

static gchar *
value_paint_node_collect_value (GValue      *value,
                                guint        n_collect_values,
                                GTypeCValue *collect_values,
                                guint        collect_flags)
{
  ClutterPaintNode *node;

  node = collect_values[0].v_pointer;

  if (node == NULL)
    {
      value->data[0].v_pointer = NULL;
      return NULL;
    }

  if (node->parent_instance.g_class == NULL)
    return g_strconcat ("invalid unclassed ClutterPaintNode pointer for "
                        "value type '",
                        G_VALUE_TYPE_NAME (value),
                        "'",
                        NULL);

  value->data[0].v_pointer = clutter_paint_node_ref (node);

  return NULL;
}

static gchar *
value_paint_node_lcopy_value (const GValue *value,
                              guint         n_collect_values,
                              GTypeCValue  *collect_values,
                              guint         collect_flags)
{
  ClutterPaintNode **node_p = collect_values[0].v_pointer;

  if (node_p == NULL)
    return g_strconcat ("value location for '",
                        G_VALUE_TYPE_NAME (value),
                        "' passed as NULL",
                        NULL);

  if (value->data[0].v_pointer == NULL)
    *node_p = NULL;
  else if (collect_flags & G_VALUE_NOCOPY_CONTENTS)
    *node_p = value->data[0].v_pointer;
  else
    *node_p = clutter_paint_node_ref (value->data[0].v_pointer);

  return NULL;
}

static void
clutter_paint_node_class_base_init (ClutterPaintNodeClass *klass)
{
}

static void
clutter_paint_node_class_base_finalize (ClutterPaintNodeClass *klass)
{
}

static void
clutter_paint_node_real_finalize (ClutterPaintNode *node)
{
  ClutterPaintNode *iter;

  if (node->operations != NULL)
    {
      guint i;

      for (i = 0; i < node->operations->len; i++)
        {
          ClutterPaintOperation *op;

          op = &g_array_index (node->operations, ClutterPaintOperation, i);
          clutter_paint_operation_clear (op);
        }

      g_array_unref (node->operations);
    }

  iter = node->first_child;
  while (iter != NULL)
    {
      ClutterPaintNode *next = iter->next_sibling;

      clutter_paint_node_remove_child (node, iter);

      iter = next;
    }

  g_type_free_instance ((GTypeInstance *) node);
}

static gboolean
clutter_paint_node_real_pre_draw (ClutterPaintNode    *node,
                                  ClutterPaintContext *paint_context)
{
  return FALSE;
}

static void
clutter_paint_node_real_draw (ClutterPaintNode    *node,
                              ClutterPaintContext *paint_context)
{
}

static void
clutter_paint_node_real_post_draw (ClutterPaintNode    *node,
                                   ClutterPaintContext *paint_context)
{
}

static void
clutter_paint_node_class_init (ClutterPaintNodeClass *klass)
{
  klass->pre_draw = clutter_paint_node_real_pre_draw;
  klass->draw = clutter_paint_node_real_draw;
  klass->post_draw = clutter_paint_node_real_post_draw;
  klass->finalize = clutter_paint_node_real_finalize;
}

static void
clutter_paint_node_init (ClutterPaintNode *self)
{
  self->ref_count = 1;
}

GType
clutter_paint_node_get_type (void)
{
  static size_t paint_node_type_id = 0;

  if (g_once_init_enter (&paint_node_type_id))
    {
      static const GTypeFundamentalInfo finfo = {
        (G_TYPE_FLAG_CLASSED |
         G_TYPE_FLAG_INSTANTIATABLE |
         G_TYPE_FLAG_DERIVABLE |
         G_TYPE_FLAG_DEEP_DERIVABLE),
      };

      static const GTypeValueTable value_table = {
        value_paint_node_init,
        value_paint_node_free_value,
        value_paint_node_copy_value,
        value_paint_node_peek_pointer,
        "p",
        value_paint_node_collect_value,
        "p",
        value_paint_node_lcopy_value,
      };

      const GTypeInfo node_info = {
        sizeof (ClutterPaintNodeClass),

        (GBaseInitFunc) clutter_paint_node_class_base_init,
        (GBaseFinalizeFunc) clutter_paint_node_class_base_finalize,
        (GClassInitFunc) clutter_paint_node_class_init,
        (GClassFinalizeFunc) NULL,
        NULL,

        sizeof (ClutterPaintNode),
        0,
        (GInstanceInitFunc) clutter_paint_node_init,

        &value_table,
      };

      GType id =
        g_type_register_fundamental (g_type_fundamental_next (),
                                     I_("ClutterPaintNode"),
                                     &node_info, &finfo,
                                     G_TYPE_FLAG_ABSTRACT);

      g_once_init_leave (&paint_node_type_id, id);
    }

  return paint_node_type_id;
}

/**
 * clutter_paint_node_set_name:
 * @node: a #ClutterPaintNode
 * @name: a string annotating the @node
 *
 * Sets a user-readable @name for @node.
 *
 * The @name will be used for debugging purposes.
 *
 * The @node will intern @name using g_intern_string(). If you have access to a
 * static string, use clutter_paint_node_set_static_name() instead.
 */
void
clutter_paint_node_set_name (ClutterPaintNode *node,
                             const char       *name)
{
  g_return_if_fail (CLUTTER_IS_PAINT_NODE (node));

  node->name = g_intern_string (name);
}

/**
 * clutter_paint_node_set_static_name: (skip)
 *
 * Like clutter_paint_node_set_name() but uses a static or interned string
 * containing the name.
 */
void
clutter_paint_node_set_static_name (ClutterPaintNode *node,
                                    const char       *name)
{
  g_return_if_fail (CLUTTER_IS_PAINT_NODE (node));

  node->name = name;
}

/**
 * clutter_paint_node_ref:
 * @node: a #ClutterPaintNode
 *
 * Acquires a reference on @node.
 *
 * Return value: (transfer full): the #ClutterPaintNode
 */
ClutterPaintNode *
clutter_paint_node_ref (ClutterPaintNode *node)
{
  g_return_val_if_fail (CLUTTER_IS_PAINT_NODE (node), NULL);

  g_atomic_int_inc (&node->ref_count);

  return node;
}

/**
 * clutter_paint_node_unref:
 * @node: a #ClutterPaintNode
 *
 * Releases a reference on @node.
 */
void
clutter_paint_node_unref (ClutterPaintNode *node)
{
  g_return_if_fail (CLUTTER_IS_PAINT_NODE (node));

  if (g_atomic_int_dec_and_test (&node->ref_count))
    {
      ClutterPaintNodeClass *klass = CLUTTER_PAINT_NODE_GET_CLASS (node);

      klass->finalize (node);
    }
}

/**
 * clutter_paint_node_add_child:
 * @node: a #ClutterPaintNode
 * @child: the child #ClutterPaintNode to add
 *
 * Adds @child to the list of children of @node.
 *
 * This function will acquire a reference on @child.
 */
void
clutter_paint_node_add_child (ClutterPaintNode *node,
                              ClutterPaintNode *child)
{
  g_return_if_fail (CLUTTER_IS_PAINT_NODE (node));
  g_return_if_fail (CLUTTER_IS_PAINT_NODE (child));
  g_return_if_fail (node != child);
  g_return_if_fail (child->parent == NULL);

  child->parent = node;
  clutter_paint_node_ref (child);

  node->n_children += 1;

  child->prev_sibling = node->last_child;

  if (node->last_child != NULL)
    {
      ClutterPaintNode *tmp = node->last_child;

      tmp->next_sibling = child;
    }

  if (child->prev_sibling == NULL)
    node->first_child = child;

  if (child->next_sibling == NULL)
    node->last_child = child;
}

/**
 * clutter_paint_node_remove_child:
 * @node: a #ClutterPaintNode
 * @child: the #ClutterPaintNode to remove
 *
 * Removes @child from the list of children of @node.
 *
 * This function will release the reference on @child acquired by
 * using clutter_paint_node_add_child().
 */
static void
clutter_paint_node_remove_child (ClutterPaintNode *node,
                                 ClutterPaintNode *child)
{
  ClutterPaintNode *prev, *next;

  g_return_if_fail (CLUTTER_IS_PAINT_NODE (node));
  g_return_if_fail (CLUTTER_IS_PAINT_NODE (child));
  g_return_if_fail (node != child);
  g_return_if_fail (child->parent == node);

  node->n_children -= 1;

  prev = child->prev_sibling;
  next = child->next_sibling;

  if (prev != NULL)
    prev->next_sibling = next;

  if (next != NULL)
    next->prev_sibling = prev;

  if (node->first_child == child)
    node->first_child = next;

  if (node->last_child == child)
    node->last_child = prev;

  child->prev_sibling = NULL;
  child->next_sibling = NULL;
  child->parent = NULL;

  clutter_paint_node_unref (child);
}


/**
 * clutter_paint_node_get_n_children:
 * @node: a #ClutterPaintNode
 *
 * Retrieves the number of children of @node.
 *
 * Return value: the number of children of a #ClutterPaintNode
 */
guint
clutter_paint_node_get_n_children (ClutterPaintNode *node)
{
  g_return_val_if_fail (CLUTTER_IS_PAINT_NODE (node), 0);

  return node->n_children;
}

/**
 * clutter_value_set_paint_node:
 * @value: a #GValue initialized with %CLUTTER_TYPE_PAINT_NODE
 * @node: (type Clutter.PaintNode) (allow-none): a #ClutterPaintNode, or %NULL
 *
 * Sets the contents of a #GValue initialized with %CLUTTER_TYPE_PAINT_NODE.
 *
 * This function increased the reference count of @node; if you do not wish
 * to increase the reference count, use clutter_value_take_paint_node()
 * instead. The reference count will be released by g_value_unset().
 */
void
clutter_value_set_paint_node (GValue   *value,
                              gpointer  node)
{
  ClutterPaintNode *old_node;

  g_return_if_fail (CLUTTER_VALUE_HOLDS_PAINT_NODE (value));

  old_node = value->data[0].v_pointer;

  if (node != NULL)
    {
      g_return_if_fail (CLUTTER_IS_PAINT_NODE (node));

      value->data[0].v_pointer = clutter_paint_node_ref (node);
    }
  else
    value->data[0].v_pointer = NULL;

  if (old_node != NULL)
    clutter_paint_node_unref (old_node);
}

/**
 * clutter_value_take_paint_node:
 * @value: a #GValue, initialized with %CLUTTER_TYPE_PAINT_NODE
 * @node: (type Clutter.PaintNode) (allow-none): a #ClutterPaintNode, or %NULL
 *
 * Sets the contents of a #GValue initialized with %CLUTTER_TYPE_PAINT_NODE.
 *
 * Unlike clutter_value_set_paint_node(), this function will not take a
 * reference on the passed @node: instead, it will take ownership of the
 * current reference count.
 */
void
clutter_value_take_paint_node (GValue   *value,
                               gpointer  node)
{
  ClutterPaintNode *old_node;

  g_return_if_fail (CLUTTER_VALUE_HOLDS_PAINT_NODE (value));

  old_node = value->data[0].v_pointer;

  if (node != NULL)
    {
      g_return_if_fail (CLUTTER_IS_PAINT_NODE (node));

      /* take over ownership */
      value->data[0].v_pointer = node;
    }
  else
    value->data[0].v_pointer = NULL;

  if (old_node != NULL)
    clutter_paint_node_unref (old_node);
}

/**
 * clutter_value_get_paint_node:
 * @value: a #GValue initialized with %CLUTTER_TYPE_PAINT_NODE
 *
 * Retrieves a pointer to the #ClutterPaintNode contained inside
 * the passed #GValue.
 *
 * Return value: (transfer none) (type Clutter.PaintNode): a pointer to
 *   a #ClutterPaintNode, or %NULL
 */
gpointer
clutter_value_get_paint_node (const GValue *value)
{
  g_return_val_if_fail (CLUTTER_VALUE_HOLDS_PAINT_NODE (value), NULL);

  return value->data[0].v_pointer;
}

/**
 * clutter_value_dup_paint_node:
 * @value: a #GValue initialized with %CLUTTER_TYPE_PAINT_NODE
 *
 * Retrieves a pointer to the #ClutterPaintNode contained inside
 * the passed #GValue, and if not %NULL it will increase the
 * reference count.
 *
 * Return value: (transfer full) (type Clutter.PaintNode): a pointer
 *   to the #ClutterPaintNode, with its reference count increased,
 *   or %NULL
 */
gpointer
clutter_value_dup_paint_node (const GValue *value)
{
  g_return_val_if_fail (CLUTTER_VALUE_HOLDS_PAINT_NODE (value), NULL);

  if (value->data[0].v_pointer != NULL)
    return clutter_paint_node_ref (value->data[0].v_pointer);

  return NULL;
}

static inline void
clutter_paint_operation_clear (ClutterPaintOperation *op)
{
  switch (op->opcode)
    {
    case PAINT_OP_INVALID:
      break;

    case PAINT_OP_TEX_RECT:
      break;

    case PAINT_OP_TEX_RECTS:
    case PAINT_OP_MULTITEX_RECT:
      g_clear_pointer (&op->coords, g_array_unref);
      break;

    case PAINT_OP_PRIMITIVE:
      if (op->op.primitive != NULL)
        g_object_unref (op->op.primitive);
      break;
    }
}

static inline void
clutter_paint_op_init_tex_rect (ClutterPaintOperation *op,
                                const ClutterActorBox *rect,
                                float                  x_1,
                                float                  y_1,
                                float                  x_2,
                                float                  y_2)
{
  clutter_paint_operation_clear (op);

  op->opcode = PAINT_OP_TEX_RECT;
  op->op.texrect[0] = rect->x1;
  op->op.texrect[1] = rect->y1;
  op->op.texrect[2] = rect->x2;
  op->op.texrect[3] = rect->y2;
  op->op.texrect[4] = x_1;
  op->op.texrect[5] = y_1;
  op->op.texrect[6] = x_2;
  op->op.texrect[7] = y_2;
}

static inline void
clutter_paint_op_init_tex_rects (ClutterPaintOperation *op,
                                 const float           *coords,
                                 unsigned int           n_rects,
                                 gboolean               use_default_tex_coords)
{
  const unsigned int n_floats = n_rects * 8;

  clutter_paint_operation_clear (op);

  op->opcode = PAINT_OP_TEX_RECTS;
  op->coords = g_array_sized_new (FALSE, FALSE, sizeof (float), n_floats);

  if (use_default_tex_coords)
    {
      const float default_tex_coords[4] = { 0.0, 0.0, 1.0, 1.0 };
      int i;

      for (i = 0; i < n_rects; i++)
        {
          g_array_append_vals (op->coords, &coords[i * 4], 4);
          g_array_append_vals (op->coords, default_tex_coords, 4);
        }
    }
  else
    {
      g_array_append_vals (op->coords, coords, n_floats);
    }
}

static inline void
clutter_paint_op_init_multitex_rect (ClutterPaintOperation *op,
                                     const ClutterActorBox *rect,
                                     const float           *tex_coords,
                                     unsigned int           tex_coords_len)
{
  clutter_paint_operation_clear (op);

  op->opcode = PAINT_OP_MULTITEX_RECT;
  op->coords = g_array_sized_new (FALSE, FALSE,
                                  sizeof (float),
                                  tex_coords_len);

  g_array_append_vals (op->coords, tex_coords, tex_coords_len);

  op->op.texrect[0] = rect->x1;
  op->op.texrect[1] = rect->y1;
  op->op.texrect[2] = rect->x2;
  op->op.texrect[3] = rect->y2;
}

static inline void
clutter_paint_op_init_primitive (ClutterPaintOperation *op,
                                 CoglPrimitive         *primitive)
{
  clutter_paint_operation_clear (op);

  op->opcode = PAINT_OP_PRIMITIVE;
  op->op.primitive = g_object_ref (primitive);
}

static inline void
clutter_paint_node_maybe_init_operations (ClutterPaintNode *node)
{
  if (node->operations != NULL)
    return;

  node->operations =
    g_array_new (FALSE, FALSE, sizeof (ClutterPaintOperation));
}

/**
 * clutter_paint_node_add_rectangle:
 * @node: a #ClutterPaintNode
 * @rect: a #ClutterActorBox
 *
 * Adds a rectangle region to the @node, as described by the
 * passed @rect.
 */
void
clutter_paint_node_add_rectangle (ClutterPaintNode      *node,
                                  const ClutterActorBox *rect)
{
  ClutterPaintOperation operation = PAINT_OP_INIT;

  g_return_if_fail (CLUTTER_IS_PAINT_NODE (node));
  g_return_if_fail (rect != NULL);

  clutter_paint_node_maybe_init_operations (node);

  clutter_paint_op_init_tex_rect (&operation, rect, 0.0, 0.0, 1.0, 1.0);
  g_array_append_val (node->operations, operation);
}

/**
 * clutter_paint_node_add_texture_rectangle:
 * @node: a #ClutterPaintNode
 * @rect: a #ClutterActorBox
 * @x_1: the left X coordinate of the texture
 * @y_1: the top Y coordinate of the texture
 * @x_2: the right X coordinate of the texture
 * @y_2: the bottom Y coordinate of the texture
 *
 * Adds a rectangle region to the @node, with texture coordinates.
 */
void
clutter_paint_node_add_texture_rectangle (ClutterPaintNode      *node,
                                          const ClutterActorBox *rect,
                                          float                  x_1,
                                          float                  y_1,
                                          float                  x_2,
                                          float                  y_2)
{
  ClutterPaintOperation operation = PAINT_OP_INIT;

  g_return_if_fail (CLUTTER_IS_PAINT_NODE (node));
  g_return_if_fail (rect != NULL);

  clutter_paint_node_maybe_init_operations (node);

  clutter_paint_op_init_tex_rect (&operation, rect, x_1, y_1, x_2, y_2);
  g_array_append_val (node->operations, operation);
}


/**
 * clutter_paint_node_add_multitexture_rectangle:
 * @node: a #ClutterPaintNode
 * @rect: a #ClutterActorBox
 * @text_coords: array of multitexture values
 * @text_coords_len: number of items of @text_coords
 *
 * Adds a rectangle region to the @node, with multitexture coordinates.
 */
void
clutter_paint_node_add_multitexture_rectangle (ClutterPaintNode      *node,
                                               const ClutterActorBox *rect,
                                               const float           *text_coords,
                                               unsigned int           text_coords_len)
{
  ClutterPaintOperation operation = PAINT_OP_INIT;

  g_return_if_fail (CLUTTER_IS_PAINT_NODE (node));
  g_return_if_fail (rect != NULL);

  clutter_paint_node_maybe_init_operations (node);

  clutter_paint_op_init_multitex_rect (&operation, rect, text_coords, text_coords_len);
  g_array_append_val (node->operations, operation);
}

/**
 * clutter_paint_node_add_rectangles:
 * @node: a #ClutterPaintNode
 * @coords: (in) (array length=n_rects) (transfer none): array of
 *   coordinates containing groups of 4 float values: [x_1, y_1, x_2, y_2] that
 *   are interpreted as two position coordinates; one for the top left of the
 *   rectangle (x1, y1), and one for the bottom right of the rectangle
 *   (x2, y2).
 * @n_rects: number of rectangles defined in @coords.
 *
 * Adds a series of rectangles to @node.
 *
 * As a general rule for better performance its recommended to use this API
 * instead of calling clutter_paint_node_add_rectangle() separately for
 * multiple rectangles if all of the rectangles will be drawn together.
 *
 * See cogl_framebuffer_draw_rectangles().
 */
void
clutter_paint_node_add_rectangles (ClutterPaintNode *node,
                                   const float      *coords,
                                   unsigned int      n_rects)
{
  ClutterPaintOperation operation = PAINT_OP_INIT;

  g_return_if_fail (CLUTTER_IS_PAINT_NODE (node));
  g_return_if_fail (coords != NULL);

  clutter_paint_node_maybe_init_operations (node);

  clutter_paint_op_init_tex_rects (&operation, coords, n_rects, TRUE);
  g_array_append_val (node->operations, operation);
}

/**
 * clutter_paint_node_add_texture_rectangles:
 * @node: a #ClutterPaintNode
 * @coords: (in) (array length=n_rects) (transfer none): array containing
 *   groups of 8 float values: [x_1, y_1, x_2, y_2, s_1, t_1, s_2, t_2]
 *   that have the same meaning as the arguments for
 *   cogl_framebuffer_draw_textured_rectangle().
 * @n_rects: number of rectangles defined in @coords.
 *
 * Adds a series of rectangles to @node.
 *
 * The given texture coordinates should always be normalized such that
 * (0, 0) corresponds to the top left and (1, 1) corresponds to the
 * bottom right. To map an entire texture across the rectangle pass
 * in s_1=0, t_1=0, s_2=1, t_2=1.
 *
 * See cogl_framebuffer_draw_textured_rectangles().
 */
void
clutter_paint_node_add_texture_rectangles (ClutterPaintNode *node,
                                           const float      *coords,
                                           unsigned int      n_rects)
{
  ClutterPaintOperation operation = PAINT_OP_INIT;

  g_return_if_fail (CLUTTER_IS_PAINT_NODE (node));
  g_return_if_fail (coords != NULL);

  clutter_paint_node_maybe_init_operations (node);

  clutter_paint_op_init_tex_rects (&operation, coords, n_rects, FALSE);
  g_array_append_val (node->operations, operation);
}

/**
 * clutter_paint_node_add_primitive: (skip)
 * @node: a #ClutterPaintNode
 * @primitive: a Cogl primitive
 *
 * Adds a region described by a Cogl primitive to the @node.
 *
 * This function acquires a reference on @primitive, so it is safe
 * to call g_object_unref() when it returns.
 */
void
clutter_paint_node_add_primitive (ClutterPaintNode *node,
                                  CoglPrimitive    *primitive)
{
  ClutterPaintOperation operation = PAINT_OP_INIT;

  g_return_if_fail (CLUTTER_IS_PAINT_NODE (node));
  g_return_if_fail (COGL_IS_PRIMITIVE (primitive));

  clutter_paint_node_maybe_init_operations (node);

  clutter_paint_op_init_primitive (&operation, primitive);
  g_array_append_val (node->operations, operation);
}

/**
 * clutter_paint_node_paint:
 * @node: a #ClutterPaintNode
 *
 * Paints the @node using the class implementation, traversing
 * its children, if any.
 */
void
clutter_paint_node_paint (ClutterPaintNode    *node,
                          ClutterPaintContext *paint_context)
{
  ClutterPaintNodeClass *klass = CLUTTER_PAINT_NODE_GET_CLASS (node);
  ClutterPaintNode *iter;
  gboolean res;

  res = klass->pre_draw (node, paint_context);

  if (res)
    {
      klass->draw (node, paint_context);
    }

  for (iter = node->first_child;
       iter != NULL;
       iter = iter->next_sibling)
    {
      clutter_paint_node_paint (iter, paint_context);
    }

  if (res)
    {
      klass->post_draw (node, paint_context);
    }
}

/*< private >
 * _clutter_paint_node_create:
 * @gtype: a #ClutterPaintNode type
 *
 * Creates a new #ClutterPaintNode instance using @gtype
 *
 * Return value: (transfer full): the newly created #ClutterPaintNode
 *   sub-class instance; use clutter_paint_node_unref() when done
 */
gpointer
_clutter_paint_node_create (GType gtype)
{
  g_return_val_if_fail (g_type_is_a (gtype, CLUTTER_TYPE_PAINT_NODE), NULL);

  return (gpointer) g_type_create_instance (gtype);
}

/**
 * clutter_paint_node_get_framebuffer:
 * @node: a #ClutterPaintNode
 *
 * Retrieves the #CoglFramebuffer that @node will draw
 * into. If @node doesn't specify a custom framebuffer,
 * the first ancestor with a custom framebuffer will be
 * used.
 *
 * Returns: (transfer none): a #CoglFramebuffer or %NULL if no custom one is
 * set.
 */
CoglFramebuffer *
clutter_paint_node_get_framebuffer (ClutterPaintNode *node)
{
  ClutterPaintNodeClass *klass;

  while (node)
    {
      klass = CLUTTER_PAINT_NODE_GET_CLASS (node);

      if (klass->get_framebuffer != NULL)
        return klass->get_framebuffer (node);

      node = node->parent;
    }

  return NULL;
}
