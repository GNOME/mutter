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

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "cogl/cogl.h"
#include "clutter/clutter-types.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_COLOR_NODE         (clutter_color_node_get_type ())
#define CLUTTER_COLOR_NODE(obj)         (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_COLOR_NODE, ClutterColorNode))
#define CLUTTER_IS_COLOR_NODE(obj)      (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_COLOR_NODE))

typedef struct _ClutterColorNode                ClutterColorNode;
typedef struct _ClutterColorNodeClass           ClutterColorNodeClass;

CLUTTER_EXPORT
GType clutter_color_node_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterPaintNode *      clutter_color_node_new          (const CoglColor    *color);

#define CLUTTER_TYPE_TEXTURE_NODE               (clutter_texture_node_get_type ())
#define CLUTTER_TEXTURE_NODE(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_TEXTURE_NODE, ClutterTextureNode))
#define CLUTTER_IS_TEXTURE_NODE(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_TEXTURE_NODE))

typedef struct _ClutterTextureNode              ClutterTextureNode;
typedef struct _ClutterTextureNodeClass         ClutterTextureNodeClass;

CLUTTER_EXPORT
GType clutter_texture_node_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterPaintNode *      clutter_texture_node_new        (CoglTexture           *texture,
                                                         const CoglColor       *color,
                                                         ClutterScalingFilter   min_filter,
                                                         ClutterScalingFilter   mag_filter);

#define CLUTTER_TYPE_CLIP_NODE                  (clutter_clip_node_get_type ())
#define CLUTTER_CLIP_NODE(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_CLIP_NODE, ClutterClipNode))
#define CLUTTER_IS_CLIP_NODE(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_CLIP_NODE))

typedef struct _ClutterClipNode                 ClutterClipNode;
typedef struct _ClutterClipNodeClass            ClutterClipNodeClass;

CLUTTER_EXPORT
GType clutter_clip_node_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterPaintNode *      clutter_clip_node_new           (void);

#define CLUTTER_TYPE_PIPELINE_NODE              (clutter_pipeline_node_get_type ())
#define CLUTTER_PIPELINE_NODE(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_PIPELINE_NODE, ClutterPipelineNode))
#define CLUTTER_IS_PIPELINE_NODE(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_PIPELINE_NODE))

typedef struct _ClutterPipelineNode             ClutterPipelineNode;
typedef struct _ClutterPipelineNodeClass        ClutterPipelineNodeClass;

CLUTTER_EXPORT
GType clutter_pipeline_node_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterPaintNode *      clutter_pipeline_node_new       (CoglPipeline          *pipeline);

#define CLUTTER_TYPE_ACTOR_NODE                 (clutter_actor_node_get_type ())
#define CLUTTER_ACTOR_NODE(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_ACTOR_NODE, ClutterActorNode))
#define CLUTTER_IS_ACTOR_NODE(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_ACTOR_NODE))

typedef struct _ClutterActorNode ClutterActorNode;
typedef struct _ClutterActorNode ClutterActorNodeClass;

CLUTTER_EXPORT
GType clutter_actor_node_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterPaintNode * clutter_actor_node_new (ClutterActor *actor,
                                           int           opacity);

#define CLUTTER_TYPE_ROOT_NODE                  (clutter_root_node_get_type ())
#define CLUTTER_ROOT_NODE(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_ROOT_NODE, ClutterRootNode))
#define CLUTTER_IS_ROOT_NODE(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_ROOT_NODE))

typedef struct _ClutterRootNode                 ClutterRootNode;
typedef struct _ClutterPaintNodeClass           ClutterRootNodeClass;

CLUTTER_EXPORT
GType clutter_root_node_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterPaintNode *      clutter_root_node_new           (CoglFramebuffer       *framebuffer,
                                                         const CoglColor       *clear_color,
                                                         CoglBufferBit          clear_flags);

#define CLUTTER_TYPE_LAYER_NODE                 (clutter_layer_node_get_type ())
#define CLUTTER_LAYER_NODE(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_LAYER_NODE, ClutterLayerNode))
#define CLUTTER_IS_LAYER_NODE(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_LAYER_NODE))

typedef struct _ClutterLayerNode                ClutterLayerNode;
typedef struct _ClutterLayerNodeClass           ClutterLayerNodeClass;

CLUTTER_EXPORT
GType clutter_layer_node_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterPaintNode * clutter_layer_node_new_to_framebuffer (CoglFramebuffer *framebuffer,
                                                          CoglPipeline    *pipeline);


#define CLUTTER_TYPE_TRANSFORM_NODE             (clutter_transform_node_get_type ())
#define CLUTTER_TRANSFORM_NODE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_TRANSFORM_NODE, ClutterTransformNode))
#define CLUTTER_IS_TRANSFORM_NODE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_TRANSFORM_NODE))

typedef struct _ClutterTransformNode            ClutterTransformNode;
typedef struct _ClutterPaintNodeClass           ClutterTransformNodeClass;

CLUTTER_EXPORT
GType clutter_transform_node_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterPaintNode *      clutter_transform_node_new          (const graphene_matrix_t *projection);

#define CLUTTER_TYPE_BLIT_NODE                  (clutter_blit_node_get_type ())
#define CLUTTER_BLIT_NODE(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_BLIT_NODE, ClutterBlitNode))
#define CLUTTER_IS_BLIT_NODE(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_BLIT_NODE))

typedef struct _ClutterBlitNode                 ClutterBlitNode;
typedef struct _ClutterPaintNodeClass           ClutterBlitNodeClass;

CLUTTER_EXPORT
GType clutter_blit_node_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterPaintNode * clutter_blit_node_new (CoglFramebuffer *src);

CLUTTER_EXPORT
void clutter_blit_node_add_blit_rectangle (ClutterBlitNode *blit_node,
                                           int              src_x,
                                           int              src_y,
                                           int              dst_x,
                                           int              dst_y,
                                           int              width,
                                           int              height);

#define CLUTTER_TYPE_BLUR_NODE                  (clutter_blur_node_get_type ())
#define CLUTTER_BLUR_NODE(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_BLUR_NODE, ClutterBlurNode))
#define CLUTTER_IS_BLUR_NODE(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_BLUR_NODE))

typedef struct _ClutterBlurNode                 ClutterBlurNode;
typedef struct _ClutterLayerNodeClass           ClutterBlurNodeClass;

CLUTTER_EXPORT
GType clutter_blur_node_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterPaintNode * clutter_blur_node_new (unsigned int width,
                                          unsigned int height,
                                          float        radius);

G_END_DECLS
