#include <stdlib.h>
#include <gmodule.h>
#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"

static const CoglColor colors[] = {
  { 255,   0,   0, 255 },
  {   0, 255,   0, 255 },
  {   0,   0, 255, 255 },
};

#define PADDING         (64.0f)
#define SIZE            (64.0f)

const char *
test_keyframe_transition_describe (void);

int
test_keyframe_transition_main (int argc, char *argv[]);

static void
on_transition_stopped (ClutterActor *actor,
                       const gchar  *transition_name,
                       gboolean      is_finished)
{
  g_print ("%s: transition stopped: %s (finished: %s)\n",
           clutter_actor_get_name (actor),
           transition_name,
           is_finished ? "yes" : "no");
}

G_MODULE_EXPORT const char *
test_keyframe_transition_describe (void)
{
  return "Demonstrate the keyframe transition.";
}

G_MODULE_EXPORT int
test_keyframe_transition_main (int argc, char *argv[])
{
  ClutterActor *stage;
  int i;

  clutter_test_init (&argc, &argv);

  stage = clutter_test_get_stage ();
  g_signal_connect (stage, "destroy", G_CALLBACK (clutter_test_quit), NULL);

  for (i = 0; i < 3; i++)
    {
      ClutterTransition *transition, *group;
      ClutterActor *rect;
      float cur_x, cur_y;
      float new_x, new_y;
      gchar *name;

      cur_x = PADDING;
      cur_y = PADDING + ((SIZE + PADDING) * i);

      new_x = clutter_actor_get_width (stage) - PADDING - SIZE;
      new_y = (float) g_random_double_range (PADDING,
                                             clutter_actor_get_height (stage) - PADDING - SIZE);

      name = g_strdup_printf ("rect%02d", i);

      rect = clutter_actor_new ();

      clutter_actor_set_name (rect, name);
      clutter_actor_set_background_color (rect, &colors[i]);
      clutter_actor_set_size (rect, SIZE, SIZE);
      clutter_actor_set_position (rect, PADDING, cur_y);
      clutter_actor_add_child (stage, rect);

      group = clutter_transition_group_new ();
      clutter_timeline_set_duration (CLUTTER_TIMELINE (group), 2000);
      clutter_timeline_set_repeat_count (CLUTTER_TIMELINE (group), 1);
      clutter_timeline_set_auto_reverse (CLUTTER_TIMELINE (group), TRUE);

      transition = clutter_keyframe_transition_new ("x");
      clutter_transition_set_from (transition, G_TYPE_FLOAT, cur_x);
      clutter_transition_set_to (transition, G_TYPE_FLOAT, new_x);

      clutter_keyframe_transition_set (CLUTTER_KEYFRAME_TRANSITION (transition),
                                       G_TYPE_FLOAT, 1,
                                       0.5, new_x / 2.0f, CLUTTER_EASE_OUT_EXPO);
      clutter_transition_group_add_transition (CLUTTER_TRANSITION_GROUP (group), transition);
      g_object_unref (transition);

      transition = clutter_keyframe_transition_new ("y");
      clutter_transition_set_from (transition, G_TYPE_FLOAT, cur_y);
      clutter_transition_set_to (transition, G_TYPE_FLOAT, cur_y);

      clutter_keyframe_transition_set (CLUTTER_KEYFRAME_TRANSITION (transition),
                                       G_TYPE_FLOAT, 1,
                                       0.5, new_y, CLUTTER_EASE_OUT_EXPO);
      clutter_transition_group_add_transition (CLUTTER_TRANSITION_GROUP (group), transition);
      g_object_unref (transition);

      clutter_actor_add_transition (rect, "rectAnimation", group);

      g_signal_connect (rect, "transition-stopped",
                        G_CALLBACK (on_transition_stopped),
                        NULL);
      g_object_unref (group);

      g_free (name);
    }

  clutter_actor_show (stage);

  clutter_test_main ();

  return EXIT_SUCCESS;
}
