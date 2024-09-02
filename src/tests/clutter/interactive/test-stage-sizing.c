#include <stdlib.h>
#include <gmodule.h>
#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"

int
test_stage_sizing_main (int argc, char *argv[]);

const char *
test_stage_sizing_describe (void);

static gboolean
shrink_clicked_cb (ClutterActor *stage)
{
  gfloat width, height;
  clutter_actor_get_size (stage, &width, &height);
  clutter_actor_set_size (stage, MAX (0, width - 10.f), MAX (0, height - 10.f));
  return CLUTTER_EVENT_STOP;
}

static gboolean
expand_clicked_cb (ClutterActor *stage)
{
  gfloat width, height;
  clutter_actor_get_size (stage, &width, &height);
  clutter_actor_set_size (stage, width + 10.f, height + 10.f);
  return CLUTTER_EVENT_STOP;
}

G_MODULE_EXPORT int
test_stage_sizing_main (int argc, char *argv[])
{
  ClutterActor *stage, *rect, *actor, *box;
  ClutterMargin margin = { 12.f, 12.f, 6.f, 6.f };

  clutter_test_init (&argc, &argv);

  stage = clutter_test_get_stage ();
  g_signal_connect (stage, "destroy", G_CALLBACK (clutter_test_quit), NULL);

  box = clutter_actor_new ();
  clutter_actor_set_layout_manager (box, clutter_box_layout_new ());
  clutter_actor_add_constraint (box, clutter_align_constraint_new (stage, CLUTTER_ALIGN_BOTH, 0.5));
  clutter_actor_add_child (stage, box);

  rect = clutter_actor_new ();
  clutter_actor_set_layout_manager (rect, clutter_bin_layout_new ());
  clutter_actor_set_background_color (rect, &COGL_COLOR_INIT (52, 101, 164, 255));
  clutter_actor_set_reactive (rect, TRUE);
  g_signal_connect_swapped (rect, "button-press-event",
                            G_CALLBACK (shrink_clicked_cb), stage);
  actor = clutter_actor_new ();
  clutter_actor_set_margin (actor, &margin);
  clutter_actor_add_child (rect, actor);
  clutter_actor_add_child (box, rect);

  rect = clutter_actor_new ();
  clutter_actor_set_layout_manager (rect, clutter_bin_layout_new ());
  clutter_actor_set_background_color (rect, &COGL_COLOR_INIT (237, 212, 0, 255));
  clutter_actor_set_reactive (rect, TRUE);
  g_signal_connect_swapped (rect, "button-press-event",
                            G_CALLBACK (expand_clicked_cb), stage);
  actor = clutter_actor_new ();
  clutter_actor_set_margin (actor, &margin);
  clutter_actor_add_child (rect, actor);
  clutter_actor_add_child (box, rect);

  clutter_actor_show (stage);

  clutter_test_main ();

  return EXIT_SUCCESS;
}

G_MODULE_EXPORT const char *
test_stage_sizing_describe (void)
{
  return "Check stage sizing policies.";
}
