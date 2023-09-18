/*
 * Copyright (C) 2022 Red Hat Inc.
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
 *
 */

#include "config.h"

#include "tests/cogl-test-utils.h"

#include "backends/meta-backend-private.h"

static gboolean cogl_test_is_verbose;
CoglContext *test_ctx;
CoglFramebuffer *test_fb;

static gboolean
compare_component (int a, int b)
{
  return ABS (a - b) <= 1;
}

void
test_utils_compare_pixel_and_alpha (const uint8_t *screen_pixel,
                                    uint32_t       expected_pixel)
{
  /* Compare each component with a small fuzz factor */
  if (!compare_component (screen_pixel[0], expected_pixel >> 24) ||
      !compare_component (screen_pixel[1], (expected_pixel >> 16) & 0xff) ||
      !compare_component (screen_pixel[2], (expected_pixel >> 8) & 0xff) ||
      !compare_component (screen_pixel[3], (expected_pixel >> 0) & 0xff))
    {
      uint32_t screen_pixel_num = GUINT32_FROM_BE (*(uint32_t *) screen_pixel);
      char *screen_pixel_string =
        g_strdup_printf ("#%08x", screen_pixel_num);
      char *expected_pixel_string =
        g_strdup_printf ("#%08x", expected_pixel);

      g_assert_cmpstr (screen_pixel_string, ==, expected_pixel_string);

      g_free (screen_pixel_string);
      g_free (expected_pixel_string);
    }
}

void
test_utils_compare_pixel (const uint8_t *screen_pixel,
                          uint32_t       expected_pixel)
{
  /* Compare each component with a small fuzz factor */
  if (!compare_component (screen_pixel[0], expected_pixel >> 24) ||
      !compare_component (screen_pixel[1], (expected_pixel >> 16) & 0xff) ||
      !compare_component (screen_pixel[2], (expected_pixel >> 8) & 0xff))
    {
      uint32_t screen_pixel_num = GUINT32_FROM_BE (*(uint32_t *) screen_pixel);
      char *screen_pixel_string =
        g_strdup_printf ("#%06x", screen_pixel_num >> 8);
      char *expected_pixel_string =
        g_strdup_printf ("#%06x", expected_pixel >> 8);

      g_assert_cmpstr (screen_pixel_string, ==, expected_pixel_string);

      g_free (screen_pixel_string);
      g_free (expected_pixel_string);
    }
}

void
test_utils_check_pixel (CoglFramebuffer *test_fb,
                        int              x,
                        int              y,
                        uint32_t         expected_pixel)
{
  uint8_t pixel[4];

  cogl_framebuffer_read_pixels (test_fb,
                                x, y, 1, 1,
                                COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                                pixel);

  test_utils_compare_pixel (pixel, expected_pixel);
}

void
test_utils_check_pixel_and_alpha (CoglFramebuffer *test_fb,
                                  int              x,
                                  int              y,
                                  uint32_t         expected_pixel)
{
  uint8_t pixel[4];

  cogl_framebuffer_read_pixels (test_fb,
                                x, y, 1, 1,
                                COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                                pixel);

  test_utils_compare_pixel_and_alpha (pixel, expected_pixel);
}

void
test_utils_check_pixel_rgb (CoglFramebuffer *test_fb,
                            int              x,
                            int              y,
                            int              r,
                            int              g,
                            int              b)
{
  g_return_if_fail (r >= 0);
  g_return_if_fail (g >= 0);
  g_return_if_fail (b >= 0);
  g_return_if_fail (r <= 0xFF);
  g_return_if_fail (g <= 0xFF);
  g_return_if_fail (b <= 0xFF);

  test_utils_check_pixel (test_fb, x, y,
                          (((guint32) r) << 24) |
                          (((guint32) g) << 16) |
                          (((guint32) b) << 8));
}

void
test_utils_check_region (CoglFramebuffer *test_fb,
                         int              x,
                         int              y,
                         int              width,
                         int              height,
                         uint32_t         expected_rgba)
{
  uint8_t *pixels, *p;

  pixels = p = g_malloc (width * height * 4);
  cogl_framebuffer_read_pixels (test_fb,
                                x,
                                y,
                                width,
                                height,
                                COGL_PIXEL_FORMAT_RGBA_8888,
                                p);

  /* Check whether the center of each division is the right color */
  for (y = 0; y < height; y++)
    {
      for (x = 0; x < width; x++)
        {
          test_utils_compare_pixel (p, expected_rgba);
          p += 4;
        }
    }

  g_free (pixels);
}

CoglTexture *
test_utils_create_color_texture (CoglContext *context,
                                 uint32_t     color)
{
  CoglTexture *tex_2d;

  color = GUINT32_TO_BE (color);

  tex_2d = cogl_texture_2d_new_from_data (context,
                                          1, 1, /* width/height */
                                          COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                                          4, /* rowstride */
                                          (uint8_t *) &color,
                                          NULL);

  return tex_2d;
}

gboolean
cogl_test_verbose (void)
{
  return cogl_test_is_verbose;
}

static void
set_auto_mipmap_cb (CoglTexture *sub_texture,
                    const float *sub_texture_coords,
                    const float *meta_coords,
                    void        *user_data)
{
  cogl_primitive_texture_set_auto_mipmap (sub_texture, FALSE);
}

CoglTexture *
test_utils_texture_new_with_size (CoglContext           *ctx,
                                  int                    width,
                                  int                    height,
                                  TestUtilsTextureFlags  flags,
                                  CoglTextureComponents  components)
{
  CoglTexture *tex;
  GError *skip_error = NULL;

  /* First try creating a fast-path non-sliced texture */
  tex = cogl_texture_2d_new_with_size (ctx, width, height);

  cogl_texture_set_components (tex, components);

  if (!cogl_texture_allocate (tex, &skip_error))
    {
      g_error_free (skip_error);
      g_object_unref (tex);
      tex = NULL;
    }

  if (!tex)
    {
      /* If it fails resort to sliced textures */
      int max_waste = flags & TEST_UTILS_TEXTURE_NO_SLICING ?
        -1 : COGL_TEXTURE_MAX_WASTE;
      tex =
        cogl_texture_2d_sliced_new_with_size (ctx,
                                              width,
                                              height,
                                              max_waste);
      cogl_texture_set_components (tex, components);
    }

  if (flags & TEST_UTILS_TEXTURE_NO_AUTO_MIPMAP)
    {
      /* To be able to iterate the slices of a #CoglTexture2DSliced we
       * need to ensure the texture is allocated... */
      cogl_texture_allocate (tex, NULL); /* don't catch exceptions */

      cogl_meta_texture_foreach_in_region (tex,
                                           0, 0, 1, 1,
                                           COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE,
                                           COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE,
                                           set_auto_mipmap_cb,
                                           NULL); /* don't catch exceptions */
    }

  cogl_texture_allocate (tex, NULL);

  return tex;
}

CoglTexture *
test_utils_texture_new_from_bitmap (CoglBitmap            *bitmap,
                                    TestUtilsTextureFlags  flags,
                                    gboolean               premultiplied)
{
  CoglTexture *atlas_tex;
  CoglTexture *tex;
  GError *internal_error = NULL;

  if (!flags)
    {
      /* First try putting the texture in the atlas */
      atlas_tex = cogl_atlas_texture_new_from_bitmap (bitmap);

      cogl_texture_set_premultiplied (atlas_tex, premultiplied);

      if (cogl_texture_allocate (atlas_tex, &internal_error))
        return atlas_tex;

      g_object_unref (atlas_tex);
    }

  g_clear_error (&internal_error);

  /* If that doesn't work try a fast path 2D texture */
  tex = cogl_texture_2d_new_from_bitmap (bitmap);

  cogl_texture_set_premultiplied (tex, premultiplied);

  if (g_error_matches (internal_error,
                       COGL_SYSTEM_ERROR,
                       COGL_SYSTEM_ERROR_NO_MEMORY))
    {
      g_assert_not_reached ();
      return NULL;
    }

  g_clear_error (&internal_error);

  if (!tex)
    {
      /* Otherwise create a sliced texture */
      int max_waste = flags & TEST_UTILS_TEXTURE_NO_SLICING ?
        -1 : COGL_TEXTURE_MAX_WASTE;
      tex =
        cogl_texture_2d_sliced_new_from_bitmap (bitmap, max_waste);

      cogl_texture_set_premultiplied (tex, premultiplied);
    }

  if (flags & TEST_UTILS_TEXTURE_NO_AUTO_MIPMAP)
    {
      cogl_meta_texture_foreach_in_region (tex,
                                           0, 0, 1, 1,
                                           COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE,
                                           COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE,
                                           set_auto_mipmap_cb,
                                           NULL); /* don't catch exceptions */
    }

  cogl_texture_allocate (tex, NULL);

  return tex;
}

CoglTexture *
test_utils_texture_new_from_data (CoglContext           *ctx,
                                  int                    width,
                                  int                    height,
                                  TestUtilsTextureFlags  flags,
                                  CoglPixelFormat        format,
                                  int                    rowstride,
                                  const uint8_t         *data)
{
  CoglBitmap *bmp;
  CoglTexture *tex;

  g_assert_cmpint (format, !=, COGL_PIXEL_FORMAT_ANY);
  g_assert (data != NULL);

  /* Wrap the data into a bitmap */
  bmp = cogl_bitmap_new_for_data (ctx,
                                  width, height,
                                  format,
                                  rowstride,
                                  (uint8_t *) data);

  tex = test_utils_texture_new_from_bitmap (bmp, flags, TRUE);

  g_object_unref (bmp);

  return tex;
}

static void
on_before_tests (MetaContext *context)
{
  MetaBackend *backend = meta_context_get_backend (context);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglOffscreen *offscreen;
  CoglTexture *tex;
  GError *error = NULL;

  test_ctx = clutter_backend_get_cogl_context (clutter_backend);

  tex = cogl_texture_2d_new_with_size (test_ctx, FB_WIDTH, FB_HEIGHT);
  g_assert_nonnull (tex);
  offscreen = cogl_offscreen_new_with_texture (tex);
  g_assert_nonnull (offscreen);
  test_fb = COGL_FRAMEBUFFER (offscreen);

  if (!cogl_framebuffer_allocate (test_fb, &error))
    g_error ("Failed to allocate framebuffer: %s", error->message);

  cogl_framebuffer_clear4f (test_fb,
                            COGL_BUFFER_BIT_COLOR |
                            COGL_BUFFER_BIT_DEPTH |
                            COGL_BUFFER_BIT_STENCIL,
                            0, 0, 0, 1);
}

static void
on_after_tests (MetaContext *context)
{
  g_clear_object (&test_fb);
}

MetaContext *
meta_create_cogl_test_context (int    argc,
                               char **argv)
{
  MetaContext *context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert (meta_context_configure (context, &argc, &argv, NULL));

  if (g_strcmp0 ("COGL_TEST_VERBOSE", "1") == 0)
    cogl_test_is_verbose = TRUE;

  g_signal_connect (context, "before-tests",
                    G_CALLBACK (on_before_tests), NULL);
  g_signal_connect (context, "after-tests",
                    G_CALLBACK (on_after_tests), NULL);

  return context;
}
