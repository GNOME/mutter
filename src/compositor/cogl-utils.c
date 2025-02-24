/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Utilities for use with Cogl
 *
 * Copyright 2010 Red Hat, Inc.
 * Copyright 2010 Intel Corporation
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

#include "clutter/clutter.h"
#include "compositor/cogl-utils.h"

/* Based on gnome-shell/src/st/st-private.c:_st_create_texture_material.c */

/**
 * meta_create_texture_pipeline:
 * @cogl_context: A #CoglContext
 * @src_texture: (nullable): texture to use initially for the layer
 *
 * Creates a pipeline with a single layer. Using a common template
 * makes it easier for Cogl to share a shader for different uses in
 * Mutter.
 *
 * Return value: (transfer full): a newly created #CoglPipeline
 */
CoglPipeline *
meta_create_texture_pipeline (CoglContext *cogl_context,
                              CoglTexture *src_texture)
{
  static CoglPipeline *texture_pipeline_template = NULL;
  CoglPipeline *pipeline;

  /* The only state used in the pipeline that would affect the shader
     generation is the texture type on the layer. Therefore we create
     a template pipeline which sets this state and all texture
     pipelines are created as a copy of this. That way Cogl can find
     the shader state for the pipeline more quickly by looking at the
     pipeline ancestry instead of resorting to the shader cache. */
  if (G_UNLIKELY (texture_pipeline_template == NULL))
    {
      texture_pipeline_template = cogl_pipeline_new (cogl_context);
      cogl_pipeline_set_layer_null_texture (texture_pipeline_template, 0);
    }

  pipeline = cogl_pipeline_copy (texture_pipeline_template);

  if (src_texture != NULL)
    cogl_pipeline_set_layer_texture (pipeline, 0, src_texture);

  return pipeline;
}

/**
 * meta_create_texture:
 * @width: width of the texture to create
 * @height: height of the texture to create
 * @ctx: A #CoglContext
 * @components: components to store in the texture (color or alpha)
 * @flags: flags that affect the allocation behavior
 *
 * Creates a texture of the given size with the specified components
 * for use as a frame buffer object.
 *
 * If %META_TEXTURE_ALLOW_SLICING is present in @flags, and the texture
 * is larger than the texture size limits of the system, then the texture
 * will be created as a sliced texture. This also will cause problems
 * with using the texture with GLSL, and is more likely to be an issue
 * since all GL implementations have texture size limits, and they can
 * be as small as 2048x2048 on reasonably current systems.
 */
CoglTexture *
meta_create_texture (int                    width,
                     int                    height,
                     CoglContext           *ctx,
                     CoglTextureComponents  components,
                     MetaTextureFlags       flags)
{
  CoglTexture *texture;

  texture = cogl_texture_2d_new_with_size (ctx, width, height);
  cogl_texture_set_components (texture, components);

  if ((flags & META_TEXTURE_ALLOW_SLICING) != 0)
    {
      /* To find out if we need to slice the texture, we have to go ahead and force storage
       * to be allocated
       */
      GError *catch_error = NULL;
      if (!cogl_texture_allocate (texture, &catch_error))
        {
          g_error_free (catch_error);
          g_object_unref (texture);
          texture = cogl_texture_2d_sliced_new_with_size (ctx, width, height, COGL_TEXTURE_MAX_WASTE);
          cogl_texture_set_components (texture, components);
        }
    }

  return texture;
}
