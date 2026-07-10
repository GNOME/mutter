/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2026 Red Hat, Inc.
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

#include "config.h"

#include "clutter/clutter-color-op.h"
#include "clutter/clutter-color-pipeline.h"

#include <float.h>

typedef struct _ClutterColorPipeline
{
  GObject parent;

  GList *ops; /* ClutterColorOp */
} ClutterColorPipeline;

G_DEFINE_TYPE (ClutterColorPipeline,
               clutter_color_pipeline,
               G_TYPE_OBJECT)

static void
clutter_color_pipeline_dispose (GObject *gobject)
{
  ClutterColorPipeline *color_pipeline = CLUTTER_COLOR_PIPELINE (gobject);

  g_list_free_full (color_pipeline->ops, g_object_unref);
  color_pipeline->ops = NULL;

  G_OBJECT_CLASS (clutter_color_pipeline_parent_class)->dispose (gobject);
}

static void
clutter_color_pipeline_class_init (ClutterColorPipelineClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = clutter_color_pipeline_dispose;
}

static void
clutter_color_pipeline_init (ClutterColorPipeline *color_pipeline)
{
}

char *
clutter_color_pipeline_to_string (ClutterColorPipeline *color_pipeline)
{
  g_autoptr (GString) str = NULL;

  str = g_string_new ("ClutterColorPipeline: ");

  if (!color_pipeline->ops)
    g_string_append (str, "[empty]");

  for (GList *l = color_pipeline->ops; l != NULL; l = l->next)
    {
      ClutterColorOp *op = l->data;
      g_autofree char *op_str = NULL;

      op_str = clutter_color_op_to_string (op);
      g_string_append_printf (str, "%s%s", op_str, l->next != NULL ? " -> " : "");
    }

  return g_string_free_and_steal (g_steal_pointer (&str));
}

void
clutter_color_pipeline_add_op (ClutterColorPipeline *color_pipeline,
                               ClutterColorOp       *op)
{
  color_pipeline->ops = g_list_append (color_pipeline->ops, g_object_ref (op));
}

void
clutter_color_pipeline_take_op (ClutterColorPipeline *color_pipeline,
                                ClutterColorOp       *op)
{
  color_pipeline->ops = g_list_append (color_pipeline->ops, op);
}

void
clutter_color_pipeline_append (ClutterColorPipeline *color_pipeline,
                               ClutterColorPipeline *other)
{
  for (GList *l = other->ops; l != NULL; l = l->next)
    {
      ClutterColorOp *op = l->data;
      color_pipeline->ops = g_list_append (color_pipeline->ops,
                                           g_object_ref (op));
    }
}

gboolean
clutter_color_pipeline_is_empty (ClutterColorPipeline *color_pipeline)
{
  return color_pipeline->ops == NULL;
}

/**
 * clutter_color_pipeline_get_ops:
 *
 * Returns: (transfer none) (element-type ClutterColorOp): The list of ops
 */
const GList *
clutter_color_pipeline_get_ops (ClutterColorPipeline *color_pipeline)
{
  return color_pipeline->ops;
}

void
clutter_color_pipeline_do_transform (ClutterColorPipeline *color_pipeline,
                                     float                *data,
                                     size_t                n_samples)
{
  for (GList *l = color_pipeline->ops; l != NULL; l = l->next)
    {
      ClutterColorOp *op = l->data;
      clutter_color_op_do_transform (op, data, n_samples);
    }
}

static gboolean
is_inverse_pair (ClutterColorOp *a,
                 ClutterColorOp *b)
{
  return (CLUTTER_IS_COLOR_OP_SRGB_PIECEWISE_EOTF (a) &&
          CLUTTER_IS_COLOR_OP_SRGB_PIECEWISE_INV_EOTF (b)) ||
         (CLUTTER_IS_COLOR_OP_SRGB_PIECEWISE_INV_EOTF (a) &&
          CLUTTER_IS_COLOR_OP_SRGB_PIECEWISE_EOTF (b)) ||
         (CLUTTER_IS_COLOR_OP_PQ_EOTF (a) &&
          CLUTTER_IS_COLOR_OP_PQ_INV_EOTF (b)) ||
         (CLUTTER_IS_COLOR_OP_PQ_INV_EOTF (a) &&
          CLUTTER_IS_COLOR_OP_PQ_EOTF (b));
}

typedef enum _SimplifyAction
{
  SIMPLIFY_KEEP,
  SIMPLIFY_DROP_BOTH,
  SIMPLIFY_DROP_PREV,
  SIMPLIFY_DROP_OP,
} SimplifyAction;

static SimplifyAction
try_simplify (ClutterColorOp *prev_op,
              ClutterColorOp *op)
{
  if (is_inverse_pair (prev_op, op))
    return SIMPLIFY_DROP_BOTH;

  if (CLUTTER_IS_COLOR_OP_GAMMA_POWER (prev_op) &&
      CLUTTER_IS_COLOR_OP_GAMMA_POWER (op))
    {
      float pa, pb;

      pa = clutter_color_op_gamma_power_get_power (prev_op);
      pb = clutter_color_op_gamma_power_get_power (op);

      if (G_APPROX_VALUE (pa * pb, 1.0f, FLT_EPSILON))
        return SIMPLIFY_DROP_BOTH;
    }

  if (CLUTTER_IS_COLOR_OP_CLAMP_UNIT (prev_op) &&
      clutter_color_op_get_clamps_input (op))
    return SIMPLIFY_DROP_PREV;

  if (CLUTTER_IS_COLOR_OP_CLAMP_UNIT (op) &&
      clutter_color_op_get_clamps_output (prev_op))
    return SIMPLIFY_DROP_OP;

  return SIMPLIFY_KEEP;
}

static gboolean
clutter_color_pipeline_simplify_pass (ClutterColorPipeline *color_pipeline)
{
  g_autolist (ClutterColorOp) old_ops = g_steal_pointer (&color_pipeline->ops);
  gboolean changed = FALSE;

  for (GList *l = old_ops; l != NULL; l = l->next)
    {
      ClutterColorOp *op = l->data;
      GList *prev = g_list_last (color_pipeline->ops);

      if (prev)
        {
          switch (try_simplify (prev->data, op))
            {
            case SIMPLIFY_DROP_BOTH:
              g_object_unref (prev->data);
              color_pipeline->ops = g_list_delete_link (color_pipeline->ops, prev);
              changed = TRUE;
              continue;
            case SIMPLIFY_DROP_PREV:
              g_object_unref (prev->data);
              color_pipeline->ops = g_list_delete_link (color_pipeline->ops, prev);
              changed = TRUE;
              break;
            case SIMPLIFY_DROP_OP:
              changed = TRUE;
              continue;
            case SIMPLIFY_KEEP:
              break;
            }
        }

      color_pipeline->ops = g_list_append (color_pipeline->ops, g_object_ref (op));
    }

  return changed;
}

void
clutter_color_pipeline_simplify (ClutterColorPipeline *color_pipeline)
{
  while (clutter_color_pipeline_simplify_pass (color_pipeline))
    ;
}

static gboolean
clutter_color_pipeline_combine_pass (ClutterColorPipeline *color_pipeline)
{
  g_autolist (ClutterColorOp) old_ops = g_steal_pointer (&color_pipeline->ops);
  gboolean changed = FALSE;

  for (GList *l = old_ops; l != NULL; l = l->next)
    {
      ClutterColorOp *op = l->data;
      GList *prev = g_list_last (color_pipeline->ops);
      ClutterColorOp *combined = NULL;

      if (prev)
        combined = clutter_color_op_try_combine (prev->data, op);

      if (combined)
        {
          g_object_unref (prev->data);
          color_pipeline->ops = g_list_delete_link (color_pipeline->ops, prev);
          color_pipeline->ops = g_list_append (color_pipeline->ops, combined);
          changed = TRUE;
          continue;
        }

      color_pipeline->ops = g_list_append (color_pipeline->ops, g_object_ref (op));
    }

  return changed;
}

void
clutter_color_pipeline_combine (ClutterColorPipeline *color_pipeline)
{
  while (clutter_color_pipeline_combine_pass (color_pipeline))
    ;
}

