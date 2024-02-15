
#include <math.h>
#include <stdlib.h>
#include <clutter/clutter.h>
#include "test-common.h"

#define N_ACTORS 100
#define N_EVENTS 5

static gint n_actors = N_ACTORS;
static gint n_events = N_EVENTS;

static gboolean
motion_event_cb (ClutterActor *actor, ClutterEvent *event, gpointer user_data)
{
  return FALSE;
}

static void
do_events (ClutterActor *stage)
{
  glong i;
  static gdouble angle = 0;

  for (i = 0; i < n_events; i++)
    {
      angle += (2.0 * G_PI) / (gdouble)n_actors;
      while (angle > G_PI * 2.0)
        angle -= G_PI * 2.0;

      /* If we synthesized events, they would be motion compressed;
       * calling get_actor_at_position() doesn't have that problem
       */
      clutter_stage_get_actor_at_pos (CLUTTER_STAGE (stage),
				      CLUTTER_PICK_REACTIVE,
				      256.0 + 206.0 * cos (angle),
				      256.0 + 206.0 * sin (angle));
    }
}

static gboolean queue_redraw (gpointer data)
{
  ClutterActor *stage = CLUTTER_ACTOR (data);
  clutter_actor_queue_redraw (stage);
  do_events (stage);
  return TRUE;
}

int
main (int argc, char **argv)
{
  glong i;
  gdouble angle;
  ClutterColor color = { 0x00, 0x00, 0x00, 0xff };
  ClutterActor *stage, *rect;

  clutter_perf_fps_init ();

  clutter_test_init (&argc, &argv);

  stage = clutter_test_get_stage ();
  clutter_actor_set_size (stage, 512, 512);
  clutter_actor_set_background_color (CLUTTER_ACTOR (stage), 
                                      &CLUTTER_COLOR_INIT (0, 0, 0, 255));
  clutter_stage_set_title (CLUTTER_STAGE (stage), "Picking Performance");
  g_signal_connect (stage, "destroy", G_CALLBACK (clutter_test_quit), NULL);

  printf ("Picking performance test with "
          "%d actors and %d events per frame\n",
          n_actors,
          n_events);

  for (i = n_actors - 1; i >= 0; i--)
    {
      angle = ((2.0 * G_PI) / (gdouble) n_actors) * i;

      color.red = (1.0 - ABS ((MAX (0, MIN (n_actors/2.0 + 0, i))) /
                  (gdouble)(n_actors/4.0) - 1.0)) * 255.0;
      color.green = (1.0 - ABS ((MAX (0, MIN (n_actors/2.0 + 0,
                    fmod (i + (n_actors/3.0)*2, n_actors)))) /
                    (gdouble)(n_actors/4) - 1.0)) * 255.0;
      color.blue = (1.0 - ABS ((MAX (0, MIN (n_actors/2.0 + 0,
                   fmod ((i + (n_actors/3.0)), n_actors)))) /
                   (gdouble)(n_actors/4.0) - 1.0)) * 255.0;

      rect = clutter_actor_new ();
      clutter_actor_set_background_color (rect, &color);
      clutter_actor_set_size (rect, 100, 100);
      clutter_actor_set_translation (rect, -50.f, -50.f, 0.f);
      clutter_actor_set_position (rect,
                                  256 + 206 * cos (angle),
                                  256 + 206 * sin (angle));
      clutter_actor_set_reactive (rect, TRUE);
      g_signal_connect (rect, "motion-event",
                        G_CALLBACK (motion_event_cb), NULL);

      clutter_actor_add_child (stage, rect);
    }

  clutter_actor_show (stage);

  clutter_perf_fps_start (CLUTTER_STAGE (stage));
  clutter_threads_add_idle (queue_redraw, stage);
  clutter_test_main ();
  clutter_perf_fps_report ("test-picking");

  return 0;
}


