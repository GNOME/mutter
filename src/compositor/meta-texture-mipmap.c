/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * MetaTextureMipmap
 *
 * Mipmap management object using OpenGL
 *
 * Copyright (C) 2009 Red Hat, Inc.
 * Copyright (C) 2021 Canonical Ltd.
 * Copyright (C) 2022 Neil Moore
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

#include "compositor/meta-texture-mipmap.h"
#include "compositor/meta-multi-texture-format-private.h"

#include <math.h>
#include <string.h>

struct _MetaTextureMipmap
{
  MetaMultiTexture *base_texture;
  MetaMultiTexture *mipmap_texture;
  CoglPipeline *pipeline;
  CoglFramebuffer *fb;
  gboolean invalid;
};

/**
 * meta_texture_mipmap_new:
 *
 * Creates a new mipmap handler. The base texture has to be set with
 * meta_texture_mipmap_set_base_texture() before use.
 *
 * Return value: the new texture mipmap handler. Free with meta_texture_mipmap_free()
 */
MetaTextureMipmap *
meta_texture_mipmap_new (void)
{
  MetaTextureMipmap *mipmap;

  mipmap = g_new0 (MetaTextureMipmap, 1);

  return mipmap;
}

/**
 * meta_texture_mipmap_free:
 * @mipmap: a #MetaTextureMipmap
 *
 * Frees a texture mipmap handler created with meta_texture_mipmap_new().
 */
void
meta_texture_mipmap_free (MetaTextureMipmap *mipmap)
{
  g_return_if_fail (mipmap != NULL);

  g_clear_object (&mipmap->pipeline);
  g_clear_object (&mipmap->base_texture);
  g_clear_object (&mipmap->mipmap_texture);
  g_clear_object (&mipmap->fb);

  g_free (mipmap);
}

/**
 * meta_texture_mipmap_set_base_texture:
 * @mipmap: a #MetaTextureMipmap
 * @texture: the new texture used as a base for scaled down versions
 *
 * Sets the base texture that is the scaled texture that the
 * scaled textures of the tower are derived from. The texture itself
 * will be used as level 0 of the tower and will be referenced until
 * unset or until the tower is freed.
 */
void
meta_texture_mipmap_set_base_texture (MetaTextureMipmap *mipmap,
                                      MetaMultiTexture  *texture)
{
  g_return_if_fail (mipmap != NULL);

  if (texture == mipmap->base_texture)
    return;

  g_clear_object (&mipmap->base_texture);

  mipmap->base_texture = texture;

  if (mipmap->base_texture != NULL)
    {
      g_object_ref (mipmap->base_texture);
      mipmap->invalid = TRUE;
    }
}

void
meta_texture_mipmap_invalidate (MetaTextureMipmap *mipmap)
{
  g_return_if_fail (mipmap != NULL);

  mipmap->invalid = TRUE;
}

static void
free_mipmaps (MetaTextureMipmap *mipmap)
{
  g_clear_object (&mipmap->fb);
  g_clear_object (&mipmap->mipmap_texture);
}

void
meta_texture_mipmap_clear (MetaTextureMipmap *mipmap)
{
  g_return_if_fail (mipmap != NULL);

  free_mipmaps (mipmap);
}

static void
ensure_mipmap_texture (MetaTextureMipmap *mipmap)
{
  CoglContext *ctx =
    clutter_backend_get_cogl_context (clutter_get_default_backend ());
  int width, height;

  /* Let's avoid spending any texture memory copying the base level texture
   * because we'll never need that one and it would have used most of the
   * memory;
   *    S(0) = W x H
   *    S(n) = S(n-1) / 4
   *    sum to infinity of S(n) = 4/3 * S(0)
   * So subtracting S(0) means even infinite mipmap levels only need one third
   * of the original texture's memory. Finite levels need less.
   *
   * The fact that mipmap level 0 of mipmap texture is half the
   * resolution of original texture makes no visual difference, so long as you're
   * never trying to view a level of detail higher than half. If you need that
   * then just use the original texture instead of mipmap texture, which is
   * faster anyway.
   */
  width = meta_multi_texture_get_width (mipmap->base_texture);
  height = meta_multi_texture_get_height (mipmap->base_texture);

  if (!width || !height)
    {
      free_mipmaps (mipmap);
      return;
    }

  width = MAX (width / 2, 1);
  height = MAX (height / 2, 1);

  if (!mipmap->mipmap_texture ||
      meta_multi_texture_get_width (mipmap->mipmap_texture) != width ||
      meta_multi_texture_get_height (mipmap->mipmap_texture) != height)
    {
      CoglOffscreen *offscreen;
      CoglTexture *tex;

      free_mipmaps (mipmap);

      tex = cogl_texture_2d_new_with_size (ctx, width, height);
      if (!tex)
        return;

      mipmap->mipmap_texture = meta_multi_texture_new_simple (tex);

      offscreen = cogl_offscreen_new_with_texture (tex);
      if (!offscreen)
        {
          free_mipmaps (mipmap);
          return;
        }

      mipmap->fb = COGL_FRAMEBUFFER (offscreen);

      if (!cogl_framebuffer_allocate (mipmap->fb, NULL))
        {
          free_mipmaps (mipmap);
          return;
        }

      cogl_framebuffer_orthographic (mipmap->fb,
                                     0, 0, width, height, -1.0, 1.0);

      mipmap->invalid = TRUE;
    }

  if (mipmap->invalid)
    {
      int n_planes, i;

      n_planes = meta_multi_texture_get_n_planes (mipmap->base_texture);

      if (!mipmap->pipeline)
        {
          MetaMultiTextureFormat format =
            meta_multi_texture_get_format (mipmap->base_texture);
          CoglSnippet *fragment_globals_snippet;
          CoglSnippet *fragment_snippet;

          mipmap->pipeline = cogl_pipeline_new (ctx);
          cogl_pipeline_set_blend (mipmap->pipeline,
                                   "RGBA = ADD (SRC_COLOR, 0)",
                                   NULL);

          for (i = 0; i < n_planes; i++)
            {
              cogl_pipeline_set_layer_filters (mipmap->pipeline, i,
                                               COGL_PIPELINE_FILTER_LINEAR,
                                               COGL_PIPELINE_FILTER_LINEAR);
              cogl_pipeline_set_layer_combine (mipmap->pipeline, i,
                                               "RGBA = REPLACE(TEXTURE)",
                                               NULL);
            }

          meta_multi_texture_format_get_snippets (format,
                                                  &fragment_globals_snippet,
                                                  &fragment_snippet);
          cogl_pipeline_add_snippet (mipmap->pipeline, fragment_globals_snippet);
          cogl_pipeline_add_snippet (mipmap->pipeline, fragment_snippet);

          g_clear_object (&fragment_globals_snippet);
          g_clear_object (&fragment_snippet);
        }

      for (i = 0; i < n_planes; i++)
        {
          CoglTexture *plane = meta_multi_texture_get_plane (mipmap->base_texture, i);

          cogl_pipeline_set_layer_texture (mipmap->pipeline, i, plane);
        }

      cogl_framebuffer_draw_textured_rectangle (mipmap->fb,
                                                mipmap->pipeline,
                                                0, 0, width, height,
                                                0.0, 0.0, 1.0, 1.0);

      mipmap->invalid = FALSE;
    }
}

/**
 * meta_texture_tower_get_paint_texture:
 * @mipmap: a #MetaTextureMipmap
 *
 * Gets the texture from the tower that best matches the current
 * rendering scale. (On the assumption here the texture is going to
 * be rendered with vertex coordinates that correspond to its
 * size in pixels, so a 200x200 texture will be rendered on the
 * rectangle (0, 0, 200, 200).
 *
 * Return value: the COGL texture handle to use for painting, or
 *  %NULL if no base texture has yet been set.
 */
MetaMultiTexture *
meta_texture_mipmap_get_paint_texture (MetaTextureMipmap *mipmap)
{
  g_return_val_if_fail (mipmap != NULL, NULL);

  ensure_mipmap_texture (mipmap);

  return mipmap->mipmap_texture;
}
