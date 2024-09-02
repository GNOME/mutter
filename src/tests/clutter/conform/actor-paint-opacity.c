#include <clutter/clutter.h>
#include <stdlib.h>

#include "tests/clutter-test-utils.h"

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
static void
opacity_rectangle (void)
{
  ClutterActor *stage;
  ClutterActor *rect;
  CoglColor rect_color = { 0, 0, 255, 255 };
  CoglColor color_check = { 0, };

  stage = clutter_test_get_stage ();

  rect = clutter_actor_new ();
  clutter_actor_set_background_color (rect, &rect_color);
  clutter_actor_set_size (rect, 128, 128);
  clutter_actor_set_position (rect, 150, 90);

  if (!g_test_quiet ())
    g_print ("rect 100%%.get_color()/1\n");
  clutter_actor_get_background_color (rect, &color_check);
  g_assert (color_check.alpha == rect_color.alpha);

  clutter_actor_add_child (stage, rect);

  if (!g_test_quiet ())
    g_print ("rect 100%%.get_color()/2\n");
  clutter_actor_set_background_color (rect, &color_check);
  g_assert (color_check.alpha == rect_color.alpha);

  if (!g_test_quiet ())
    g_print ("rect 100%%.get_paint_opacity()\n");
  g_assert (clutter_actor_get_paint_opacity (rect) == 255);

  clutter_actor_destroy (rect);
}
G_GNUC_END_IGNORE_DEPRECATIONS

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
static void
opacity_paint (void)
{
  ClutterActor *group1, *group2;
  ClutterActor *rect;
  CoglColor rect_color = { 0, 0, 255, 255 };
  CoglColor color_check = { 0, };

  group1 = clutter_actor_new ();
  group2 = clutter_actor_new ();
  clutter_actor_add_child (group1, group2);
  clutter_actor_set_position (group2, 10, 60);

  rect = clutter_actor_new ();
  clutter_actor_set_background_color (rect, &rect_color);
  clutter_actor_set_size (rect, 128, 128);

  if (!g_test_quiet ())
    g_print ("rect 100%% + group 100%% + group 50%%.get_color()/1\n");
  clutter_actor_get_background_color (rect, &color_check);
  g_assert (color_check.alpha == rect_color.alpha);

  clutter_actor_add_child (group2, rect);

  if (!g_test_quiet ())
    g_print ("rect 100%% + group 100%% + group 50%%.get_color()/2\n");
  clutter_actor_get_background_color (rect, &color_check);
  g_assert (color_check.alpha == rect_color.alpha);

  if (!g_test_quiet ())
    g_print ("rect 100%%.get_paint_opacity()\n");
  g_assert (clutter_actor_get_paint_opacity (rect) == 128);

  clutter_actor_destroy (group1);
}
G_GNUC_END_IGNORE_DEPRECATIONS

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/actor/opacity/rectangle", opacity_rectangle)
  CLUTTER_TEST_UNIT ("/actor/opacity/paint", opacity_paint)
)
