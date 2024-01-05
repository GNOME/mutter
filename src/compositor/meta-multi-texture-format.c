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
#include "config.h"

#include "compositor/meta-multi-texture-format-private.h"

#include <stdlib.h>
#include <string.h>

#include "cogl/cogl.h"

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
  "yuva.yz = texture2D(cogl_sampler1, cogl_tex_coord0_in.st).ga;            \n"
  "cogl_color_out = yuv_to_rgb(yuva);                                       \n";

/* Shader for 1 Y-plane and 1 UV-plane */
static const char y_uv_shader[] =
  "vec4 yuva = vec4(0.0, 0.0, 0.0, cogl_color_in.a);                        \n"
  "yuva.x = texture2D(cogl_sampler0, cogl_tex_coord0_in.st).x;              \n"
  "yuva.yz = texture2D(cogl_sampler1, cogl_tex_coord0_in.st).rg;            \n"
  "cogl_color_out = yuv_to_rgb(yuva);                                       \n";

/* Shader for 1 Y-plane, 1 U-plane and 1 V-plane */
static const char y_u_v_shader[] =
  "vec4 yuva = vec4(0.0, 0.0, 0.0, cogl_color_in.a);                        \n"
  "yuva.x = texture2D(cogl_sampler0, cogl_tex_coord0_in.st).x;              \n"
  "yuva.y = texture2D(cogl_sampler1, cogl_tex_coord0_in.st).x;              \n"
  "yuva.z = texture2D(cogl_sampler2, cogl_tex_coord0_in.st).x;              \n"
  "cogl_color_out = yuv_to_rgb(yuva);                                       \n";

typedef struct _MetaMultiTextureFormatFullInfo
{
  MetaMultiTextureFormatInfo info;

  /* Name */
  const char *name;
  /* Shader to convert to RGBA (or NULL) */
  const char *rgb_shader;
  /* Cached snippet */
  GOnce snippet_once;
} MetaMultiTextureFormatFullInfo;

/* NOTE: The actual enum values are used as the index, so you don't need to
 * loop over the table */
static MetaMultiTextureFormatFullInfo multi_format_table[] = {
  /* Invalid */
  [META_MULTI_TEXTURE_FORMAT_INVALID] = {},
  /* Simple */
  [META_MULTI_TEXTURE_FORMAT_SIMPLE] = {
    .name = "",
    .rgb_shader = rgba_shader,
    .snippet_once = G_ONCE_INIT,
    .info = {
      .n_planes = 1,
      .subformats = { COGL_PIXEL_FORMAT_ANY },
      .plane_indices = { 0 },
      .hsub = { 1 },
      .vsub = { 1 },
    },
  },
  /* Packed YUV */
  [META_MULTI_TEXTURE_FORMAT_YUYV] = {
    .name = "YUYV",
    .rgb_shader = y_xuxv_shader,
    .snippet_once = G_ONCE_INIT,
    .info = {
      .n_planes = 2,
      .subformats = { COGL_PIXEL_FORMAT_RG_88, COGL_PIXEL_FORMAT_BGRA_8888_PRE },
      .plane_indices = { 0, 0 },
      .hsub = { 1, 2 },
      .vsub = { 1, 1 },
    },
  },
  /* 2 plane YUV */
  [META_MULTI_TEXTURE_FORMAT_NV12] = {
    .name = "NV12",
    .rgb_shader = y_uv_shader,
    .snippet_once = G_ONCE_INIT,
    .info = {
      .n_planes = 2,
      .subformats = { COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_RG_88 },
      .plane_indices = { 0, 1 },
      .hsub = { 1, 2 },
      .vsub = { 1, 2 },
    },
  },
  [META_MULTI_TEXTURE_FORMAT_P010] = {
    .name = "P010",
    .rgb_shader = y_uv_shader,
    .snippet_once = G_ONCE_INIT,
    .info = {
      .n_planes = 2,
      .subformats = { COGL_PIXEL_FORMAT_R_16, COGL_PIXEL_FORMAT_RG_1616 },
      .plane_indices = { 0, 1 },
      .hsub = { 1, 2 },
      .vsub = { 1, 2 },
    },
  },
  /* 3 plane YUV */
  [META_MULTI_TEXTURE_FORMAT_YUV420] = {
    .name = "YUV420",
    .rgb_shader = y_u_v_shader,
    .snippet_once = G_ONCE_INIT,
    .info = {
      .n_planes = 3,
      .subformats = { COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_R_8 },
      .plane_indices = { 0, 1, 2 },
      .hsub = { 1, 2, 2 },
      .vsub = { 1, 2, 2 },
    },
  },
};

const char *
meta_multi_texture_format_to_string (MetaMultiTextureFormat format)
{
  g_return_val_if_fail (format < G_N_ELEMENTS (multi_format_table), NULL);

  return multi_format_table[format].name;
}

const MetaMultiTextureFormatInfo *
meta_multi_texture_format_get_info (MetaMultiTextureFormat format)
{
  g_return_val_if_fail (format < G_N_ELEMENTS (multi_format_table), NULL);

  return &multi_format_table[format].info;
}

static gpointer
create_globals_snippet (gpointer data)
{
  return cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT_GLOBALS,
                           shader_global_conversions,
                           NULL);
}

static gpointer
create_format_snippet (gpointer data)
{
  MetaMultiTextureFormat format =
    (MetaMultiTextureFormat) GPOINTER_TO_INT (data);

  return cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                           NULL,
                           multi_format_table[format].rgb_shader);
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
      static GOnce globals_once = G_ONCE_INIT;
      CoglSnippet *globals_snippet;

      globals_snippet = g_once (&globals_once, create_globals_snippet, NULL);
      *fragment_globals_snippet = g_object_ref (globals_snippet);
    }

  if (fragment_snippet)
    {
      CoglSnippet *format_snippet;

      format_snippet = g_once (&multi_format_table[format].snippet_once,
                               create_format_snippet,
                               GINT_TO_POINTER (format));
      *fragment_snippet = g_object_ref (format_snippet);
    }

  return TRUE;
}
