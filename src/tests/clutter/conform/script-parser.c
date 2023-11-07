#include <stdlib.h>
#include <string.h>

#define CLUTTER_DISABLE_DEPRECATION_WARNINGS
#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"

#define TEST_TYPE_GROUP (test_group_get_type ())
G_DECLARE_FINAL_TYPE (TestGroup, test_group, TEST, GROUP, ClutterActor)

struct _TestGroup
{
  ClutterActor parent;
};

G_DEFINE_TYPE (TestGroup, test_group, CLUTTER_TYPE_ACTOR)

static void
test_group_class_init (TestGroupClass *klass)
{
}

static void
test_group_init (TestGroup *self)
{
}

static void
script_child (void)
{
  ClutterScript *script = clutter_script_new ();
  GObject *container, *actor;
  GError *error = NULL;
  gchar *test_file;

  test_file = g_test_build_filename (G_TEST_DIST, "scripts", "test-script-child.json", NULL);
  clutter_script_load_from_file (script, test_file, &error);
  if (!g_test_quiet () && error)
    g_print ("Error: %s", error->message);

  g_assert_no_error (error);

  container = actor = NULL;
  clutter_script_get_objects (script,
                              "test-group", &container,
                              "test-rect-1", &actor,
                              NULL);
  g_assert (TEST_IS_GROUP (container));
  g_assert (CLUTTER_IS_ACTOR (actor));

  actor = clutter_script_get_object (script, "test-rect-2");
  g_assert (CLUTTER_IS_ACTOR (actor));

  g_object_unref (script);
  g_free (test_file);
}

static void
script_single (void)
{
  ClutterScript *script = clutter_script_new ();
  ClutterColor color = { 0, };
  GObject *actor = NULL;
  GError *error = NULL;
  ClutterActor *rect;
  gchar *test_file;

  test_file = g_test_build_filename (G_TEST_DIST, "scripts", "test-script-single.json", NULL);
  clutter_script_load_from_file (script, test_file, &error);
  if (!g_test_quiet () && error)
    g_print ("Error: %s", error->message);

  g_assert_no_error (error);

  actor = clutter_script_get_object (script, "test");
  g_assert (CLUTTER_IS_ACTOR (actor));

  rect = CLUTTER_ACTOR (actor);
  g_assert_cmpfloat (clutter_actor_get_width (rect), ==, 50.0);
  g_assert_cmpfloat (clutter_actor_get_y (rect), ==, 100.0);

  clutter_actor_get_background_color (rect, &color);
  g_assert_cmpint (color.red, ==, 255);
  g_assert_cmpint (color.green, ==, 0xcc);
  g_assert_cmpint (color.alpha, ==, 0xff);

  g_object_unref (script);
  g_free (test_file);
}

static void
script_object_property (void)
{
  ClutterScript *script = clutter_script_new ();
  ClutterLayoutManager *manager;
  GObject *actor = NULL;
  GError *error = NULL;
  gchar *test_file;

  test_file = g_test_build_filename (G_TEST_DIST, "scripts", "test-script-object-property.json", NULL);
  clutter_script_load_from_file (script, test_file, &error);
  if (!g_test_quiet () && error)
    g_print ("Error: %s", error->message);

  g_assert_no_error (error);

  actor = clutter_script_get_object (script, "test");
  g_assert (CLUTTER_IS_ACTOR (actor));

  manager = clutter_actor_get_layout_manager (CLUTTER_ACTOR (actor));
  g_assert (CLUTTER_IS_BIN_LAYOUT (manager));

  g_object_unref (script);
  g_free (test_file);
}

static void
script_named_object (void)
{
  ClutterScript *script = clutter_script_new ();
  ClutterLayoutManager *manager;
  GObject *actor = NULL;
  GError *error = NULL;
  gchar *test_file;

  test_file = g_test_build_filename (G_TEST_DIST, "scripts", "test-script-named-object.json", NULL);
  clutter_script_load_from_file (script, test_file, &error);
  if (!g_test_quiet () && error)
    g_print ("Error: %s", error->message);

  g_assert_no_error (error);

  actor = clutter_script_get_object (script, "test");
  g_assert (CLUTTER_IS_ACTOR (actor));

  manager = clutter_actor_get_layout_manager (CLUTTER_ACTOR (actor));
  g_assert (CLUTTER_IS_BOX_LAYOUT (manager));
  g_assert (clutter_box_layout_get_orientation (CLUTTER_BOX_LAYOUT (manager)) == CLUTTER_ORIENTATION_VERTICAL);

  g_object_unref (script);
  g_free (test_file);
}

static void
script_margin (void)
{
  ClutterScript *script = clutter_script_new ();
  ClutterActor *actor;
  gchar *test_file;
  GError *error = NULL;

  test_file = g_test_build_filename (G_TEST_DIST, "scripts", "test-script-margin.json", NULL);
  clutter_script_load_from_file (script, test_file, &error);
  if (!g_test_quiet () && error)
    g_print ("Error: %s", error->message);

  g_assert_no_error (error);

  actor = CLUTTER_ACTOR (clutter_script_get_object (script, "actor-1"));
  g_assert_cmpfloat (clutter_actor_get_margin_top (actor), ==, 10.0f);
  g_assert_cmpfloat (clutter_actor_get_margin_right (actor), ==, 10.0f);
  g_assert_cmpfloat (clutter_actor_get_margin_bottom (actor), ==, 10.0f);
  g_assert_cmpfloat (clutter_actor_get_margin_left (actor), ==, 10.0f);

  actor = CLUTTER_ACTOR (clutter_script_get_object (script, "actor-2"));
  g_assert_cmpfloat (clutter_actor_get_margin_top (actor), ==, 10.0f);
  g_assert_cmpfloat (clutter_actor_get_margin_right (actor), ==, 20.0f);
  g_assert_cmpfloat (clutter_actor_get_margin_bottom (actor), ==, 10.0f);
  g_assert_cmpfloat (clutter_actor_get_margin_left (actor), ==, 20.0f);

  actor = CLUTTER_ACTOR (clutter_script_get_object (script, "actor-3"));
  g_assert_cmpfloat (clutter_actor_get_margin_top (actor), ==, 10.0f);
  g_assert_cmpfloat (clutter_actor_get_margin_right (actor), ==, 20.0f);
  g_assert_cmpfloat (clutter_actor_get_margin_bottom (actor), ==, 30.0f);
  g_assert_cmpfloat (clutter_actor_get_margin_left (actor), ==, 20.0f);

  actor = CLUTTER_ACTOR (clutter_script_get_object (script, "actor-4"));
  g_assert_cmpfloat (clutter_actor_get_margin_top (actor), ==, 10.0f);
  g_assert_cmpfloat (clutter_actor_get_margin_right (actor), ==, 20.0f);
  g_assert_cmpfloat (clutter_actor_get_margin_bottom (actor), ==, 30.0f);
  g_assert_cmpfloat (clutter_actor_get_margin_left (actor), ==, 40.0f);

  g_object_unref (script);
  g_free (test_file);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/script/single-object", script_single)
  CLUTTER_TEST_UNIT ("/script/container-child", script_child)
  CLUTTER_TEST_UNIT ("/script/named-object", script_named_object)
  CLUTTER_TEST_UNIT ("/script/object-property", script_object_property)
  CLUTTER_TEST_UNIT ("/script/actor-margin", script_margin)
)
