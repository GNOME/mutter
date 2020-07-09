#include <stdlib.h>

#include <gmodule.h>

#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"

#define TEST_STATE_SCRIPT_FILE  "test-script-signals.json"

int
test_state_script_main (int argc, char *argv[]);

G_MODULE_EXPORT int
test_state_script_main (int argc, char *argv[])
{
  ClutterActor *stage, *button;
  ClutterScript *script;
  GError *error = NULL;

  clutter_test_init (&argc, &argv);

  script = clutter_script_new ();
  clutter_script_load_from_file (script, TEST_STATE_SCRIPT_FILE, &error);
  if (error != NULL)
    g_error ("Unable to load '%s': %s\n",
             TEST_STATE_SCRIPT_FILE,
             error->message);

  stage = clutter_test_get_stage ();
  clutter_stage_set_title (CLUTTER_STAGE (stage), "State Script");
  g_signal_connect (stage, "destroy", G_CALLBACK (clutter_test_quit), NULL);
  clutter_actor_show (stage);

  button = CLUTTER_ACTOR (clutter_script_get_object (script, "button"));
  clutter_container_add_actor (CLUTTER_CONTAINER (stage), button);
  clutter_actor_add_constraint (button, clutter_align_constraint_new (stage, CLUTTER_ALIGN_BOTH, 0.5));

  clutter_script_connect_signals (script, NULL);

  clutter_test_main ();

  g_object_unref (script);

  return EXIT_SUCCESS;
}
