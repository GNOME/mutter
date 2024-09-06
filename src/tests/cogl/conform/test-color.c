#include <math.h>
#include <string.h>

#include <cogl/cogl.h>

#include "tests/cogl-test-utils.h"

#define TEST_CASE_EPSILON 0.01f

static void
test_color_hsl (void)
{
  CoglColor color;
  float hue, saturation, luminance;

  cogl_color_init_from_4f (&color, 108.0f / 255.0f, 198 / 255.0f, 78.0f / 255.0f, 1.0f);
  cogl_color_to_hsl (&color, &hue, &saturation, &luminance);

  g_assert_cmpfloat_with_epsilon (hue, 105.f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (saturation, 0.512821, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (luminance, 0.541176, TEST_CASE_EPSILON);

  memset (&color, 0, sizeof (CoglColor));
  cogl_color_init_from_hsl (&color, hue, saturation, luminance);

  g_assert_cmpfloat_with_epsilon (cogl_color_get_red (&color), 108.0f / 255.0f,
                                  TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_green (&color), 198.0f / 255.0f,
                                  TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_blue (&color), 78.0f / 255.0f,
                                  TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_alpha (&color), 1.0f,
                                  TEST_CASE_EPSILON);

  memset (&color, 0, sizeof (CoglColor));
  cogl_color_init_from_hsl (&color, hue, 0, luminance);

  g_assert_cmpfloat_with_epsilon (cogl_color_get_red (&color), luminance,
                                  TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_green (&color), luminance,
                                  TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_blue (&color), luminance,
                                  TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_alpha (&color), 1.0f,
                                  TEST_CASE_EPSILON);

  if (cogl_test_verbose ())
    g_print ("OK\n");
}

static void
color_hls_roundtrip (void)
{
  CoglColor color;
  gfloat hue, luminance, saturation;

  /* test luminance only */
  cogl_color_from_string (&color, "#7f7f7f");
  g_assert_cmpfloat_with_epsilon (cogl_color_get_red (&color),
                                  (float)0x7f / 255.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_green (&color),
                                  (float)0x7f / 255.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_blue (&color),
                                  (float)0x7f / 255.0f, TEST_CASE_EPSILON);

  cogl_color_to_hsl (&color, &hue, &saturation, &luminance);
  g_assert_cmpfloat (hue, ==, 0.0f);
  g_assert_true (luminance >= 0.0f && luminance <= 1.0f);
  g_assert_cmpfloat (saturation, ==, 0.0f);
  if (!g_test_quiet ())
    {
      g_print ("RGB = { %x, %x, %x }, HLS = { %.2f, %.2f, %.2f }\n",
               color.red,
               color.green,
               color.blue,
               hue,
               luminance,
               saturation);
    }

  color.red = color.green = color.blue = 0;
  cogl_color_init_from_hsl (&color, hue, saturation, luminance);

  g_assert_cmpfloat_with_epsilon (cogl_color_get_red (&color),
                                  (float)0x7f / 255.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_green (&color),
                                  (float)0x7f / 255.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_blue (&color),
                                  (float)0x7f / 255.0f, TEST_CASE_EPSILON);

  /* full conversion */
  cogl_color_from_string (&color, "#7f8f7f");
  color.alpha = 255;

  g_assert_cmpfloat_with_epsilon (cogl_color_get_red (&color),
                                  (float)0x7f / 255.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_green (&color),
                                  (float)0x8f / 255.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_blue (&color),
                                  (float)0x7f / 255.0f, TEST_CASE_EPSILON);

  cogl_color_to_hsl (&color, &hue, &saturation, &luminance);
  g_assert_true (hue >= 0.0f && hue < 360.0f);
  g_assert_true (luminance >= 0.0f && luminance <= 1.0f);
  g_assert_true (saturation >= 0.0f && saturation <= 1.0f);
  if (!g_test_quiet ())
    {
      g_print ("RGB = { %x, %x, %x }, HLS = { %.2f, %.2f, %.2f }\n",
               color.red,
               color.green,
               color.blue,
               hue,
               luminance,
               saturation);
    }

  color.red = color.green = color.blue = 0;
  cogl_color_init_from_hsl (&color, hue, saturation, luminance);

  g_assert_cmpfloat_with_epsilon (cogl_color_get_red (&color),
                                  (float)0x7f / 255.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_green (&color),
                                  (float)0x8f / 255.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_blue (&color),
                                  (float)0x7f / 255.0f, TEST_CASE_EPSILON);

  /* the alpha channel should be untouched */
  g_assert_cmpuint (color.alpha, ==, 255);
}

static void
color_from_string_invalid (void)
{
  CoglColor color;

  g_assert_false (cogl_color_from_string (&color, "ff0000ff"));
  g_assert_false (cogl_color_from_string (&color, "#decaffbad"));
  g_assert_false (cogl_color_from_string (&color, "ponies"));
  g_assert_false (cogl_color_from_string (&color, "rgb(255, 0, 0, 0)"));
  g_assert_false (cogl_color_from_string (&color, "rgba(1.0, 0, 0)"));
  g_assert_false (cogl_color_from_string (&color, "hsl(100, 0, 0)"));
  g_assert_false (cogl_color_from_string (&color, "hsla(10%, 0%, 50%)"));
  g_assert_false (cogl_color_from_string (&color, "hsla(100%, 0%, 50%, 20%)"));
  g_assert_false (cogl_color_from_string (&color, "hsla(0.5, 0.9, 0.2, 0.4)"));
}

static void
color_from_string_valid (void)
{
  CoglColor color;

  g_assert_true (cogl_color_from_string (&color, "#ff0000ff"));
  if (!g_test_quiet ())
    {
      g_print ("color = { %x, %x, %x, %x }, expected = { 0xff, 0, 0, 0xff }\n",
               color.red,
               color.green,
               color.blue,
               color.alpha);
    }
  g_assert_cmpfloat_with_epsilon (cogl_color_get_red (&color),
                                  1.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_green (&color),
                                  0.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_blue (&color),
                                  0.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_alpha (&color),
                                  1.0f, TEST_CASE_EPSILON);

  g_assert_true (cogl_color_from_string (&color, "#0f0f"));
  if (!g_test_quiet ())
    {
      g_print ("color = { %x, %x, %x, %x }, expected = { 0, 0xff, 0, 0xff }\n",
               color.red,
               color.green,
               color.blue,
               color.alpha);
    }
  g_assert_cmpfloat_with_epsilon (cogl_color_get_red (&color),
                                  0.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_green (&color),
                                  1.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_blue (&color),
                                  0.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_alpha (&color),
                                  1.0f, TEST_CASE_EPSILON);

  g_assert_true (cogl_color_from_string (&color, "#0000ff"));
  if (!g_test_quiet ())
    {
      g_print ("color = { %x, %x, %x, %x }, expected = { 0, 0, 0xff, 0xff }\n",
               color.red,
               color.green,
               color.blue,
               color.alpha);
    }
  g_assert_cmpfloat_with_epsilon (cogl_color_get_red (&color),
                                  0.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_green (&color),
                                  0.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_blue (&color),
                                  1.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_alpha (&color),
                                  1.0f, TEST_CASE_EPSILON);

  g_assert_true (cogl_color_from_string (&color, "#abc"));
  if (!g_test_quiet ())
    {
      g_print ("color = { %x, %x, %x, %x }, expected = { 0xaa, 0xbb, 0xcc, 0xff }\n",
               color.red,
               color.green,
               color.blue,
               color.alpha);
    }
  g_assert_cmpfloat_with_epsilon (cogl_color_get_red (&color),
                                  (float)0xaa / 255.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_green (&color),
                                  (float)0xbb / 255.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_blue (&color),
                                  (float)0xcc / 255.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_alpha (&color),
                                  1.0f, TEST_CASE_EPSILON);

  g_assert_true (cogl_color_from_string (&color, "#123abc"));
  if (!g_test_quiet ())
    {
      g_print ("color = { %x, %x, %x, %x }, expected = { 0x12, 0x3a, 0xbc, 0xff }\n",
               color.red,
               color.green,
               color.blue,
               color.alpha);
    }
  g_assert_cmpfloat_with_epsilon (cogl_color_get_red (&color),
                                  (float)0x12 / 255.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_green (&color),
                                  (float)0x3a / 255.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_blue (&color),
                                  (float)0xbc / 255.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_alpha (&color),
                                  1.0f, TEST_CASE_EPSILON);

  g_assert_true (cogl_color_from_string (&color, "rgb(255, 128, 64)"));
  if (!g_test_quiet ())
    {
      g_print ("color = { %x, %x, %x, %x }, expected = { 255, 128, 64, 255 }\n",
               color.red,
               color.green,
               color.blue,
               color.alpha);
    }
  g_assert_cmpfloat_with_epsilon (cogl_color_get_red (&color),
                                  1.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_green (&color),
                                  128.0f / 255.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_blue (&color),
                                  64.0f / 255.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_alpha (&color),
                                  1.0f, TEST_CASE_EPSILON);

  g_assert_true (cogl_color_from_string (&color, "rgba ( 30%, 0,    25%,  0.5 )   "));
  if (!g_test_quiet ())
    {
      g_print ("color = { %x, %x, %x, %x }, expected = { %.1f, 0, %.1f, 128 }\n",
               color.red,
               color.green,
               color.blue,
               color.alpha,
               CLAMP (255.0f / 100.0f * 30.0f, 0, 255),
               CLAMP (255.0f / 100.0f * 25.0f, 0, 255));
    }
  g_assert_cmpfloat_with_epsilon (cogl_color_get_red (&color),
                                  (255.0f / 100.0f * 30.0f) / 255.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_green (&color),
                                  0.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_blue (&color),
                                  (255.0f / 100.0f * 25.0f) / 255.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_alpha (&color),
                                  0.5, TEST_CASE_EPSILON);

  g_assert_true (cogl_color_from_string (&color, "rgb( 50%, -50%, 150% )"));
  if (!g_test_quiet ())
    {
      g_print ("color = { %x, %x, %x, %x }, expected = { 127, 0, 255, 255 }\n",
               color.red,
               color.green,
               color.blue,
               color.alpha);
    }
  g_assert_cmpfloat_with_epsilon (cogl_color_get_red (&color),
                                  0.5, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_green (&color),
                                  0.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_blue (&color),
                                  1.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_alpha (&color),
                                  1.0f, TEST_CASE_EPSILON);

  g_assert_true (cogl_color_from_string (&color, "hsl( 0, 100%, 50% )"));
  if (!g_test_quiet ())
    {
      g_print ("color = { %x, %x, %x, %x }, expected = { 255, 0, 0, 255 }\n",
               color.red,
               color.green,
               color.blue,
               color.alpha);
    }
  g_assert_cmpfloat_with_epsilon (cogl_color_get_red (&color),
                                  1.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_green (&color),
                                  0.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_blue (&color),
                                  0.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_alpha (&color),
                                  1.0f, TEST_CASE_EPSILON);

  g_assert_true (cogl_color_from_string (&color, "hsl( 0, 100%, 50%     )"));

  g_assert_true (cogl_color_from_string (&color, "hsla( 0, 100%, 50%, 0.5 )"));
  if (!g_test_quiet ())
    {
      g_print ("color = { %x, %x, %x, %x }, expected = { 255, 0, 0, 127 }\n",
               color.red,
               color.green,
               color.blue,
               color.alpha);
    }
  g_assert_cmpfloat_with_epsilon (cogl_color_get_red (&color),
                                  1.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_green (&color),
                                  0.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_blue (&color),
                                  0.0f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_alpha (&color),
                                  0.5, TEST_CASE_EPSILON);


  g_test_bug ("662818");
  g_assert_true (cogl_color_from_string (&color, "hsla(0,100%,50% , 0.5)"));
}

static void
color_to_string (void)
{
  CoglColor color;
  gchar *str;

  color.red = 0xcc;
  color.green = 0xcc;
  color.blue = 0xcc;
  color.alpha = 0x22;

  str = cogl_color_to_string (&color);
  g_assert_cmpstr (str, ==, "#cccccc22");

  g_free (str);
}

COGL_TEST_SUITE (
  g_test_add_func ("/color/hsl", test_color_hsl);
  g_test_add_func ("/color/hls-roundtrip", color_hls_roundtrip);
  g_test_add_func ("/color/from-string/invalid", color_from_string_invalid);
  g_test_add_func ("/color/from-string/valid", color_from_string_valid);
  g_test_add_func ("/color/to-string", color_to_string);
)
