#include "config.h"

#include "core/window-private.h"
#include "tests/monitor-tests-common.h"

typedef struct
{
  int count;
  MetaWindow *expected_parent;
  gboolean consistent;
} NotifyData;

static gboolean
window_has_transient_child (MetaWindow *window,
                            MetaWindow *child)
{
  GPtrArray *children = meta_window_get_transient_children (window);

  return children && g_ptr_array_find (children, child, NULL);
}

static void
on_transient_for_changed (MetaWindow *window,
                          GParamSpec *pspec,
                          NotifyData *data)
{
  MetaWindow *parent = meta_window_get_transient_for (window);

  data->count++;

  if (parent != data->expected_parent)
    data->consistent = FALSE;

  if (parent && !window_has_transient_child (parent, window))
    data->consistent = FALSE;
}

static MetaWindow *
create_and_show_window (MetaTestClient *client,
                        const char     *name)
{
  g_autoptr (GError) error = NULL;
  MetaWindow *window;

  if (!meta_test_client_do (client, &error,
                            "create", name,
                            NULL))
    g_error ("Failed to create window %s: %s", name, error->message);

  if (!meta_test_client_do (client, &error,
                            "show", name,
                            NULL))
    g_error ("Failed to show window %s: %s", name, error->message);

  window = meta_test_client_find_window (client, name, &error);
  if (!window)
    g_error ("Failed to find window %s: %s", name, error->message);
  meta_wait_for_window_shown (window);

  return window;
}

static void
set_parent (MetaTestClient *client,
            const char     *name,
            const char     *parent_name)
{
  g_autoptr (GError) error = NULL;

  if (!meta_test_client_do (client, &error,
                            "set_parent", name, parent_name,
                            NULL))
    g_error ("Failed to set parent of %s: %s", name, error->message);

  if (!meta_test_client_wait (client, &error))
    g_error ("Failed to wait for client: %s", error->message);
}

static void
meta_test_window_transient_for_property (void)
{
  g_autoptr (GError) error = NULL;
  MetaTestClient *client;
  MetaWindow *parent1;
  MetaWindow *parent2;
  MetaWindow *child;
  NotifyData data = { .consistent = TRUE };

  client = meta_test_client_new (test_context, "transient-for",
                                 META_WINDOW_CLIENT_TYPE_WAYLAND, &error);
  if (!client)
    g_error ("Failed to launch test client: %s", error->message);

  parent1 = create_and_show_window (client, "parent1");
  parent2 = create_and_show_window (client, "parent2");
  child = create_and_show_window (client, "child");

  g_assert_null (meta_window_get_transient_for (child));
  g_signal_connect (child, "notify::transient-for",
                    G_CALLBACK (on_transient_for_changed), &data);

  data.expected_parent = parent1;
  set_parent (client, "child", "parent1");
  g_assert_cmpint (data.count, ==, 1);
  g_assert_true (data.consistent);
  g_assert_true (meta_window_get_transient_for (child) == parent1);
  g_assert_true (window_has_transient_child (parent1, child));

  data.expected_parent = parent2;
  set_parent (client, "child", "parent2");
  g_assert_cmpint (data.count, ==, 2);
  g_assert_true (data.consistent);
  g_assert_true (meta_window_get_transient_for (child) == parent2);
  g_assert_false (window_has_transient_child (parent1, child));
  g_assert_true (window_has_transient_child (parent2, child));

  /* Setting the same parent again does not notify */
  set_parent (client, "child", "parent2");
  g_assert_cmpint (data.count, ==, 2);

  g_signal_handlers_disconnect_by_data (child, &data);
  meta_test_client_destroy (client);
}

static void
init_transient_for_tests (void)
{
  meta_add_monitor_test ("/core/window/transient-for-property",
                         meta_test_window_transient_for_property);
}

int
main (int   argc,
      char *argv[])
{
  return meta_monitor_test_main (argc, argv, init_transient_for_tests);
}
