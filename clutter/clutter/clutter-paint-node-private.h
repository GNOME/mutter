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

#pragma once

#include <glib-object.h>

#include "clutter/clutter-backend.h"
#include "clutter/clutter-paint-context.h"
#include "clutter/clutter-paint-node.h"

G_BEGIN_DECLS

#define CLUTTER_PAINT_NODE_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), CLUTTER_TYPE_PAINT_NODE, ClutterPaintNodeClass))
#define CLUTTER_IS_PAINT_NODE_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), CLUTTER_TYPE_PAINT_NODE))
#define CLUTTER_PAINT_NODE_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), CLUTTER_TYPE_PAINT_NODE, ClutterPaintNodeClass))

typedef struct _ClutterPaintOperation   ClutterPaintOperation;

struct _ClutterPaintNode
{
  GTypeInstance parent_instance;

  ClutterPaintNode *parent;

  ClutterPaintNode *first_child;
  ClutterPaintNode *prev_sibling;
  ClutterPaintNode *next_sibling;
  ClutterPaintNode *last_child;

  GArray *operations;

  const gchar *name;

  guint n_children;

  volatile int ref_count;
};

struct _ClutterPaintNodeClass
{
  GTypeClass base_class;

  void     (* finalize)  (ClutterPaintNode *node);

  gboolean (* pre_draw)  (ClutterPaintNode    *node,
                          ClutterPaintContext *paint_context);
  void     (* draw)      (ClutterPaintNode    *node,
                          ClutterPaintContext *paint_context);
  void     (* post_draw) (ClutterPaintNode    *node,
                          ClutterPaintContext *paint_context);
  CoglFramebuffer *(* get_framebuffer) (ClutterPaintNode *node);
};

#define PAINT_OP_INIT   { PAINT_OP_INVALID }

typedef enum
{
  PAINT_OP_INVALID = 0,
  PAINT_OP_TEX_RECT,
  PAINT_OP_TEX_RECTS,
  PAINT_OP_MULTITEX_RECT,
  PAINT_OP_PRIMITIVE
} PaintOpCode;

struct _ClutterPaintOperation
{
  PaintOpCode opcode;

  GArray *coords;

  union {
    float texrect[8];

    CoglPrimitive *primitive;
  } op;
};

GType _clutter_dummy_node_get_type (void) G_GNUC_CONST;

void                    clutter_paint_node_init_types                   (ClutterBackend *clutter_backend);
gpointer                _clutter_paint_node_create                      (GType gtype);

ClutterPaintNode *      _clutter_dummy_node_new                         (ClutterActor                *actor,
                                                                         CoglFramebuffer             *framebuffer);
G_GNUC_INTERNAL
guint                   clutter_paint_node_get_n_children               (ClutterPaintNode      *node);

#define CLUTTER_TYPE_EFFECT_NODE                (clutter_effect_node_get_type ())
#define CLUTTER_EFFECT_NODE(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_EFFECT_NODE, ClutterEffectNode))
#define CLUTTER_IS_EFFECT_NODE(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_EFFECT_NODE))

/**
 * ClutterEffectNode:
 *
 * The #ClutterEffectNode structure is an opaque
 * type whose members cannot be directly accessed.
 */
typedef struct _ClutterEffectNode ClutterEffectNode;
typedef struct _ClutterEffectNode ClutterEffectNodeClass;

CLUTTER_EXPORT
GType clutter_effect_node_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterPaintNode * clutter_effect_node_new (ClutterEffect *effect);

G_END_DECLS
