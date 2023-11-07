/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2009,2010,2011 Intel Corporation.
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
 *  Neil Roberts   <neil@linux.intel.com>
 */

#include "config.h"

#include "cogl/cogl-util.h"
#include "cogl/cogl-texture-private.h"
#include "cogl/cogl-sub-texture-private.h"
#include "cogl/cogl-sub-texture.h"
#include "cogl/cogl-context-private.h"
#include "cogl/cogl-texture-driver.h"
#include "cogl/cogl-texture-2d.h"
#include "cogl/driver/gl/cogl-texture-gl-private.h"

#include <string.h>
#include <math.h>


G_DEFINE_FINAL_TYPE (CoglSubTexture, cogl_sub_texture, COGL_TYPE_TEXTURE)

static void
cogl_sub_texture_dispose (GObject *object)
{
  CoglSubTexture *sub_tex = COGL_SUB_TEXTURE (object);

  g_object_unref (sub_tex->next_texture);
  g_object_unref (sub_tex->full_texture);

  G_OBJECT_CLASS (cogl_sub_texture_parent_class)->dispose (object);
}

static void
_cogl_sub_texture_unmap_quad (CoglSubTexture *sub_tex,
                              float *coords)
{
  CoglTexture *tex = COGL_TEXTURE (sub_tex);
  float width = cogl_texture_get_width (sub_tex->full_texture);
  float height = cogl_texture_get_height (sub_tex->full_texture);

  coords[0] = (coords[0] * width - sub_tex->sub_x) / cogl_texture_get_width (tex);
  coords[1] = (coords[1] * height - sub_tex->sub_y) / cogl_texture_get_height (tex);
  coords[2] = (coords[2] * width - sub_tex->sub_x) / cogl_texture_get_width (tex);
  coords[3] = (coords[3] * height - sub_tex->sub_y) / cogl_texture_get_height (tex);
}

static void
_cogl_sub_texture_map_quad (CoglSubTexture *sub_tex,
                            float *coords)
{
  CoglTexture *tex = COGL_TEXTURE (sub_tex);
  float width = cogl_texture_get_width (sub_tex->full_texture);
  float height = cogl_texture_get_height (sub_tex->full_texture);

  coords[0] = (coords[0] * cogl_texture_get_width (tex) + sub_tex->sub_x) / width;
  coords[1] = (coords[1] * cogl_texture_get_height (tex) + sub_tex->sub_y) / height;
  coords[2] = (coords[2] * cogl_texture_get_width (tex) + sub_tex->sub_x) / width;
  coords[3] = (coords[3] * cogl_texture_get_height (tex) + sub_tex->sub_y) / height;
}

typedef struct _CoglSubTextureForeachData
{
  CoglSubTexture *sub_tex;
  CoglMetaTextureCallback callback;
  void *user_data;
} CoglSubTextureForeachData;

static void
unmap_coords_cb (CoglTexture *slice_texture,
                 const float *slice_texture_coords,
                 const float *meta_coords,
                 void *user_data)
{
  CoglSubTextureForeachData *data = user_data;
  float unmapped_coords[4];

  memcpy (unmapped_coords, meta_coords, sizeof (unmapped_coords));

  _cogl_sub_texture_unmap_quad (data->sub_tex, unmapped_coords);

  data->callback (slice_texture,
                  slice_texture_coords,
                  unmapped_coords,
                  data->user_data);
}

static void
_cogl_sub_texture_foreach_sub_texture_in_region (
                                       CoglTexture *tex,
                                       float virtual_tx_1,
                                       float virtual_ty_1,
                                       float virtual_tx_2,
                                       float virtual_ty_2,
                                       CoglMetaTextureCallback callback,
                                       void *user_data)
{
  CoglSubTexture *sub_tex = COGL_SUB_TEXTURE (tex);
  CoglTexture *full_texture = sub_tex->full_texture;
  float mapped_coords[4] =
    { virtual_tx_1, virtual_ty_1, virtual_tx_2, virtual_ty_2};
  float virtual_coords[4] =
    { virtual_tx_1, virtual_ty_1, virtual_tx_2, virtual_ty_2};

  /* map the virtual coordinates to ->full_texture coordinates */
  _cogl_sub_texture_map_quad (sub_tex, mapped_coords);

  /* TODO: Add something like cogl_is_low_level_texture() */
  if (COGL_IS_TEXTURE_2D (full_texture))
    {
      callback (sub_tex->full_texture,
                mapped_coords,
                virtual_coords,
                user_data);
    }
  else
    {
      CoglSubTextureForeachData data;

      data.sub_tex = sub_tex;
      data.callback = callback;
      data.user_data = user_data;

      cogl_meta_texture_foreach_in_region (full_texture,
                                           mapped_coords[0],
                                           mapped_coords[1],
                                           mapped_coords[2],
                                           mapped_coords[3],
                                           COGL_PIPELINE_WRAP_MODE_REPEAT,
                                           COGL_PIPELINE_WRAP_MODE_REPEAT,
                                           unmap_coords_cb,
                                           &data);
    }
}

static void
_cogl_sub_texture_gl_flush_legacy_texobj_wrap_modes (CoglTexture *tex,
                                                     GLenum wrap_mode_s,
                                                     GLenum wrap_mode_t)
{
  CoglSubTexture *sub_tex = COGL_SUB_TEXTURE (tex);

  _cogl_texture_gl_flush_legacy_texobj_wrap_modes (sub_tex->full_texture,
                                                   wrap_mode_s,
                                                   wrap_mode_t);
}

static gboolean
_cogl_sub_texture_allocate (CoglTexture *tex,
                            GError **error)
{
  CoglSubTexture *sub_tex = COGL_SUB_TEXTURE (tex);
  gboolean status = cogl_texture_allocate (sub_tex->full_texture, error);

  _cogl_texture_set_allocated (tex,
                               _cogl_texture_get_format (sub_tex->full_texture),
                               cogl_texture_get_width (tex),
                               cogl_texture_get_height (tex));

  return status;
}

static int
_cogl_sub_texture_get_max_waste (CoglTexture *tex)
{
  CoglSubTexture *sub_tex = COGL_SUB_TEXTURE (tex);

  return cogl_texture_get_max_waste (sub_tex->full_texture);
}

static gboolean
_cogl_sub_texture_is_sliced (CoglTexture *tex)
{
  CoglSubTexture *sub_tex = COGL_SUB_TEXTURE (tex);

  return cogl_texture_is_sliced (sub_tex->full_texture);
}

static gboolean
_cogl_sub_texture_can_hardware_repeat (CoglTexture *tex)
{
  CoglSubTexture *sub_tex = COGL_SUB_TEXTURE (tex);

  /* We can hardware repeat if the subtexture actually represents all of the
     of the full texture */
  return (cogl_texture_get_width (tex) ==
          cogl_texture_get_width (sub_tex->full_texture) &&
          cogl_texture_get_height (tex) ==
          cogl_texture_get_height (sub_tex->full_texture) &&
          _cogl_texture_can_hardware_repeat (sub_tex->full_texture));
}

static void
_cogl_sub_texture_transform_coords_to_gl (CoglTexture *tex,
                                          float *s,
                                          float *t)
{
  CoglSubTexture *sub_tex = COGL_SUB_TEXTURE (tex);

  /* This won't work if the sub texture is not the size of the full
     texture and the coordinates are outside the range [0,1] */
  *s = ((*s * cogl_texture_get_width (tex) + sub_tex->sub_x) /
        cogl_texture_get_width (sub_tex->full_texture));
  *t = ((*t * cogl_texture_get_height (tex) + sub_tex->sub_y) /
        cogl_texture_get_height (sub_tex->full_texture));

  _cogl_texture_transform_coords_to_gl (sub_tex->full_texture, s, t);
}

static CoglTransformResult
_cogl_sub_texture_transform_quad_coords_to_gl (CoglTexture *tex,
                                               float *coords)
{
  CoglSubTexture *sub_tex = COGL_SUB_TEXTURE (tex);
  int i;

  /* We can't support repeating with this method. In this case
     cogl-primitives will resort to manual repeating */
  for (i = 0; i < 4; i++)
    if (coords[i] < 0.0f || coords[i] > 1.0f)
      return COGL_TRANSFORM_SOFTWARE_REPEAT;

  _cogl_sub_texture_map_quad (sub_tex, coords);

  return _cogl_texture_transform_quad_coords_to_gl (sub_tex->full_texture,
                                                    coords);
}

static gboolean
_cogl_sub_texture_get_gl_texture (CoglTexture *tex,
                                  GLuint *out_gl_handle,
                                  GLenum *out_gl_target)
{
  CoglSubTexture *sub_tex = COGL_SUB_TEXTURE (tex);

  return cogl_texture_get_gl_texture (sub_tex->full_texture,
                                      out_gl_handle,
                                      out_gl_target);
}

static void
_cogl_sub_texture_gl_flush_legacy_texobj_filters (CoglTexture *tex,
                                                  GLenum min_filter,
                                                  GLenum mag_filter)
{
  CoglSubTexture *sub_tex = COGL_SUB_TEXTURE (tex);

  _cogl_texture_gl_flush_legacy_texobj_filters (sub_tex->full_texture,
                                                min_filter, mag_filter);
}

static void
_cogl_sub_texture_pre_paint (CoglTexture *tex,
                             CoglTexturePrePaintFlags flags)
{
  CoglSubTexture *sub_tex = COGL_SUB_TEXTURE (tex);

  _cogl_texture_pre_paint (sub_tex->full_texture, flags);
}

static void
_cogl_sub_texture_ensure_non_quad_rendering (CoglTexture *tex)
{
}

static gboolean
_cogl_sub_texture_set_region (CoglTexture *tex,
                              int src_x,
                              int src_y,
                              int dst_x,
                              int dst_y,
                              int dst_width,
                              int dst_height,
                              int level,
                              CoglBitmap *bmp,
                              GError **error)
{
  CoglSubTexture  *sub_tex = COGL_SUB_TEXTURE (tex);

  if (level != 0)
    {
      int full_width = cogl_texture_get_width (sub_tex->full_texture);
      int full_height = cogl_texture_get_width (sub_tex->full_texture);

      g_return_val_if_fail (sub_tex->sub_x == 0 &&
                            cogl_texture_get_width (tex) == full_width,
                            FALSE);
      g_return_val_if_fail (sub_tex->sub_y == 0 &&
                            cogl_texture_get_height (tex) == full_height,
                            FALSE);
    }

  return _cogl_texture_set_region_from_bitmap (sub_tex->full_texture,
                                               src_x, src_y,
                                               dst_width, dst_height,
                                               bmp,
                                               dst_x + sub_tex->sub_x,
                                               dst_y + sub_tex->sub_y,
                                               level,
                                               error);
}

static gboolean
_cogl_sub_texture_is_get_data_supported (CoglTexture *tex)
{
  CoglSubTexture *sub_tex = COGL_SUB_TEXTURE (tex);

  return cogl_texture_is_get_data_supported (sub_tex->full_texture);
}

static CoglPixelFormat
_cogl_sub_texture_get_format (CoglTexture *tex)
{
  CoglSubTexture *sub_tex = COGL_SUB_TEXTURE (tex);

  return _cogl_texture_get_format (sub_tex->full_texture);
}

static GLenum
_cogl_sub_texture_get_gl_format (CoglTexture *tex)
{
  CoglSubTexture *sub_tex = COGL_SUB_TEXTURE (tex);

  return _cogl_texture_gl_get_format (sub_tex->full_texture);
}

static void
cogl_sub_texture_class_init (CoglSubTextureClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  CoglTextureClass *texture_class = COGL_TEXTURE_CLASS (klass);

  gobject_class->dispose = cogl_sub_texture_dispose;

  texture_class->allocate = _cogl_sub_texture_allocate;
  texture_class->set_region = _cogl_sub_texture_set_region;
  texture_class->is_get_data_supported = _cogl_sub_texture_is_get_data_supported;
  texture_class->foreach_sub_texture_in_region = _cogl_sub_texture_foreach_sub_texture_in_region;
  texture_class->get_max_waste = _cogl_sub_texture_get_max_waste;
  texture_class->is_sliced = _cogl_sub_texture_is_sliced;
  texture_class->can_hardware_repeat = _cogl_sub_texture_can_hardware_repeat;
  texture_class->transform_coords_to_gl = _cogl_sub_texture_transform_coords_to_gl;
  texture_class->transform_quad_coords_to_gl = _cogl_sub_texture_transform_quad_coords_to_gl;
  texture_class->get_gl_texture = _cogl_sub_texture_get_gl_texture;
  texture_class->gl_flush_legacy_texobj_filters = _cogl_sub_texture_gl_flush_legacy_texobj_filters;
  texture_class->pre_paint = _cogl_sub_texture_pre_paint;
  texture_class->ensure_non_quad_rendering = _cogl_sub_texture_ensure_non_quad_rendering;
  texture_class->gl_flush_legacy_texobj_wrap_modes = _cogl_sub_texture_gl_flush_legacy_texobj_wrap_modes;
  texture_class->get_format = _cogl_sub_texture_get_format;
  texture_class->get_gl_format = _cogl_sub_texture_get_gl_format;
}

static void
cogl_sub_texture_init (CoglSubTexture *self)
{
  CoglTexture *texture = COGL_TEXTURE (self);

  texture->is_primitive = FALSE;
}

CoglTexture *
cogl_sub_texture_new (CoglContext *ctx,
                      CoglTexture *next_texture,
                      int sub_x, int sub_y,
                      int sub_width, int sub_height)
{
  CoglTexture    *full_texture;
  CoglSubTexture *sub_tex;
  unsigned int    next_width, next_height;

  next_width = cogl_texture_get_width (next_texture);
  next_height = cogl_texture_get_height (next_texture);

  /* The region must specify a non-zero subset of the full texture */
  g_return_val_if_fail (sub_x >= 0 && sub_y >= 0, NULL);
  g_return_val_if_fail (sub_width > 0 && sub_height > 0, NULL);
  g_return_val_if_fail (sub_x + sub_width <= next_width, NULL);
  g_return_val_if_fail (sub_y + sub_height <= next_height, NULL);

  sub_tex = g_object_new (COGL_TYPE_SUB_TEXTURE,
                          "context", ctx,
                          "width", sub_width,
                          "height", sub_height,
                          "format", _cogl_texture_get_format (next_texture),
                          NULL);

  /* If the next texture is also a sub texture we can avoid one level
     of indirection by referencing the full texture of that texture
     instead. */
  if (COGL_IS_SUB_TEXTURE (next_texture))
    {
      CoglSubTexture *other_sub_tex = COGL_SUB_TEXTURE (next_texture);
      full_texture = other_sub_tex->full_texture;
      sub_x += other_sub_tex->sub_x;
      sub_y += other_sub_tex->sub_y;
    }
  else
    full_texture = next_texture;

  sub_tex->next_texture = g_object_ref (next_texture);
  sub_tex->full_texture = g_object_ref (full_texture);

  sub_tex->sub_x = sub_x;
  sub_tex->sub_y = sub_y;

  return COGL_TEXTURE (sub_tex);
}

CoglTexture *
cogl_sub_texture_get_parent (CoglSubTexture *sub_texture)
{
  return sub_texture->next_texture;
}
