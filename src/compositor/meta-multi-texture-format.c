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

#include "config.h"

#include "compositor/meta-multi-texture-format-private.h"

#include <cogl/cogl.h>
#include <stdlib.h>
#include <string.h>

static const char *shader_global_conversions =
  "vec4 yuv_to_rgb(vec4 yuva)                                               \n"
  "{                                                                        \n"
  "  vec4 res;                                                              \n"
  "  float Y = 1.16438356 * (yuva.x - 0.0625);                              \n"
  "  float su = yuva.y - 0.5;                                               \n"
  "  float sv = yuva.z - 0.5;                                               \n"
  "  res.r = Y                   + 1.59602678 * sv;                         \n"
  "  res.g = Y - 0.39176229 * su - 0.81296764 * sv;                         \n"
  "  res.b = Y + 2.01723214 * su;                                           \n"
  "  res.rgb *= yuva.w;                                                     \n"
  "  res.a = yuva.w;                                                        \n"
  "  return res;                                                            \n"
  "}                                                                        \n";

static const char rgba_shader[] =
  "cogl_color_out =                                                         \n"
  "  texture2D(cogl_sampler0, cogl_tex_coord0_in.st) * cogl_color_in.a;     \n";

/* Shader for a single YUV plane */
static const char y_xuxv_shader[] =
  "vec4 yuva = vec4(0.0, 0.0, 0.0, cogl_color_in.a);                        \n"
  "yuva.x = texture2D(cogl_sampler0, cogl_tex_coord0_in.st).x;              \n"
  "yuva.yz = texture2D(cogl_sampler1, cogl_tex_coord1_in.st).ga;            \n"
  "cogl_color_out = yuv_to_rgb(yuva);                                       \n";

/* Shader for 1 Y-plane and 1 UV-plane */
static const char nv12_shader[] =
  "vec4 yuva = vec4(0.0, 0.0, 0.0, cogl_color_in.a);                        \n"
  "yuva.x = texture2D(cogl_sampler0, cogl_tex_coord0_in.st).x;              \n"
  "yuva.yz = texture2D(cogl_sampler1, cogl_tex_coord1_in.st).rg;            \n"
  "cogl_color_out = yuv_to_rgb(yuva);                                       \n";

/* Shader for 1 Y-plane, 1 U-plane and 1 V-plane */
static const char yuv420_shader[] =
  "vec4 yuva = vec4(0.0, 0.0, 0.0, cogl_color_in.a);                        \n"
  "yuva.x = texture2D(cogl_sampler0, cogl_tex_coord0_in.st).x;              \n"
  "yuva.y = texture2D(cogl_sampler1, cogl_tex_coord1_in.st).x;              \n"
  "yuva.z = texture2D(cogl_sampler2, cogl_tex_coord2_in.st).x;              \n"
  "cogl_color_out = yuv_to_rgb(yuva);                                       \n";

typedef struct _MetaMultiTextureFormatInfo
{
  MetaMultiTextureFormat multi_format;
  const char *name;
  uint8_t n_planes;

  /* Per plane-information */
  CoglPixelFormat subformats[COGL_PIXEL_FORMAT_MAX_PLANES]; /* influences how we deal with it on a GL level */
  uint8_t plane_indices[COGL_PIXEL_FORMAT_MAX_PLANES]; /* source plane */
  uint8_t hsub[COGL_PIXEL_FORMAT_MAX_PLANES]; /* horizontal subsampling */
  uint8_t vsub[COGL_PIXEL_FORMAT_MAX_PLANES]; /* vertical subsampling */

  /* Shaders */
  const char *rgb_shader;  /* Shader to convert to RGBA (or NULL) */
} MetaMultiTextureFormatInfo;

/* NOTE: The actual enum values are used as the index, so you don't need to
 * loop over the table */
static MetaMultiTextureFormatInfo multi_format_table[] = {
  /* Invalid */
  {},
  /* Simple */
  {
    .name = "",
    .n_planes = 1,
    .subformats = { COGL_PIXEL_FORMAT_ANY },
    .plane_indices = { 0 },
    .hsub = { 1 },
    .vsub = { 1 },
    .rgb_shader = rgba_shader,
  },
  /* Packed YUV */
  {
    .name = "YUYV",
    .n_planes = 2,
    .subformats = { COGL_PIXEL_FORMAT_RG_88, COGL_PIXEL_FORMAT_BGRA_8888_PRE },
    .plane_indices = { 0, 0 },
    .hsub = { 1, 2 },
    .vsub = { 1, 1 },
    .rgb_shader = y_xuxv_shader,
  },
  /* 2 plane YUV */
  {
    .name = "NV12",
    .n_planes = 2,
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_RG_88 },
    .plane_indices = { 0, 1 },
    .hsub = { 1, 2 },
    .vsub = { 1, 2 },
    .rgb_shader = nv12_shader,
  },
  /* 3 plane YUV */
  {
    .name = "YUV420",
    .n_planes = 3,
    .subformats = { COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8, COGL_PIXEL_FORMAT_G_8 },
    .plane_indices = { 0, 1, 2 },
    .hsub = { 1, 2, 2 },
    .vsub = { 1, 2, 2 },
    .rgb_shader = yuv420_shader,
  },
};

const char *
meta_multi_texture_format_to_string (MetaMultiTextureFormat format)
{
  g_return_val_if_fail (format < G_N_ELEMENTS (multi_format_table), NULL);

  return multi_format_table[format].name;
}

int
meta_multi_texture_format_get_n_planes (MetaMultiTextureFormat format)
{
  g_return_val_if_fail (format < G_N_ELEMENTS (multi_format_table), 0);

  return multi_format_table[format].n_planes;
}

void
meta_multi_texture_format_get_subformats (MetaMultiTextureFormat  format,
                                          CoglPixelFormat        *formats_out)
{
  size_t i;

  g_return_if_fail (format < G_N_ELEMENTS (multi_format_table));

  for (i = 0; i < multi_format_table[format].n_planes; i++)
    formats_out[i] = multi_format_table[format].subformats[i];
}

void
meta_multi_texture_format_get_plane_indices (MetaMultiTextureFormat  format,
                                             uint8_t                *plane_indices)
{
  size_t i;

  g_return_if_fail (format < G_N_ELEMENTS (multi_format_table));

  for (i = 0; i < multi_format_table[format].n_planes; i++)
    plane_indices[i] = multi_format_table[format].plane_indices[i];
}

void
meta_multi_texture_format_get_subsampling_factors (MetaMultiTextureFormat  format,
                                                   uint8_t                *horizontal_factors,
                                                   uint8_t                *vertical_factors)
{
  size_t i;

  g_return_if_fail (format < G_N_ELEMENTS (multi_format_table));

  for (i = 0; i < multi_format_table[format].n_planes; i++)
    {
      horizontal_factors[i] = multi_format_table[format].hsub[i];
      vertical_factors[i] = multi_format_table[format].vsub[i];
    }
}

gboolean
meta_multi_texture_format_get_snippets (MetaMultiTextureFormat   format,
                                        CoglSnippet            **fragment_globals_snippet,
                                        CoglSnippet            **fragment_snippet)
{
  g_return_val_if_fail (format < G_N_ELEMENTS (multi_format_table), FALSE);

  if (multi_format_table[format].rgb_shader == NULL)
    return FALSE;

  if (fragment_globals_snippet)
    {
      *fragment_globals_snippet =
        cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT_GLOBALS,
                          shader_global_conversions,
                          NULL);
    }

  if (fragment_snippet)
    {
      *fragment_snippet =
        cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                          NULL,
                          multi_format_table[format].rgb_shader);
    }

  return TRUE;
}
