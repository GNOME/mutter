#include "tests/clutter-test-utils.h"

#include <clutter/clutter.h>

static void
point_on_nonempty_quadrilateral (void)
{
  int p;
  static const ClutterPoint vertices[4] =
    {
      { 1.f, 2.f },
      { 6.f, 3.f },
      { 7.f, 6.f },
      { 0.f, 5.f }
    };
  static const ClutterPoint points_inside[] =
    {
      { 2.f, 3.f },
      { 1.f, 4.f },
      { 5.f, 5.f },
      { 4.f, 3.f },
    };
  static const ClutterPoint points_outside[] =
    {
      { 3.f, 1.f },
      { 7.f, 4.f },
      { 4.f, 6.f },
      { 99.f, -77.f },
      { -1.f, 3.f },
      { -8.f, -8.f },
      { 11.f, 4.f },
      { -7.f, 4.f },
    };

  for (p = 0; p < G_N_ELEMENTS (points_inside); p++)
    {
      const ClutterPoint *point = &points_inside[p];

      g_assert_true (clutter_point_inside_quadrilateral (point, vertices));
    }

  for (p = 0; p < G_N_ELEMENTS (points_outside); p++)
    {
      const ClutterPoint *point = &points_outside[p];

      g_assert_false (clutter_point_inside_quadrilateral (point, vertices));
    }
}

static void
point_on_empty_quadrilateral (void)
{
  int p;
  static const ClutterPoint vertices[4] =
    {
      { 5.f, 6.f },
      { 5.f, 6.f },
      { 5.f, 6.f },
      { 5.f, 6.f },
    };
  static const ClutterPoint points_outside[] =
    {
      { 3.f, 1.f },
      { 7.f, 4.f },
      { 4.f, 6.f },
      { 99.f, -77.f },
      { -1.f, 3.f },
      { -8.f, -8.f },
    };

  for (p = 0; p < G_N_ELEMENTS (points_outside); p++)
    {
      const ClutterPoint *point = &points_outside[p];

      g_assert_false (clutter_point_inside_quadrilateral (point, vertices));
    }

  g_assert_false (clutter_point_inside_quadrilateral (&vertices[0], vertices));
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/point/on_nonempty_quadrilateral", point_on_nonempty_quadrilateral)
  CLUTTER_TEST_UNIT ("/point/on_empty_quadrilateral", point_on_empty_quadrilateral)
)
