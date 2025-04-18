#include <clutter/clutter.h>
#include <clutter/clutter-pango.h>

#include <stdlib.h>
#include <string.h>

#include "tests/clutter-test-utils.h"

#define STAGE_WIDTH  640
#define STAGE_HEIGHT 480

#define COLS 18
#define ROWS 20

static void
on_after_paint (ClutterActor        *actor,
                ClutterStageView    *view,
                ClutterFrame        *frame,
                gconstpointer       *data)
{
  static GTimer *timer = NULL;
  static int fps = 0;

  if (!timer)
    {
      timer = g_timer_new ();
      g_timer_start (timer);
    }

  if (g_timer_elapsed (timer, NULL) >= 1)
    {
      printf ("fps: %d\n", fps);
      g_timer_start (timer);
      fps = 0;
    }

  ++fps;
}

static gboolean
queue_redraw (gpointer stage)
{
  clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));

  return G_SOURCE_CONTINUE;
}

int
main (int argc, char *argv[])
{
  ClutterActor    *stage;
  ClutterActor    *group;

  g_setenv ("CLUTTER_VBLANK", "none", FALSE);
  g_setenv ("CLUTTER_DEFAULT_FPS", "1000", FALSE);

  clutter_test_init (&argc, &argv);

  stage = clutter_test_get_stage ();
  clutter_actor_set_size (stage, STAGE_WIDTH, STAGE_HEIGHT);
  clutter_actor_set_background_color (CLUTTER_ACTOR (stage),
                                      &COGL_COLOR_INIT (0, 0, 0, 255));

  group = clutter_actor_new ();
  clutter_actor_set_size (group, STAGE_WIDTH, STAGE_WIDTH);
  clutter_actor_add_child (stage, group);

  g_idle_add (queue_redraw, stage);

  g_signal_connect (CLUTTER_STAGE (stage), "after-paint", G_CALLBACK (on_after_paint), NULL);

  {
    gint row, col;

    for (row=0; row<ROWS; row++)
      for (col=0; col<COLS; col++)
        {
          ClutterActor *label;
          gchar font_name[64];
          gchar text[64];
          gint  font_size = row+10;
          gdouble scale = 0.17 + (1.5 * col / COLS);

          sprintf (font_name, "Sans %ipx", font_size);
          sprintf (text, "OH");

          if (row==0)
            {
              sprintf (font_name, "Sans 10px");
              sprintf (text, "%1.2f", scale);
              font_size = 10;
              scale = 1.0;
            }
          if (col==0)
            {
              sprintf (font_name, "Sans 10px");
              sprintf (text, "%ipx", font_size);
              if (row == 0)
                strcpy (text, "");
              font_size = 10;
              scale = 1.0;
            }

          label = clutter_text_new_with_text (font_name, text);
          clutter_text_set_color (CLUTTER_TEXT (label),
                                  &COGL_COLOR_INIT (255, 255, 255, 255));
          clutter_actor_set_position (label, (1.0f * STAGE_WIDTH / COLS) * col,
                                             (1.0f * STAGE_HEIGHT / ROWS) * row);
          /*clutter_actor_set_clip (label, 0,0, (1.0*STAGE_WIDTH/COLS),
                                              (1.0*STAGE_HEIGHT/ROWS));*/
          clutter_actor_set_scale (label, scale, scale);
          clutter_text_set_line_wrap (CLUTTER_TEXT (label), FALSE);
          clutter_actor_add_child (group, label);
        }
  }
  clutter_actor_show (stage);

  g_signal_connect (stage, "key-press-event",
		    G_CALLBACK (clutter_test_quit), NULL);

  clutter_test_main ();

  clutter_actor_destroy (stage);

  return 0;
}
