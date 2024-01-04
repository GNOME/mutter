#include "clutter-test-utils.h"

#include <stdlib.h>
#include <glib-object.h>
#include <clutter/clutter.h>

#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-virtual-monitor.h"
#include "compositor/meta-plugin-manager.h"
#include "core/meta-context-private.h"
#include "tests/meta-test-utils.h"

typedef struct
{
  MetaContext *context;
} ClutterTestEnvironment;

struct _ClutterTestActor
{
  ClutterActor parent;
};

G_DEFINE_TYPE (ClutterTestActor, clutter_test_actor, CLUTTER_TYPE_ACTOR)

static ClutterTestEnvironment *test_environ = NULL;

static GMainLoop *clutter_test_main_loop = NULL;

#define DBUS_NAME_WARNING "Lost or failed to acquire name"

static gboolean
log_func (const gchar    *log_domain,
          GLogLevelFlags  log_level,
          const gchar    *message,
          gpointer        user_data)
{
  if ((log_level & G_LOG_LEVEL_WARNING) &&
      g_strcmp0 (log_domain, "mutter") == 0 &&
      g_str_has_prefix (message, DBUS_NAME_WARNING))
    return FALSE;

  return TRUE;
}

/*
 * clutter_test_init:
 * @argc: (inout): number of arguments in @argv
 * @argv: (inout) (array length=argc) (nullable): array of arguments
 *
 * Initializes the Clutter test environment.
 */
void
clutter_test_init (int    *argc,
                   char ***argv)
{
  MetaContext *context;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert (meta_context_configure (context, argc, argv, NULL));
  g_assert (meta_context_setup (context, NULL));

  test_environ = g_new0 (ClutterTestEnvironment, 1);
  test_environ->context = context;

  g_assert (meta_context_start (context, NULL));

  clutter_test_main_loop = g_main_loop_new (NULL, FALSE);
}

/**
 * clutter_test_get_stage:
 *
 * Retrieves the #ClutterStage used for testing.
 *
 * Return value: (transfer none): the stage used for testing
 */
ClutterActor *
clutter_test_get_stage (void)
{
  MetaContext *context = test_environ->context;
  MetaBackend *backend = meta_context_get_backend (context);

  return meta_backend_get_stage (backend);
}

void
clutter_test_flush_input (void)
{
  meta_flush_input (test_environ->context);
}

typedef struct {
  gpointer test_func;
  gpointer test_data;
  GDestroyNotify test_notify;
} ClutterTestData;

static gboolean
list_equal_unsorted (GList *list_a,
                     GList *list_b)
{
  GList *l_a;
  GList *l_b;

  for (l_a = list_a, l_b = list_b;
       l_a && l_b;
       l_a = l_a->next, l_b = l_b->next)
    {
      if (l_a->data != l_b->data)
        return FALSE;
    }

  return !l_a && !l_b;
}

static void
clutter_test_func_wrapper (gconstpointer data_)
{
  const ClutterTestData *data = data_;
  ClutterActor *stage;
  GList *pre_stage_children;
  GList *post_stage_children;

  g_test_log_set_fatal_handler (log_func, NULL);

  /* ensure that the previous test state has been cleaned up */
  stage = clutter_test_get_stage ();
  clutter_actor_hide (stage);

  pre_stage_children = clutter_actor_get_children (stage);

  if (data->test_data != NULL)
    {
      GTestDataFunc test_func = data->test_func;

      test_func (data->test_data);
    }
  else
    {
      GTestFunc test_func = data->test_func;

      test_func ();
    }

  if (data->test_notify != NULL)
    data->test_notify (data->test_data);

  post_stage_children = clutter_actor_get_children (stage);

  g_assert_true (list_equal_unsorted (pre_stage_children, post_stage_children));

  g_list_free (pre_stage_children);
  g_list_free (post_stage_children);

  clutter_actor_hide (stage);
}

/**
 * clutter_test_add: (skip)
 * @test_path: unique path for identifying the test
 * @test_func: function containing the test
 *
 * Adds a test unit to the Clutter test environment.
 *
 * See also: g_test_add()
 */
void
clutter_test_add (const char *test_path,
                  GTestFunc   test_func)
{
  clutter_test_add_data_full (test_path, (GTestDataFunc) test_func, NULL, NULL);
}

/**
 * clutter_test_add_data: (skip)
 * @test_path: unique path for identifying the test
 * @test_func: function containing the test
 * @test_data: data to pass to the test function
 *
 * Adds a test unit to the Clutter test environment.
 *
 * See also: g_test_add_data_func()
 */
void
clutter_test_add_data (const char    *test_path,
                       GTestDataFunc  test_func,
                       gpointer       test_data)
{
  clutter_test_add_data_full (test_path, test_func, test_data, NULL);
}

/**
 * clutter_test_add_data_full:
 * @test_path: unique path for identifying the test
 * @test_func: (scope notified): function containing the test
 * @test_data: (closure): data to pass to the test function
 * @test_notify: function called when the test function ends
 *
 * Adds a test unit to the Clutter test environment.
 *
 * See also: g_test_add_data_func_full()
 */
void
clutter_test_add_data_full (const char     *test_path,
                            GTestDataFunc   test_func,
                            gpointer        test_data,
                            GDestroyNotify  test_notify)
{
  ClutterTestData *data;

  g_return_if_fail (test_path != NULL);
  g_return_if_fail (test_func != NULL);

  g_assert (test_environ != NULL);

  data = g_new (ClutterTestData, 1);
  data->test_func = test_func;
  data->test_data = test_data;
  data->test_notify = test_notify;

  g_test_add_data_func_full (test_path, data,
                             clutter_test_func_wrapper,
                             g_free);
}

/**
 * clutter_test_run:
 *
 * Runs the test suite using the units added by calling
 * clutter_test_add().
 *
 * The typical test suite is composed of a list of functions
 * called by clutter_test_run(), for instance:
 *
 * ```c
 * static void unit_foo (void) { ... }
 *
 * static void unit_bar (void) { ... }
 *
 * static void unit_baz (void) { ... }
 *
 * int
 * main (int argc, char *argv[])
 * {
 *   clutter_test_init (&argc, &argv);
 *
 *   clutter_test_add ("/unit/foo", unit_foo);
 *   clutter_test_add ("/unit/bar", unit_bar);
 *   clutter_test_add ("/unit/baz", unit_baz);
 *
 *   return clutter_test_run ();
 * }
 * ```
 *
 * Return value: the exit code for the test suite
 */
int
clutter_test_run (void)
{
  MetaBackend *backend = meta_context_get_backend (test_environ->context);
  MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);
  MetaVirtualMonitor *virtual_monitor;
  g_autoptr (MetaVirtualMonitorInfo) monitor_info = NULL;
  g_autoptr (GError) error = NULL;
  int res;

  monitor_info = meta_virtual_monitor_info_new (800, 600, 10.0,
                                                "MetaTestVendor",
                                                "ClutterTestMonitor",
                                                "0x123");
  virtual_monitor = meta_monitor_manager_create_virtual_monitor (monitor_manager,
                                                                 monitor_info,
                                                                 &error);
  if (!virtual_monitor)
    g_error ("Failed to create virtual monitor: %s", error->message);

  meta_monitor_manager_reload (monitor_manager);

  res = g_test_run ();

  g_object_unref (virtual_monitor);

  g_clear_object (&test_environ->context);
  g_free (test_environ);

  return res;
}

void
clutter_test_main (void)
{
  g_assert_nonnull (clutter_test_main_loop);

  g_main_loop_run (clutter_test_main_loop);
}

void
clutter_test_quit (void)
{
  g_assert_nonnull (clutter_test_main_loop);

  g_main_loop_quit (clutter_test_main_loop);
}

typedef struct {
  ClutterActor *stage;

  graphene_point_t point;

  gpointer result;

  guint check_actor : 1;
  guint check_color : 1;

  guint was_painted : 1;
} ValidateData;

static gboolean
validate_stage (gpointer data_)
{
  ValidateData *data = data_;

  if (data->check_actor)
    {
      data->result =
        clutter_stage_get_actor_at_pos (CLUTTER_STAGE (data->stage),
                                        CLUTTER_PICK_ALL,
                                        data->point.x,
                                        data->point.y);
    }

  if (data->check_color)
    {
      data->result =
        clutter_stage_read_pixels (CLUTTER_STAGE (data->stage),
                                   data->point.x,
                                   data->point.y,
                                   1, 1);
    }

  if (!g_test_verbose ())
    {
      clutter_actor_hide (data->stage);
      data->was_painted = TRUE;
    }

  return G_SOURCE_REMOVE;
}

static gboolean
on_key_press_event (ClutterActor *stage,
                    ClutterEvent *event,
                    gpointer      data_)
{
  ValidateData *data = data_;

  if (data->stage == stage &&
      clutter_event_get_key_symbol (event) == CLUTTER_KEY_Escape)
    {
      clutter_actor_hide (stage);

      data->was_painted = TRUE;
    }

  return CLUTTER_EVENT_PROPAGATE;
}

/**
 * clutter_test_check_actor_at_point:
 * @stage: a #ClutterStage
 * @point: coordinates to check
 * @actor: the expected actor at the given coordinates
 * @result: (out) (nullable): actor at the coordinates
 *
 * Checks the given coordinates of the @stage and compares the
 * actor found there with the given @actor.
 *
 * Returns: %TRUE if the actor at the given coordinates matches
 */
gboolean
clutter_test_check_actor_at_point (ClutterActor            *stage,
                                   const graphene_point_t  *point,
                                   ClutterActor            *actor,
                                   ClutterActor           **result)
{
  ValidateData *data;
  gulong press_id = 0;

  g_return_val_if_fail (CLUTTER_IS_STAGE (stage), FALSE);
  g_return_val_if_fail (point != NULL, FALSE);
  g_return_val_if_fail (CLUTTER_IS_ACTOR (stage), FALSE);
  g_return_val_if_fail (result != NULL, FALSE);

  data = g_new0 (ValidateData, 1);
  data->stage = stage;
  data->point = *point;
  data->check_actor = TRUE;

  if (g_test_verbose ())
    {
      g_printerr ("Press ESC to close the stage and resume the test\n");
      press_id = g_signal_connect (stage, "key-press-event",
                                   G_CALLBACK (on_key_press_event),
                                   data);
    }

  clutter_actor_show (stage);

  clutter_threads_add_repaint_func_full (CLUTTER_REPAINT_FLAGS_POST_PAINT,
                                         validate_stage,
                                         data,
                                         NULL);

  while (!data->was_painted)
    g_main_context_iteration (NULL, TRUE);

  *result = data->result;

  g_clear_signal_handler (&press_id, stage);

  g_free (data);

  return *result == actor;
}

/**
 * clutter_test_check_color_at_point:
 * @stage: a #ClutterStage
 * @point: coordinates to check
 * @color: expected color
 * @result: (out caller-allocates): color at the given coordinates
 *
 * Checks the color at the given coordinates on @stage, and matches
 * it with the red, green, and blue channels of @color. The alpha
 * component of @color and @result is ignored.
 *
 * Returns: %TRUE if the colors match
 */
gboolean
clutter_test_check_color_at_point (ClutterActor           *stage,
                                   const graphene_point_t *point,
                                   const ClutterColor     *color,
                                   ClutterColor           *result)
{
  ValidateData *data;
  gboolean retval;
  guint8 *buffer;
  gulong press_id = 0;

  g_return_val_if_fail (CLUTTER_IS_STAGE (stage), FALSE);
  g_return_val_if_fail (point != NULL, FALSE);
  g_return_val_if_fail (color != NULL, FALSE);
  g_return_val_if_fail (result != NULL, FALSE);

  data = g_new0 (ValidateData, 1);
  data->stage = stage;
  data->point = *point;
  data->check_color = TRUE;

  if (g_test_verbose ())
    {
      g_printerr ("Press ESC to close the stage and resume the test\n");
      press_id = g_signal_connect (stage, "key-press-event",
                                   G_CALLBACK (on_key_press_event),
                                   data);
    }

  clutter_actor_show (stage);

  clutter_threads_add_repaint_func_full (CLUTTER_REPAINT_FLAGS_POST_PAINT,
                                         validate_stage,
                                         data,
                                         NULL);

  while (!data->was_painted)
    g_main_context_iteration (NULL, TRUE);

  g_clear_signal_handler (&press_id, stage);

  buffer = data->result;

  clutter_color_init (result, buffer[0], buffer[1], buffer[2], 255);

  /* we only check the color channels, so we can't use clutter_color_equal() */
  retval = buffer[0] == color->red &&
           buffer[1] == color->green &&
           buffer[2] == color->blue;

  g_free (data->result);
  g_free (data);

  return retval;
}

static void
test_actor_paint (ClutterActor        *actor,
                  ClutterPaintContext *paint_context)
{
  g_signal_emit_by_name (actor, "paint", paint_context);
}

static void
clutter_test_actor_class_init (ClutterTestActorClass *klass)
{
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);

  actor_class->paint = test_actor_paint;

  g_signal_new ("paint",
                G_TYPE_FROM_CLASS (klass),
                G_SIGNAL_RUN_LAST,
                0,
                NULL, NULL, NULL,
                G_TYPE_NONE, 1,
                CLUTTER_TYPE_PAINT_CONTEXT);
}

static void
clutter_test_actor_init (ClutterTestActor *test_actor)
{
}
