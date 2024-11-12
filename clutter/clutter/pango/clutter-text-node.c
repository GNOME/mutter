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

#include "clutter/pango/clutter-text-node.h"

#include "clutter/pango/clutter-pango-private.h"
#include "clutter/clutter-paint-node-private.h"
#include "clutter/clutter-private.h"

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
clutter_text_node_draw (ClutterPaintNode    *node,
                        ClutterPaintContext *paint_context)
{
  ClutterTextNode *tnode = CLUTTER_TEXT_NODE (node);
  ClutterColorState *color_state =
    clutter_paint_context_get_color_state (paint_context);
  ClutterColorState *target_color_state =
    clutter_paint_context_get_target_color_state (paint_context);
  ClutterContext *context = _clutter_context_get_default ();
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

          clutter_show_layout (context,
                               fb,
                               tnode->layout,
                               op->op.texrect[0],
                               op->op.texrect[1],
                               &tnode->color,
                               color_state,
                               target_color_state);

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
clutter_text_node_new (PangoLayout     *layout,
                       const CoglColor *color)
{
  ClutterTextNode *res;

  g_return_val_if_fail (layout == NULL || PANGO_IS_LAYOUT (layout), NULL);

  res = _clutter_paint_node_create (CLUTTER_TYPE_TEXT_NODE);

  if (layout != NULL)
    res->layout = g_object_ref (layout);

  if (color != NULL)
    res->color = *color;

  return (ClutterPaintNode *) res;
}
