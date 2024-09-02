/* CALLY - The Clutter Accessibility Implementation Library
 *
 * Copyright (C) 2009 Igalia, S.L.
 *
 * Author: Alejandro Piñeiro Iglesias <apinheiro@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * The purpose of this example is test key event and global event
 * implementation, specifically:
 *
 *  atk_add_global_event_listener
 *  atk_remove_global_event_listener
 *  atk_add_key_event_listener
 *  atk_remove_key_event_listener
 */
#include <atk/atk.h>
#include <clutter/clutter.h>

#include "cally-examples-util.h"

#define WIDTH 800
#define HEIGHT 600
#define HEIGHT_STEP 100
#define NUM_ENTRIES 3

struct _Data{
  gint value;
};
typedef struct _Data Data;

static gboolean
atk_key_listener (AtkKeyEventStruct *event, gpointer data)
{
  Data *my_data = (Data*) data;

  g_print ("atk_listener: 0x%x ", event->keyval);

  if (my_data != NULL) {
    g_print ("\t Data value: %i\n", my_data->value);
  } else {
    g_print ("\tNo data!!\n");
  }

  return FALSE;
}

static gboolean
window_event_listener (GSignalInvocationHint * signal_hint,
                       guint n_param_values,
                       const GValue * param_values, gpointer data)
{
  AtkObject *accessible;
  GSignalQuery signal_query;
  const gchar *name, *s;

  g_signal_query (signal_hint->signal_id, &signal_query);
  name = signal_query.signal_name;

  accessible = ATK_OBJECT (g_value_get_object (&param_values[0]));
  s = atk_object_get_name (accessible);

  g_print ("Detected window event \"%s\" from object \"%p\" named \"%s\"\n",
           name, accessible, s);

  return TRUE;
}
static void
make_ui (ClutterActor *stage)
{
  gint             i             = 0;
  ClutterActor    *rectangle     = NULL;
  CoglColor color_rect = { 0x00, 0xff, 0xff, 0x55 };

  clutter_actor_set_background_color (CLUTTER_ACTOR (stage),
                                      &COGL_COLOR_INIT (255, 255, 255, 255));
  clutter_actor_set_size (stage, WIDTH, HEIGHT);

  for (i = 0; i < NUM_ENTRIES; i++)
    {
      /* rectangle: to create a entry "feeling" */
      rectangle = clutter_actor_new ();
      clutter_actor_set_background_color (rectangle, &color_rect);
      clutter_actor_set_size (rectangle, 500, 75);

      clutter_actor_add_child (stage, rectangle);

    }
}

int
main (int argc, char *argv[])
{
  ClutterActor *stage, *stage_main;
  Data data1, data2, data3;
  guint id_1 = 0, id_2 = 0, id_3 = 0;

  g_set_application_name ("AtkText");

  if (cally_util_a11y_init (&argc, &argv) == FALSE)
    {
      g_error ("This example requires the accessibility support, "
               "specifically AtkUtil implementation loaded, "
               "as it tries to register and remove event listeners");
    }

  data1.value = 10;
  data2.value = 20;
  data3.value = 30;

  /* key event listeners */
  id_1 = atk_add_key_event_listener ((AtkKeySnoopFunc)atk_key_listener, &data1);
  atk_remove_key_event_listener (id_1);
  id_2 = atk_add_key_event_listener ((AtkKeySnoopFunc)atk_key_listener, &data2);
  id_3 = atk_add_key_event_listener ((AtkKeySnoopFunc)atk_key_listener, &data3);

  atk_remove_key_event_listener (id_2);

  g_print ("key event listener ids registered: (%i, %i, %i)\n", id_1, id_2, id_3);

  /* event listeners */
  atk_add_global_event_listener (window_event_listener, "Atk:AtkWindow:create");
  atk_add_global_event_listener (window_event_listener, "Atk:AtkWindow:destroy");
  atk_add_global_event_listener (window_event_listener, "Atk:AtkWindow:activate");
  atk_add_global_event_listener (window_event_listener, "Atk:AtkWindow:deactivate");

  stage_main = clutter_test_get_stage ();
  g_signal_connect (stage_main, "destroy", G_CALLBACK (clutter_test_quit), NULL);
  make_ui (stage_main);

  clutter_actor_show (stage_main);

  stage = clutter_test_get_stage ();
  g_signal_connect (stage, "destroy", G_CALLBACK (clutter_test_quit), NULL);

  make_ui (stage);
  clutter_actor_show (stage);

  clutter_test_main ();

  return 0;
}
