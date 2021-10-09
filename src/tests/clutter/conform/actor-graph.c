#include <clutter/clutter.h>

#include "glib.h"
#include "tests/clutter-test-utils.h"

typedef struct _ChildNotifyData
{
  GParamSpec *pspec;
  ClutterActor *child;
} ChildNotifyData;

static void
child_notify_data_clear (ChildNotifyData *data)
{
  g_clear_pointer (&data->pspec, g_param_spec_unref);
  g_clear_object (&data->child);
}

static void
on_first_last_child_notify (GObject    *object,
                            GParamSpec *pspec,
                            gpointer    user_data)
{
  ClutterActor *actor = CLUTTER_ACTOR (object);
  ChildNotifyData *data = user_data;

  g_assert_null (data->pspec);
  g_assert_null (data->child);

  data->pspec = g_param_spec_ref (pspec);
  g_object_get (object, pspec->name, &data->child, NULL);

  g_assert_nonnull (data->child);
  g_assert_true (clutter_actor_get_parent (data->child) == actor);
  g_test_message ("%s is now %s", pspec->name,
                  clutter_actor_get_name (data->child));

  if (g_str_equal (pspec->name, "first-child"))
    g_assert_true (clutter_actor_get_first_child (actor) == data->child);
  else if (g_str_equal (pspec->name, "last-child"))
    g_assert_true (clutter_actor_get_last_child (actor) == data->child);
  else
    g_assert_not_reached ();
}

static void
assert_child_notified (ChildNotifyData *notify_data,
                       const char      *property_name,
                       ClutterActor    *child)
{
  g_test_message ("Checking %s is %s", property_name,
                  clutter_actor_get_name (child));

  g_assert_nonnull (notify_data->pspec);
  g_assert_cmpstr (notify_data->pspec->name, ==, property_name);
  g_assert_cmpstr (clutter_actor_get_name (notify_data->child),
                   ==,
                   clutter_actor_get_name (child));
  g_assert_true (notify_data->child == child);
  child_notify_data_clear (notify_data);
}

static void
assert_child_not_notified (ChildNotifyData *notify_data)
{
  g_assert_null (notify_data->pspec);
  g_assert_null (notify_data->child);
}

static void
assert_first_child_notified (ChildNotifyData *notify_data,
                             ClutterActor    *child)
{
  assert_child_notified (notify_data, "first-child", child);
}

static void
assert_last_child_notified (ChildNotifyData *notify_data,
                            ClutterActor    *child)
{
  assert_child_notified (notify_data, "last-child", child);
}

static void
actor_add_child (void)
{
  ClutterActor *actor = clutter_actor_new ();
  ChildNotifyData first_child_notify_data = {0};
  ChildNotifyData last_child_notify_data = {0};
  ClutterActor *iter;

  g_object_ref_sink (actor);
  g_object_add_weak_pointer (G_OBJECT (actor), (gpointer *) &actor);

  g_signal_connect (actor,
                    "notify::first-child",
                    G_CALLBACK (on_first_last_child_notify),
                    &first_child_notify_data);
  g_signal_connect (actor,
                    "notify::last-child",
                    G_CALLBACK (on_first_last_child_notify),
                    &last_child_notify_data);

  clutter_actor_add_child (actor, g_object_new (CLUTTER_TYPE_ACTOR,
                                                "name", "foo",
                                                NULL));
  assert_first_child_notified (&first_child_notify_data,
                               clutter_actor_get_child_at_index (actor, 0));
  assert_last_child_notified (&last_child_notify_data,
                              clutter_actor_get_child_at_index (actor, 0));

  clutter_actor_add_child (actor, g_object_new (CLUTTER_TYPE_ACTOR,
                                                "name", "bar",
                                                NULL));
  assert_child_not_notified (&first_child_notify_data);

  assert_last_child_notified (&last_child_notify_data,
                              clutter_actor_get_child_at_index (actor, 1));

  clutter_actor_add_child (actor, g_object_new (CLUTTER_TYPE_ACTOR,
                                                "name", "baz",
                                                NULL));
  assert_child_not_notified (&first_child_notify_data);

  assert_last_child_notified (&last_child_notify_data,
                              clutter_actor_get_child_at_index (actor, 2));

  g_assert_cmpint (clutter_actor_get_n_children (actor), ==, 3);

  iter = clutter_actor_get_first_child (actor);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "foo");

  iter = clutter_actor_get_next_sibling (iter);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "bar");

  iter = clutter_actor_get_next_sibling (iter);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "baz");
  g_assert_true (iter == clutter_actor_get_last_child (actor));
  g_assert_null (clutter_actor_get_next_sibling (iter));

  iter = clutter_actor_get_last_child (actor);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "baz");

  iter = clutter_actor_get_previous_sibling (iter);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "bar");

  iter = clutter_actor_get_previous_sibling (iter);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "foo");
  g_assert_true (iter == clutter_actor_get_first_child (actor));
  g_assert_null (clutter_actor_get_previous_sibling (iter));

  clutter_actor_destroy (actor);
  g_assert_null (actor);
}

static void
actor_insert_child (void)
{
  ClutterActor *actor = clutter_actor_new ();
  ChildNotifyData first_child_notify_data = {0};
  ChildNotifyData last_child_notify_data = {0};
  ClutterActor *iter;
  gulong first_child_added_id, last_child_added_id;

  g_object_ref_sink (actor);
  g_object_add_weak_pointer (G_OBJECT (actor), (gpointer *) &actor);

  first_child_added_id =
    g_signal_connect (actor,
                      "notify::first-child",
                      G_CALLBACK (on_first_last_child_notify),
                      &first_child_notify_data);

  last_child_added_id =
    g_signal_connect (actor,
                      "notify::last-child",
                      G_CALLBACK (on_first_last_child_notify),
                      &last_child_notify_data);

  clutter_actor_insert_child_at_index (actor,
                                       g_object_new (CLUTTER_TYPE_ACTOR,
                                                     "name", "foo",
                                                     NULL),
                                       0);

  assert_first_child_notified (&first_child_notify_data,
                               clutter_actor_get_child_at_index (actor, 0));
  assert_last_child_notified (&last_child_notify_data,
                              clutter_actor_get_child_at_index (actor, 0));

  g_signal_handler_block (actor, first_child_added_id);
  g_signal_handler_block (actor, last_child_added_id);

  iter = clutter_actor_get_first_child (actor);
  g_assert_nonnull (iter);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "foo");
  g_assert_true (iter == clutter_actor_get_child_at_index (actor, 0));
  iter = clutter_actor_get_last_child (actor);
  g_assert_nonnull (iter);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "foo");
  g_assert_true (iter == clutter_actor_get_child_at_index (actor, 0));

  clutter_actor_insert_child_below (actor,
                                    g_object_new (CLUTTER_TYPE_ACTOR,
                                                  "name", "bar",
                                                  NULL),
                                    iter);

  g_assert_cmpint (clutter_actor_get_n_children (actor), ==, 2);

  iter = clutter_actor_get_first_child (actor);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "bar");
  iter = clutter_actor_get_next_sibling (iter);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "foo");
  g_assert_true (iter == clutter_actor_get_child_at_index (actor, 1));
  iter = clutter_actor_get_last_child (actor);
  g_assert_nonnull (iter);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "foo");
  g_assert_true (iter == clutter_actor_get_child_at_index (actor, 1));

  iter = clutter_actor_get_first_child (actor);
  clutter_actor_insert_child_above (actor,
                                    g_object_new (CLUTTER_TYPE_ACTOR,
                                                  "name", "baz",
                                                  NULL),
                                    iter);

  iter = clutter_actor_get_last_child (actor);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "foo");

  iter = clutter_actor_get_previous_sibling (iter);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "baz");

  iter = clutter_actor_get_previous_sibling (iter);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "bar");

  clutter_actor_remove_all_children (actor);

  g_signal_handler_unblock (actor, first_child_added_id);
  g_signal_handler_unblock (actor, last_child_added_id);

  clutter_actor_insert_child_at_index (actor,
                                       g_object_new (CLUTTER_TYPE_ACTOR,
                                                     "name", "1",
                                                     NULL),
                                       0);
  iter = clutter_actor_get_child_at_index (actor, 0);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "1");
  g_assert_true (clutter_actor_get_first_child (actor) == iter);
  g_assert_true (clutter_actor_get_last_child (actor) == iter);

  assert_first_child_notified (&first_child_notify_data, iter);
  assert_last_child_notified (&last_child_notify_data, iter);

  clutter_actor_insert_child_at_index (actor,
                                       g_object_new (CLUTTER_TYPE_ACTOR,
                                                     "name", "2",
                                                     NULL),
                                       0);
  iter = clutter_actor_get_child_at_index (actor, 0);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "2");
  g_assert_true (clutter_actor_get_first_child (actor) == iter);
  iter = clutter_actor_get_child_at_index (actor, 1);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "1");
  g_assert_true (clutter_actor_get_last_child (actor) == iter);

  assert_first_child_notified (&first_child_notify_data,
                               clutter_actor_get_child_at_index (actor, 0));
  assert_child_not_notified (&last_child_notify_data);

  clutter_actor_insert_child_at_index (actor,
                                       g_object_new (CLUTTER_TYPE_ACTOR,
                                                     "name", "3",
                                                     NULL),
                                       -1);
  iter = clutter_actor_get_child_at_index (actor, 2);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "3");
  g_assert_true (clutter_actor_get_last_child (actor) == iter);

  assert_child_not_notified (&first_child_notify_data);
  assert_last_child_notified (&last_child_notify_data, iter);

  clutter_actor_destroy (actor);
  g_assert (actor == NULL);
}

static void
actor_swap_child (void)
{
  ClutterActor *actor = clutter_actor_new ();
  ChildNotifyData first_child_notify_data = {0};
  ChildNotifyData last_child_notify_data = {0};
  ClutterActor *child1, *child2;

  g_object_ref_sink (actor);
  g_object_add_weak_pointer (G_OBJECT (actor), (gpointer *) &actor);

  g_signal_connect (actor,
                    "notify::first-child",
                    G_CALLBACK (on_first_last_child_notify),
                    &first_child_notify_data);
  g_signal_connect (actor,
                    "notify::last-child",
                    G_CALLBACK (on_first_last_child_notify),
                    &last_child_notify_data);

  child1 = g_object_new (CLUTTER_TYPE_ACTOR, "name", "child1", NULL);
  child2 = g_object_new (CLUTTER_TYPE_ACTOR, "name", "child2", NULL);

  g_test_message ("Adding child1");
  clutter_actor_add_child (actor, child1);

  assert_first_child_notified (&first_child_notify_data, child1);
  assert_last_child_notified (&last_child_notify_data, child1);

  g_test_message ("adding child2");
  clutter_actor_add_child (actor, child2);

  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_child_at_index (actor, 0)),
                   ==,
                   "child1");
  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_child_at_index (actor, 1)),
                   ==,
                   "child2");
  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_first_child (actor)),
                   ==,
                   "child1");
  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_last_child (actor)),
                   ==,
                   "child2");

  assert_child_not_notified (&first_child_notify_data);
  assert_last_child_notified (&last_child_notify_data, child2);

  g_test_message ("Moving child2 below child1");
  clutter_actor_set_child_below_sibling (actor, child2, child1);

  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_child_at_index (actor, 0)),
                   ==,
                   "child2");
  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_child_at_index (actor, 1)),
                   ==,
                   "child1");
  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_first_child (actor)),
                   ==,
                   "child2");
  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_last_child (actor)),
                   ==,
                   "child1");

  assert_first_child_notified (&first_child_notify_data, child2);
  assert_last_child_notified (&last_child_notify_data, child1);

  g_test_message ("Keep child2 below child1 (no change)");
  clutter_actor_set_child_below_sibling (actor, child2, child1);

  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_child_at_index (actor, 0)),
                   ==,
                   "child2");
  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_child_at_index (actor, 1)),
                   ==,
                   "child1");
  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_first_child (actor)),
                   ==,
                   "child2");
  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_last_child (actor)),
                   ==,
                   "child1");

  assert_child_not_notified (&first_child_notify_data);
  assert_child_not_notified (&last_child_notify_data);

  g_test_message ("Moving child2 above child1");
  clutter_actor_set_child_above_sibling (actor, child2, child1);

  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_child_at_index (actor, 0)),
                   ==,
                   "child1");
  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_child_at_index (actor, 1)),
                   ==,
                   "child2");
  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_first_child (actor)),
                   ==,
                   "child1");
  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_last_child (actor)),
                   ==,
                   "child2");

  assert_first_child_notified (&first_child_notify_data, child1);
  assert_last_child_notified (&last_child_notify_data, child2);

  g_test_message ("Keep child2 above child1 (no change)");
  clutter_actor_set_child_above_sibling (actor, child2, child1);

  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_child_at_index (actor, 0)),
                   ==,
                   "child1");
  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_child_at_index (actor, 1)),
                   ==,
                   "child2");
  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_first_child (actor)),
                   ==,
                   "child1");
  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_last_child (actor)),
                   ==,
                   "child2");

  assert_child_not_notified (&first_child_notify_data);
  assert_child_not_notified (&last_child_notify_data);

  g_test_message ("Moving child1 above at index 1");
  clutter_actor_set_child_at_index (actor, child1, 1);

  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_child_at_index (actor, 0)),
                   ==,
                   "child2");
  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_child_at_index (actor, 1)),
                   ==,
                   "child1");
  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_first_child (actor)),
                   ==,
                   "child2");
  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_last_child (actor)),
                   ==,
                   "child1");

  assert_first_child_notified (&first_child_notify_data, child2);
  assert_last_child_notified (&last_child_notify_data, child1);

  g_test_message ("Keep child1 above at index 1 (no change)");
  clutter_actor_set_child_at_index (actor, child1, 1);

  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_child_at_index (actor, 0)),
                   ==,
                   "child2");
  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_child_at_index (actor, 1)),
                   ==,
                   "child1");
  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_first_child (actor)),
                   ==,
                   "child2");
  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_last_child (actor)),
                   ==,
                   "child1");

  assert_child_not_notified (&first_child_notify_data);
  assert_child_not_notified (&last_child_notify_data);

  g_test_message ("Moving child2 at index 1");
  clutter_actor_set_child_at_index (actor, child2, 1);

  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_child_at_index (actor, 0)),
                   ==,
                   "child1");
  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_child_at_index (actor, 1)),
                   ==,
                   "child2");
  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_first_child (actor)),
                   ==,
                   "child1");
  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_last_child (actor)),
                   ==,
                   "child2");

  assert_first_child_notified (&first_child_notify_data, child1);
  assert_last_child_notified (&last_child_notify_data, child2);

  g_test_message ("Keeping child2 at index 1 (no change)");
  clutter_actor_set_child_at_index (actor, child2, 1);

  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_child_at_index (actor, 0)),
                   ==,
                   "child1");
  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_child_at_index (actor, 1)),
                   ==,
                   "child2");
  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_first_child (actor)),
                   ==,
                   "child1");
  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_last_child (actor)),
                   ==,
                   "child2");

  assert_child_not_notified (&first_child_notify_data);
  assert_child_not_notified (&last_child_notify_data);

  clutter_actor_destroy (actor);
  g_assert_null (actor);
}

static void
actor_remove_child (void)
{
  ClutterActor *actor = clutter_actor_new ();
  ClutterActor *iter;

  g_object_ref_sink (actor);
  g_object_add_weak_pointer (G_OBJECT (actor), (gpointer *) &actor);

  clutter_actor_add_child (actor, g_object_new (CLUTTER_TYPE_ACTOR,
                                                "name", "foo",
                                                NULL));
  clutter_actor_add_child (actor, g_object_new (CLUTTER_TYPE_ACTOR,
                                                "name", "bar",
                                                NULL));

  g_assert_cmpint (clutter_actor_get_n_children (actor), ==, 2);

  g_assert_true (clutter_actor_get_first_child (actor) != clutter_actor_get_last_child (actor));

  iter = clutter_actor_get_first_child (actor);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "foo");

  iter = clutter_actor_get_last_child (actor);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "bar");

  clutter_actor_remove_child (actor, clutter_actor_get_first_child (actor));

  g_assert_cmpint (clutter_actor_get_n_children (actor), ==, 1);

  iter = clutter_actor_get_first_child (actor);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "bar");
  g_assert_true (clutter_actor_get_first_child (actor) == clutter_actor_get_last_child (actor));

  clutter_actor_remove_child (actor, clutter_actor_get_first_child (actor));

  g_assert_cmpint (clutter_actor_get_n_children (actor), ==, 0);
  g_assert_null (clutter_actor_get_first_child (actor));
  g_assert_null (clutter_actor_get_last_child (actor));

  clutter_actor_destroy (actor);
  g_assert_null (actor);
}

static void
actor_raise_child (void)
{
  ClutterActor *actor = clutter_actor_new ();
  ChildNotifyData first_child_notify_data = {0};
  ChildNotifyData last_child_notify_data = {0};
  ClutterActor *iter;
  gboolean show_on_set_parent;

  g_object_ref_sink (actor);
  g_object_add_weak_pointer (G_OBJECT (actor), (gpointer *) &actor);

  clutter_actor_add_child (actor, g_object_new (CLUTTER_TYPE_ACTOR,
                                                "name", "foo",
                                                "visible", FALSE,
                                                NULL));
  clutter_actor_add_child (actor, g_object_new (CLUTTER_TYPE_ACTOR,
                                                "name", "bar",
                                                "visible", FALSE,
                                                NULL));
  clutter_actor_add_child (actor, g_object_new (CLUTTER_TYPE_ACTOR,
                                                "name", "baz",
                                                "visible", FALSE,
                                                NULL));

  g_assert_cmpint (clutter_actor_get_n_children (actor), ==, 3);

  g_signal_connect (actor,
                    "notify::first-child",
                    G_CALLBACK (on_first_last_child_notify),
                    &first_child_notify_data);
  g_signal_connect (actor,
                    "notify::last-child",
                    G_CALLBACK (on_first_last_child_notify),
                    &last_child_notify_data);

  iter = clutter_actor_get_child_at_index (actor, 1);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "bar");

  clutter_actor_set_child_above_sibling (actor, iter,
                                         clutter_actor_get_child_at_index (actor, 2));

  g_assert_cmpstr (clutter_actor_get_name (clutter_actor_get_child_at_index (actor, 0)),
                   ==,
                   "foo");
  g_assert_cmpstr (clutter_actor_get_name (clutter_actor_get_child_at_index (actor, 1)),
                   ==,
                   "baz");
  g_assert_cmpstr (clutter_actor_get_name (clutter_actor_get_child_at_index (actor, 2)),
                   ==,
                   "bar");
  g_assert_false (clutter_actor_is_visible (iter));
  g_object_get (iter, "show-on-set-parent", &show_on_set_parent, NULL);
  g_assert_false (show_on_set_parent);

  assert_child_not_notified (&first_child_notify_data);
  assert_last_child_notified (&last_child_notify_data,
                              clutter_actor_get_child_at_index (actor, 2));

  iter = clutter_actor_get_child_at_index (actor, 0);
  clutter_actor_set_child_above_sibling (actor, iter, NULL);
  g_object_add_weak_pointer (G_OBJECT (iter), (gpointer *) &iter);

  g_assert_cmpstr (clutter_actor_get_name (clutter_actor_get_child_at_index (actor, 0)),
                   ==,
                   "baz");
  g_assert_cmpstr (clutter_actor_get_name (clutter_actor_get_child_at_index (actor, 1)),
                   ==,
                   "bar");
  g_assert_cmpstr (clutter_actor_get_name (clutter_actor_get_child_at_index (actor, 2)),
                   ==,
                   "foo");
  g_assert_false (clutter_actor_is_visible (iter));
  g_object_get (iter, "show-on-set-parent", &show_on_set_parent, NULL);
  g_assert_false (show_on_set_parent);

  assert_first_child_notified (&first_child_notify_data,
                               clutter_actor_get_child_at_index (actor, 0));
  assert_last_child_notified (&last_child_notify_data,
                              clutter_actor_get_child_at_index (actor, 2));

  iter = clutter_actor_get_child_at_index (actor, 2);
  clutter_actor_set_child_above_sibling (actor, iter,
                                         clutter_actor_get_child_at_index (actor, 0));

  g_assert_cmpstr (clutter_actor_get_name (clutter_actor_get_child_at_index (actor, 0)),
                   ==,
                   "baz");
  g_assert_cmpstr (clutter_actor_get_name (clutter_actor_get_child_at_index (actor, 1)),
                   ==,
                   "foo");
  g_assert_cmpstr (clutter_actor_get_name (clutter_actor_get_child_at_index (actor, 2)),
                   ==,
                   "bar");

  assert_child_not_notified (&first_child_notify_data);
  assert_last_child_notified (&last_child_notify_data,
                              clutter_actor_get_child_at_index (actor, 2));

  clutter_actor_add_child (actor, g_object_new (CLUTTER_TYPE_ACTOR,
                                                "name", "zap",
                                                "visible", FALSE,
                                                NULL));
  g_assert_cmpint (clutter_actor_get_n_children (actor), ==, 4);
  assert_last_child_notified (&last_child_notify_data,
                              clutter_actor_get_child_at_index (actor, 3));

  iter = clutter_actor_get_child_at_index (actor, 1);
  clutter_actor_set_child_above_sibling (actor, iter,
                                         clutter_actor_get_child_at_index (actor, 2));

  g_assert_cmpstr (clutter_actor_get_name (clutter_actor_get_child_at_index (actor, 0)),
                   ==,
                   "baz");
  g_assert_cmpstr (clutter_actor_get_name (clutter_actor_get_child_at_index (actor, 1)),
                   ==,
                   "bar");
  g_assert_cmpstr (clutter_actor_get_name (clutter_actor_get_child_at_index (actor, 2)),
                   ==,
                   "foo");
  g_assert_cmpstr (clutter_actor_get_name (clutter_actor_get_child_at_index (actor, 3)),
                   ==,
                   "zap");
  assert_child_not_notified (&first_child_notify_data);
  assert_child_not_notified (&last_child_notify_data);

  clutter_actor_destroy (actor);
  g_assert_null (actor);
  g_assert_null (iter);
}

static void
actor_lower_child (void)
{
  ClutterActor *actor = clutter_actor_new ();
  ChildNotifyData first_child_notify_data = {0};
  ChildNotifyData last_child_notify_data = {0};
  ClutterActor *iter;
  gboolean show_on_set_parent;

  g_object_ref_sink (actor);
  g_object_add_weak_pointer (G_OBJECT (actor), (gpointer *) &actor);

  clutter_actor_add_child (actor, g_object_new (CLUTTER_TYPE_ACTOR,
                                                "name", "foo",
                                                "visible", FALSE,
                                                NULL));
  clutter_actor_add_child (actor, g_object_new (CLUTTER_TYPE_ACTOR,
                                                "name", "bar",
                                                "visible", FALSE,
                                                NULL));
  clutter_actor_add_child (actor, g_object_new (CLUTTER_TYPE_ACTOR,
                                                "name", "baz",
                                                "visible", FALSE,
                                                NULL));

  g_assert_cmpint (clutter_actor_get_n_children (actor), ==, 3);

  g_signal_connect (actor,
                    "notify::first-child",
                    G_CALLBACK (on_first_last_child_notify),
                    &first_child_notify_data);
  g_signal_connect (actor,
                    "notify::last-child",
                    G_CALLBACK (on_first_last_child_notify),
                    &last_child_notify_data);

  iter = clutter_actor_get_child_at_index (actor, 1);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "bar");

  clutter_actor_set_child_below_sibling (actor, iter,
                                         clutter_actor_get_child_at_index (actor, 0));

  g_assert_cmpstr (clutter_actor_get_name (clutter_actor_get_child_at_index (actor, 0)),
                   ==,
                   "bar");
  g_assert_cmpstr (clutter_actor_get_name (clutter_actor_get_child_at_index (actor, 1)),
                   ==,
                   "foo");
  g_assert_cmpstr (clutter_actor_get_name (clutter_actor_get_child_at_index (actor, 2)),
                   ==,
                   "baz");
  g_assert_false (clutter_actor_is_visible (iter));
  g_object_get (iter, "show-on-set-parent", &show_on_set_parent, NULL);
  g_assert_false (show_on_set_parent);

  assert_first_child_notified (&first_child_notify_data,
                               clutter_actor_get_child_at_index (actor, 0));
  assert_child_not_notified (&last_child_notify_data);

  iter = clutter_actor_get_child_at_index (actor, 2);
  clutter_actor_set_child_below_sibling (actor, iter, NULL);

  g_assert_cmpstr (clutter_actor_get_name (clutter_actor_get_child_at_index (actor, 0)),
                   ==,
                   "baz");
  g_assert_cmpstr (clutter_actor_get_name (clutter_actor_get_child_at_index (actor, 1)),
                   ==,
                   "bar");
  g_assert_cmpstr (clutter_actor_get_name (clutter_actor_get_child_at_index (actor, 2)),
                   ==,
                   "foo");
  g_assert_false (clutter_actor_is_visible (iter));
  g_object_get (iter, "show-on-set-parent", &show_on_set_parent, NULL);
  g_assert_false (show_on_set_parent);

  assert_first_child_notified (&first_child_notify_data,
                               clutter_actor_get_child_at_index (actor, 0));
  assert_last_child_notified (&last_child_notify_data,
                              clutter_actor_get_child_at_index (actor, 2));

  iter = clutter_actor_get_child_at_index (actor, 0);
  clutter_actor_set_child_below_sibling (actor, iter,
                                         clutter_actor_get_child_at_index (actor, 2));

  g_assert_cmpstr (clutter_actor_get_name (clutter_actor_get_child_at_index (actor, 0)),
                   ==,
                   "bar");
  g_assert_cmpstr (clutter_actor_get_name (clutter_actor_get_child_at_index (actor, 1)),
                   ==,
                   "baz");
  g_assert_cmpstr (clutter_actor_get_name (clutter_actor_get_child_at_index (actor, 2)),
                   ==,
                   "foo");

  assert_first_child_notified (&first_child_notify_data,
                               clutter_actor_get_child_at_index (actor, 0));
  assert_child_not_notified (&last_child_notify_data);

  clutter_actor_add_child (actor, g_object_new (CLUTTER_TYPE_ACTOR,
                                                "name", "zap",
                                                "visible", FALSE,
                                                NULL));
  g_assert_cmpint (clutter_actor_get_n_children (actor), ==, 4);
  assert_last_child_notified (&last_child_notify_data,
                              clutter_actor_get_child_at_index (actor, 3));

  iter = clutter_actor_get_child_at_index (actor, 2);
  clutter_actor_set_child_below_sibling (actor, iter,
                                         clutter_actor_get_child_at_index (actor, 1));

  g_assert_cmpstr (clutter_actor_get_name (clutter_actor_get_child_at_index (actor, 0)),
                   ==,
                   "bar");
  g_assert_cmpstr (clutter_actor_get_name (clutter_actor_get_child_at_index (actor, 1)),
                   ==,
                   "foo");
  g_assert_cmpstr (clutter_actor_get_name (clutter_actor_get_child_at_index (actor, 2)),
                   ==,
                   "baz");
  g_assert_cmpstr (clutter_actor_get_name (clutter_actor_get_child_at_index (actor, 3)),
                   ==,
                   "zap");
  assert_child_not_notified (&first_child_notify_data);
  assert_child_not_notified (&last_child_notify_data);

  clutter_actor_destroy (actor);
  g_assert_null (actor);
}

static void
actor_replace_child (void)
{
  ClutterActor *actor = clutter_actor_new ();
  ClutterActor *iter;

  g_object_ref_sink (actor);
  g_object_add_weak_pointer (G_OBJECT (actor), (gpointer *) &actor);

  clutter_actor_add_child (actor, g_object_new (CLUTTER_TYPE_ACTOR,
                                                "name", "foo",
                                                NULL));
  clutter_actor_add_child (actor, g_object_new (CLUTTER_TYPE_ACTOR,
                                                "name", "bar",
                                                NULL));

  iter = clutter_actor_get_child_at_index (actor, 0);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "foo");

  clutter_actor_replace_child (actor, iter,
                               g_object_new (CLUTTER_TYPE_ACTOR,
                                             "name", "baz",
                                             NULL));

  iter = clutter_actor_get_child_at_index (actor, 0);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "baz");

  iter = clutter_actor_get_child_at_index (actor, 1);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "bar");

  clutter_actor_replace_child (actor, iter,
                               g_object_new (CLUTTER_TYPE_ACTOR,
                                             "name", "qux",
                                             NULL));

  iter = clutter_actor_get_child_at_index (actor, 0);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "baz");

  iter = clutter_actor_get_child_at_index (actor, 1);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "qux");

  clutter_actor_add_child (actor, g_object_new (CLUTTER_TYPE_ACTOR,
                                                "name", "foo",
                                                NULL));

  clutter_actor_replace_child (actor, iter,
                               g_object_new (CLUTTER_TYPE_ACTOR,
                                             "name", "bar",
                                             NULL));

  iter = clutter_actor_get_last_child (actor);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "foo");
  iter = clutter_actor_get_previous_sibling (iter);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "bar");
  iter = clutter_actor_get_previous_sibling (iter);
  g_assert_cmpstr (clutter_actor_get_name (iter), ==, "baz");

  clutter_actor_destroy (actor);
  g_assert_null (actor);
}

static void
actor_remove_all (void)
{
  ClutterActor *actor = clutter_actor_new ();

  g_object_ref_sink (actor);
  g_object_add_weak_pointer (G_OBJECT (actor), (gpointer *) &actor);

  clutter_actor_add_child (actor, g_object_new (CLUTTER_TYPE_ACTOR,
                                                "name", "foo",
                                                NULL));
  clutter_actor_add_child (actor, g_object_new (CLUTTER_TYPE_ACTOR,
                                                "name", "bar",
                                                NULL));
  clutter_actor_add_child (actor, g_object_new (CLUTTER_TYPE_ACTOR,
                                                "name", "baz",
                                                NULL));

  g_assert_cmpint (clutter_actor_get_n_children (actor), ==, 3);

  clutter_actor_remove_all_children (actor);

  g_assert_cmpint (clutter_actor_get_n_children (actor), ==, 0);

  clutter_actor_destroy (actor);
  g_assert_null (actor);
}

static void
child_added (ClutterActor *container,
             ClutterActor *child,
             gpointer      data)
{
  int *counter = data;

  if (!g_test_quiet ())
    g_print ("Adding actor '%s'\n", clutter_actor_get_name (child));

  *counter += 1;
}

static void
remove_child_added (ClutterActor *container,
                    ClutterActor *child,
                    gpointer      data)
{
  ClutterActor *actor = CLUTTER_ACTOR (container);
  ClutterActor *old_child;

  child_added (container, child, data);

  old_child = clutter_actor_get_child_at_index (actor, 0);
  if (old_child != child)
    clutter_actor_remove_child (actor, old_child);
}

static void
child_removed (ClutterActor *container,
               ClutterActor *child,
               gpointer      data)
{
  int *counter = data;

  if (!g_test_quiet ())
    g_print ("Removing actor '%s'\n", clutter_actor_get_name (child));

  *counter += 1;
}

static void
actor_container_signals (void)
{
  ClutterActor *actor = clutter_actor_new ();
  int add_count, remove_count;

  g_object_ref_sink (actor);
  g_object_add_weak_pointer (G_OBJECT (actor), (gpointer *) &actor);

  add_count = remove_count = 0;
  g_signal_connect (actor,
                    "child-added", G_CALLBACK (remove_child_added),
                    &add_count);
  g_signal_connect (actor,
                    "child-removed", G_CALLBACK (child_removed),
                    &remove_count);

  clutter_actor_add_child (actor, g_object_new (CLUTTER_TYPE_ACTOR,
                                                "name", "foo",
                                                NULL));

  g_assert_cmpint (add_count, ==, 1);
  g_assert_cmpint (remove_count, ==, 0);
  g_assert_cmpint (clutter_actor_get_n_children (actor), ==, 1);

  clutter_actor_add_child (actor, g_object_new (CLUTTER_TYPE_ACTOR,
                                                "name", "bar",
                                                NULL));

  g_assert_cmpint (add_count, ==, 2);
  g_assert_cmpint (remove_count, ==, 1);
  g_assert_cmpint (clutter_actor_get_n_children (actor), ==, 1);

  g_signal_handlers_disconnect_by_func (actor, G_CALLBACK (remove_child_added),
                                        &add_count);
  g_signal_handlers_disconnect_by_func (actor, G_CALLBACK (child_removed),
                                        &remove_count);

  clutter_actor_destroy (actor);
  g_assert_null (actor);
}

static void
actor_noop_child_assert_no_change (ClutterActor  *actor,
                                   ClutterActor **children)
{
  for (int i = 0; i < clutter_actor_get_n_children (actor); ++i)
    {
      ClutterActor *child = clutter_actor_get_child_at_index (actor, i);
      g_autofree char *expected_name = g_strdup_printf ("child%d", i + 1);

      g_assert_cmpstr (clutter_actor_get_name (child), ==, expected_name);
    }

  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_first_child (actor)),
                   ==,
                   clutter_actor_get_name (children[0]));
  g_assert_cmpstr (clutter_actor_get_name (
                     clutter_actor_get_last_child (actor)),
                   ==,
                   clutter_actor_get_name (
                     children[clutter_actor_get_n_children (actor) - 1]));
}

static void
actor_noop_child (void)
{
  ClutterActor *actor = clutter_actor_new ();
  ChildNotifyData first_child_notify_data = {0};
  ChildNotifyData last_child_notify_data = {0};
  ClutterActor *children[5];
  int add_count = 0;
  int remove_count = 0;

  g_object_ref_sink (actor);
  g_object_add_weak_pointer (G_OBJECT (actor), (gpointer *) &actor);

  g_signal_connect (actor,
                    "child-added", G_CALLBACK (child_added),
                    &add_count);
  g_signal_connect (actor,
                    "child-removed", G_CALLBACK (child_removed),
                    &remove_count);

  g_signal_connect (actor,
                    "notify::first-child",
                    G_CALLBACK (on_first_last_child_notify),
                    &first_child_notify_data);
  g_signal_connect (actor,
                    "notify::last-child",
                    G_CALLBACK (on_first_last_child_notify),
                    &last_child_notify_data);

  children[0] = g_object_new (CLUTTER_TYPE_ACTOR, "name", "child1", NULL);
  children[1] = g_object_new (CLUTTER_TYPE_ACTOR, "name", "child2", NULL);
  children[2] = g_object_new (CLUTTER_TYPE_ACTOR, "name", "child3", NULL);
  children[3] = g_object_new (CLUTTER_TYPE_ACTOR, "name", "child4", NULL);
  children[4] = g_object_new (CLUTTER_TYPE_ACTOR, "name", "child5", NULL);

  for (int i = 0; i < G_N_ELEMENTS (children); ++i)
    {
      g_test_message ("Adding %s", clutter_actor_get_name (children[i]));
      clutter_actor_add_child (actor, children[i]);
      g_assert_cmpint (add_count, ==, i + 1);
      g_assert_cmpint (remove_count, ==, 0);

      if (i == 0)
        assert_first_child_notified (&first_child_notify_data, children[i]);
      else
        assert_child_not_notified (&first_child_notify_data);
      assert_last_child_notified (&last_child_notify_data, children[i]);
    }

  g_assert_cmpint (clutter_actor_get_n_children (actor), ==,
                   G_N_ELEMENTS (children));

  actor_noop_child_assert_no_change (actor, children);

  for (int i = -1; i < (int) G_N_ELEMENTS (children) - 1; ++i)
    {
      ClutterActor *sibling = i < 0 ? NULL : children[i + 1];
      ClutterActor *child = i < 0 ? children[0] : children[i];

      g_test_message ("Keep %s below %s (no change)",
                      clutter_actor_get_name (child),
                      sibling ? clutter_actor_get_name (sibling) : NULL);
      clutter_actor_set_child_below_sibling (actor, child, sibling);
      actor_noop_child_assert_no_change (actor, children);
      g_assert_cmpint (add_count, ==, G_N_ELEMENTS (children));
      g_assert_cmpint (remove_count, ==, 0);
      assert_child_not_notified (&first_child_notify_data);
      assert_child_not_notified (&last_child_notify_data);
    }

  for (unsigned i = G_N_ELEMENTS (children); i > 0; --i)
    {
      ClutterActor *sibling = i == G_N_ELEMENTS (children) ?
                              NULL : children[i - 1];
      ClutterActor *child = i == G_N_ELEMENTS (children) ?
                            children[G_N_ELEMENTS (children) - 1] : children[i];

      g_test_message ("Keep %s above %s (no change)",
                      clutter_actor_get_name (child),
                      sibling ? clutter_actor_get_name (sibling) : NULL);
      clutter_actor_set_child_above_sibling (actor, child, sibling);
      actor_noop_child_assert_no_change (actor, children);
      g_assert_cmpint (add_count, ==, G_N_ELEMENTS (children));
      g_assert_cmpint (remove_count, ==, 0);
      assert_child_not_notified (&first_child_notify_data);
      assert_child_not_notified (&last_child_notify_data);
    }

  for (unsigned i = 0; i < G_N_ELEMENTS (children); ++i)
    {
      g_test_message ("Keep %s at index %d",
                      clutter_actor_get_name (children[i]), i);
      clutter_actor_set_child_at_index (actor, children[i], i);
      actor_noop_child_assert_no_change (actor, children);
      g_assert_cmpint (add_count, ==, G_N_ELEMENTS (children));
      g_assert_cmpint (remove_count, ==, 0);
      assert_child_not_notified (&first_child_notify_data);
      assert_child_not_notified (&last_child_notify_data);
    }

  clutter_actor_destroy (actor);
  g_assert_null (actor);
}

static void
actor_contains (void)
{
  /* This build up the following tree:
   *
   *              a
   *          ╱   │   ╲
   *         ╱    │    ╲
   *        b     c     d
   *       ╱ ╲   ╱ ╲   ╱ ╲
   *      e   f g   h i   j
   */
  struct {
    ClutterActor *actor_a, *actor_b, *actor_c, *actor_d, *actor_e;
    ClutterActor *actor_f, *actor_g, *actor_h, *actor_i, *actor_j;
  } d;
  int x, y;
  ClutterActor **actor_array = &d.actor_a;

  /* Matrix of expected results */
  static const gboolean expected_results[] =
    {         /* a, b, c, d, e, f, g, h, i, j */
      /* a */    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      /* b */    0, 1, 0, 0, 1, 1, 0, 0, 0, 0,
      /* c */    0, 0, 1, 0, 0, 0, 1, 1, 0, 0,
      /* d */    0, 0, 0, 1, 0, 0, 0, 0, 1, 1,
      /* e */    0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
      /* f */    0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
      /* g */    0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
      /* h */    0, 0, 0, 0, 0, 0, 0, 1, 0, 0,
      /* i */    0, 0, 0, 0, 0, 0, 0, 0, 1, 0,
      /* j */    0, 0, 0, 0, 0, 0, 0, 0, 0, 1
    };

  d.actor_a = clutter_actor_new ();
  d.actor_b = clutter_actor_new ();
  d.actor_c = clutter_actor_new ();
  d.actor_d = clutter_actor_new ();
  d.actor_e = clutter_actor_new ();
  d.actor_f = clutter_actor_new ();
  d.actor_g = clutter_actor_new ();
  d.actor_h = clutter_actor_new ();
  d.actor_i = clutter_actor_new ();
  d.actor_j = clutter_actor_new ();

  clutter_actor_add_child (d.actor_a, d.actor_b);
  clutter_actor_add_child (d.actor_a, d.actor_c);
  clutter_actor_add_child (d.actor_a, d.actor_d);

  clutter_actor_add_child (d.actor_b, d.actor_e);
  clutter_actor_add_child (d.actor_b, d.actor_f);

  clutter_actor_add_child (d.actor_c, d.actor_g);
  clutter_actor_add_child (d.actor_c, d.actor_h);

  clutter_actor_add_child (d.actor_d, d.actor_i);
  clutter_actor_add_child (d.actor_d, d.actor_j);

  for (y = 0; y < 10; y++)
    for (x = 0; x < 10; x++)
      g_assert_cmpint (clutter_actor_contains (actor_array[x],
                                               actor_array[y]),
                       ==,
                       expected_results[x * 10 + y]);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/actor/graph/add-child", actor_add_child)
  CLUTTER_TEST_UNIT ("/actor/graph/insert-child", actor_insert_child)
  CLUTTER_TEST_UNIT ("/actor/graph/swap-child", actor_swap_child)
  CLUTTER_TEST_UNIT ("/actor/graph/remove-child", actor_remove_child)
  CLUTTER_TEST_UNIT ("/actor/graph/raise-child", actor_raise_child)
  CLUTTER_TEST_UNIT ("/actor/graph/lower-child", actor_lower_child)
  CLUTTER_TEST_UNIT ("/actor/graph/replace-child", actor_replace_child)
  CLUTTER_TEST_UNIT ("/actor/graph/noop-child", actor_noop_child)
  CLUTTER_TEST_UNIT ("/actor/graph/remove-all", actor_remove_all)
  CLUTTER_TEST_UNIT ("/actor/graph/container-signals", actor_container_signals)
  CLUTTER_TEST_UNIT ("/actor/graph/contains", actor_contains)
)
