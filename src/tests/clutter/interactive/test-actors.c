#include <clutter/clutter.h>

#include <math.h>
#include <errno.h>
#include <stdlib.h>
#include <glib.h>
#include <gmodule.h>

#include "test-utils.h"
#include "tests/clutter-test-utils.h"

#define NHANDS  6

typedef struct SuperOH
{
  ClutterActor **hand;
  ClutterActor  *bgtex;
  ClutterActor  *real_hand;
  ClutterActor  *group;
  ClutterActor  *stage;

  gint stage_width;
  gint stage_height;
  gfloat radius;

  ClutterTimeline *timeline;
} SuperOH;

int
test_actors_main (int argc, char *argv[]);

static void
on_group_destroy (ClutterActor *actor,
                  SuperOH      *oh)
{
  oh->group = NULL;
}

static void
on_hand_destroy (ClutterActor *actor,
                 SuperOH      *oh)
{
  int i;

  for (i = 0; i < NHANDS; i++)
    {
      if (oh->hand[i] == actor)
        oh->hand[i] = NULL;
    }
}

static gboolean
on_button_press_event (ClutterActor *actor,
                       ClutterEvent *event,
                       SuperOH      *oh)
{
  gfloat x, y;

  clutter_event_get_coords (event, &x, &y);

  g_print ("*** button press event (button:%d) at %.2f, %.2f on %s ***\n",
           clutter_event_get_button (event),
           x, y,
           clutter_actor_get_name (actor));

  clutter_actor_hide (actor);

  return TRUE;
}

static gboolean
input_cb (ClutterActor *stage,
          ClutterEvent *event,
          gpointer      data)
{
  SuperOH *oh = data;

  if (clutter_event_type (event) == CLUTTER_KEY_RELEASE)
    {
      g_print ("*** key press event (key:%c) ***\n",
               clutter_event_get_key_symbol (event));

      if (clutter_event_get_key_symbol (event) == CLUTTER_KEY_q)
        {
          clutter_test_quit ();

          return TRUE;
        }
      else if (clutter_event_get_key_symbol (event) == CLUTTER_KEY_r)
        {
          gint i;

          for (i = 0; i < NHANDS; i++)
            {
              if (oh->hand[i] != NULL)
                clutter_actor_show (oh->hand[i]);
            }

          return TRUE;
        }
    }

  return FALSE;
}

/* Timeline handler */
static void
frame_cb (ClutterTimeline *timeline,
          gint             msecs,
          gpointer         data)
{
  SuperOH *oh = data;
  gint i;
  float rotation = clutter_timeline_get_progress (timeline) * 360.0f;

  /* Rotate everything clockwise about stage center*/
  if (oh->group != NULL)
    clutter_actor_set_rotation_angle (oh->group, CLUTTER_Z_AXIS, rotation);

  for (i = 0; i < NHANDS; i++)
    {
      /* Rotate each hand around there centers - to get this we need
       * to take into account any scaling.
       */
      if (oh->hand[i] != NULL)
        clutter_actor_set_rotation_angle (oh->hand[i],
                                          CLUTTER_Z_AXIS,
                                          -6.0 * rotation);
    }
}

static void
stop_and_quit (ClutterActor *stage,
               SuperOH      *data)
{
  clutter_timeline_stop (data->timeline);

  clutter_test_quit ();
}

G_MODULE_EXPORT int
test_actors_main (int argc, char *argv[])
{
  SuperOH      *oh;
  gint          i;
  GError       *error;
  ClutterActor *real_hand;
  gchar        *file;

  error = NULL;

  clutter_test_init (&argc, &argv);

  oh = g_new (SuperOH, 1);

  oh->stage = clutter_test_get_stage ();
  clutter_actor_set_size (oh->stage, 800, 600);
  clutter_actor_set_name (oh->stage, "Default Stage");
  clutter_actor_set_background_color (oh->stage, &CLUTTER_COLOR_INIT (114, 159, 207, 255));
  g_signal_connect (oh->stage, "destroy", G_CALLBACK (stop_and_quit), oh);

  clutter_stage_set_title (CLUTTER_STAGE (oh->stage), "Actors");

  /* Create a timeline to manage animation */
  oh->timeline = clutter_timeline_new_for_actor (oh->stage, 6000);
  clutter_timeline_set_repeat_count (oh->timeline, -1);

  /* fire a callback for frame change */
  g_signal_connect (oh->timeline, "new-frame", G_CALLBACK (frame_cb), oh);

  file = g_build_filename (TESTS_DATADIR, "redhand.png", NULL);
  real_hand = clutter_test_utils_create_texture_from_file (file, &error);
  if (real_hand == NULL)
    g_error ("image load failed: %s", error->message);

  g_free (file);

  /* create a new actor to hold other actors */
  oh->group = clutter_actor_new ();
  clutter_actor_set_pivot_point (oh->group, 0.5, 0.5);
  clutter_actor_set_layout_manager (oh->group, clutter_fixed_layout_new ());
  clutter_actor_set_name (oh->group, "Group");
  g_signal_connect (oh->group, "destroy", G_CALLBACK (on_group_destroy), oh);
  clutter_actor_add_constraint (oh->group, clutter_align_constraint_new (oh->stage, CLUTTER_ALIGN_BOTH, 0.5));
  clutter_actor_add_constraint (oh->group, clutter_bind_constraint_new (oh->stage, CLUTTER_BIND_SIZE, 0.0f));

  oh->hand = g_new (ClutterActor *, NHANDS);

  oh->stage_width = clutter_actor_get_width (oh->stage);
  oh->stage_height = clutter_actor_get_height (oh->stage);
  oh->radius = (oh->stage_width + oh->stage_height) / NHANDS;

  for (i = 0; i < NHANDS; i++)
    {
      gint x, y, w, h;

      if (i == 0)
        {
          oh->hand[i] = real_hand;
          clutter_actor_set_name (oh->hand[i], "Real Hand");
        }
      else
        {
          oh->hand[i] = clutter_clone_new (real_hand);
          clutter_actor_set_name (oh->hand[i], "Clone Hand");
        }

      clutter_actor_set_reactive (oh->hand[i], TRUE);

      clutter_actor_set_size (oh->hand[i], 200, 213);

      /* Place around a circle */
      w = clutter_actor_get_width (oh->hand[i]);
      h = clutter_actor_get_height (oh->hand[i]);

      x = oh->stage_width / 2
        + oh->radius
        * cos (i * G_PI / (NHANDS / 2))
        - w / 2;

      y = oh->stage_height / 2
        + oh->radius
        * sin (i * G_PI / (NHANDS / 2))
        - h / 2;

      clutter_actor_set_position (oh->hand[i], x, y);
      clutter_actor_set_translation (oh->hand[i], -100.f, -106.5, 0);

      /* Add to our group group */
      clutter_actor_add_child (oh->group, oh->hand[i]);

      g_signal_connect (oh->hand[i], "button-press-event",
                        G_CALLBACK (on_button_press_event),
                        oh);

      g_signal_connect (oh->hand[i], "destroy",
                        G_CALLBACK (on_hand_destroy),
                        oh);
    }

  /* Add the group to the stage */
  clutter_actor_add_child (oh->stage, oh->group);

  /* Show everying */
  clutter_actor_show (oh->stage);

  g_signal_connect (oh->stage, "key-release-event", G_CALLBACK (input_cb), oh);

  /* and start it */
  clutter_timeline_start (oh->timeline);

  clutter_test_main ();

  clutter_timeline_stop (oh->timeline);

  /* clean up */
  g_object_unref (oh->timeline);
  g_free (oh->hand);
  g_free (oh);

  return EXIT_SUCCESS;
}
