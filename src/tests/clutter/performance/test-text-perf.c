#include <clutter/clutter.h>
#include <clutter/clutter-pango.h>

#include <stdlib.h>
#include <string.h>
#include "test-common.h"

#define STAGE_WIDTH  800
#define STAGE_HEIGHT 600

static int font_size;
static int n_chars;
static int rows, cols;

static gboolean
queue_redraw (gpointer stage)
{
  clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));

  return G_SOURCE_CONTINUE;
}

static gunichar
get_character (int ch)
{
  int total_letters = 0;
  int i;

  static const struct
  {
    gunichar first_letter;
    int n_letters;
  }
  ranges[] =
    {
      { 'a', 26 }, /* lower case letters */
      { 'A', 26 }, /* upper case letters */
      { '0', 10 }, /* digits */
      { 0x410, 0x40 }, /* cyrillic alphabet */
      { 0x3b1, 18 } /* greek alphabet */
    };

  for (i = 0; i < G_N_ELEMENTS (ranges); i++)
    total_letters += ranges[i].n_letters;

  ch %= total_letters;

  for (i = 0; i < G_N_ELEMENTS (ranges) - 1; i++)
    if (ch < ranges[i].n_letters)
      return ch + ranges[i].first_letter;
    else
      ch -= ranges[i].n_letters;

  return ch + ranges[i].first_letter;
}

static ClutterActor *
create_label (void)
{
  CoglColor label_color = { 0xff, 0xff, 0xff, 0xff };
  ClutterActor *label;
  char         *font_name;
  GString      *str;
  int           i;

  font_name = g_strdup_printf ("Monospace %dpx", font_size);

  str = g_string_new (NULL);
  for (i = 0; i < n_chars; i++)
    g_string_append_unichar (str, get_character (i));

  label = clutter_text_new_with_text (font_name, str->str);
  clutter_text_set_color (CLUTTER_TEXT (label), &label_color);

  g_free (font_name);
  g_string_free (str, TRUE);

  return label;
}

int
main (int argc, char *argv[])
{
  ClutterActor    *stage;
  CoglColor stage_color = { 0x00, 0x00, 0x00, 0xff };
  ClutterActor    *label;
  int              w, h;
  int              row, col;
  float            scale = 1.0f;

  clutter_perf_fps_init ();

  clutter_test_init (&argc, &argv);

  if (argc != 3)
    {
      //g_printerr ("Usage test-text-perf FONT_SIZE N_CHARS\n");
      //exit (1);
      font_size = 30;
      n_chars = 400;
    }
  else
    {
      font_size = atoi (argv[1]);
      n_chars = atoi (argv[2]);
    }

  g_print ("Monospace %dpx, string length = %d\n", font_size, n_chars);

  stage = clutter_test_get_stage ();
  clutter_actor_set_size (stage, STAGE_WIDTH, STAGE_HEIGHT);
  clutter_actor_set_background_color (CLUTTER_ACTOR (stage), &stage_color);
  g_signal_connect (stage, "destroy", G_CALLBACK (clutter_test_quit), NULL);

  label = create_label ();
  w = (int) clutter_actor_get_width (label);
  h = (int) clutter_actor_get_height (label);

  /* If the label is too big to fit on the stage then scale it so that
     it will fit */
  if (w > STAGE_WIDTH || h > STAGE_HEIGHT)
    {
      float x_scale = STAGE_WIDTH / (float) w;
      float y_scale = STAGE_HEIGHT / (float) h;

      if (x_scale < y_scale)
        {
          scale = x_scale;
          cols = 1;
          rows = (int) (STAGE_HEIGHT / (h * scale));
        }
      else
        {
          scale = y_scale;
          cols = (int) (STAGE_WIDTH / (w * scale));
          rows = 1;
        }

      g_print ("Text scaled by %f to fit on the stage\n", scale);
    }
  else
    {
      cols = STAGE_WIDTH / w;
      rows = STAGE_HEIGHT / h;
    }

  clutter_actor_destroy (label);

  for (row=0; row<rows; row++)
    for (col=0; col<cols; col++)
      {
        label = create_label();
        clutter_actor_set_scale (label, scale, scale);
        clutter_actor_set_position (label, w * col * scale, h * row * scale);
        clutter_actor_add_child (stage, label);
      }

  clutter_actor_show (stage);

  clutter_perf_fps_start (CLUTTER_STAGE (stage));
  g_idle_add (queue_redraw, stage);
  clutter_test_main ();
  clutter_perf_fps_report ("test-text-perf");

  return 0;
}
