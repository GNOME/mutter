#include <math.h>
#include <string.h>

#include <cogl/cogl.h>

#include "tests/cogl-test-utils.h"

#define TEST_CASE_EPSILON 0.0001

static void
test_color_hsl (void)
{
  CoglColor color;
  float hue, saturation, luminance;

  cogl_color_init_from_4f (&color, 108.0 / 255.0, 198 / 255.0, 78.0 / 255.0, 1.0);
  cogl_color_to_hsl(&color, &hue, &saturation, &luminance);

  g_assert_cmpfloat_with_epsilon (hue, 105.f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (saturation, 0.512821, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (luminance, 0.541176, TEST_CASE_EPSILON);

  memset(&color, 0, sizeof (CoglColor));
  cogl_color_init_from_hsl (&color, hue, saturation, luminance);

  g_assert_cmpfloat_with_epsilon (cogl_color_get_red (&color), 108.0 / 255.0,
                                  TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_green (&color), 198.0 / 255.0,
                                  TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_blue (&color), 78.0 / 255.0,
                                  TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_alpha (&color), 1.0,
                                  TEST_CASE_EPSILON);

  memset(&color, 0, sizeof (CoglColor));
  cogl_color_init_from_hsl (&color, hue, 0, luminance);

  g_assert_cmpfloat_with_epsilon (cogl_color_get_red (&color), luminance,
                                  TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_green (&color), luminance,
                                  TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_blue (&color), luminance,
                                  TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_alpha (&color), 1.0,
                                  TEST_CASE_EPSILON);

  if (cogl_test_verbose ())
    g_print ("OK\n");
}

COGL_TEST_SUITE (
  g_test_add_func ("/color/hsl", test_color_hsl);
)
