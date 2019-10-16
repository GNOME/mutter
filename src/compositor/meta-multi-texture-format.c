/*
 * Authored By Niels De Graef <niels.degraef@barco.com>
 *
 * Copyright (C) 2018 Barco NV
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

/**
 * SECTION:meta-multi-texture-format
 * @title: MetaMultiTextureFormat
 * @short_description: A representation for complex pixel formats
 *
 * Some pixel formats that are used in the wild are a bit more complex than
 * just ARGB and all its variants. For example: a component might be put in a
 * different plane (i.e. at a different place in memory). Another example are
 * formats that use Y, U, and V components rather than RGB; if we composit them
 * onto an RGBA framebuffer, we have to make sure for example that these get
 * converted to the right color format first (using e.g. a shader).
 */

#include "meta/meta-multi-texture-format.h"

#include <cogl/cogl.h>
#include <stdlib.h>
#include <string.h>

typedef struct _MetaMultiTextureFormatInfo
{
  MetaMultiTextureFormat multi_format;
  uint8_t n_planes;

  /* Per plane-information */
  uint8_t bpp[COGL_PIXEL_FORMAT_MAX_PLANES];        /* Bytes per pixel (without subsampling) */
  uint8_t hsub[COGL_PIXEL_FORMAT_MAX_PLANES];       /* horizontal subsampling                */
  uint8_t vsub[COGL_PIXEL_FORMAT_MAX_PLANES];       /* vertical subsampling                  */
  CoglPixelFormat subformats[COGL_PIXEL_FORMAT_MAX_PLANES]; /* influences how we deal with it on a GL level */
} MetaMultiTextureFormatInfo;

/* NOTE: The actual enum values are used as the index, so you don't need to
 * loop over the table */
static MetaMultiTextureFormatInfo multi_format_table[] = {
  /* Simple */
  {
    .n_planes = 1,
    .bpp  = { 4 },
    .hsub = { 1 },
    .vsub = { 1 },
    .subformats = { COGL_PIXEL_FORMAT_ANY },
  },
  /* Packed YUV */
  { /* YUYV */
    .n_planes = 1,
    .bpp  = { 4 },
    .hsub = { 1 },
    .vsub = { 1 },
    .subformats = { COGL_PIXEL_FORMAT_ARGB_8888 },
  },
  { /* YVYU */
    .n_planes = 1,
    .bpp  = { 4 },
    .hsub = { 1 },
    .vsub = { 1 },
    .subformats = { COGL_PIXEL_FORMAT_ARGB_8888 },
  },
  { /* UYVY */
    .n_planes = 1,
    .bpp  = { 4 },
    .hsub = { 1 },
    .vsub = { 1 },
    .subformats = { COGL_PIXEL_FORMAT_ARGB_8888 },
  },
  { /* VYUY */
    .n_planes = 1,
    .bpp  = { 4 },
    .hsub = { 1 },
    .vsub = { 1 },
    .subformats = { COGL_PIXEL_FORMAT_ARGB_8888 },
  },
  { /* AYUV */
    .n_planes = 1,
    .bpp  = { 4 },
    .hsub = { 1 },
    .vsub = { 1 },
    .subformats = { COGL_PIXEL_FORMAT_ARGB_8888 },
  },
  /* 2 plane RGB + A */
  { /* XRGB8888_A8 */
    .n_planes = 2,
    .bpp  = { 4, 1 },
    .hsub = { 1, 1 },
    .vsub = { 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_ARGB_8888, COGL_PIXEL_FORMAT_A_8 },
  },
  { /* XBGR8888_A8 */
    .n_planes = 2,
    .bpp  = { 4, 1 },
    .hsub = { 1, 1 },
    .vsub = { 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_ABGR_8888, COGL_PIXEL_FORMAT_A_8 },
  },
  { /* RGBX8888_A8 */
    .n_planes = 2,
    .bpp  = { 4, 1 },
    .hsub = { 1, 1 },
    .vsub = { 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_RGBA_8888, COGL_PIXEL_FORMAT_A_8 },
  },
  { /* BGRX8888_A8 */
    .n_planes = 2,
    .bpp  = { 4, 1 },
    .hsub = { 1, 1 },
    .vsub = { 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_BGRA_8888, COGL_PIXEL_FORMAT_A_8 },
  },
  { /* RGB888_A8 */
    .n_planes = 2,
    .bpp  = { 3, 1 },
    .hsub = { 1, 1 },
    .vsub = { 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_RGB_888, COGL_PIXEL_FORMAT_A_8 },
  },
  { /* BGR888_A8 */
    .n_planes = 2,
    .bpp  = { 3, 1 },
    .hsub = { 1, 1 },
    .vsub = { 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_BGR_888, COGL_PIXEL_FORMAT_A_8 },
  },
  { /* RGB565_A8 */
    .n_planes = 2,
    .bpp  = { 2, 1 },
    .hsub = { 1, 1 },
    .vsub = { 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_RGB_565, COGL_PIXEL_FORMAT_A_8 },
  },
  { /* BGR565_A8 */
    .n_planes = 2,
    .bpp  = { 2, 1 },
    .hsub = { 1, 1 },
    .vsub = { 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_RGB_565, COGL_PIXEL_FORMAT_A_8 },
  },
  /* 2 plane YUV */
  { /* NV12 */
    .n_planes = 2,
    .bpp  = { 1, 2 },
    .hsub = { 1, 2 },
    .vsub = { 1, 2 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_RG_88 },
  },
  { /* NV21 */
    .n_planes = 2,
    .bpp  = { 1, 2 },
    .hsub = { 1, 2 },
    .vsub = { 1, 2 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_RG_88 },
  },
  { /* NV16 */
    .n_planes = 2,
    .bpp  = { 1, 2 },
    .hsub = { 1, 2 },
    .vsub = { 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_RG_88 },
  },
  { /* NV61 */
    .n_planes = 2,
    .bpp  = { 1, 2 },
    .hsub = { 1, 2 },
    .vsub = { 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_RG_88 },
  },
  { /* NV24 */
    .n_planes = 2,
    .bpp  = { 1, 2 },
    .hsub = { 1, 1 },
    .vsub = { 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_RG_88 },
  },
  { /* NV42 */
    .n_planes = 2,
    .bpp  = { 1, 2 },
    .hsub = { 1, 1 },
    .vsub = { 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_RG_88 },
  },
  /* 3 plane YUV */
  { /* YUV410 */
    .n_planes = 3,
    .bpp  = { 1, 1, 1 },
    .hsub = { 1, 4, 4 },
    .vsub = { 1, 4, 4 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8 },
  },
  { /* YVU410 */
    .n_planes = 3,
    .bpp  = { 1, 1, 1 },
    .hsub = { 1, 4, 4 },
    .vsub = { 1, 4, 4 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8 },
  },
  { /* YUV411 */
    .n_planes = 3,
    .bpp  = { 1, 1, 1 },
    .hsub = { 1, 4, 4 },
    .vsub = { 1, 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8 },
  },
  { /* YVU411 */
    .n_planes = 3,
    .bpp  = { 1, 1, 1 },
    .hsub = { 1, 4, 4 },
    .vsub = { 1, 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8 },
  },
  { /* YUV420 */
    .n_planes = 3,
    .bpp  = { 1, 1, 1 },
    .hsub = { 1, 2, 2 },
    .vsub = { 1, 2, 2 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8 },
  },
  { /* YVU420 */
    .n_planes = 3,
    .bpp  = { 1, 1, 1 },
    .hsub = { 1, 2, 2 },
    .vsub = { 1, 2, 2 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8 },
  },
  { /* YUV422 */
    .n_planes = 3,
    .bpp  = { 1, 1, 1 },
    .hsub = { 1, 2, 2 },
    .vsub = { 1, 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8 },
  },
  { /* YVU422 */
    .n_planes = 3,
    .bpp  = { 1, 1, 1 },
    .hsub = { 1, 2, 2 },
    .vsub = { 1, 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8 },
  },
  { /* YUV444 */
    .n_planes = 3,
    .bpp  = { 1, 1, 1 },
    .hsub = { 1, 1, 1 },
    .vsub = { 1, 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8 },
  },
  { /* YVU444 */
    .n_planes = 3,
    .bpp  = { 1, 1, 1 },
    .hsub = { 1, 1, 1 },
    .vsub = { 1, 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8 },
  },
};

void
meta_multi_texture_format_get_bytes_per_pixel (MetaMultiTextureFormat format,
                                               uint8_t               *bpp_out)
{
  size_t i;

  g_return_if_fail (format < G_N_ELEMENTS (multi_format_table));

  for (i = 0; i < multi_format_table[format].n_planes; i++)
    bpp_out[i] = multi_format_table[format].bpp[i];
}

int
meta_multi_texture_format_get_n_planes (MetaMultiTextureFormat format)
{
  g_return_val_if_fail (format < G_N_ELEMENTS (multi_format_table), 0);

  return multi_format_table[format].n_planes;
}

void
meta_multi_texture_format_get_subsampling_factors (MetaMultiTextureFormat format,
                                                   uint8_t               *horizontal_factors,
                                                   uint8_t               *vertical_factors)
{
  size_t i;

  g_return_if_fail (format < G_N_ELEMENTS (multi_format_table));

  for (i = 0; i < multi_format_table[format].n_planes; i++)
    {
      horizontal_factors[i] = multi_format_table[format].hsub[i];
      vertical_factors[i] = multi_format_table[format].vsub[i];
    }
}

void
meta_multi_texture_format_get_subformats (MetaMultiTextureFormat format,
                                          CoglPixelFormat       *formats_out)
{
  size_t i;

  g_return_if_fail (format < G_N_ELEMENTS (multi_format_table));

  for (i = 0; i < multi_format_table[format].n_planes; i++)
    formats_out[i] = multi_format_table[format].subformats[i];
}
