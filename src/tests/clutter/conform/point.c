#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"

static void
point_on_nonempty_polygon (void)
{
  int p;
  static const ClutterPoint vertices[4] =
    {
      {1.f, 2.f},
      {6.f, 3.f},
      {7.f, 6.f},
      {0.f, 5.f}
    };
  static const ClutterPoint points_inside[] =
    {
      {2.f, 3.f},
      {1.f, 4.f},
      {5.f, 5.f},
      {4.f, 3.f},
    };
  static const ClutterPoint points_outside[] =
    {
      {3.f, 1.f},
      {7.f, 4.f},
      {4.f, 6.f},
      {99.f, -77.f},
      {-1.f, 3.f},
      {-8.f, -8.f},
      {11.f, 4.f},
      {-7.f, 4.f},
    };
  static const ClutterPoint points_touching[] =
    {
      {1.f, 2.f},
      {3.5f, 2.5f},
      {6.f, 3.f},
      {6.5f, 4.5f},
      {7.f, 6.f},
      {3.5f, 5.5f},
      {0.f, 5.f}
    };

  for (p = 0; p < G_N_ELEMENTS (points_inside); p++)
    {
      const ClutterPoint *point = points_inside + p;

      g_assert_true (clutter_point_inside_polygon (point, vertices, 4));
      g_assert_true (clutter_point_touches_polygon (point, vertices, 4));
    }

  for (p = 0; p < G_N_ELEMENTS (points_outside); p++)
    {
      const ClutterPoint *point = points_outside + p;

      g_assert_false (clutter_point_inside_polygon (point, vertices, 4));
      g_assert_false (clutter_point_touches_polygon (point, vertices, 4));
    }

  for (p = 0; p < G_N_ELEMENTS (points_touching); p++)
    {
      const ClutterPoint *point = points_touching + p;

      g_assert_false (clutter_point_inside_polygon (point, vertices, 4));
      g_assert_true (clutter_point_touches_polygon (point, vertices, 4));
    }
}

static void
point_on_empty_polygon (void)
{
  int p;
  static const ClutterPoint vertices[4] =
    {
      {5.f, 6.f},
      {5.f, 6.f},
      {5.f, 6.f},
      {5.f, 6.f},
    };
  static const ClutterPoint points_outside[] =
    {
      {3.f, 1.f},
      {7.f, 4.f},
      {4.f, 6.f},
      {99.f, -77.f},
      {-1.f, 3.f},
      {-8.f, -8.f},
    };

  for (p = 0; p < G_N_ELEMENTS (points_outside); p++)
    {
      const ClutterPoint *point = points_outside + p;

      g_assert_false (clutter_point_inside_polygon (point, vertices, 4));
      g_assert_false (clutter_point_touches_polygon (point, vertices, 4));
    }

  g_assert_false (clutter_point_inside_polygon (&vertices[0], vertices, 4));
  g_assert_true (clutter_point_touches_polygon (&vertices[0], vertices, 4));
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/point/on_nonempty_polygon", point_on_nonempty_polygon)
  CLUTTER_TEST_UNIT ("/point/on_empty_polygon", point_on_empty_polygon)
)
