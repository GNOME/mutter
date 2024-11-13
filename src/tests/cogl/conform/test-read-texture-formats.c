#include <cogl/cogl.h>
#include <stdarg.h>
#include <stdint.h>

#include "tests/cogl-test-utils.h"

/*
 * This tests reading back an RGBA texture in all of the available
 * pixel formats
 */

static const uint8_t tex_data[4] = { 0x12, 0x34, 0x56, 0x78 };

typedef struct {
  CoglTexture    *tex_2d;
  CoglPixelFormat format;
  uint32_t expected_value;
} TestFormatArgs;

static void
add_format_test_case (CoglTexture    *tex_2d,
                      CoglPixelFormat format,
                      uint32_t        expected_value,
                      GTestDataFunc   test_func)
{
  g_autofree char *test_name = NULL;
  TestFormatArgs *args;

  test_name = g_strdup_printf ("/read-texture-formats/%s/0x%08x",
                               cogl_pixel_format_to_string (format),
                               expected_value);
  args = g_memdup2 (&(TestFormatArgs) {
    .tex_2d = g_object_ref (tex_2d),
    .format = format,
    .expected_value = expected_value,
  }, sizeof (TestFormatArgs));
  g_test_add_data_func_full (test_name, args, test_func, g_free);
}

#define test_read(what, text_2d, format, expected_pixel) \
  add_format_test_case (tex_2d, format, expected_pixel, test_read_##what##_case)

#define test_read_byte(...) \
  test_read (byte, __VA_ARGS__)

static void
test_read_byte_case (gconstpointer data)
{
  const TestFormatArgs *args = data;
  g_autoptr (CoglTexture) tex_2d = NULL;
  uint8_t received_byte;

  tex_2d = g_steal_pointer ((CoglTexture **) &args->tex_2d);

  cogl_texture_get_data (tex_2d,
                         args->format,
                         1, /* rowstride */
                         &received_byte);

  g_assert_cmpint (args->expected_value, ==, received_byte);
}

static void
test_read_short_case (gconstpointer data)
{
  const TestFormatArgs *args = data;
  g_autoptr (CoglTexture) tex_2d = NULL;
  uint16_t expected_value = args->expected_value;
  uint16_t received_value;

  tex_2d = g_steal_pointer ((CoglTexture **) &args->tex_2d);

  char *received_value_str;
  char *expected_value_str;

  cogl_texture_get_data (tex_2d,
                         args->format,
                         2, /* rowstride */
                         (uint8_t *) &received_value);

  received_value_str = g_strdup_printf ("0x%04x", received_value);
  expected_value_str = g_strdup_printf ("0x%04x", expected_value);
  g_assert_cmpstr (received_value_str, ==, expected_value_str);
  g_free (received_value_str);
  g_free (expected_value_str);
}

static void
test_read_short (CoglTexture    *tex_2d,
                 CoglPixelFormat format,
                 ...)
{
  va_list ap;
  int bits;
  int bits_sum = 0;
  uint16_t expected_value = 0;

  va_start (ap, format);

  /* Convert the va args into a single 16-bit expected value */
  while ((bits = va_arg (ap, int)) != -1)
    {
      int value = (va_arg (ap, int) * ((1 << bits) - 1) + 128) / 255;

      bits_sum += bits;

      expected_value |= value << (16 - bits_sum);
    }

  va_end (ap);

  add_format_test_case (tex_2d, format, expected_value, test_read_short_case);
}

static void
test_read_888_case (gconstpointer data)
{
  const TestFormatArgs *args = data;
  g_autoptr (CoglTexture) tex_2d = NULL;
  uint8_t pixel[4];

  tex_2d = g_steal_pointer ((CoglTexture **) &args->tex_2d);

  cogl_texture_get_data (tex_2d,
                         args->format,
                         4, /* rowstride */
                         pixel);

  test_utils_compare_pixel (pixel, args->expected_value);
}

#define test_read_888(...) \
  test_read (888, __VA_ARGS__)

static void
test_read_88_case (gconstpointer data)
{
  const TestFormatArgs *args = data;
  g_autoptr (CoglTexture) tex_2d = NULL;
  uint8_t pixel[4];

  pixel[2] = 0x00;
  tex_2d = g_steal_pointer ((CoglTexture **) &args->tex_2d);

  cogl_texture_get_data (tex_2d,
                         args->format,
                         2, /* rowstride */
                         pixel);

  test_utils_compare_pixel (pixel, args->expected_value);
}

#define test_read_88(...) \
  test_read (88, __VA_ARGS__)

static void
test_read_8888_case (gconstpointer data)
{
  const TestFormatArgs *args = data;
  g_autoptr (CoglTexture) tex_2d = NULL;
  uint32_t received_pixel;
  char *received_value_str;
  char *expected_value_str;

  tex_2d = g_steal_pointer ((CoglTexture **) &args->tex_2d);

  cogl_texture_get_data (tex_2d,
                         args->format,
                         4, /* rowstride */
                         (uint8_t *) &received_pixel);

  received_pixel = GUINT32_FROM_BE (received_pixel);

  received_value_str = g_strdup_printf ("0x%08x", received_pixel);
  expected_value_str = g_strdup_printf ("0x%08x", args->expected_value);
  g_assert_cmpstr (received_value_str, ==, expected_value_str);
  g_free (received_value_str);
  g_free (expected_value_str);
}

#define test_read_8888(...) \
  test_read (8888, __VA_ARGS__)

static void
test_read_int_case (gconstpointer data)
{
  const TestFormatArgs *args = data;
  g_autoptr (CoglTexture) tex_2d = NULL;
  uint32_t received_value;
  g_autofree char *received_value_str = NULL;
  g_autofree char *expected_value_str = NULL;

  tex_2d = g_steal_pointer ((CoglTexture **) &args->tex_2d);

  cogl_texture_get_data (tex_2d,
                         args->format,
                         4, /* rowstride */
                         (uint8_t *) &received_value);

  received_value_str = g_strdup_printf ("0x%08x", received_value);
  expected_value_str = g_strdup_printf ("0x%08x", args->expected_value);

  /* The test is currently failing using sw rendering or intel hardware, but
   * it does pass under AMD, so at least keep checking if this case works.
   */
  if (!g_test_undefined () &&
      !g_str_equal (test_utils_get_cogl_driver_vendor (test_ctx), "AMD"))
    {
      /* This test case is always considered failing */
      g_test_skip_printf ("This test is a well known failure, "
                          "expected: '%s', actual: '%s'",
                          expected_value_str, received_value_str);
      return;
    }
  else if (g_test_undefined () &&
           g_str_equal (test_utils_get_cogl_driver_vendor (test_ctx), "AMD") &&
           cogl_renderer_get_driver_id (cogl_context_get_renderer (test_ctx)) ==
                                        COGL_DRIVER_ID_GL3)
    {
      g_test_fail_printf ("This test is not failing on AMD, but we mark it "
                          "to make meson happy.");
      return;
    }

  g_assert_cmpstr (received_value_str, ==, expected_value_str);
}

static void
test_read_int (CoglTexture    *tex_2d,
               CoglPixelFormat format,
               ...)
{
  va_list ap;
  uint32_t expected_value = 0;
  int bits;
  int bits_sum = 0;

  va_start (ap, format);

  /* Convert the va args into a single 32-bit expected value */
  while ((bits = va_arg (ap, int)) != -1)
    {
      uint32_t value = (va_arg (ap, int) * ((1 << bits) - 1) + 128) / 255;

      bits_sum += bits;

      expected_value |= value << (32 - bits_sum);
    }

  va_end (ap);

  add_format_test_case (tex_2d, format, expected_value, test_read_int_case);
}

static void
test_read_texture_formats (void)
{
  CoglTexture *tex_2d;

  tex_2d = cogl_texture_2d_new_from_data (test_ctx,
                                          1, 1, /* width / height */
                                          COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                                          4, /* rowstride */
                                          tex_data,
                                          NULL);

  test_read_byte (tex_2d, COGL_PIXEL_FORMAT_A_8, 0x78);

#if 0
  /* I'm not sure what's the right value to put here because Nvidia
     and Mesa seem to behave differently so one of them must be
     wrong. */
  test_read_byte (tex_2d, COGL_PIXEL_FORMAT_R_8, 0x9c);
#endif

  /* We should always be able to read into an RG buffer regardless of
   * whether RG textures are supported because Cogl will do the
   * conversion for us */
  test_read_88 (tex_2d, COGL_PIXEL_FORMAT_RG_88, 0x123400ff);

  test_read_short (tex_2d, COGL_PIXEL_FORMAT_RGB_565,
                   5, 0x12, 6, 0x34, 5, 0x56,
                   -1);
  test_read_short (tex_2d, COGL_PIXEL_FORMAT_RGBA_4444_PRE,
                   4, 0x12, 4, 0x34, 4, 0x56, 4, 0x78,
                   -1);
  test_read_short (tex_2d, COGL_PIXEL_FORMAT_RGBA_5551_PRE,
                   5, 0x12, 5, 0x34, 5, 0x56, 1, 0x78,
                   -1);

  test_read_888 (tex_2d, COGL_PIXEL_FORMAT_RGB_888, 0x123456ff);
  test_read_888 (tex_2d, COGL_PIXEL_FORMAT_BGR_888, 0x563412ff);

  test_read_8888 (tex_2d, COGL_PIXEL_FORMAT_RGBA_8888_PRE, 0x12345678);
  test_read_8888 (tex_2d, COGL_PIXEL_FORMAT_BGRA_8888_PRE, 0x56341278);
  test_read_8888 (tex_2d, COGL_PIXEL_FORMAT_ARGB_8888_PRE, 0x78123456);
  test_read_8888 (tex_2d, COGL_PIXEL_FORMAT_ABGR_8888_PRE, 0x78563412);

  test_read_int (tex_2d, COGL_PIXEL_FORMAT_RGBA_1010102_PRE,
                 10, 0x12, 10, 0x34, 10, 0x56, 2, 0x78,
                 -1);
  test_read_int (tex_2d, COGL_PIXEL_FORMAT_BGRA_1010102_PRE,
                 10, 0x56, 10, 0x34, 10, 0x12, 2, 0x78,
                 -1);
  test_read_int (tex_2d, COGL_PIXEL_FORMAT_ARGB_2101010_PRE,
                 2, 0x78, 10, 0x12, 10, 0x34, 10, 0x56,
                 -1);
  test_read_int (tex_2d, COGL_PIXEL_FORMAT_ABGR_2101010_PRE,
                 2, 0x78, 10, 0x56, 10, 0x34, 10, 0x12,
                 -1);

  g_object_unref (tex_2d);

  if (cogl_test_verbose ())
    g_print ("OK\n");
}

COGL_TEST_SUITE (
  g_test_add_func ("/read-texture-formats", test_read_texture_formats);
)
