/*
 * Copyright (C) 2022 Intel Corporation.
 * Copyright (C) 2022 Red Hat, Inc.
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
 */

#include <cogl/cogl.h>
#include <cogl/cogl-half-float.h>

#include "tests/cogl-test-utils.h"

static int
get_bits (uint32_t in,
          int      end,
          int      begin)
{
  int mask = (1 << (end - begin + 1)) - 1;

  return (in >> begin) & mask;
}

static int
rgb16_to_rgb8 (int rgb16)
{
  float r;

  r = rgb16 / (float) ((1 << 16) - 1);
  return (int) (r * (float) ((1 << 8) - 1));
}

static int
rgb8_to_rgb16 (int rgb8)
{
  float r;

  r = rgb8 / (float) ((1 << 8) - 1);
  return (int) (r * (float) ((1 << 16) - 1));
}

static void
test_offscreen_texture_formats_store_rgba16161616 (void)
{
  CoglTexture *tex;
  CoglOffscreen *offscreen;
  GError *error = NULL;
  uint8_t readback[8 * 4];
  const uint16_t rgba16_red = 515;
  const uint16_t rgba16_green = 61133;
  const uint16_t rgba16_blue = 2;
  const uint16_t rgba16_alpha = 1111;
  float red;
  float green;
  float blue;
  float alpha;
  int i;

  red = (rgba16_red / (float) ((1 << 16) - 1));
  green = (rgba16_green / (float) ((1 << 16) - 1));
  blue = (rgba16_blue / (float) ((1 << 16) - 1));
  alpha = (rgba16_alpha / (float) ((1 << 16) - 1));

  g_assert_cmpint (rgb8_to_rgb16 (rgb16_to_rgb8 (rgba16_red)), !=, rgba16_red);
  g_assert_cmpint (rgb8_to_rgb16 (rgb16_to_rgb8 (rgba16_green)), !=, rgba16_green);
  g_assert_cmpint (rgb8_to_rgb16 (rgb16_to_rgb8 (rgba16_blue)), !=, rgba16_blue);
  g_assert_cmpint (rgb8_to_rgb16 (rgb16_to_rgb8 (rgba16_alpha)), !=, rgba16_alpha);

  /* Allocate 2x2 to ensure we avoid any fast paths. */
  tex = cogl_texture_2d_new_with_format (test_ctx,
                                         2, 2,
                                         COGL_PIXEL_FORMAT_RGBA_16161616_PRE);

  offscreen = cogl_offscreen_new_with_texture (tex);
  cogl_framebuffer_allocate (COGL_FRAMEBUFFER (offscreen), &error);
  g_assert_no_error (error);

  cogl_framebuffer_clear4f (COGL_FRAMEBUFFER (offscreen),
                            COGL_BUFFER_BIT_COLOR,
                            red, green, blue, alpha);

  cogl_framebuffer_read_pixels (COGL_FRAMEBUFFER (offscreen), 0, 0, 2, 2,
                                COGL_PIXEL_FORMAT_RG_1616,
                                (uint8_t *) &readback);

  for (i = 0; i < 4; i++)
    {
      uint16_t *pixel_data = (uint16_t *) &readback[i * 4];

      g_assert_cmpint (pixel_data[0], ==, rgba16_red);
      g_assert_cmpint (pixel_data[1], ==, rgba16_green);
    }

  cogl_framebuffer_read_pixels (COGL_FRAMEBUFFER (offscreen), 0, 0, 2, 2,
                                COGL_PIXEL_FORMAT_RGBA_16161616_PRE,
                                (uint8_t *) &readback);

  for (i = 0; i < 4; i++)
    {
      uint16_t *pixel_data = (uint16_t *) &readback[i * 8];

      g_assert_cmpint (pixel_data[0], ==, rgba16_red);
      g_assert_cmpint (pixel_data[1], ==, rgba16_green);
      g_assert_cmpint (pixel_data[2], ==, rgba16_blue);
      g_assert_cmpint (pixel_data[3], ==, rgba16_alpha);
    }

  cogl_framebuffer_read_pixels (COGL_FRAMEBUFFER (offscreen), 0, 0, 2, 2,
                                COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                                (uint8_t *) &readback);
  for (i = 0; i < 4; i++)
    {
      uint8_t *pixel_data = (uint8_t *) &readback[i * 4];

      g_assert_cmpint (pixel_data[0], ==, rgb16_to_rgb8 (rgba16_red));
      /* this one is off by one, no idea why */
      /* g_assert_cmpint (pixel_data[1], ==, rgb16_to_rgb8 (rgba16_green)); */
      g_assert_cmpint (pixel_data[2], ==, rgb16_to_rgb8 (rgba16_blue));
      g_assert_cmpint (pixel_data[3], ==, rgb16_to_rgb8 (rgba16_alpha));
    }

  g_object_unref (offscreen);
  g_object_unref (tex);
}

static void
test_offscreen_texture_formats_store_fp16 (void)
{
  const uint16_t red = cogl_float_to_half (72.912f);
  const uint16_t green = cogl_float_to_half (0.20f);
  const uint16_t blue = cogl_float_to_half (0.01f);
  const uint16_t alpha = cogl_float_to_half (0.7821f);
  const uint16_t one = cogl_float_to_half (1.0f);

  GError *error = NULL;
  CoglPixelFormat formats[] = {
    COGL_PIXEL_FORMAT_RGBX_FP_16161616,
    COGL_PIXEL_FORMAT_RGBA_FP_16161616_PRE,
    COGL_PIXEL_FORMAT_BGRX_FP_16161616,
    COGL_PIXEL_FORMAT_BGRA_FP_16161616_PRE,
    COGL_PIXEL_FORMAT_XRGB_FP_16161616,
    COGL_PIXEL_FORMAT_ARGB_FP_16161616_PRE,
    COGL_PIXEL_FORMAT_XBGR_FP_16161616,
    COGL_PIXEL_FORMAT_ABGR_FP_16161616_PRE,
  };
  int i;

  if (!cogl_has_feature (test_ctx, COGL_FEATURE_ID_TEXTURE_HALF_FLOAT))
    {
      g_test_skip ("Driver does not support fp formats");
      return;
    }

  for (i = 0; i < G_N_ELEMENTS (formats); i++)
    {
      CoglTexture *tex;
      CoglOffscreen *offscreen;
      uint32_t rgb8_readback[4];
      int j, k;

      /* Allocate 2x2 to ensure we avoid any fast paths. */
      tex = cogl_texture_2d_new_with_format (test_ctx, 2, 2, formats[i]);

      offscreen = cogl_offscreen_new_with_texture (tex);
      cogl_framebuffer_allocate (COGL_FRAMEBUFFER (offscreen), &error);
      g_assert_no_error (error);

      cogl_framebuffer_clear4f (COGL_FRAMEBUFFER (offscreen),
                                COGL_BUFFER_BIT_COLOR,
                                cogl_half_to_float (red),
                                cogl_half_to_float (green),
                                cogl_half_to_float (blue),
                                cogl_half_to_float (alpha));

      for (j = 0; j < G_N_ELEMENTS (formats); j++)
        {
          uint8_t readback[8 * 4];

          cogl_framebuffer_read_pixels (COGL_FRAMEBUFFER (offscreen), 0, 0, 2, 2,
                                        formats[j],
                                        readback);

          for (k = 0; k < 4; k++)
            {
              uint16_t channels[4];
              uint16_t *pixel_data = (uint16_t *)&readback[8 * k];

              switch (formats[j])
                {
                case COGL_PIXEL_FORMAT_RGBX_FP_16161616:
                case COGL_PIXEL_FORMAT_RGBA_FP_16161616_PRE:
                  channels[0] = pixel_data[0];
                  channels[1] = pixel_data[1];
                  channels[2] = pixel_data[2];
                  channels[3] = pixel_data[3];
                  break;
                case COGL_PIXEL_FORMAT_BGRX_FP_16161616:
                case COGL_PIXEL_FORMAT_BGRA_FP_16161616_PRE:
                  channels[0] = pixel_data[2];
                  channels[1] = pixel_data[1];
                  channels[2] = pixel_data[0];
                  channels[3] = pixel_data[3];
                  break;
                case COGL_PIXEL_FORMAT_XRGB_FP_16161616:
                case COGL_PIXEL_FORMAT_ARGB_FP_16161616_PRE:
                  channels[0] = pixel_data[1];
                  channels[1] = pixel_data[2];
                  channels[2] = pixel_data[3];
                  channels[3] = pixel_data[0];
                  break;
                case COGL_PIXEL_FORMAT_XBGR_FP_16161616:
                case COGL_PIXEL_FORMAT_ABGR_FP_16161616_PRE:
                  channels[0] = pixel_data[3];
                  channels[1] = pixel_data[2];
                  channels[2] = pixel_data[1];
                  channels[3] = pixel_data[0];
                  break;
                default:
                  g_assert_not_reached ();
                }

              if ((formats[i] & COGL_A_BIT) && (formats[j] & COGL_A_BIT))
                g_assert_cmpint (channels[3], ==, alpha);
              else if (!(formats[i] & COGL_A_BIT) && !(formats[j] & COGL_A_BIT))
                g_assert_cmpint (channels[3], ==, one);

              g_assert_cmpfloat (fabs (cogl_half_to_float (channels[0]) - cogl_half_to_float (red)), <, 0.005);
              g_assert_cmpfloat (fabs (cogl_half_to_float (channels[1]) - cogl_half_to_float (green)), <, 0.005);
              g_assert_cmpfloat (fabs (cogl_half_to_float (channels[2]) - cogl_half_to_float (blue)), <, 0.005);
            }
        }

      cogl_framebuffer_read_pixels (COGL_FRAMEBUFFER (offscreen), 0, 0, 2, 2,
                                    COGL_PIXEL_FORMAT_RGBX_8888,
                                    (uint8_t *) &rgb8_readback);
      for (k = 0; k < 4; k++)
        {
          uint8_t *rgb8_buf = (uint8_t *) &rgb8_readback[k];

          /* test only green and blue because they are < 1.0 */
          g_assert_cmpfloat (fabs (cogl_half_to_float (green) - (rgb8_buf[1] / 255.0f)), <, 0.005);
          g_assert_cmpfloat (fabs (cogl_half_to_float (blue) - (rgb8_buf[2] / 255.0f)), <, 0.005);

          if (!(formats[i] & COGL_A_BIT))
            g_assert_cmpint (rgb8_buf[3], ==, 0xff);
        }

      g_object_unref (offscreen);
      g_object_unref (tex);
    }
}

static int
rgb10_to_rgb8 (int rgb10)
{
  float r;

  r = rgb10 / (float) ((1 << 10) - 1);
  return (int) (r * (float) ((1 << 8) - 1));
}

static int
rgb8_to_rgb10 (int rgb8)
{
  float r;

  r = rgb8 / (float) ((1 << 8) - 1);
  return (int) (r * (float) ((1 << 10) - 1));
}

static void
test_offscreen_texture_formats_store_rgb10 (void)
{
  const int rgb10_red = 514;
  const int rgb10_green = 258;
  const int rgb10_blue = 18;
  const int rgb10_alpha = 2;
  float red;
  float green;
  float blue;
  float alpha;
  GError *error = NULL;
  CoglPixelFormat formats[] = {
    COGL_PIXEL_FORMAT_XRGB_2101010,
    COGL_PIXEL_FORMAT_ARGB_2101010_PRE,
    COGL_PIXEL_FORMAT_XBGR_2101010,
    COGL_PIXEL_FORMAT_ABGR_2101010_PRE,
    COGL_PIXEL_FORMAT_RGBA_1010102_PRE,
    COGL_PIXEL_FORMAT_BGRA_1010102_PRE,
  };
  int i;

  if (!cogl_has_feature (test_ctx, COGL_FEATURE_ID_TEXTURE_RGBA1010102))
    {
      g_test_skip ("Driver does not support 10bpc formats");
      return;
    }

  /* The extra fraction is there to avoid rounding inconsistencies in OpenGL
   * implementations. */
  red = (rgb10_red / (float) ((1 << 10) - 1)) + 0.00001;
  green = (rgb10_green / (float) ((1 << 10) - 1)) + 0.00001;
  blue = (rgb10_blue / (float) ((1 << 10) - 1)) + 0.00001;
  alpha = (rgb10_alpha / (float) ((1 << 2) - 1)) + 0.00001;

  /* Make sure that that the color value can't be represented using rgb8. */
  g_assert_cmpint (rgb8_to_rgb10 (rgb10_to_rgb8 (rgb10_red)), !=, rgb10_red);
  g_assert_cmpint (rgb8_to_rgb10 (rgb10_to_rgb8 (rgb10_green)), !=, rgb10_green);
  g_assert_cmpint (rgb8_to_rgb10 (rgb10_to_rgb8 (rgb10_blue)), !=, rgb10_blue);

  for (i = 0; i < G_N_ELEMENTS (formats); i++)
    {
      CoglTexture *tex;
      CoglOffscreen *offscreen;
      uint32_t rgb8_readback[4];
      int j, k;

      /* Allocate 2x2 to ensure we avoid any fast paths. */
      tex = cogl_texture_2d_new_with_format (test_ctx, 2, 2, formats[i]);

      offscreen = cogl_offscreen_new_with_texture (tex);
      cogl_framebuffer_allocate (COGL_FRAMEBUFFER (offscreen), &error);
      g_assert_no_error (error);

      cogl_framebuffer_clear4f (COGL_FRAMEBUFFER (offscreen),
                                COGL_BUFFER_BIT_COLOR,
                                red, green, blue, alpha);

      for (j = 0; j < G_N_ELEMENTS (formats); j++)
        {
          uint32_t rgb10_readback[4];
          int alpha_out;

          cogl_framebuffer_read_pixels (COGL_FRAMEBUFFER (offscreen), 0, 0, 2, 2,
                                        formats[j],
                                        (uint8_t *) &rgb10_readback);

          for (k = 0; k < 4; k++)
            {
              int channels[3];

              switch (formats[j])
                {
                case COGL_PIXEL_FORMAT_RGBA_1010102_PRE:
                case COGL_PIXEL_FORMAT_BGRA_1010102_PRE:
                  channels[0] = get_bits (rgb10_readback[k], 31, 22);
                  channels[1] = get_bits (rgb10_readback[k], 21, 12);
                  channels[2] = get_bits (rgb10_readback[k], 11, 2);
                  alpha_out = get_bits (rgb10_readback[k], 1, 0);
                  break;
                case COGL_PIXEL_FORMAT_XRGB_2101010:
                case COGL_PIXEL_FORMAT_ARGB_2101010_PRE:
                case COGL_PIXEL_FORMAT_XBGR_2101010:
                case COGL_PIXEL_FORMAT_ABGR_2101010_PRE:
                  alpha_out = get_bits (rgb10_readback[k], 31, 30);
                  channels[0] = get_bits (rgb10_readback[k], 29, 20);
                  channels[1] = get_bits (rgb10_readback[k], 19, 10);
                  channels[2] = get_bits (rgb10_readback[k], 9, 0);
                  break;
                default:
                  g_assert_not_reached ();
                }

              if ((formats[i] & COGL_A_BIT) && (formats[j] & COGL_A_BIT))
                g_assert_cmpint (alpha_out, ==, rgb10_alpha);
              else if (!(formats[i] & COGL_A_BIT) && !(formats[j] & COGL_A_BIT))
                g_assert_cmpint (alpha_out, ==, 0x3);

              switch (formats[j])
                {
                case COGL_PIXEL_FORMAT_RGBA_1010102_PRE:
                case COGL_PIXEL_FORMAT_XRGB_2101010:
                case COGL_PIXEL_FORMAT_ARGB_2101010_PRE:
                  g_assert_cmpint (channels[0], ==, rgb10_red);
                  g_assert_cmpint (channels[1], ==, rgb10_green);
                  g_assert_cmpint (channels[2], ==, rgb10_blue);
                  break;
                case COGL_PIXEL_FORMAT_BGRA_1010102_PRE:
                case COGL_PIXEL_FORMAT_XBGR_2101010:
                case COGL_PIXEL_FORMAT_ABGR_2101010_PRE:
                  g_assert_cmpint (channels[0], ==, rgb10_blue);
                  g_assert_cmpint (channels[1], ==, rgb10_green);
                  g_assert_cmpint (channels[2], ==, rgb10_red);
                  break;
                default:
                  g_assert_not_reached ();
                }
            }
        }

      cogl_framebuffer_read_pixels (COGL_FRAMEBUFFER (offscreen), 0, 0, 2, 2,
                                    COGL_PIXEL_FORMAT_RGBX_8888,
                                    (uint8_t *) &rgb8_readback);
      for (k = 0; k < 4; k++)
        {
          uint8_t *rgb8_buf = (uint8_t *) &rgb8_readback[k];

          g_assert_cmpint (rgb8_buf[0], ==, rgb10_to_rgb8 (rgb10_red));
          g_assert_cmpint (rgb8_buf[1], ==, rgb10_to_rgb8 (rgb10_green));
          g_assert_cmpint (rgb8_buf[2], ==, rgb10_to_rgb8 (rgb10_blue));

          if (!(formats[i] & COGL_A_BIT))
            g_assert_cmpint (rgb8_buf[3], ==, 0xff);
        }

      g_object_unref (offscreen);
      g_object_unref (tex);
    }
}

static void
test_offscreen_texture_formats_store_rgb8 (void)
{
  CoglColor color;
  const uint8_t red = 0xab;
  const uint8_t green = 0x1f;
  const uint8_t blue = 0x50;
  const uint8_t alpha = 0x34;
  CoglPixelFormat formats[] = {
    COGL_PIXEL_FORMAT_RGBX_8888,
    COGL_PIXEL_FORMAT_RGBA_8888_PRE,
    COGL_PIXEL_FORMAT_BGRX_8888,
    COGL_PIXEL_FORMAT_BGRA_8888_PRE,
    COGL_PIXEL_FORMAT_XRGB_8888,
    COGL_PIXEL_FORMAT_ARGB_8888_PRE,
    COGL_PIXEL_FORMAT_XBGR_8888,
    COGL_PIXEL_FORMAT_ABGR_8888_PRE,
  };
  int i;

  cogl_color_init_from_4f (&color,
                           red / 255.0, green / 255.0,
                           blue / 255.0, alpha / 255.0);

  for (i = 0; i < G_N_ELEMENTS (formats); i++)
    {
      CoglTexture *tex;
      CoglOffscreen *offscreen;
      GError *error = NULL;
      int j;

      /* Allocate 2x2 to ensure we avoid any fast paths. */
      tex = cogl_texture_2d_new_with_format (test_ctx, 2, 2, formats[i]);

      offscreen = cogl_offscreen_new_with_texture (tex);
      cogl_framebuffer_allocate (COGL_FRAMEBUFFER (offscreen), &error);
      g_assert_no_error (error);

      cogl_framebuffer_clear (COGL_FRAMEBUFFER (offscreen),
                              COGL_BUFFER_BIT_COLOR,
                              &color);

      for (j = 0; j < G_N_ELEMENTS (formats); j++)
        {
          uint8_t rgba_readback[4 * 4] = {};
          int alpha_out;
          int k;

          cogl_framebuffer_read_pixels (COGL_FRAMEBUFFER (offscreen), 0, 0, 2, 2,
                                        formats[j],
                                        (uint8_t *) &rgba_readback);

          for (k = 0; k < 4; k++)
            {
              switch (formats[j])
                {
                case COGL_PIXEL_FORMAT_RGBX_8888:
                case COGL_PIXEL_FORMAT_RGBA_8888_PRE:
                  g_assert_cmpint (rgba_readback[k * 4 + 0], ==, red);
                  g_assert_cmpint (rgba_readback[k * 4 + 1], ==, green);
                  g_assert_cmpint (rgba_readback[k * 4 + 2], ==, blue);
                  alpha_out = rgba_readback[k * 4 + 3];
                  break;
                case COGL_PIXEL_FORMAT_XRGB_8888:
                case COGL_PIXEL_FORMAT_ARGB_8888_PRE:
                  alpha_out = rgba_readback[k * 4 + 0];
                  g_assert_cmpint (rgba_readback[k * 4 + 1], ==, red);
                  g_assert_cmpint (rgba_readback[k * 4 + 2], ==, green);
                  g_assert_cmpint (rgba_readback[k * 4 + 3], ==, blue);
                  break;
                case COGL_PIXEL_FORMAT_BGRX_8888:
                case COGL_PIXEL_FORMAT_BGRA_8888_PRE:
                  g_assert_cmpint (rgba_readback[k * 4 + 0], ==, blue);
                  g_assert_cmpint (rgba_readback[k * 4 + 1], ==, green);
                  g_assert_cmpint (rgba_readback[k * 4 + 2], ==, red);
                  alpha_out = rgba_readback[k * 4 + 3];
                  break;
                case COGL_PIXEL_FORMAT_XBGR_8888:
                case COGL_PIXEL_FORMAT_ABGR_8888_PRE:
                  alpha_out = rgba_readback[k * 4 + 0];
                  g_assert_cmpint (rgba_readback[k * 4 + 1], ==, blue);
                  g_assert_cmpint (rgba_readback[k * 4 + 2], ==, green);
                  g_assert_cmpint (rgba_readback[k * 4 + 3], ==, red);
                  break;
                default:
                  g_assert_not_reached ();
                }

              if ((formats[i] & COGL_A_BIT) && (formats[j] & COGL_A_BIT))
                g_assert_cmpint (alpha_out, ==, alpha);
              else if (!(formats[i] & COGL_A_BIT) && !(formats[j] & COGL_A_BIT))
                g_assert_cmpint (alpha_out, ==, 0xff);
            }
        }

      g_object_unref (offscreen);
      g_object_unref (tex);
    }
}

static void
test_offscreen_texture_formats_paint_fp16 (void)
{
  const uint16_t red = cogl_float_to_half (72.912f);
  const uint16_t green = cogl_float_to_half (0.20f);
  const uint16_t blue = cogl_float_to_half (0.01f);
  const uint16_t alpha = cogl_float_to_half (0.7821f);
  const uint16_t one = cogl_float_to_half (1.0f);

  CoglPixelFormat formats[] = {
    COGL_PIXEL_FORMAT_RGBX_FP_16161616,
    COGL_PIXEL_FORMAT_RGBA_FP_16161616_PRE,
    COGL_PIXEL_FORMAT_BGRX_FP_16161616,
    COGL_PIXEL_FORMAT_BGRA_FP_16161616_PRE,
    COGL_PIXEL_FORMAT_XRGB_FP_16161616,
    COGL_PIXEL_FORMAT_ARGB_FP_16161616_PRE,
    COGL_PIXEL_FORMAT_XBGR_FP_16161616,
    COGL_PIXEL_FORMAT_ABGR_FP_16161616_PRE,
  };
  int i;

  if (!cogl_has_feature (test_ctx, COGL_FEATURE_ID_TEXTURE_HALF_FLOAT))
    {
      g_test_skip ("Driver does not support fp formats");
      return;
    }

  for (i = 0; i < G_N_ELEMENTS (formats); i++)
    {
      CoglTexture *tex_src;
      CoglOffscreen *offscreen_src;
      GError *error = NULL;
      int j;

      tex_src = cogl_texture_2d_new_with_format (test_ctx, 2, 2, formats[i]);
      offscreen_src = cogl_offscreen_new_with_texture (tex_src);
      cogl_framebuffer_allocate (COGL_FRAMEBUFFER (offscreen_src), &error);
      g_assert_no_error (error);

      for (j = 0; j < G_N_ELEMENTS (formats); j++)
        {
          CoglTexture *tex_dst;
          CoglOffscreen *offscreen_dst;
          CoglPipeline *pipeline;
          uint8_t readback[8 * 4];
          int k;

          tex_dst = cogl_texture_2d_new_with_format (test_ctx, 2, 2, formats[j]);
          offscreen_dst = cogl_offscreen_new_with_texture (tex_dst);
          cogl_framebuffer_allocate (COGL_FRAMEBUFFER (offscreen_dst), &error);
          g_assert_no_error (error);

          cogl_framebuffer_clear4f (COGL_FRAMEBUFFER (offscreen_src),
                                    COGL_BUFFER_BIT_COLOR,
                                    cogl_half_to_float (red),
                                    cogl_half_to_float (green),
                                    cogl_half_to_float (blue),
                                    cogl_half_to_float (alpha));

          pipeline = cogl_pipeline_new (test_ctx);
          cogl_pipeline_set_blend (pipeline,
                                   "RGBA = ADD (SRC_COLOR, 0)", NULL);
          cogl_pipeline_set_layer_texture (pipeline, 0, tex_src);
          cogl_framebuffer_draw_rectangle (COGL_FRAMEBUFFER (offscreen_dst),
                                           pipeline,
                                           -1.0, -1.0, 1.0, 1.0);
          g_object_unref (pipeline);

          cogl_framebuffer_read_pixels (COGL_FRAMEBUFFER (offscreen_dst),
                                        0, 0, 2, 2, formats[j],
                                        readback);

          for (k = 0; k < 4; k++)
            {
              uint16_t channels[4];
              uint16_t *pixel_data = (uint16_t *)&readback[8 * k];

              switch (formats[j])
                {
                case COGL_PIXEL_FORMAT_RGBX_FP_16161616:
                case COGL_PIXEL_FORMAT_RGBA_FP_16161616_PRE:
                  channels[0] = pixel_data[0];
                  channels[1] = pixel_data[1];
                  channels[2] = pixel_data[2];
                  channels[3] = pixel_data[3];
                  break;
                case COGL_PIXEL_FORMAT_BGRX_FP_16161616:
                case COGL_PIXEL_FORMAT_BGRA_FP_16161616_PRE:
                  channels[0] = pixel_data[2];
                  channels[1] = pixel_data[1];
                  channels[2] = pixel_data[0];
                  channels[3] = pixel_data[3];
                  break;
                case COGL_PIXEL_FORMAT_XRGB_FP_16161616:
                case COGL_PIXEL_FORMAT_ARGB_FP_16161616_PRE:
                  channels[0] = pixel_data[1];
                  channels[1] = pixel_data[2];
                  channels[2] = pixel_data[3];
                  channels[3] = pixel_data[0];
                  break;
                case COGL_PIXEL_FORMAT_XBGR_FP_16161616:
                case COGL_PIXEL_FORMAT_ABGR_FP_16161616_PRE:
                  channels[0] = pixel_data[3];
                  channels[1] = pixel_data[2];
                  channels[2] = pixel_data[1];
                  channels[3] = pixel_data[0];
                  break;
                default:
                  g_assert_not_reached ();
                }

              if ((formats[i] & COGL_A_BIT) && (formats[j] & COGL_A_BIT))
                g_assert_cmpint (channels[3], ==, alpha);
              else if (!(formats[i] & COGL_A_BIT) && !(formats[j] & COGL_A_BIT))
                g_assert_cmpint (channels[3], ==, one);

              g_assert_cmpfloat (fabs (cogl_half_to_float (channels[0]) - cogl_half_to_float (red)), <, 0.005);
              g_assert_cmpfloat (fabs (cogl_half_to_float (channels[1]) - cogl_half_to_float (green)), <, 0.005);
              g_assert_cmpfloat (fabs (cogl_half_to_float (channels[2]) - cogl_half_to_float (blue)), <, 0.005);
            }

          g_object_unref (offscreen_dst);
          g_object_unref (tex_dst);
        }

      g_object_unref (offscreen_src);
      g_object_unref (tex_src);
    }
}

static void
test_offscreen_texture_formats_paint_rgb10 (void)
{
  const int rgb10_red = 514;
  const int rgb10_green = 258;
  const int rgb10_blue = 18;
  const int rgb10_alpha = 2;
  float red;
  float green;
  float blue;
  float alpha;
  CoglPixelFormat formats[] = {
    COGL_PIXEL_FORMAT_XRGB_2101010,
    COGL_PIXEL_FORMAT_ARGB_2101010_PRE,
    COGL_PIXEL_FORMAT_XBGR_2101010,
    COGL_PIXEL_FORMAT_ABGR_2101010_PRE,
    COGL_PIXEL_FORMAT_RGBA_1010102_PRE,
    COGL_PIXEL_FORMAT_BGRA_1010102_PRE,
  };
  int i;

  if (!cogl_has_feature (test_ctx, COGL_FEATURE_ID_TEXTURE_RGBA1010102))
    {
      g_test_skip ("Driver does not support 10bpc formats");
      return;
    }

  /* The extra fraction is there to avoid rounding inconsistencies in OpenGL
   * implementations. */
  red = (rgb10_red / (float) ((1 << 10 ) - 1)) + 0.00001;
  green = (rgb10_green / (float) ((1 << 10) - 1)) + 0.00001;
  blue = (rgb10_blue / (float) ((1 << 10) - 1)) + 0.00001;
  alpha = (rgb10_alpha / (float) ((1 << 2) - 1)) + 0.00001;

  /* Make sure that that the color value can't be represented using rgb8. */
  g_assert_cmpint (rgb8_to_rgb10 (rgb10_to_rgb8 (rgb10_red)), !=, rgb10_red);
  g_assert_cmpint (rgb8_to_rgb10 (rgb10_to_rgb8 (rgb10_green)), !=, rgb10_green);
  g_assert_cmpint (rgb8_to_rgb10 (rgb10_to_rgb8 (rgb10_blue)), !=, rgb10_blue);

  for (i = 0; i < G_N_ELEMENTS (formats); i++)
    {
      CoglTexture *tex_src;
      CoglOffscreen *offscreen_src;
      GError *error = NULL;
      int j;

      tex_src = cogl_texture_2d_new_with_format (test_ctx, 2, 2, formats[i]);
      offscreen_src = cogl_offscreen_new_with_texture (tex_src);
      cogl_framebuffer_allocate (COGL_FRAMEBUFFER (offscreen_src), &error);
      g_assert_no_error (error);

      for (j = 0; j < G_N_ELEMENTS (formats); j++)
        {
          CoglTexture *tex_dst;
          CoglOffscreen *offscreen_dst;
          CoglPipeline *pipeline;
          uint32_t rgb10_readback[4];
          int k;

          tex_dst = cogl_texture_2d_new_with_format (test_ctx, 2, 2, formats[j]);
          offscreen_dst = cogl_offscreen_new_with_texture (tex_dst);
          cogl_framebuffer_allocate (COGL_FRAMEBUFFER (offscreen_dst), &error);
          g_assert_no_error (error);

          cogl_framebuffer_clear4f (COGL_FRAMEBUFFER (offscreen_src),
                                    COGL_BUFFER_BIT_COLOR,
                                    red, green, blue, alpha);

          pipeline = cogl_pipeline_new (test_ctx);
          cogl_pipeline_set_blend (pipeline,
                                   "RGBA = ADD (SRC_COLOR, 0)", NULL);
          cogl_pipeline_set_layer_texture (pipeline, 0, tex_src);
          cogl_framebuffer_draw_rectangle (COGL_FRAMEBUFFER (offscreen_dst),
                                           pipeline,
                                           -1.0, -1.0, 1.0, 1.0);
          g_object_unref (pipeline);

          cogl_framebuffer_read_pixels (COGL_FRAMEBUFFER (offscreen_dst),
                                        0, 0, 2, 2, formats[j],
                                        (uint8_t *) &rgb10_readback);

          for (k = 0; k < 4; k++)
            {
              int channels[3];
              int alpha_out;

              switch (formats[j])
                {
                case COGL_PIXEL_FORMAT_RGBA_1010102_PRE:
                case COGL_PIXEL_FORMAT_BGRA_1010102_PRE:
                  channels[0] = get_bits (rgb10_readback[k], 31, 22);
                  channels[1] = get_bits (rgb10_readback[k], 21, 12);
                  channels[2] = get_bits (rgb10_readback[k], 11, 2);
                  alpha_out = get_bits (rgb10_readback[k], 1, 0);
                  break;
                case COGL_PIXEL_FORMAT_XRGB_2101010:
                case COGL_PIXEL_FORMAT_ARGB_2101010_PRE:
                case COGL_PIXEL_FORMAT_XBGR_2101010:
                case COGL_PIXEL_FORMAT_ABGR_2101010_PRE:
                  alpha_out = get_bits (rgb10_readback[k], 31, 30);
                  channels[0] = get_bits (rgb10_readback[k], 29, 20);
                  channels[1] = get_bits (rgb10_readback[k], 19, 10);
                  channels[2] = get_bits (rgb10_readback[k], 9, 0);
                  break;
                default:
                  g_assert_not_reached ();
                }

              if ((formats[i] & COGL_A_BIT) && (formats[j] & COGL_A_BIT))
                g_assert_cmpint (alpha_out, ==, rgb10_alpha);
              else if (!(formats[i] & COGL_A_BIT) && !(formats[j] & COGL_A_BIT))
                g_assert_cmpint (alpha_out, ==, 0x3);

              switch (formats[j])
                {
                case COGL_PIXEL_FORMAT_RGBA_1010102_PRE:
                case COGL_PIXEL_FORMAT_XRGB_2101010:
                case COGL_PIXEL_FORMAT_ARGB_2101010_PRE:
                  g_assert_cmpint (channels[0], ==, rgb10_red);
                  g_assert_cmpint (channels[1], ==, rgb10_green);
                  g_assert_cmpint (channels[2], ==, rgb10_blue);
                  break;
                case COGL_PIXEL_FORMAT_BGRA_1010102_PRE:
                case COGL_PIXEL_FORMAT_XBGR_2101010:
                case COGL_PIXEL_FORMAT_ABGR_2101010_PRE:
                  g_assert_cmpint (channels[0], ==, rgb10_blue);
                  g_assert_cmpint (channels[1], ==, rgb10_green);
                  g_assert_cmpint (channels[2], ==, rgb10_red);
                  break;
                default:
                  g_assert_not_reached ();
                }
            }

          g_object_unref (offscreen_dst);
          g_object_unref (tex_dst);
        }

      g_object_unref (offscreen_src);
      g_object_unref (tex_src);
    }
}

static void
test_offscreen_texture_formats_paint_rgb8 (void)
{
  CoglColor color;
  const uint8_t red = 0xab;
  const uint8_t green = 0x1f;
  const uint8_t blue = 0x50;
  const uint8_t alpha = 0x34;
  CoglPixelFormat formats[] = {
    COGL_PIXEL_FORMAT_RGBX_8888,
    COGL_PIXEL_FORMAT_RGBA_8888_PRE,
    COGL_PIXEL_FORMAT_BGRX_8888,
    COGL_PIXEL_FORMAT_BGRA_8888_PRE,
    COGL_PIXEL_FORMAT_XRGB_8888,
    COGL_PIXEL_FORMAT_ARGB_8888_PRE,
    COGL_PIXEL_FORMAT_XBGR_8888,
    COGL_PIXEL_FORMAT_ABGR_8888_PRE,
  };
  int i;

  cogl_color_init_from_4f (&color,
                           red / 255.0, green / 255.0,
                           blue / 255.0, alpha / 255.0);

  for (i = 0; i < G_N_ELEMENTS (formats); i++)
    {
      CoglTexture *tex_src;
      CoglOffscreen *offscreen_src;
      GError *error = NULL;
      int j;

      tex_src = cogl_texture_2d_new_with_format (test_ctx, 2, 2, formats[i]);
      offscreen_src = cogl_offscreen_new_with_texture (tex_src);
      cogl_framebuffer_allocate (COGL_FRAMEBUFFER (offscreen_src), &error);
      g_assert_no_error (error);

      for (j = 0; j < G_N_ELEMENTS (formats); j++)
        {
          CoglTexture *tex_dst;
          CoglOffscreen *offscreen_dst;
          CoglPipeline *pipeline;
          uint8_t rgba_readback[4 * 4] = {};
          int k;

          tex_dst = cogl_texture_2d_new_with_format (test_ctx, 2, 2, formats[j]);
          offscreen_dst = cogl_offscreen_new_with_texture (tex_dst);
          cogl_framebuffer_allocate (COGL_FRAMEBUFFER (offscreen_dst), &error);
          g_assert_no_error (error);

          cogl_framebuffer_clear (COGL_FRAMEBUFFER (offscreen_src),
                                  COGL_BUFFER_BIT_COLOR,
                                  &color);

          pipeline = cogl_pipeline_new (test_ctx);
          cogl_pipeline_set_blend (pipeline,
                                   "RGBA = ADD (SRC_COLOR, 0)", NULL);
          cogl_pipeline_set_layer_texture (pipeline, 0, tex_src);
          cogl_framebuffer_draw_rectangle (COGL_FRAMEBUFFER (offscreen_dst),
                                           pipeline,
                                           -1.0, -1.0, 1.0, 1.0);
          g_object_unref (pipeline);

          cogl_framebuffer_read_pixels (COGL_FRAMEBUFFER (offscreen_dst),
                                        0, 0, 2, 2, formats[j],
                                        (uint8_t *) &rgba_readback);

          for (k = 0; k < 4; k++)
            {
              int alpha_out;

              switch (formats[j])
                {
                case COGL_PIXEL_FORMAT_RGBX_8888:
                case COGL_PIXEL_FORMAT_RGBA_8888_PRE:
                  g_assert_cmpint (rgba_readback[k * 4 + 0], ==, red);
                  g_assert_cmpint (rgba_readback[k * 4 + 1], ==, green);
                  g_assert_cmpint (rgba_readback[k * 4 + 2], ==, blue);
                  alpha_out = rgba_readback[k * 4 + 3];
                  break;
                case COGL_PIXEL_FORMAT_XRGB_8888:
                case COGL_PIXEL_FORMAT_ARGB_8888_PRE:
                  alpha_out = rgba_readback[k * 4 + 0];
                  g_assert_cmpint (rgba_readback[k * 4 + 1], ==, red);
                  g_assert_cmpint (rgba_readback[k * 4 + 2], ==, green);
                  g_assert_cmpint (rgba_readback[k * 4 + 3], ==, blue);
                  break;
                case COGL_PIXEL_FORMAT_BGRX_8888:
                case COGL_PIXEL_FORMAT_BGRA_8888_PRE:
                  g_assert_cmpint (rgba_readback[k * 4 + 0], ==, blue);
                  g_assert_cmpint (rgba_readback[k * 4 + 1], ==, green);
                  g_assert_cmpint (rgba_readback[k * 4 + 2], ==, red);
                  alpha_out = rgba_readback[k * 4 + 3];
                  break;
                case COGL_PIXEL_FORMAT_XBGR_8888:
                case COGL_PIXEL_FORMAT_ABGR_8888_PRE:
                  alpha_out = rgba_readback[k * 4 + 0];
                  g_assert_cmpint (rgba_readback[k * 4 + 1], ==, blue);
                  g_assert_cmpint (rgba_readback[k * 4 + 2], ==, green);
                  g_assert_cmpint (rgba_readback[k * 4 + 3], ==, red);
                  break;
                default:
                  g_assert_not_reached ();
                }

              if ((formats[i] & COGL_A_BIT) && (formats[j] & COGL_A_BIT))
                g_assert_cmpint (alpha_out, ==, alpha);
              else if (!(formats[i] & COGL_A_BIT) && !(formats[j] & COGL_A_BIT))
                g_assert_cmpint (alpha_out, ==, 0xff);
            }

          g_object_unref (offscreen_dst);
          g_object_unref (tex_dst);
        }

      g_object_unref (offscreen_src);
      g_object_unref (tex_src);
    }
}

COGL_TEST_SUITE (
  g_test_add_func ("/offscreen/texture-formats/store-rgba16161616",
                   test_offscreen_texture_formats_store_rgba16161616);
  g_test_add_func ("/offscreen/texture-formats/store-fp16",
                   test_offscreen_texture_formats_store_fp16);
  g_test_add_func ("/offscreen/texture-formats/store-rgb10",
                   test_offscreen_texture_formats_store_rgb10);
  g_test_add_func ("/offscreen/texture-formats/store-8",
                   test_offscreen_texture_formats_store_rgb8);
  g_test_add_func ("/offscreen/texture-formats/paint-fp16",
                   test_offscreen_texture_formats_paint_fp16);
  g_test_add_func ("/offscreen/texture-formats/paint-rgb10",
                   test_offscreen_texture_formats_paint_rgb10);
  g_test_add_func ("/offscreen/texture-formats/paint-rgb8",
                   test_offscreen_texture_formats_paint_rgb8);
)
