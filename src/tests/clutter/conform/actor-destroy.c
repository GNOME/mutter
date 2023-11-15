#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"

#define TEST_TYPE_DESTROY (test_destroy_get_type ())
G_DECLARE_FINAL_TYPE (TestDestroy, test_destroy, TEST, DESTROY, ClutterActor)

struct _TestDestroy
{
  ClutterActor parent_instance;

  ClutterActor *bg;
  ClutterActor *label;

  GList *children;
};

G_DEFINE_TYPE (TestDestroy, test_destroy, CLUTTER_TYPE_ACTOR)

static void
test_destroy_destroy (ClutterActor *self)
{
  TestDestroy *test = TEST_DESTROY (self);
  GList *children;

  children = clutter_actor_get_children (self);
  g_assert_cmpuint (g_list_length (children), ==, 3);
  g_list_free (children);

  if (test->bg != NULL)
    {
      if (!g_test_quiet ())
        g_print ("Destroying '%s' (type:%s)\n",
                 clutter_actor_get_name (test->bg),
                 G_OBJECT_TYPE_NAME (test->bg));

      clutter_actor_destroy (test->bg);
      test->bg = NULL;
    }

  if (test->label != NULL)
    {
      if (!g_test_quiet ())
        g_print ("Destroying '%s' (type:%s)\n",
                 clutter_actor_get_name (test->label),
                 G_OBJECT_TYPE_NAME (test->label));

      clutter_actor_destroy (test->label);
      test->label = NULL;
    }

  children = clutter_actor_get_children (self);
  g_assert_cmpuint (g_list_length (children), ==, 1);
  g_list_free (children);

  if (CLUTTER_ACTOR_CLASS (test_destroy_parent_class)->destroy)
    CLUTTER_ACTOR_CLASS (test_destroy_parent_class)->destroy (self);

  children = clutter_actor_get_children (self);
  g_assert_null (children);
  g_list_free (children);
}

static void
test_destroy_class_init (TestDestroyClass *klass)
{
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);

  actor_class->destroy = test_destroy_destroy;
}

static void
test_destroy_init (TestDestroy *self)
{
  self->bg = clutter_actor_new ();
  clutter_actor_add_child (CLUTTER_ACTOR (self), self->bg);
  clutter_actor_set_name (self->bg, "Background");

  self->label = clutter_text_new ();
  clutter_actor_add_child (CLUTTER_ACTOR (self), self->label);
  clutter_actor_set_name (self->label, "Label");
}

static void
on_destroy (ClutterActor *actor,
            gpointer      data)
{
  gboolean *destroy_called = data;

  g_assert_true (CLUTTER_IS_ACTOR (clutter_actor_get_parent (actor)));

  *destroy_called = TRUE;
}

static void
on_parent_set (ClutterActor *actor,
               ClutterActor *old_parent,
               gpointer      data)
{
  gboolean *parent_set_called = data;

  *parent_set_called = TRUE;
}

static void
on_notify (ClutterActor *actor,
           ClutterActor *old_parent,
           gpointer      data)
{
  gboolean *property_changed = data;

  *property_changed = TRUE;
}

static void
actor_destruction (void)
{
  ClutterActor *test = g_object_new (TEST_TYPE_DESTROY, NULL);
  ClutterActor *child = clutter_actor_new ();
  gboolean destroy_called = FALSE;
  gboolean parent_set_called = FALSE;
  gboolean property_changed = FALSE;

  g_object_ref_sink (test);

  g_object_add_weak_pointer (G_OBJECT (test), (gpointer *) &test);
  g_object_add_weak_pointer (G_OBJECT (child), (gpointer *) &child);

  if (!g_test_quiet ())
    g_print ("Adding external child...\n");

  clutter_actor_set_name (child, "Child");
  clutter_actor_add_child (test, child);
  g_signal_connect (child, "parent-set", G_CALLBACK (on_parent_set),
                    &parent_set_called);
  g_signal_connect (child, "notify", G_CALLBACK (on_notify), &property_changed);
  g_signal_connect (child, "destroy", G_CALLBACK (on_destroy), &destroy_called);

  if (!g_test_quiet ())
    g_print ("Calling destroy()...\n");

  clutter_actor_destroy (test);
  g_assert (destroy_called);
  g_assert_false (parent_set_called);
  g_assert_false (property_changed);
  g_assert_null (child);
  g_assert_null (test);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/actor/destruction", actor_destruction)
)
