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

#define _YUV_TO_RGB(res, y, u, v)                                \
    "vec4 " res ";\n"                                             \
    res ".r = (" y ") + 1.59765625 * (" v ");\n"                  \
    res ".g = (" y ") - 0.390625 * (" u ") - 0.8125 * (" v ");\n" \
    res ".b = (" y ") + 2.015625 * (" u ");\n"                    \
    res ".a = 1.0;\n"

/* Shader for a single YUV plane */
#define YUV_TO_RGB_FUNC "meta_yuv_to_rgba"
static const char yuv_to_rgb_shader[] =
    "vec4\n"
    YUV_TO_RGB_FUNC " (vec2 UV)\n"
    "{\n"
    "  vec4 orig_color = texture2D(cogl_sampler0, UV);\n"
    "  float y = 1.16438356 * (orig_color.r - 0.0625);\n"
    "  float u = orig_color.g - 0.5;\n"
    "  float v = orig_color.b - 0.5;\n"
       _YUV_TO_RGB ("color", "y", "u", "v")
    "  return color;\n"
    "}\n";

/* Shader for 1 Y-plane and 1 UV-plane */
#define Y_UV_TO_RGB_FUNC "meta_y_uv_to_rgba"
static const char y_uv_to_rgb_shader[] =
    "vec4\n"
    Y_UV_TO_RGB_FUNC "(vec2 UV)\n"
    "{\n"
    "  float y = 1.1640625 * (texture2D (cogl_sampler0, UV).x - 0.0625);\n"
    "  vec2 uv = texture2D (cogl_sampler1, UV).rg;\n"
    "  uv -= 0.5;\n"
    "  float u = uv.x;\n"
    "  float v = uv.y;\n"
       _YUV_TO_RGB ("color", "y", "u", "v")
    "  return color;\n"
    "}\n";

/* Shader for 1 Y-plane, 1 U-plane and 1 V-plane */
#define Y_U_V_TO_RGB_FUNC "meta_y_u_v_to_rgba"
static const char y_u_v_to_rgb_shader[] =
    "vec4\n"
    Y_U_V_TO_RGB_FUNC "(vec2 UV)\n"
    "{\n"
    "  float y = 1.16438356 * (texture2D(cogl_sampler0, UV).x - 0.0625);\n"
    "  float u = texture2D(cogl_sampler1, UV).x - 0.5;\n"
    "  float v = texture2D(cogl_sampler2, UV).x - 0.5;\n"
       _YUV_TO_RGB ("color", "y", "u", "v")
    "  return color;\n"
    "}\n";


typedef struct _MetaMultiTextureFormatInfo
{
  MetaMultiTextureFormat multi_format;
  const char *name;
  uint8_t n_planes;

  /* Per plane-information */
  uint8_t bpp[COGL_PIXEL_FORMAT_MAX_PLANES];        /* Bytes per pixel (without subsampling) */
  uint8_t hsub[COGL_PIXEL_FORMAT_MAX_PLANES];       /* horizontal subsampling                */
  uint8_t vsub[COGL_PIXEL_FORMAT_MAX_PLANES];       /* vertical subsampling                  */
  CoglPixelFormat subformats[COGL_PIXEL_FORMAT_MAX_PLANES]; /* influences how we deal with it on a GL level */

  /* Shaders */
  const char *rgb_shaderfunc;  /* Shader name to convert to RGBA (or NULL) */
  const char *rgb_shader;      /* Shader to convert to RGBA (or NULL)      */
} MetaMultiTextureFormatInfo;

/* NOTE: The actual enum values are used as the index, so you don't need to
 * loop over the table */
static MetaMultiTextureFormatInfo multi_format_table[] = {
  /* Simple */
  {
    .name = "",
    .n_planes = 1,
    .bpp  = { 4 },
    .hsub = { 1 },
    .vsub = { 1 },
    .subformats = { COGL_PIXEL_FORMAT_ANY },
    .rgb_shaderfunc = NULL,
    .rgb_shader = NULL,
  },
  /* Packed YUV */
  {
    .name = "YUYV",
    .n_planes = 1,
    .bpp  = { 4 },
    .hsub = { 1 },
    .vsub = { 1 },
    .subformats = { COGL_PIXEL_FORMAT_ARGB_8888 },
    .rgb_shaderfunc = YUV_TO_RGB_FUNC,
    .rgb_shader = yuv_to_rgb_shader,
  },
  {
    .name = "YVYU",
    .n_planes = 1,
    .bpp  = { 4 },
    .hsub = { 1 },
    .vsub = { 1 },
    .subformats = { COGL_PIXEL_FORMAT_ARGB_8888 },
    .rgb_shaderfunc = YUV_TO_RGB_FUNC,
    .rgb_shader = yuv_to_rgb_shader,
  },
  {
    .name = "UYVY",
    .n_planes = 1,
    .bpp  = { 4 },
    .hsub = { 1 },
    .vsub = { 1 },
    .subformats = { COGL_PIXEL_FORMAT_ARGB_8888 },
    .rgb_shaderfunc = YUV_TO_RGB_FUNC,
    .rgb_shader = yuv_to_rgb_shader,
  },
  {
    .name = "VYUY",
    .n_planes = 1,
    .bpp  = { 4 },
    .hsub = { 1 },
    .vsub = { 1 },
    .subformats = { COGL_PIXEL_FORMAT_ARGB_8888 },
    .rgb_shaderfunc = YUV_TO_RGB_FUNC,
    .rgb_shader = yuv_to_rgb_shader,
  },
  {
    .name = "AYUV",
    .n_planes = 1,
    .bpp  = { 4 },
    .hsub = { 1 },
    .vsub = { 1 },
    .subformats = { COGL_PIXEL_FORMAT_ARGB_8888 },
    .rgb_shaderfunc = YUV_TO_RGB_FUNC,
    .rgb_shader = yuv_to_rgb_shader,
  },
  /* 2 plane RGB + A */
  {
    .name = "XRGB8888_A8",
    .n_planes = 2,
    .bpp  = { 4, 1 },
    .hsub = { 1, 1 },
    .vsub = { 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_ARGB_8888, COGL_PIXEL_FORMAT_A_8 },
    .rgb_shaderfunc = NULL,
    .rgb_shader = NULL,
  },
  {
    .name = "XBGR8888_A8",
    .n_planes = 2,
    .bpp  = { 4, 1 },
    .hsub = { 1, 1 },
    .vsub = { 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_ABGR_8888, COGL_PIXEL_FORMAT_A_8 },
    .rgb_shaderfunc = NULL,
    .rgb_shader = NULL,
  },
  {
    .name = "RGBX8888_A8",
    .n_planes = 2,
    .bpp  = { 4, 1 },
    .hsub = { 1, 1 },
    .vsub = { 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_RGBA_8888, COGL_PIXEL_FORMAT_A_8 },
    .rgb_shaderfunc = NULL,
    .rgb_shader = NULL,
  },
  {
    .name = "BGRX8888_A8",
    .n_planes = 2,
    .bpp  = { 4, 1 },
    .hsub = { 1, 1 },
    .vsub = { 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_BGRA_8888, COGL_PIXEL_FORMAT_A_8 },
    .rgb_shaderfunc = NULL,
    .rgb_shader = NULL,
  },
  {
    .name = "RGB888_A8",
    .n_planes = 2,
    .bpp  = { 3, 1 },
    .hsub = { 1, 1 },
    .vsub = { 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_RGB_888, COGL_PIXEL_FORMAT_A_8 },
    .rgb_shaderfunc = NULL,
    .rgb_shader = NULL,
  },
  {
    .name = "BGR888_A8",
    .n_planes = 2,
    .bpp  = { 3, 1 },
    .hsub = { 1, 1 },
    .vsub = { 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_BGR_888, COGL_PIXEL_FORMAT_A_8 },
    .rgb_shaderfunc = NULL,
    .rgb_shader = NULL,
  },
  {
    .name = "RGB565_A8",
    .n_planes = 2,
    .bpp  = { 2, 1 },
    .hsub = { 1, 1 },
    .vsub = { 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_RGB_565, COGL_PIXEL_FORMAT_A_8 },
    .rgb_shaderfunc = NULL,
    .rgb_shader = NULL,
  },
  {
    .name = "BGR565_A8",
    .n_planes = 2,
    .bpp  = { 2, 1 },
    .hsub = { 1, 1 },
    .vsub = { 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_RGB_565, COGL_PIXEL_FORMAT_A_8 },
    .rgb_shaderfunc = NULL,
    .rgb_shader = NULL,
  },
  /* 2 plane YUV */
  {
    .name = "NV12",
    .n_planes = 2,
    .bpp  = { 1, 2 },
    .hsub = { 1, 2 },
    .vsub = { 1, 2 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_RG_88 },
    .rgb_shaderfunc = Y_UV_TO_RGB_FUNC,
    .rgb_shader = y_uv_to_rgb_shader,
  },
  {
    .name = "NV21",
    .n_planes = 2,
    .bpp  = { 1, 2 },
    .hsub = { 1, 2 },
    .vsub = { 1, 2 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_RG_88 },
    .rgb_shaderfunc = Y_UV_TO_RGB_FUNC,
    .rgb_shader = y_uv_to_rgb_shader,
  },
  {
    .name = "NV16",
    .n_planes = 2,
    .bpp  = { 1, 2 },
    .hsub = { 1, 2 },
    .vsub = { 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_RG_88 },
    .rgb_shaderfunc = Y_UV_TO_RGB_FUNC,
    .rgb_shader = y_uv_to_rgb_shader,
  },
  {
    .name = "NV61",
    .n_planes = 2,
    .bpp  = { 1, 2 },
    .hsub = { 1, 2 },
    .vsub = { 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_RG_88 },
    .rgb_shaderfunc = Y_UV_TO_RGB_FUNC,
    .rgb_shader = y_uv_to_rgb_shader,
  },
  {
    .name = "NV24",
    .n_planes = 2,
    .bpp  = { 1, 2 },
    .hsub = { 1, 1 },
    .vsub = { 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_RG_88 },
    .rgb_shaderfunc = Y_UV_TO_RGB_FUNC,
    .rgb_shader = y_uv_to_rgb_shader,
  },
  {
    .name = "NV42",
    .n_planes = 2,
    .bpp  = { 1, 2 },
    .hsub = { 1, 1 },
    .vsub = { 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_RG_88 },
    .rgb_shaderfunc = Y_UV_TO_RGB_FUNC,
    .rgb_shader = y_uv_to_rgb_shader,
  },
  /* 3 plane YUV */
  {
    .name = "YUV410",
    .n_planes = 3,
    .bpp  = { 1, 1, 1 },
    .hsub = { 1, 4, 4 },
    .vsub = { 1, 4, 4 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8 },
    .rgb_shaderfunc = Y_U_V_TO_RGB_FUNC,
    .rgb_shader = y_u_v_to_rgb_shader,
  },
  {
    .name = "YVU410",
    .n_planes = 3,
    .bpp  = { 1, 1, 1 },
    .hsub = { 1, 4, 4 },
    .vsub = { 1, 4, 4 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8 },
    .rgb_shaderfunc = Y_U_V_TO_RGB_FUNC,
    .rgb_shader = y_u_v_to_rgb_shader,
  },
  {
    .name = "YUV411",
    .n_planes = 3,
    .bpp  = { 1, 1, 1 },
    .hsub = { 1, 4, 4 },
    .vsub = { 1, 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8 },
    .rgb_shaderfunc = Y_U_V_TO_RGB_FUNC,
    .rgb_shader = y_u_v_to_rgb_shader,
  },
  {
    .name = "YVU411",
    .n_planes = 3,
    .bpp  = { 1, 1, 1 },
    .hsub = { 1, 4, 4 },
    .vsub = { 1, 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8 },
    .rgb_shaderfunc = Y_U_V_TO_RGB_FUNC,
    .rgb_shader = y_u_v_to_rgb_shader,
  },
  {
    .name = "YUV420",
    .n_planes = 3,
    .bpp  = { 1, 1, 1 },
    .hsub = { 1, 2, 2 },
    .vsub = { 1, 2, 2 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8 },
    .rgb_shaderfunc = Y_U_V_TO_RGB_FUNC,
    .rgb_shader = y_u_v_to_rgb_shader,
  },
  {
    .name = "YVU420",
    .n_planes = 3,
    .bpp  = { 1, 1, 1 },
    .hsub = { 1, 2, 2 },
    .vsub = { 1, 2, 2 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8 },
    .rgb_shaderfunc = Y_U_V_TO_RGB_FUNC,
    .rgb_shader = y_u_v_to_rgb_shader,
  },
  {
    .name = "YUV422",
    .n_planes = 3,
    .bpp  = { 1, 1, 1 },
    .hsub = { 1, 2, 2 },
    .vsub = { 1, 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8 },
    .rgb_shaderfunc = Y_U_V_TO_RGB_FUNC,
    .rgb_shader = y_u_v_to_rgb_shader,
  },
  {
    .name = "YVU422",
    .n_planes = 3,
    .bpp  = { 1, 1, 1 },
    .hsub = { 1, 2, 2 },
    .vsub = { 1, 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8 },
    .rgb_shaderfunc = Y_U_V_TO_RGB_FUNC,
    .rgb_shader = y_u_v_to_rgb_shader,
  },
  {
    .name = "YUV444",
    .n_planes = 3,
    .bpp  = { 1, 1, 1 },
    .hsub = { 1, 1, 1 },
    .vsub = { 1, 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8 },
    .rgb_shaderfunc = Y_U_V_TO_RGB_FUNC,
    .rgb_shader = y_u_v_to_rgb_shader,
  },
  {
    .name = "YVU444",
    .n_planes = 3,
    .bpp  = { 1, 1, 1 },
    .hsub = { 1, 1, 1 },
    .vsub = { 1, 1, 1 },
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8 },
    .rgb_shaderfunc = Y_U_V_TO_RGB_FUNC,
    .rgb_shader = y_u_v_to_rgb_shader,
  },
};

const char *
meta_multi_texture_format_to_string (MetaMultiTextureFormat format)
{
  g_return_val_if_fail (format < G_N_ELEMENTS (multi_format_table), NULL);

  return multi_format_table[format].name;
}

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

gboolean
meta_multi_texture_format_needs_shaders (MetaMultiTextureFormat format)
{
  g_return_val_if_fail (format < G_N_ELEMENTS (multi_format_table), FALSE);

  return multi_format_table[format].rgb_shaderfunc != NULL;
}

gboolean
meta_multi_texture_format_get_snippets (MetaMultiTextureFormat format,
                                        CoglSnippet          **vertex_snippet,
                                        CoglSnippet          **fragment_snippet,
                                        CoglSnippet          **layer_snippet)
{
  const char *shader_func;
  const char *shader_impl;
  g_autofree char *layer_hook = NULL;

  g_return_val_if_fail (format < G_N_ELEMENTS (multi_format_table), FALSE);

  /* Get the function name; bail out early if we don't need a shader */
  shader_func = multi_format_table[format].rgb_shaderfunc;
  if (shader_func == NULL)
    return FALSE;

  shader_impl = multi_format_table[format].rgb_shader;

  /* Make sure we actually call the function */
  layer_hook = g_strdup_printf ("cogl_layer = %s(cogl_tex_coord0_in.st);\n",
                                shader_func);

  if (vertex_snippet)
    *vertex_snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_VERTEX_GLOBALS,
                                        shader_impl,
                                        NULL);

  if (fragment_snippet)
    *fragment_snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT_GLOBALS,
                                          shader_impl,
                                          NULL);

  if (layer_snippet)
    *layer_snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_LAYER_FRAGMENT,
                                       NULL,
                                       layer_hook);

  return TRUE;
}
