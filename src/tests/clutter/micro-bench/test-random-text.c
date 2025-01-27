#include <gmodule.h>
#include <clutter/clutter.h>
#include <clutter/clutter-pango.h>
#include <stdlib.h>

#include "tests/clutter-test-utils.h"

#define MAX_TEXT_LEN  10
#define MIN_FONT_SIZE 10
#define MAX_FONT_SIZE 30

static const char * const font_names[] =
  {
    "Sans", "Sans Italic", "Serif", "Serif Bold", "Times", "Monospace"
  };
#define FONT_NAME_COUNT 6

static gboolean
on_idle (gpointer data)
{
  ClutterActor *stage = CLUTTER_ACTOR (data);
  int line_height = 0, xpos = 0, ypos = 0;
  int stage_width = (int) clutter_actor_get_width (stage);
  int stage_height = (int) clutter_actor_get_height (stage);
  char text[MAX_TEXT_LEN + 1];
  char font_name[64];
  int i;
  GList *children, *node;
  static GTimer *timer = NULL;
  static int frame_count = 0;

  /* Remove all of the children of the stage */
  children = clutter_actor_get_children (stage);
  for (node = children; node; node = node->next)
    clutter_actor_remove_child (stage, CLUTTER_ACTOR (node->data));
  g_list_free (children);

  /* Fill the stage with new random labels */
  while (ypos < stage_height)
    {
      int text_len = rand () % MAX_TEXT_LEN + 1;
      ClutterActor *label;

      for (i = 0; i < text_len; i++)
        text[i] = rand () % (128 - 32) + 32;
      text[text_len] = '\0';

      sprintf (font_name, "%s %i",
               font_names[rand () % FONT_NAME_COUNT],
               rand () % (MAX_FONT_SIZE - MIN_FONT_SIZE) + MIN_FONT_SIZE);

      label = clutter_text_new_with_text (font_name, text);

      if (clutter_actor_get_height (label) > line_height)
        line_height = (int) clutter_actor_get_height (label);

      if (xpos + clutter_actor_get_width (label) > stage_width)
        {
          xpos = 0;
          ypos += line_height;
          line_height = 0;
        }

      clutter_actor_set_position (label, xpos, ypos);

      clutter_actor_add_child (stage, label);

      xpos += (int) clutter_actor_get_width (label);
    }

  if (timer == NULL)
    timer = g_timer_new ();
  else
    {
      if (++frame_count >= 10)
        {
          printf ("10 frames in %f seconds\n",
                  g_timer_elapsed (timer, NULL));
          g_timer_start (timer);
          frame_count = 0;
        }
    }

  return TRUE;
}

int
main (int argc, char *argv[])
{
  ClutterActor *stage;

  clutter_test_init (&argc, &argv);

  stage = clutter_test_get_stage ();

  clutter_actor_show (stage);

  g_idle_add (on_idle, stage);

  clutter_test_main ();

  clutter_actor_destroy (stage);

  return 0;
}
