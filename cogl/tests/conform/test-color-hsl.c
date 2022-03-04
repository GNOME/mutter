#include <math.h>
#include <string.h>

#include <cogl/cogl.h>

#include "test-declarations.h"
#include "test-utils.h"

#define TEST_CASE_EPSILON 0.0001

void
test_color_hsl (void)
{
  CoglColor color;
  float hue, saturation, luminance;

  cogl_color_init_from_4ub(&color, 108, 198, 78, 255);
  cogl_color_to_hsl(&color, &hue, &saturation, &luminance);

  g_assert_cmpfloat_with_epsilon (hue, 105.f, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (saturation, 0.512821, TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (luminance, 0.541176, TEST_CASE_EPSILON);

  memset(&color, 0, sizeof (CoglColor));
  cogl_color_init_from_hsl(&color, hue, saturation, luminance);

  g_assert_cmpint (cogl_color_get_red_byte (&color), ==, 108);
  g_assert_cmpint (cogl_color_get_green_byte (&color), ==, 198);
  g_assert_cmpint (cogl_color_get_blue_byte (&color), ==, 78);
  g_assert_cmpint (cogl_color_get_alpha_byte (&color), ==, 255);

  memset(&color, 0, sizeof (CoglColor));
  cogl_color_init_from_hsl(&color, hue, 0, luminance);

  g_assert_cmpfloat_with_epsilon (cogl_color_get_red_float (&color), luminance,
                                  TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_green_float (&color), luminance,
                                  TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_blue_float (&color), luminance,
                                  TEST_CASE_EPSILON);
  g_assert_cmpfloat_with_epsilon (cogl_color_get_alpha_float (&color), 1.0,
                                  TEST_CASE_EPSILON);

  if (cogl_test_verbose ())
    g_print ("OK\n");
}
