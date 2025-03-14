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

typedef struct _OpSnippet
{
  const char *source;
  const char *name;
} OpSnippet;

static const char coeffs_identity_limited_shader[] =
  "vec4 identity_limited_to_rgb(vec4 yuva)\n"
  "{\n"
  "  vec4 res;\n"
  "  res.x = (255.0/219.0 * (yuva.x - 16.0/255.0);\n"
  "  res.y = (255.0/219.0 * (yuva.y - 16.0/255.0);\n"
  "  res.z = (255.0/219.0 * (yuva.z - 16.0/255.0);\n"
  "  res.w = yuva.w;\n"
  "  return res;\n"
  "}\n";

static const char coeffs_bt709_full_shader[] =
  "vec4 bt709_full_to_rgb(vec4 yuva)\n"
  "{\n"
  "  vec4 res;\n"
  "  float Y = yuva.x;\n"
  "  float su = yuva.y - 128.0/255.0;\n"
  "  float sv = yuva.z - 128.0/255.0;\n"
  "  res.r = Y                   + 1.79274107 * sv;\n"
  "  res.g = Y - 0.21324861 * su - 0.53290933 * sv;\n"
  "  res.b = Y + 2.11240179 * su;\n"
  "  res.rgb *= yuva.w;\n"
  "  res.a = yuva.w;\n"
  "  return res;\n"
  "}\n";

static const char coeffs_bt709_limited_shader[] =
  "vec4 bt709_limited_to_rgb(vec4 yuva)\n"
  "{\n"
  "  vec4 res;\n"
  "  float Y = 255.0/219.0 * (yuva.x - 16.0/255.0);\n"
  "  float su = yuva.y - 128.0/255.0;\n"
  "  float sv = yuva.z - 128.0/255.0;\n"
  "  res.r = Y                   + 1.79274107 * sv;\n"
  "  res.g = Y - 0.21324861 * su - 0.53290933 * sv;\n"
  "  res.b = Y + 2.11240179 * su;\n"
  "  res.rgb *= yuva.w;\n"
  "  res.a = yuva.w;\n"
  "  return res;\n"
  "}\n";

static const char coeffs_bt601_full_shader[] =
  "vec4 bt601_full_to_rgb(vec4 yuva)\n"
  "{\n"
  "  vec4 res;\n"
  "  float Y = yuva.x;\n"
  "  float su = yuva.y - 128.0/255.0;\n"
  "  float sv = yuva.z - 128.0/255.0;\n"
  "  res.r = Y                   + 1.59602678 * sv;\n"
  "  res.g = Y - 0.39176229 * su - 0.81296764 * sv;\n"
  "  res.b = Y + 2.01723214 * su;\n"
  "  res.rgb *= yuva.w;\n"
  "  res.a = yuva.w;\n"
  "  return res;\n"
  "}\n";

static const char coeffs_bt601_limited_shader[] =
  "vec4 bt601_limited_to_rgb(vec4 yuva)\n"
  "{\n"
  "  vec4 res;\n"
  "  float Y = 255.0/219.0 * (yuva.x - 16.0/255.0);\n"
  "  float su = yuva.y - 128.0/255.0;\n"
  "  float sv = yuva.z - 128.0/255.0;\n"
  "  res.r = Y                   + 1.59602678 * sv;\n"
  "  res.g = Y - 0.39176229 * su - 0.81296764 * sv;\n"
  "  res.b = Y + 2.01723214 * su;\n"
  "  res.rgb *= yuva.w;\n"
  "  res.a = yuva.w;\n"
  "  return res;\n"
  "}\n";

static const char coeffs_bt2020_full_shader[] =
  "vec4 bt2020_full_to_rgb(vec4 yuva)\n"
  "{\n"
  "  vec4 res;\n"
  "  float Y = yuva.x;\n"
  "  float su = yuva.y - 128.0/255.0;\n"
  "  float sv = yuva.z - 128.0/255.0;\n"
  "  res.r = Y                   + 1.4747     * sv;\n"
  "  res.g = Y - 0.16455313 * su - 0.57139187 * sv;\n"
  "  res.b = Y + 1.8814     * su;\n"
  "  res.rgb *= yuva.w;\n"
  "  res.a = yuva.w;\n"
  "  return res;\n"
  "}\n";

static const char coeffs_bt2020_limited_shader[] =
  "vec4 bt2020_limited_to_rgb(vec4 yuva)\n"
  "{\n"
  "  vec4 res;\n"
  "  float Y = 255.0/219.0 * (yuva.x - 16.0/255.0);\n"
  "  float su = yuva.y - 128.0/255.0;\n"
  "  float sv = yuva.z - 128.0/255.0;\n"
  "  res.r = Y                   + 1.67878795 * sv;\n"
  "  res.g = Y - 0.18732610 * su - 0.65046843 * sv;\n"
  "  res.b = Y + 2.14177232 * su;\n"
  "  res.rgb *= yuva.w;\n"
  "  res.a = yuva.w;\n"
  "  return res;\n"
  "}\n";


static OpSnippet coeffs_table[] = {
  /* Invalid */
  [META_MULTI_TEXTURE_COEFFICIENTS_NONE] = {},
  /* Identity, full range */
  [META_MULTI_TEXTURE_COEFFICIENTS_IDENTITY_FULL] = { 0 },
  /* Identity, limited range */
  [META_MULTI_TEXTURE_COEFFICIENTS_IDENTITY_LIMITED] = {
    .name = "identity_limited_to_rgb",
    .source = coeffs_identity_limited_shader,
  },
  /* BT.709, full range */
  [META_MULTI_TEXTURE_COEFFICIENTS_BT709_FULL] = {
    .name = "bt709_full_to_rgb",
    .source = coeffs_bt709_full_shader,
  },
  /* BT.709, limited range */
  [META_MULTI_TEXTURE_COEFFICIENTS_BT709_LIMITED] = {
    .name = "bt709_limited_to_rgb",
    .source = coeffs_bt709_limited_shader,
  },
  /* BT.601, full range */
  [META_MULTI_TEXTURE_COEFFICIENTS_BT601_FULL] = {
    .name = "bt601_full_to_rgb",
    .source = coeffs_bt601_full_shader,
  },
  /* BT.601, limited range */
  [META_MULTI_TEXTURE_COEFFICIENTS_BT601_LIMITED] = {
    .name = "bt601_limited_to_rgb",
    .source = coeffs_bt601_limited_shader,
  },
  /* BT.2020, full range */
  [META_MULTI_TEXTURE_COEFFICIENTS_BT2020_FULL] = {
    .name = "bt2020_full_to_rgb",
    .source = coeffs_bt2020_full_shader,
  },
  /* BT.2020, limited range */
  [META_MULTI_TEXTURE_COEFFICIENTS_BT2020_LIMITED] = {
    .name = "bt2020_limited_to_rgb",
    .source = coeffs_bt2020_limited_shader,
  },
};

static const char premult_straight_shader[] =
  "vec4 alpha_straight_to_premult_electrical(vec4 color)\n"
  "{\n"
  "  return vec4(color.rgb * color.a, color.a);\n"
  "}\n";

static OpSnippet premult_table[] = {
  /* Invalid */
  [META_MULTI_TEXTURE_ALPHA_MODE_NONE] = {},
  /* The input is electrically premultiplied */
  [META_MULTI_TEXTURE_ALPHA_MODE_PREMULT_ELECTRICAL] = { 0 },
  /* The input has straight alpha */
  [META_MULTI_TEXTURE_ALPHA_MODE_STRAIGHT] = {
    .name = "alpha_straight_to_premult_electrical",
    .source = premult_straight_shader,
  },
};

static const char rgba_shader[] =
  "vec4 sample_rgba(vec4 unused)\n"
  "{\n"
  "  return texture2D(cogl_sampler0, cogl_tex_coord0_in.st);\n"
  "}\n";

/* Shader for a single YUV plane */
static const char y_xuxv_shader[] =
  "vec4 sample_y_xuxv(vec4 unused)\n"
  "{\n"
  "  vec4 yuva;\n"
  "  yuva.a = 1.0;\n"
  "  yuva.x = texture2D(cogl_sampler0, cogl_tex_coord0_in.st).x;\n"
  "  yuva.yz = texture2D(cogl_sampler1, cogl_tex_coord0_in.st).ga;\n"
  "  return yuva;\n"
  "}\n";

/* Shader for 1 Y-plane and 1 UV-plane */
static const char y_uv_shader[] =
  "vec4 sample_y_uv(vec4 unused)\n"
  "{\n"
  "  vec4 yuva;\n"
  "  yuva.a = 1.0;\n"
  "  yuva.x = texture2D(cogl_sampler0, cogl_tex_coord0_in.st).x;\n"
  "  yuva.yz = texture2D(cogl_sampler1, cogl_tex_coord0_in.st).rg;\n"
  "  return yuva;\n"
  "}\n";

/* Shader for 1 Y-plane, 1 U-plane and 1 V-plane */
static const char y_u_v_shader[] =
  "vec4 sample_y_u_v(vec4 unused)\n"
  "{\n"
  "  vec4 yuva;\n"
  "  yuva.a = 1.0;\n"
  "  yuva.x = texture2D(cogl_sampler0, cogl_tex_coord0_in.st).x;\n"
  "  yuva.y = texture2D(cogl_sampler1, cogl_tex_coord0_in.st).x;\n"
  "  yuva.z = texture2D(cogl_sampler2, cogl_tex_coord0_in.st).x;\n"
  "  return yuva;\n"
  "}\n";

/* Shader for 1 Y-plane, 1 U-plane and 1 V-plane, shifted by 6 bits (2^6=64) */
static const char y_u_v_shader_10bit_lsb[] =
  "vec4 sample_y_u_v_10bit_lsb(vec4 unused)\n"
  "{\n"
  "  vec4 yuva;\n"
  "  yuva.a = 1.0;\n"
  "  yuva.x = texture2D(cogl_sampler0, cogl_tex_coord0_in.st).x * 64.0;\n"
  "  yuva.y = texture2D(cogl_sampler1, cogl_tex_coord0_in.st).x * 64.0;\n"
  "  yuva.z = texture2D(cogl_sampler2, cogl_tex_coord0_in.st).x * 64.0;\n"
  "  return yuva;\n"
  "}\n";

/* Shader for 1 Y-plane, 1 U-plane and 1 V-plane, shifted by 4 bits (2^4=16) */
static const char y_u_v_shader_12bit_lsb[] =
  "vec4 sample_y_u_v_12bit_lsb(vec4 unused)\n"
  "{\n"
  "  vec4 yuva;\n"
  "  yuva.a = 1.0;\n"
  "  yuva.x = texture2D(cogl_sampler0, cogl_tex_coord0_in.st).x * 16.0;\n"
  "  yuva.y = texture2D(cogl_sampler1, cogl_tex_coord0_in.st).x * 16.0;\n"
  "  yuva.z = texture2D(cogl_sampler2, cogl_tex_coord0_in.st).x * 16.0;\n"
  "  return yuva;\n"
  "}\n";

typedef struct _MetaMultiTextureFormatFullInfo
{
  MetaMultiTextureFormatInfo info;
  const char *name;
  OpSnippet snippet;
} MetaMultiTextureFormatFullInfo;

/* NOTE: The actual enum values are used as the index, so you don't need to
 * loop over the table */
static MetaMultiTextureFormatFullInfo multi_format_table[] = {
  /* Invalid */
  [META_MULTI_TEXTURE_FORMAT_INVALID] = {},
  /* Simple */
  [META_MULTI_TEXTURE_FORMAT_SIMPLE] = {
    .name = "",
    .snippet = {
      .source = rgba_shader,
      .name = "sample_rgba",
    },
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
    .snippet = {
      .source = y_xuxv_shader,
      .name = "sample_y_xuxv",
    },
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
    .snippet = {
      .source = y_uv_shader,
      .name = "sample_y_uv",
    },
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
    .snippet = {
      .source = y_uv_shader,
      .name = "sample_y_uv",
    },
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
    .snippet = {
      .source = y_u_v_shader,
      .name = "sample_y_u_v",
    },
    .info = {
      .n_planes = 3,
      .subformats = { COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_R_8 },
      .plane_indices = { 0, 1, 2 },
      .hsub = { 1, 2, 2 },
      .vsub = { 1, 2, 2 },
    },
  },
  [META_MULTI_TEXTURE_FORMAT_YUV422] = {
    .name = "YUV422",
    .snippet = {
      .source = y_u_v_shader,
      .name = "sample_y_u_v",
    },
    .info = {
      .n_planes = 3,
      .subformats = { COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_R_8 },
      .plane_indices = { 0, 1, 2 },
      .hsub = { 1, 2, 2 },
      .vsub = { 1, 1, 1 },
    },
  },
  [META_MULTI_TEXTURE_FORMAT_YUV444] = {
    .name = "YUV444",
    .snippet = {
      .source = y_u_v_shader,
      .name = "sample_y_u_v",
    },
    .info = {
      .n_planes = 3,
      .subformats = { COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_R_8, COGL_PIXEL_FORMAT_R_8 },
      .plane_indices = { 0, 1, 2 },
      .hsub = { 1, 1, 1 },
      .vsub = { 1, 1, 1 },
    },
  },
  [META_MULTI_TEXTURE_FORMAT_S010] = {
    .name = "S010",
    .snippet = {
      .source = y_u_v_shader_10bit_lsb,
      .name = "sample_y_u_v_10bit_lsb",
    },
    .info = {
      .n_planes = 3,
      .subformats = { COGL_PIXEL_FORMAT_R_16, COGL_PIXEL_FORMAT_R_16, COGL_PIXEL_FORMAT_R_16 },
      .plane_indices = { 0, 1, 2 },
      .hsub = { 1, 2, 2 },
      .vsub = { 1, 2, 2 },
    },
  },
  [META_MULTI_TEXTURE_FORMAT_S210] = {
    .name = "S210",
    .snippet = {
      .source = y_u_v_shader_10bit_lsb,
      .name = "sample_y_u_v_10bit_lsb",
    },
    .info = {
      .n_planes = 3,
      .subformats = { COGL_PIXEL_FORMAT_R_16, COGL_PIXEL_FORMAT_R_16, COGL_PIXEL_FORMAT_R_16 },
      .plane_indices = { 0, 1, 2 },
      .hsub = { 1, 2, 2 },
      .vsub = { 1, 1, 1 },
    },
  },
  [META_MULTI_TEXTURE_FORMAT_S410] = {
    .name = "S410",
    .snippet = {
      .source = y_u_v_shader_10bit_lsb,
      .name = "sample_y_u_v_10bit_lsb",
    },
    .info = {
      .n_planes = 3,
      .subformats = { COGL_PIXEL_FORMAT_R_16, COGL_PIXEL_FORMAT_R_16, COGL_PIXEL_FORMAT_R_16 },
      .plane_indices = { 0, 1, 2 },
      .hsub = { 1, 1, 1 },
      .vsub = { 1, 1, 1 },
    },
  },
  [META_MULTI_TEXTURE_FORMAT_S012] = {
    .name = "S012",
    .snippet = {
      .source = y_u_v_shader_12bit_lsb,
      .name = "sample_y_u_v_12bit_lsb",
    },
    .info = {
      .n_planes = 3,
      .subformats = { COGL_PIXEL_FORMAT_R_16, COGL_PIXEL_FORMAT_R_16, COGL_PIXEL_FORMAT_R_16 },
      .plane_indices = { 0, 1, 2 },
      .hsub = { 1, 2, 2 },
      .vsub = { 1, 2, 2 },
    },
  },
  [META_MULTI_TEXTURE_FORMAT_S212] = {
    .name = "S212",
    .snippet = {
      .source = y_u_v_shader_12bit_lsb,
      .name = "sample_y_u_v_12bit_lsb",
    },
    .info = {
      .n_planes = 3,
      .subformats = { COGL_PIXEL_FORMAT_R_16, COGL_PIXEL_FORMAT_R_16, COGL_PIXEL_FORMAT_R_16 },
      .plane_indices = { 0, 1, 2 },
      .hsub = { 1, 2, 2 },
      .vsub = { 1, 1, 1 },
    },
  },
  [META_MULTI_TEXTURE_FORMAT_S412] = {
    .name = "S412",
    .snippet = {
      .source = y_u_v_shader_12bit_lsb,
      .name = "sample_y_u_v_12bit_lsb",
    },
    .info = {
      .n_planes = 3,
      .subformats = { COGL_PIXEL_FORMAT_R_16, COGL_PIXEL_FORMAT_R_16, COGL_PIXEL_FORMAT_R_16 },
      .plane_indices = { 0, 1, 2 },
      .hsub = { 1, 1, 1 },
      .vsub = { 1, 1, 1 },
    },
  },
  [META_MULTI_TEXTURE_FORMAT_S016] = {
    .name = "S016",
    .snippet = {
      .source = y_u_v_shader,
      .name = "sample_y_u_v",
    },
    .info = {
      .n_planes = 3,
      .subformats = { COGL_PIXEL_FORMAT_R_16, COGL_PIXEL_FORMAT_R_16, COGL_PIXEL_FORMAT_R_16 },
      .plane_indices = { 0, 1, 2 },
      .hsub = { 1, 2, 2 },
      .vsub = { 1, 2, 2 },
    },
  },
  [META_MULTI_TEXTURE_FORMAT_S216] = {
    .name = "S216",
    .snippet = {
      .source = y_u_v_shader,
      .name = "sample_y_u_v",
    },
    .info = {
      .n_planes = 3,
      .subformats = { COGL_PIXEL_FORMAT_R_16, COGL_PIXEL_FORMAT_R_16, COGL_PIXEL_FORMAT_R_16 },
      .plane_indices = { 0, 1, 2 },
      .hsub = { 1, 2, 2 },
      .vsub = { 1, 1, 1 },
    },
  },
  [META_MULTI_TEXTURE_FORMAT_S416] = {
    .name = "S416",
    .snippet = {
      .source = y_u_v_shader,
      .name = "sample_y_u_v",
    },
    .info = {
      .n_planes = 3,
      .subformats = { COGL_PIXEL_FORMAT_R_16, COGL_PIXEL_FORMAT_R_16, COGL_PIXEL_FORMAT_R_16 },
      .plane_indices = { 0, 1, 2 },
      .hsub = { 1, 1, 1 },
      .vsub = { 1, 1, 1 },
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

static void
append_snippet (const OpSnippet *snippet,
                GString         *snippet_globals,
                GString         *snippet_source,
                const char      *snippet_color_var)
{
  if (!snippet->source)
    return;

  g_string_append_printf (snippet_globals, "%s\n", snippet->source);
  g_string_append_printf (snippet_source,
                          "  %s = %s (%s);\n",
                          snippet_color_var,
                          snippet->name,
                          snippet_color_var);
}

CoglSnippet *
meta_multi_texture_format_get_snippet (MetaMultiTextureFormat       format,
                                       MetaMultiTextureCoefficients coeffs,
                                       MetaMultiTextureAlphaMode    premult)
{
  g_autoptr (GString) snippet_globals = NULL;
  g_autoptr (GString) snippet_source = NULL;
  const char *snippet_color_var;

  g_return_val_if_fail (format < G_N_ELEMENTS (multi_format_table), NULL);
  g_return_val_if_fail (coeffs < G_N_ELEMENTS (coeffs_table), NULL);
  g_return_val_if_fail (premult < G_N_ELEMENTS (premult_table), NULL);

  snippet_globals = g_string_new (NULL);
  snippet_source = g_string_new (NULL);
  snippet_color_var = "cogl_color_out";

  append_snippet (&multi_format_table[format].snippet,
                  snippet_globals,
                  snippet_source,
                  snippet_color_var);

  append_snippet (&coeffs_table[coeffs],
                  snippet_globals,
                  snippet_source,
                  snippet_color_var);

  append_snippet (&premult_table[premult],
                  snippet_globals,
                  snippet_source,
                  snippet_color_var);

  if (snippet_globals->len == 0 && snippet_source->len == 0)
    return NULL;

  g_string_append_printf (snippet_source,
                          "  cogl_color_out = %s * cogl_color_in.a;\n",
                          snippet_color_var);

  return cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                           snippet_globals->str,
                           snippet_source->str);
}
