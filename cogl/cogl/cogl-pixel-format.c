/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009,2010 Intel Corporation.
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
 */

#include "cogl-config.h"

#include <string.h>
#include <math.h>
#include <stdlib.h>

#include "cogl-pixel-format.h"
#include "cogl-texture.h"

/* An entry to map CoglPixelFormats to their respective properties */
typedef struct _CoglPixelFormatInfo
{
  CoglPixelFormat cogl_format;
  const char *format_str;
  int aligned;                               /* Is aligned? (bool; -1 if n/a) */
  uint8_t n_planes;

  /* Per plane-information */
  uint8_t bpp[COGL_PIXEL_FORMAT_MAX_PLANES];        /* Bytes per pixel        */
  uint8_t hsub[COGL_PIXEL_FORMAT_MAX_PLANES];       /* horizontal subsampling */
  uint8_t vsub[COGL_PIXEL_FORMAT_MAX_PLANES];       /* vertical subsampling   */
  CoglPixelFormat subformats[COGL_PIXEL_FORMAT_MAX_PLANES]; /* how to upload to GL    */
} CoglPixelFormatInfo;

static const CoglPixelFormatInfo format_info_table[] = {
  {
    .cogl_format = COGL_PIXEL_FORMAT_ANY,
    .format_str = "ANY",
    .n_planes = 1,
    .aligned = -1,
    .bpp = { 0 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_ANY }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_A_8,
    .format_str = "A_8",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 1 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_A_8 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_RGB_565,
    .format_str = "RGB_565",
    .n_planes = 1,
    .aligned = 0,
    .bpp = { 2 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_RGB_565 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_RGBA_4444,
    .format_str = "RGBA_4444",
    .n_planes = 1,
    .aligned = 0,
    .bpp = { 2 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_RGBA_4444 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_RGBA_5551,
    .format_str = "RGBA_5551",
    .n_planes = 1,
    .aligned = 0,
    .bpp = { 2 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_RGBA_5551 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_YUV,
    .format_str = "YUV",
    .n_planes = 1,
    .aligned = -1,
    .bpp = { 0 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_YUV }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_R_8,
    .format_str = "R_8",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 1 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_R_8 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_RG_88,
    .format_str = "RG_88",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 2 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_RG_88 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_RGB_888,
    .format_str = "RGB_888",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 3 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_RGB_888 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_BGR_888,
    .format_str = "BGR_888",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 3 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_BGR_888 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_RGBA_8888,
    .format_str = "RGBA_8888",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 4 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_RGBA_8888 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_BGRA_8888,
    .format_str = "BGRA_8888",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 4 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_BGRA_8888 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_ARGB_8888,
    .format_str = "ARGB_8888",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 4 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_ARGB_8888 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_ABGR_8888,
    .format_str = "ABGR_8888",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 4 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_ABGR_8888 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_RGBA_1010102,
    .format_str = "RGBA_1010102",
    .n_planes = 1,
    .aligned = 0,
    .bpp = { 4 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_RGBA_1010102 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_BGRA_1010102,
    .format_str = "BGRA_1010102",
    .n_planes = 1,
    .aligned = 0,
    .bpp = { 4 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_BGRA_1010102 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_ARGB_2101010,
    .format_str = "ARGB_2101010",
    .n_planes = 1,
    .aligned = 0,
    .bpp = { 4 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_ARGB_2101010 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_ABGR_2101010,
    .format_str = "ABGR_2101010",
    .n_planes = 1,
    .aligned = 0,
    .bpp = { 4 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_ABGR_2101010 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_RGBA_8888_PRE,
    .format_str = "RGBA_8888_PRE",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 4 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_RGBA_8888_PRE }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_BGRA_8888_PRE,
    .format_str = "BGRA_8888_PRE",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 4 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_BGRA_8888_PRE }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_ARGB_8888_PRE,
    .format_str = "ARGB_8888_PRE",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 4 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_ARGB_8888_PRE }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_ABGR_8888_PRE,
    .format_str = "ABGR_8888_PRE",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 4 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_ABGR_8888_PRE }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_RGBA_4444_PRE,
    .format_str = "RGBA_4444_PRE",
    .n_planes = 1,
    .aligned = 0,
    .bpp = { 2 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_RGBA_4444_PRE }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_RGBA_5551_PRE,
    .format_str = "RGBA_5551_PRE",
    .n_planes = 1,
    .aligned = 0,
    .bpp = { 2 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_RGBA_5551_PRE }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_RGBA_1010102_PRE,
    .format_str = "RGBA_1010102_PRE",
    .n_planes = 1,
    .aligned = 0,
    .bpp = { 4 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_RGBA_1010102_PRE }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_BGRA_1010102_PRE,
    .format_str = "BGRA_1010102_PRE",
    .n_planes = 1,
    .aligned = 0,
    .bpp = { 4 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_BGRA_1010102_PRE }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_ARGB_2101010_PRE,
    .format_str = "ARGB_2101010_PRE",
    .n_planes = 1,
    .aligned = 0,
    .bpp = { 4 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_ARGB_2101010_PRE }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_ABGR_2101010_PRE,
    .format_str = "ABGR_2101010_PRE",
    .n_planes = 1,
    .aligned = 0,
    .bpp = { 4 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_ABGR_2101010_PRE }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_DEPTH_16,
    .format_str = "DEPTH_16",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 2 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_DEPTH_16 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_DEPTH_32,
    .format_str = "DEPTH_32",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 4 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_DEPTH_32 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_DEPTH_24_STENCIL_8,
    .format_str = "DEPTH_24_STENCIL_8",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 4 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_DEPTH_24_STENCIL_8 }
  },
  /* Packed YUV */
  {
    .cogl_format = COGL_PIXEL_FORMAT_YUYV,
    .format_str = "YUYV",
    .n_planes = 2,
    .aligned = 0,
    .bpp = { 4 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_YUYV, COGL_PIXEL_FORMAT_YUYV }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_YVYU,
    .format_str = "YVYU",
    .n_planes = 2,
    .aligned = 0,
    .bpp = { 4 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_YVYU, COGL_PIXEL_FORMAT_YVYU }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_UYVY,
    .format_str = "UYVY",
    .n_planes = 2,
    .aligned = 0,
    .bpp = { 4 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_UYVY, COGL_PIXEL_FORMAT_UYVY }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_VYUY,
    .format_str = "VYUY",
    .n_planes = 2,
    .aligned = 0,
    .bpp = { 4 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_VYUY, COGL_PIXEL_FORMAT_VYUY }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_AYUV,
    .format_str = "AYUV",
    .n_planes = 2,
    .aligned = 0,
    .bpp = { 4 },
    .hsub = { 1, 0, 0, 0 },
    .vsub = { 1, 0, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_AYUV, COGL_PIXEL_FORMAT_AYUV }
  },
  /* 2 plane RGB + A */
  {
    .cogl_format = COGL_PIXEL_FORMAT_XRGB8888_A8,
    .format_str = "XRGB8888_A8",
    .n_planes = 2,
    .aligned = 0,
    .bpp = { 4, 1 },
    .hsub = { 1, 1, 0, 0 },
    .vsub = { 1, 1, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_ARGB_8888, COGL_PIXEL_FORMAT_A_8 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_XBGR8888_A8,
    .format_str = "XBGR8888_A8",
    .n_planes = 2,
    .aligned = 0,
    .bpp = { 4, 1 },
    .hsub = { 1, 1, 0, 0 },
    .vsub = { 1, 1, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_ABGR_8888, COGL_PIXEL_FORMAT_A_8 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_RGBX8888_A8,
    .format_str = "RGBX8888_A8",
    .n_planes = 2,
    .aligned = 0,
    .bpp = { 4, 1 },
    .hsub = { 1, 1, 0, 0 },
    .vsub = { 1, 1, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_RGBA_8888, COGL_PIXEL_FORMAT_A_8 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_BGRX8888_A8,
    .format_str = "BGRX8888_A8",
    .n_planes = 2,
    .aligned = 0,
    .bpp = { 4, 1 },
    .hsub = { 1, 1, 0, 0 },
    .vsub = { 1, 1, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_BGRA_8888, COGL_PIXEL_FORMAT_A_8 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_RGB888_A8,
    .format_str = "RGB888_A8",
    .n_planes = 2,
    .aligned = 0,
    .bpp = { 3, 1 },
    .hsub = { 1, 1, 0, 0 },
    .vsub = { 1, 1, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_RGB_888, COGL_PIXEL_FORMAT_A_8 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_BGR888_A8,
    .format_str = "BGR888_A8",
    .n_planes = 2,
    .aligned = 0,
    .bpp = { 3, 1 },
    .hsub = { 1, 1, 0, 0 },
    .vsub = { 1, 1, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_BGR_888, COGL_PIXEL_FORMAT_A_8 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_RGB565_A8,
    .format_str = "RGB565_A8",
    .n_planes = 2,
    .aligned = 0,
    .bpp = { 2, 1 },
    .hsub = { 1, 1, 0, 0 },
    .vsub = { 1, 1, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_RGB_565, COGL_PIXEL_FORMAT_A_8 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_BGR565_A8,
    .format_str = "BGR565_A8",
    .n_planes = 2,
    .aligned = 0,
    .bpp = { 2, 1 },
    .hsub = { 1, 1, 0, 0 },
    .vsub = { 1, 1, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_RGB_565, COGL_PIXEL_FORMAT_A_8 }
  },
  /* 2 plane YUV */
  {
    .cogl_format = COGL_PIXEL_FORMAT_NV12,
    .format_str = "NV12",
    .n_planes = 2,
    .aligned = 0,
    .bpp = { 1, 2 },
    .hsub = { 1, 2, 0, 0 },
    .vsub = { 1, 2, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_RG_88 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_NV21,
    .format_str = "NV21",
    .n_planes = 2,
    .aligned = 0,
    .bpp = { 1, 2 },
    .hsub = { 1, 2, 0, 0 },
    .vsub = { 1, 2, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_RG_88 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_NV16,
    .format_str = "NV16",
    .n_planes = 2,
    .aligned = 0,
    .bpp = { 1, 2 },
    .hsub = { 1, 2, 0, 0 },
    .vsub = { 1, 1, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_RG_88 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_NV61,
    .format_str = "NV61",
    .n_planes = 2,
    .aligned = 0,
    .bpp = { 1, 2 },
    .hsub = { 1, 2, 0, 0 },
    .vsub = { 1, 1, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_RG_88 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_NV24,
    .format_str = "NV24",
    .n_planes = 2,
    .aligned = 0,
    .bpp = { 1, 2 },
    .hsub = { 1, 1, 0, 0 },
    .vsub = { 1, 1, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_RG_88 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_NV42,
    .format_str = "NV42",
    .n_planes = 2,
    .aligned = 0,
    .bpp = { 1, 2 },
    .hsub = { 1, 1, 0, 0 },
    .vsub = { 1, 1, 0, 0 },
    .subformats = { COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_RG_88 }
  },
  /* 3 plane YUV */
  {
    .cogl_format = COGL_PIXEL_FORMAT_YUV410,
    .format_str = "YUV410",
    .n_planes = 3,
    .aligned = 0,
    .bpp = { 1, 1, 1 },
    .hsub = { 1, 4, 4, 0 },
    .vsub = { 1, 4, 4, 0 },
    .subformats = { COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_R_8 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_YVU410,
    .format_str = "YVU410",
    .n_planes = 3,
    .aligned = 0,
    .bpp = { 1, 1, 1 },
    .hsub = { 1, 4, 4, 0 },
    .vsub = { 1, 4, 4, 0 },
    .subformats = { COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_R_8 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_YUV411,
    .format_str = "YUV411",
    .n_planes = 3,
    .aligned = 0,
    .bpp = { 1, 1, 1 },
    .hsub = { 1, 4, 4, 0 },
    .vsub = { 1, 1, 1, 0 },
    .subformats = { COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_R_8 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_YVU411,
    .format_str = "YVU411",
    .n_planes = 3,
    .aligned = 0,
    .bpp = { 1, 1, 1 },
    .hsub = { 1, 4, 4, 0 },
    .vsub = { 1, 1, 1, 0 },
    .subformats = { COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_R_8 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_YUV420,
    .format_str = "YUV420",
    .n_planes = 3,
    .aligned = 0,
    .bpp = { 1, 1, 1 },
    .hsub = { 1, 2, 2, 0 },
    .vsub = { 1, 2, 2, 0 },
    .subformats = { COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_R_8 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_YVU420,
    .format_str = "YVU420",
    .n_planes = 3,
    .aligned = 0,
    .bpp = { 1, 1, 1 },
    .hsub = { 1, 2, 2, 0 },
    .vsub = { 1, 2, 2, 0 },
    .subformats = { COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_R_8 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_YUV422,
    .format_str = "YUV422",
    .n_planes = 3,
    .aligned = 0,
    .bpp = { 1, 1, 1 },
    .hsub = { 1, 2, 2, 0 },
    .vsub = { 1, 1, 1, 0 },
    .subformats = { COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_R_8 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_YVU422,
    .format_str = "YVU422",
    .n_planes = 3,
    .aligned = 0,
    .bpp = { 1, 1, 1 },
    .hsub = { 1, 2, 2, 0 },
    .vsub = { 1, 1, 1, 0 },
    .subformats = { COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_R_8 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_YUV444,
    .format_str = "YUV444",
    .n_planes = 3,
    .aligned = 0,
    .bpp = { 1, 1, 1 },
    .hsub = { 1, 1, 1, 0 },
    .vsub = { 1, 1, 1, 0 },
    .subformats = { COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_R_8 }
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_YVU444,
    .format_str = "YVU444",
    .n_planes = 3,
    .aligned = 0,
    .bpp = { 1, 1, 1 },
    .hsub = { 1, 1, 1, 0 },
    .vsub = { 1, 1, 1, 0 },
    .subformats = { COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_R_8 }
  },
};

/*
 * Returns the number of bytes-per-pixel of a given format. The bpp
 * can be extracted from the least significant nibble of the pixel
 * format (see CoglPixelFormat).
 *
 * The mapping is the following (see discussion on bug #660188):
 *
 * 0     = undefined
 * 1, 8  = 1 bpp (e.g. A_8, G_8)
 * 2     = 3 bpp, aligned (e.g. 888)
 * 3     = 4 bpp, aligned (e.g. 8888)
 * 4-6   = 2 bpp, not aligned (e.g. 565, 4444, 5551)
 * 7     = undefined yuv
 * 9     = 2 bpp, aligned
 * 10     = undefined
 * 11     = undefined
 * 12    = 3 bpp, not aligned
 * 13    = 4 bpp, not aligned (e.g. 2101010)
 * 14-15 = undefined
 */
/* FIXME: this needs to be changed in all places this is called */
uint8_t
_cogl_pixel_format_get_bytes_per_pixel (CoglPixelFormat format)
{
  size_t i;

  for (i = 0; i < G_N_ELEMENTS (format_info_table); i++)
    {
      if (format_info_table[i].cogl_format == format)
        return format_info_table[i].bpp[0];
    }

  g_assert_not_reached ();
}

/*
 * XXX document.
 *
 * XXX lol, this is even per macropixel, not per pixel :D
 */
/* FIXME: this needs to be merged with get_bytes_per_pixel */
void
cogl_pixel_format_get_bytes_per_pixel_ (CoglPixelFormat format, uint8_t *bpp_out)
{
  size_t i, j;

  for (i = 0; i < G_N_ELEMENTS (format_info_table); i++)
    {
      if (format_info_table[i].cogl_format != format)
        continue;

      for (j = 0; j < format_info_table[i].n_planes; j++)
        bpp_out[j] = format_info_table[i].bpp[j];
      return;
    }

  g_assert_not_reached ();
}

/* Note: this also refers to the mapping defined above for
 * _cogl_pixel_format_get_bytes_per_pixel() */
gboolean
_cogl_pixel_format_is_endian_dependant (CoglPixelFormat format)
{
  int aligned = -1;
  size_t i;

  /* NB: currently checking whether the format components are aligned
   * or not determines whether the format is endian dependent or not.
   * In the future though we might consider adding formats with
   * aligned components that are also endian independant. */

  for (i = 0; i < G_N_ELEMENTS (format_info_table); i++)
    {
      if (format_info_table[i].cogl_format == format)
        {
          aligned = format_info_table[i].aligned;
          break;
        }
    }

  g_return_val_if_fail (aligned != -1, FALSE);

  return aligned;
}

guint
cogl_pixel_format_get_n_planes (CoglPixelFormat format)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (format_info_table); i++)
    {
      if (format_info_table[i].cogl_format == format)
        return format_info_table[i].n_planes;
    }

  g_assert_not_reached ();
}

void
cogl_pixel_format_get_subsampling_factors (CoglPixelFormat format,
                                           uint8_t *horizontal_factors,
                                           uint8_t *vertical_factors)
{
  guint i, j;

  for (i = 0; i < G_N_ELEMENTS (format_info_table); i++)
    {
      if (format_info_table[i].cogl_format != format)
        continue;

      for (j = 0; j < format_info_table[i].n_planes; j++)
        {
          horizontal_factors[j] = format_info_table[i].hsub[j];
          vertical_factors[j] = format_info_table[i].vsub[j];
        }

      return;
    }

  g_assert_not_reached ();
}

const char *
cogl_pixel_format_to_string (CoglPixelFormat format)
{
  size_t i;

  for (i = 0; i < G_N_ELEMENTS (format_info_table); i++)
    {
      if (format_info_table[i].cogl_format == format)
        return format_info_table[i].format_str;
    }

  g_assert_not_reached ();
}

void
cogl_pixel_format_get_subformats (CoglPixelFormat  format,
                                  CoglPixelFormat *formats_out)
{
  size_t i, j;

  for (i = 0; i < G_N_ELEMENTS (format_info_table); i++)
    {
      if (format_info_table[i].cogl_format != format)
        continue;

      for (j = 0; j < format_info_table[i].n_planes; j++)
        formats_out[j] = format_info_table[i].subformats[j];
      return;
    }

  g_assert_not_reached ();
}

void
cogl_pixel_format_get_cogl_components (CoglPixelFormat  format,
                                       guint           *components_out)
{
  switch (format)
    {
    case COGL_PIXEL_FORMAT_NV12:
      components_out[0] = COGL_TEXTURE_COMPONENTS_R;
      components_out[1] = COGL_TEXTURE_COMPONENTS_RG;
      break;
    case COGL_PIXEL_FORMAT_NV21:
      components_out[0] = COGL_TEXTURE_COMPONENTS_R;
      components_out[1] = COGL_TEXTURE_COMPONENTS_RG;
      break;
    case COGL_PIXEL_FORMAT_YUV422:
      components_out[0] = COGL_TEXTURE_COMPONENTS_R;
      components_out[1] = COGL_TEXTURE_COMPONENTS_R;
      components_out[2] = COGL_TEXTURE_COMPONENTS_R;
      break;
    case COGL_PIXEL_FORMAT_YUV444:
      components_out[0] = COGL_TEXTURE_COMPONENTS_R;
      components_out[1] = COGL_TEXTURE_COMPONENTS_R;
      components_out[2] = COGL_TEXTURE_COMPONENTS_R;
      break;
    default:
      components_out[0] = COGL_TEXTURE_COMPONENTS_RGBA;
    }
}
