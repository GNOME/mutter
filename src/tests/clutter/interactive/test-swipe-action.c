#include <stdlib.h>
#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"

enum
{
  VERTICAL      = 0,
  HORIZONTAL    = 1,
  BOTH          = 2
};

int
test_swipe_action_main (int argc, char *argv[]);

const char *
test_swipe_action_describe (void);

static void
swipe_cb (ClutterSwipeAction    *action,
          ClutterActor          *actor,
          ClutterSwipeDirection  direction,
          gpointer               data_)
{
  guint axis = GPOINTER_TO_UINT (data_);
  gchar *direction_str = g_strdup ("");

  if (axis == HORIZONTAL &&
      ((direction & CLUTTER_SWIPE_DIRECTION_UP) != 0 ||
       (direction & CLUTTER_SWIPE_DIRECTION_DOWN) != 0))
    {
      g_print ("discarding non-horizontal swipe on '%s'\n",
               clutter_actor_get_name (actor));
      return;
    }

  if (axis == VERTICAL &&
      ((direction & CLUTTER_SWIPE_DIRECTION_LEFT) != 0 ||
       (direction & CLUTTER_SWIPE_DIRECTION_RIGHT) != 0))
    {
      g_print ("discarding non-vertical swipe on '%s'\n",
               clutter_actor_get_name (actor));
      return;
    }

  if (direction & CLUTTER_SWIPE_DIRECTION_UP)
    {
      char *old_str = direction_str;

      direction_str = g_strconcat (direction_str, " up", NULL);
      g_free (old_str);
    }

  if (direction & CLUTTER_SWIPE_DIRECTION_DOWN)
    {
      char *old_str = direction_str;

      direction_str = g_strconcat (direction_str, " down", NULL);
      g_free (old_str);
    }

  if (direction & CLUTTER_SWIPE_DIRECTION_LEFT)
    {
      char *old_str = direction_str;

      direction_str = g_strconcat (direction_str, " left", NULL);
      g_free (old_str);
    }

  if (direction & CLUTTER_SWIPE_DIRECTION_RIGHT)
    {
      char *old_str = direction_str;

      direction_str = g_strconcat (direction_str, " right", NULL);
      g_free (old_str);
    }

  g_print ("swipe: '%s': %s\n", clutter_actor_get_name (actor), direction_str);

  g_free (direction_str);
}

static void
gesture_cancel_cb (ClutterSwipeAction    *action,
                   ClutterActor          *actor,
                   gpointer               user_data)
{
  g_debug ("gesture cancelled: '%s'", clutter_actor_get_name (actor));
}

static void
attach_action (ClutterActor *actor, guint axis)
{
  ClutterAction *action;

  action = g_object_new (CLUTTER_TYPE_SWIPE_ACTION, NULL);
  clutter_actor_add_action (actor, action);
  g_signal_connect (action, "swipe", G_CALLBACK (swipe_cb), GUINT_TO_POINTER (axis));
  g_signal_connect (action, "gesture-cancel", G_CALLBACK (gesture_cancel_cb), NULL);
}

static ClutterActor *
create_label (const char *markup)
{
  return CLUTTER_ACTOR (g_object_new (CLUTTER_TYPE_TEXT,
                                      "text", markup,
                                      "use-markup", TRUE,
                                      "x-expand", TRUE,
                                      "y-expand", TRUE,
                                      "x-align", CLUTTER_ACTOR_ALIGN_START,
                                      "y-align", CLUTTER_ACTOR_ALIGN_CENTER,
                                      NULL));
}

G_MODULE_EXPORT int
test_swipe_action_main (int argc, char *argv[])
{
  ClutterActor *stage, *rect;

  clutter_test_init (&argc, &argv);

  stage = clutter_test_get_stage ();
  clutter_stage_set_title (CLUTTER_STAGE (stage), "Swipe action");
  clutter_actor_set_size (stage, 640, 480);
  g_signal_connect (stage, "destroy", G_CALLBACK (clutter_test_quit), NULL);

  rect = clutter_actor_new ();
  clutter_actor_set_background_color (rect, &CLUTTER_COLOR_INIT (255, 0, 0, 255));
  clutter_actor_set_name (rect, "Vertical swipes");
  clutter_actor_set_size (rect, 150, 150);
  clutter_actor_set_position (rect, 10, 100);
  clutter_actor_set_reactive (rect, TRUE);
  clutter_actor_add_child (stage, rect);
  attach_action (rect, VERTICAL);

  rect = clutter_actor_new ();
  clutter_actor_set_background_color (rect, &CLUTTER_COLOR_INIT (0, 0, 255, 255));
  clutter_actor_set_name (rect, "Horizontal swipes");
  clutter_actor_set_size (rect, 150, 150);
  clutter_actor_set_position (rect, 170, 100);
  clutter_actor_set_reactive (rect, TRUE);
  clutter_actor_add_child (stage, rect);
  attach_action (rect, HORIZONTAL);

  rect = clutter_actor_new ();
  clutter_actor_set_background_color (rect, &CLUTTER_COLOR_INIT (0, 255, 0, 255));
  clutter_actor_set_name (rect, "All swipes");
  clutter_actor_set_size (rect, 150, 150);
  clutter_actor_set_position (rect, 330, 100);
  clutter_actor_set_reactive (rect, TRUE);
  clutter_actor_add_child (stage, rect);
  attach_action (rect, BOTH);

  {
    ClutterLayoutManager *layout = clutter_box_layout_new ();
    ClutterActor *box;
    float offset;

    clutter_box_layout_set_orientation (CLUTTER_BOX_LAYOUT (layout),
                                        CLUTTER_ORIENTATION_VERTICAL);
    clutter_box_layout_set_spacing (CLUTTER_BOX_LAYOUT (layout), 6);

    box = clutter_actor_new ();
    clutter_actor_set_layout_manager (box, layout);

    clutter_actor_add_child (box,
                             create_label ("<b>Red</b>: vertical swipes only"));

    clutter_actor_add_child (box,
                             create_label ("<b>Blue</b>: horizontal swipes only"));

    clutter_actor_add_child (box,
                             create_label ("<b>Green</b>: both"));

    offset = clutter_actor_get_height (stage)
           - clutter_actor_get_height (box)
           - 12.0;

    clutter_actor_add_child (stage, box);
    clutter_actor_add_constraint (box, clutter_bind_constraint_new (stage,
                                                                    CLUTTER_BIND_X,
                                                                    12.0));
    clutter_actor_add_constraint (box, clutter_bind_constraint_new (stage,
                                                                    CLUTTER_BIND_Y,
                                                                    offset));
  }

  clutter_actor_show (stage);

  clutter_test_main ();

  return EXIT_SUCCESS;
}

G_MODULE_EXPORT const char *
test_swipe_action_describe (void)
{
  return "Swipe gesture recognizer.";
}
