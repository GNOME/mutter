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

#pragma once

#include "meta-test/meta-context-test.h"

 /**
 * TestUtilsTextureFlags:
 * @TEST_UTILS_TEXTURE_NONE: No flags specified
 * @TEST_UTILS_TEXTURE_NO_AUTO_MIPMAP: Disables the automatic generation of
 *   the mipmap pyramid from the base level image whenever it is
 *   updated. The mipmaps are only generated when the texture is
 *   rendered with a mipmap filter so it should be free to leave out
 *   this flag when using other filtering modes
 * @TEST_UTILS_TEXTURE_NO_SLICING: Disables the slicing of the texture
 * @TEST_UTILS_TEXTURE_NO_ATLAS: Disables the insertion of the texture inside
 *   the texture atlas used by Cogl
 *
 * Flags to pass to the test_utils_texture_new_* family of functions.
 */
typedef enum
{
  TEST_UTILS_TEXTURE_NONE           = 0,
  TEST_UTILS_TEXTURE_NO_AUTO_MIPMAP = 1 << 0,
  TEST_UTILS_TEXTURE_NO_SLICING     = 1 << 1,
  TEST_UTILS_TEXTURE_NO_ATLAS       = 1 << 2
} TestUtilsTextureFlags;

extern CoglContext *test_ctx;
extern CoglFramebuffer *test_fb;

#define FB_WIDTH 512
#define FB_HEIGHT 512

#define COGL_TEST_SUITE(units) \
int \
main (int    argc, \
      char **argv) \
{ \
  g_autoptr (MetaContext) context = NULL; \
\
  context = meta_create_cogl_test_context (argc, argv); \
\
  units \
\
  return meta_context_test_run_tests (META_CONTEXT_TEST (context), \
                                      META_TEST_RUN_FLAG_NONE); \
}

#define COGL_TEST_SUITE_MINIMAL(units) \
int \
main (int    argc, \
      char **argv) \
{ \
  g_test_init (&argc, &argv, NULL); \
\
  units \
\
  return g_test_run (); \
}

MetaContext * meta_create_cogl_test_context (int    argc,
                                             char **argv);

/*
 * test_utils_texture_new_with_size:
 * @context: A #CoglContext
 * @width: width of texture in pixels.
 * @height: height of texture in pixels.
 * @flags: Optional flags for the texture, or %TEST_UTILS_TEXTURE_NONE
 * @components: What texture components are required
 *
 * Creates a new #CoglTexture with the specified dimensions and pixel format.
 *
 * The storage for the texture is not necessarily created before this
 * function returns. The storage can be explicitly allocated using
 * cogl_texture_allocate() or preferably you can let Cogl
 * automatically allocate the storage lazily when uploading data when
 * Cogl may know more about how the texture will be used and can
 * optimize how it is allocated.
 *
 * Return value: A newly created #CoglTexture
 */
CoglTexture * test_utils_texture_new_with_size (CoglContext           *ctx,
                                                int                    width,
                                                int                    height,
                                                TestUtilsTextureFlags  flags,
                                                CoglTextureComponents  components);

/*
 * test_utils_texture_new_from_data:
 * @context: A #CoglContext
 * @width: width of texture in pixels
 * @height: height of texture in pixels
 * @flags: Optional flags for the texture, or %TEST_UTILS_TEXTURE_NONE
 * @format: the #CoglPixelFormat the buffer is stored in in RAM
 * @rowstride: the memory offset in bytes between the starts of
 *    scanlines in @data
 * @data: pointer the memory region where the source buffer resides
 * @error: A #GError to catch exceptional errors or %NULL
 *
 * Creates a new #CoglTexture based on data residing in memory.
 *
 * Note: If the given @format has an alpha channel then the data
 * will be loaded into a premultiplied internal format. If you want
 * to avoid having the source data be premultiplied then you can
 * either specify that the data is already premultiplied or use
 * test_utils_texture_new_from_bitmap which lets you explicitly
 * request whether the data should internally be premultipled or not.
 *
 * Return value: A newly created #CoglTexture or %NULL on failure
 */
CoglTexture *
test_utils_texture_new_from_data (CoglContext           *ctx,
                                  int                    width,
                                  int                    height,
                                  TestUtilsTextureFlags  flags,
                                  CoglPixelFormat        format,
                                  int                    rowstride,
                                  const uint8_t         *data);

/*
 * test_utils_texture_new_from_bitmap:
 * @bitmap: A #CoglBitmap pointer
 * @flags: Optional flags for the texture, or %TEST_UTILS_TEXTURE_NONE
 * @premultiplied: Whether the texture should hold premultipled data.
 *                 (if the bitmap already holds premultiplied data
 *                 and %TRUE is given then no premultiplication will
 *                 be done. The data will be premultipled while
 *                 uploading if the bitmap has an alpha channel but
 *                 does not already have a premultiplied format.)
 *
 * Creates a #CoglTexture from a #CoglBitmap.
 *
 * Return value: A newly created #CoglTexture or %NULL on failure
 */
CoglTexture *
test_utils_texture_new_from_bitmap (CoglBitmap            *bitmap,
                                    TestUtilsTextureFlags  flags,
                                    gboolean               premultiplied);

/*
 * test_utils_check_pixel:
 * @framebuffer: The #CoglFramebuffer to read from
 * @x: x coordinate of the pixel to test
 * @y: y coordinate of the pixel to test
 * @pixel: An integer of the form 0xRRGGBBAA representing the expected
 *         pixel value
 *
 * This performs reads a pixel on the given cogl @framebuffer and
 * asserts that it matches the given color. The alpha channel of the
 * color is ignored. The pixels are converted to a string and compared
 * with g_assert_cmpstr so that if the comparison fails then the
 * assert will display a meaningful message
 */
void
test_utils_check_pixel (CoglFramebuffer *framebuffer,
                        int              x,
                        int              y,
                        uint32_t         expected_pixel);

/**
 * @framebuffer: The #CoglFramebuffer to read from
 * @x: x coordinate of the pixel to test
 * @y: y coordinate of the pixel to test
 * @pixel: An integer of the form 0xRRGGBBAA representing the expected
 *         pixel value
 *
 * This performs reads a pixel on the given cogl @framebuffer and
 * asserts that it matches the given color. The alpha channel is also
 * checked unlike with test_utils_check_pixel(). The pixels are
 * converted to a string and compared with g_assert_cmpstr so that if
 * the comparison fails then the assert will display a meaningful
 * message.
 */
void
test_utils_check_pixel_and_alpha (CoglFramebuffer *fb,
                                  int              x,
                                  int              y,
                                  uint32_t         expected_pixel);

/*
 * test_utils_check_pixel:
 * @framebuffer: The #CoglFramebuffer to read from
 * @x: x coordinate of the pixel to test
 * @y: y coordinate of the pixel to test
 * @pixel: An integer of the form 0xrrggbb representing the expected pixel value
 *
 * This performs reads a pixel on the given cogl @framebuffer and
 * asserts that it matches the given color. The alpha channel of the
 * color is ignored. The pixels are converted to a string and compared
 * with g_assert_cmpstr so that if the comparison fails then the
 * assert will display a meaningful message
 */
void
test_utils_check_pixel_rgb (CoglFramebuffer *framebuffer,
                            int              x,
                            int              y,
                            int              r,
                            int              g,
                            int              b);

/*
 * test_utils_check_region:
 * @framebuffer: The #CoglFramebuffer to read from
 * @x: x coordinate of the region to test
 * @y: y coordinate of the region to test
 * @width: width of the region to test
 * @height: height of the region to test
 * @pixel: An integer of the form 0xrrggbb representing the expected region color
 *
 * Performs a read pixel on the specified region of the given cogl
 * @framebuffer and asserts that it matches the given color. The alpha
 * channel of the color is ignored. The pixels are converted to a
 * string and compared with g_assert_cmpstr so that if the comparison
 * fails then the assert will display a meaningful message
 */
void
test_utils_check_region (CoglFramebuffer *framebuffer,
                         int              x,
                         int              y,
                         int              width,
                         int              height,
                         uint32_t         expected_rgba);

/*
 * test_utils_compare_pixel:
 * @screen_pixel: A pixel stored in memory
 * @expected_pixel: The expected RGBA value
 *
 * Compares a pixel from a buffer to an expected value. The pixels are
 * converted to a string and compared with g_assert_cmpstr so that if
 * the comparison fails then the assert will display a meaningful
 * message.
 */
void
test_utils_compare_pixel (const uint8_t *screen_pixel,
                          uint32_t       expected_pixel);

/*
 * test_utils_compare_pixel_and_alpha:
 * @screen_pixel: A pixel stored in memory
 * @expected_pixel: The expected RGBA value
 *
 * Compares a pixel from a buffer to an expected value. This is
 * similar to test_utils_compare_pixel() except that it doesn't ignore
 * the alpha component.
 */
void
test_utils_compare_pixel_and_alpha (const uint8_t *screen_pixel,
                                    uint32_t       expected_pixel);

/*
 * test_utils_create_color_texture:
 * @context: A #CoglContext
 * @color: A color to put in the texture
 *
 * Creates a 1x1-pixel RGBA texture filled with the given color.
 */
CoglTexture *
test_utils_create_color_texture (CoglContext *context,
                                 uint32_t     color);

/* cogl_test_verbose:
 *
 * Queries if the user asked for verbose output or not.
 */
gboolean
cogl_test_verbose (void);

/* test_util_is_pot:
 * @number: A number to test
 *
 * Returns whether the given integer is a power of two
 */
static inline gboolean
test_utils_is_pot (unsigned int number)
{
  /* Make sure there is only one bit set */
  return (number & (number - 1)) == 0;
}

/*
 * test_utils_get_cogl_gl3_vendor:
 * @context: A #CoglContext
 *
 * Gets the GL driver vendor name or %NULL if gl driver is not in use.
 */
const char *
test_utils_get_cogl_driver_vendor (CoglContext *context);
