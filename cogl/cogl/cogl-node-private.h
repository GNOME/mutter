/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2008,2009,2010 Intel Corporation.
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
 *
 *
 *
 * Authors:
 *   Robert Bragg <robert@linux.intel.com>
 */

#pragma once

#include "cogl/cogl-list.h"

typedef struct _CoglNodeClass CoglNodeClass;
typedef struct _CoglNode CoglNode;

/* Pipelines and layers represent their state in a tree structure where
 * some of the state relating to a given pipeline or layer may actually
 * be owned by one if is ancestors in the tree. We have a common data
 * type to track the tree hierarchy so we can share code... */
struct _CoglNode
{
  GObject parent_instance;

  /* The parent pipeline/layer */
  CoglNode *parent;

  /* The list entry here contains pointers to the node's siblings */
  CoglList link;

  /* List of children */
  CoglList children;

  /* TRUE if the node took a strong reference on its parent. Weak
   * pipelines for instance don't take a reference on their parent. */
  gboolean has_parent_reference;
};

struct _CoglNodeClass
{
  GObjectClass parent_class;
};

#define COGL_TYPE_NODE            (cogl_node_get_type ())
#define COGL_NODE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COGL_TYPE_NODE, CoglNode))
#define COGL_NODE_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), COGL_TYPE_NODE, CoglNode const))
#define COGL_NODE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COGL_TYPE_NODE, CoglNodeClass))
#define COGL_IS_NODE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COGL_TYPE_NODE))
#define COGL_IS_NODE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COGL_TYPE_NODE))
#define COGL_NODE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COGL_TYPE_NODE, CoglNodeClass))

G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglNode, g_object_unref)

GType       cogl_node_get_type (void) G_GNUC_CONST;

void
_cogl_pipeline_node_set_parent_real (CoglNode *node,
                                     CoglNode *parent,
                                     gboolean take_strong_reference);

void
_cogl_pipeline_node_unparent_real (CoglNode *node);

typedef gboolean (*CoglNodeChildCallback) (CoglNode *child, void *user_data);

void
_cogl_pipeline_node_foreach_child (CoglNode *node,
                                   CoglNodeChildCallback callback,
                                   void *user_data);
