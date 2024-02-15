
#include <math.h>
#include <stdlib.h>
#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"

#define N_ACTORS 100
#define N_EVENTS 5

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

  for (i = 0; i < N_EVENTS; i++)
    {
      angle += (2.0 * G_PI) / (double) N_ACTORS;
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

static void
on_after_paint (ClutterActor        *stage,
                ClutterStageView    *view,
                ClutterFrame        *frame,
                gconstpointer       *data)
{
  do_events (stage);
}

static gboolean
queue_redraw (gpointer stage)
{
  clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));

  return TRUE;
}

int
main (int argc, char **argv)
{
  glong i;
  gdouble angle;
  ClutterColor color = { 0x00, 0x00, 0x00, 0xff };
  ClutterActor *stage, *rect;

  g_setenv ("CLUTTER_VBLANK", "none", FALSE);
  g_setenv ("CLUTTER_DEFAULT_FPS", "1000", FALSE);
  g_setenv ("CLUTTER_SHOW_FPS", "1", FALSE);

  clutter_test_init (&argc, &argv);

  stage = clutter_test_get_stage ();
  clutter_actor_set_size (stage, 512, 512);
  clutter_actor_set_background_color (CLUTTER_ACTOR (stage),
                                      &CLUTTER_COLOR_INIT (0, 0, 0, 255));
  clutter_stage_set_title (CLUTTER_STAGE (stage), "Picking");

  printf ("Picking performance test with "
          "%d actors and %d events per frame\n",
          N_ACTORS,
          N_EVENTS);

  for (i = N_ACTORS - 1; i >= 0; i--)
    {
      angle = ((2.0 * G_PI) / (double) N_ACTORS) * i;

      color.red = (1.0 - ABS ((MAX (0, MIN (N_ACTORS / 2.0 + 0, i))) /
                  (double) (N_ACTORS / 4.0) - 1.0)) * 255.0;
      color.green = (1.0 - ABS ((MAX (0, MIN (N_ACTORS / 2.0 + 0,
                    fmod (i + (N_ACTORS / 3.0) * 2, N_ACTORS)))) /
                    (double) (N_ACTORS / 4) - 1.0)) * 255.0;
      color.blue = (1.0 - ABS ((MAX (0, MIN (N_ACTORS / 2.0 + 0,
                   fmod ((i + (N_ACTORS / 3.0)), N_ACTORS)))) /
                   (double) (N_ACTORS / 4.0) - 1.0)) * 255.0;

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

  clutter_threads_add_idle (queue_redraw, stage);

  g_signal_connect (CLUTTER_STAGE (stage), "after-paint", G_CALLBACK (on_after_paint), NULL);

  clutter_test_main ();

  clutter_actor_destroy (stage);

  return 0;
}


