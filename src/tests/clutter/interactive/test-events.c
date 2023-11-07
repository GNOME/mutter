#include <gmodule.h>
#include <clutter/clutter.h>
#include <string.h>

#include "tests/clutter-test-utils.h"

gboolean IsMotion = TRUE;

int
test_events_main (int argc, char *argv[]);

const char *
test_events_describe (void);

static const gchar *
get_event_type_name (const ClutterEvent *event)
{
  switch (clutter_event_type (event))
    {
    case CLUTTER_BUTTON_PRESS:
      return "BUTTON PRESS";

    case CLUTTER_BUTTON_RELEASE:
      return "BUTTON_RELEASE";

    case CLUTTER_KEY_PRESS:
      return "KEY PRESS";

    case CLUTTER_KEY_RELEASE:
      return "KEY RELEASE";

    case CLUTTER_ENTER:
      return "ENTER";

    case CLUTTER_LEAVE:
      return "LEAVE";

    case CLUTTER_MOTION:
      return "MOTION";

    case CLUTTER_TOUCH_BEGIN:
      return "TOUCH BEGIN";

    case CLUTTER_TOUCH_UPDATE:
      return "TOUCH UPDATE";

    case CLUTTER_TOUCH_END:
      return "TOUCH END";

    case CLUTTER_TOUCH_CANCEL:
      return "TOUCH CANCEL";

    default:
      return "EVENT";
    }
}

static gchar *
get_event_state_string (const ClutterEvent *event)
{
  const char *mods[18];
  int i = 0;
  ClutterModifierType state = clutter_event_get_state (event);

  if (state & CLUTTER_SHIFT_MASK)
    mods[i++] = "shift";
  if (state & CLUTTER_LOCK_MASK)
    mods[i++] = "lock";
  if (state & CLUTTER_CONTROL_MASK)
    mods[i++] = "ctrl";
  if (state & CLUTTER_MOD1_MASK)
    mods[i++] = "mod1";
  if (state & CLUTTER_MOD2_MASK)
    mods[i++] = "mod2";
  if (state & CLUTTER_MOD3_MASK)
    mods[i++] = "mod3";
  if (state & CLUTTER_MOD4_MASK)
    mods[i++] = "mod4";
  if (state & CLUTTER_MOD5_MASK)
    mods[i++] = "mod5";
  if (state & CLUTTER_BUTTON1_MASK)
    mods[i++] = "btn1";
  if (state & CLUTTER_BUTTON2_MASK)
    mods[i++] = "btn2";
  if (state & CLUTTER_BUTTON3_MASK)
    mods[i++] = "btn3";
  if (state & CLUTTER_BUTTON4_MASK)
    mods[i++] = "btn4";
  if (state & CLUTTER_BUTTON5_MASK)
    mods[i++] = "btn5";
  if (state & CLUTTER_SUPER_MASK)
    mods[i++] = "super";
  if (state & CLUTTER_HYPER_MASK)
    mods[i++] = "hyper";
  if (state & CLUTTER_META_MASK)
    mods[i++] = "meta";
  if (state & CLUTTER_RELEASE_MASK)
    mods[i++] = "release";

  if (i == 0)
    mods[i++] = "-";

  mods[i] = NULL;
  return g_strjoinv (",", (char **) mods);
}

static gboolean
capture_cb (ClutterActor *actor,
            ClutterEvent *event,
            gpointer      data)
{
  g_print ("* captured event '%s' for type '%s' *\n",
           get_event_type_name (event),
           G_OBJECT_TYPE_NAME (actor));

  return FALSE;
}

static void
key_focus_in_cb (ClutterActor *actor,
         gpointer      data)
{
  ClutterActor *focus_box = CLUTTER_ACTOR (data);

  if (CLUTTER_IS_STAGE (actor))
    clutter_actor_hide (focus_box);
  else
    {
      clutter_actor_set_position (focus_box,
          clutter_actor_get_x (actor) - 5,
          clutter_actor_get_y (actor) - 5);

      clutter_actor_set_size (focus_box,
              clutter_actor_get_width (actor) + 10,
              clutter_actor_get_height (actor) + 10);
      clutter_actor_show (focus_box);
    }
}

static void
fill_keybuf (char *keybuf, ClutterKeyEvent *event)
{
  ClutterModifierType state;
  char utf8[6];
  uint32_t keyval;
  int len;

  /* printable character, if any (ß, ∑) */
  len = g_unichar_to_utf8 (clutter_event_get_key_unicode ((ClutterEvent *) event), utf8);
  utf8[len] = '\0';
  sprintf (keybuf, "'%s' ", utf8);

  /* key combination (<Mod1>s, <Shift><Mod1>S, <Ctrl><Mod1>Delete) */
  keyval = clutter_event_get_key_symbol ((ClutterEvent *) event);
  len = g_unichar_to_utf8 (clutter_keysym_to_unicode (keyval), utf8);
  utf8[len] = '\0';

  state = clutter_event_get_state ((ClutterEvent *) event);

  if (state & CLUTTER_SHIFT_MASK)
    strcat (keybuf, "<Shift>");

  if (state & CLUTTER_LOCK_MASK)
    strcat (keybuf, "<Lock>");

  if (state & CLUTTER_CONTROL_MASK)
    strcat (keybuf, "<Control>");

  if (state & CLUTTER_MOD1_MASK)
    strcat (keybuf, "<Mod1>");

  if (state & CLUTTER_MOD2_MASK)
    strcat (keybuf, "<Mod2>");

  if (state & CLUTTER_MOD3_MASK)
    strcat (keybuf, "<Mod3>");

  if (state & CLUTTER_MOD4_MASK)
    strcat (keybuf, "<Mod4>");

  if (state & CLUTTER_MOD5_MASK)
    strcat (keybuf, "<Mod5>");

  strcat (keybuf, utf8);
}

static gboolean
input_cb (ClutterActor *actor,
          ClutterEvent *event,
          gpointer      data)
{
  ClutterActor *stage = clutter_actor_get_stage (actor);
  ClutterActor *source_actor;
  graphene_point_t position;
  gchar *state;
  gchar keybuf[128];
  ClutterInputDevice *device, *source;
  const gchar *device_name, *source_name = NULL;
  ClutterEventType event_type;

  device = clutter_event_get_device (event);
  device_name = clutter_input_device_get_device_name (device);
  event_type = clutter_event_type (event);

  if (event_type == CLUTTER_KEY_PRESS ||
      event_type == CLUTTER_KEY_RELEASE)
    {
      source_actor = clutter_stage_get_key_focus (CLUTTER_STAGE (stage));
    }
  else
    {
      source_actor = clutter_stage_get_device_actor (CLUTTER_STAGE (stage),
                                                     device,
                                                     clutter_event_get_event_sequence (event));
    }

  source = clutter_event_get_source_device (event);
  if (source)
    source_name = clutter_input_device_get_device_name (source);
  else
    source_name = "None";

  state = get_event_state_string (event);

  switch (event_type)
    {
    case CLUTTER_KEY_PRESS:
      fill_keybuf (keybuf, (ClutterKeyEvent *) event);
      printf ("[%s] KEY PRESS %s",
              clutter_actor_get_name (source_actor),
              keybuf);
      break;
    case CLUTTER_KEY_RELEASE:
      fill_keybuf (keybuf, (ClutterKeyEvent *) event);
      printf ("[%s] KEY RELEASE %s",
              clutter_actor_get_name (source_actor),
              keybuf);
      break;
    case CLUTTER_MOTION:
      clutter_event_get_position (event, &position);
      g_print ("[%s] MOTION (coords:%.02f,%.02f device:%s/%s state:%s)",
               clutter_actor_get_name (source_actor), position.x, position.y,
               device_name, source_name, state);
      break;
    case CLUTTER_ENTER:
      g_print ("[%s] ENTER (from:%s device:%s/%s state:%s)",
               clutter_actor_get_name (source_actor),
               clutter_event_get_related (event) != NULL
                 ? clutter_actor_get_name (clutter_event_get_related (event))
                 : "<out of stage>",
               device_name, source_name, state);
      break;
    case CLUTTER_LEAVE:
      g_print ("[%s] LEAVE (to:%s device:%s/%s state:%s)",
               clutter_actor_get_name (source_actor),
               clutter_event_get_related (event) != NULL
                 ? clutter_actor_get_name (clutter_event_get_related (event))
                 : "<out of stage>",
               device_name, source_name, state);
      break;
    case CLUTTER_BUTTON_PRESS:
      clutter_event_get_position (event, &position);
      g_print ("[%s] BUTTON PRESS (button:%i, coords:%.02f,%.02f device:%s/%s, state:%s)",
               clutter_actor_get_name (source_actor),
               clutter_event_get_button (event),
               position.x, position.y,
               device_name, source_name, state);
      break;
    case CLUTTER_BUTTON_RELEASE:
      clutter_event_get_position (event, &position);
      g_print ("[%s] BUTTON RELEASE (button:%i, coords:%.02f,%.02f device:%s/%s state:%s)",
               clutter_actor_get_name (source_actor),
               clutter_event_get_button (event),
               position.x, position.y,
               device_name, source_name, state);

      if (source_actor == stage)
        clutter_stage_set_key_focus (CLUTTER_STAGE (stage), NULL);
      else if (source_actor == actor &&
               clutter_actor_get_parent (actor) == stage)
        clutter_stage_set_key_focus (CLUTTER_STAGE (stage), actor);
      break;
    case CLUTTER_TOUCH_BEGIN:
      clutter_event_get_position (event, &position);
      g_print ("[%s] TOUCH BEGIN (seq:%p coords:%.02f,%.02f device:%s/%s state:%s)",
               clutter_actor_get_name (source_actor),
               clutter_event_get_event_sequence (event),
               position.x, position.y,
               device_name, source_name, state);
      break;
    case CLUTTER_TOUCH_UPDATE:
      clutter_event_get_position (event, &position);
      g_print ("[%s] TOUCH UPDATE (seq:%p coords:%.02f,%.02f device:%s/%s state:%s)",
               clutter_actor_get_name (source_actor),
               clutter_event_get_event_sequence (event),
               position.x, position.y,
               device_name, source_name, state);
      break;
    case CLUTTER_TOUCH_END:
      clutter_event_get_position (event, &position);
      g_print ("[%s] TOUCH END (seq:%p coords:%.02f,%.02f device:%s/%s state:%s)",
               clutter_actor_get_name (source_actor),
               clutter_event_get_event_sequence (event),
               position.x, position.y,
               device_name, source_name, state);
      break;
    case CLUTTER_TOUCH_CANCEL:
      clutter_event_get_position (event, &position);
      g_print ("[%s] TOUCH CANCEL (seq:%p coords:%.02f,%.02f device:%s/%s state:%s)",
               clutter_actor_get_name (source_actor),
               clutter_event_get_event_sequence (event),
               position.x, position.y,
               device_name, source_name, state);
      break;
    case CLUTTER_SCROLL:
      {
        ClutterScrollDirection dir = clutter_event_get_scroll_direction (event);

        if (dir == CLUTTER_SCROLL_SMOOTH)
          {
            gdouble dx, dy;
            clutter_event_get_scroll_delta (event, &dx, &dy);
            g_print ("[%s] BUTTON SCROLL (direction:smooth %.02f,%.02f state:%s)",
                     clutter_actor_get_name (source_actor), dx, dy, state);
          }
        else
          g_print ("[%s] BUTTON SCROLL (direction:%s state:%s)",
                   clutter_actor_get_name (source_actor),
                   dir == CLUTTER_SCROLL_UP ? "up" :
                   dir == CLUTTER_SCROLL_DOWN ? "down" :
                   dir == CLUTTER_SCROLL_LEFT ? "left" :
                   dir == CLUTTER_SCROLL_RIGHT ? "right" : "?",
                   state);
      }
      break;
    case CLUTTER_TOUCHPAD_PINCH:
      g_print ("[%s] TOUCHPAD PINCH", clutter_actor_get_name (source_actor));
      break;
    case CLUTTER_TOUCHPAD_SWIPE:
      g_print ("[%s] TOUCHPAD SWIPE", clutter_actor_get_name (source_actor));
      break;
    case CLUTTER_TOUCHPAD_HOLD:
      g_print ("[%s] TOUCHPAD HOLD", clutter_actor_get_name (source_actor));
      break;
    case CLUTTER_PROXIMITY_IN:
      g_print ("[%s] PROXIMITY IN", clutter_actor_get_name (source_actor));
      break;
    case CLUTTER_PROXIMITY_OUT:
      g_print ("[%s] PROXIMITY OUT", clutter_actor_get_name (source_actor));
      break;
    case CLUTTER_PAD_BUTTON_PRESS:
      g_print ("[%s] PAD BUTTON PRESS", clutter_actor_get_name (source_actor));
      break;
    case CLUTTER_PAD_BUTTON_RELEASE:
      g_print ("[%s] PAD BUTTON RELEASE", clutter_actor_get_name (source_actor));
      break;
    case CLUTTER_PAD_STRIP:
      g_print ("[%s] PAD STRIP", clutter_actor_get_name (source_actor));
      break;
    case CLUTTER_PAD_RING:
      g_print ("[%s] PAD RING", clutter_actor_get_name (source_actor));
      break;
    case CLUTTER_NOTHING:
    default:
      return FALSE;
    }

  g_free (state);

  if (source_actor == actor)
    g_print (" *source*");

  g_print ("\n");

  return FALSE;
}

G_MODULE_EXPORT int
test_events_main (int argc, char *argv[])
{
  ClutterActor *stage, *actor, *focus_box, *group;

  clutter_test_init (&argc, &argv);

  stage = clutter_test_get_stage ();
  clutter_stage_set_title (CLUTTER_STAGE (stage), "Events");
  clutter_actor_set_name (stage, "Stage");
  g_signal_connect (stage, "destroy", G_CALLBACK (clutter_test_quit), NULL);
  g_signal_connect (stage, "event", G_CALLBACK (input_cb), (char *) "stage");

  focus_box = clutter_actor_new ();
  clutter_actor_set_background_color (focus_box, CLUTTER_COLOR_Black);
  clutter_actor_set_name (focus_box, "Focus Box");
  clutter_actor_add_child (stage, focus_box);

  actor = clutter_actor_new ();
  clutter_actor_set_background_color (actor, CLUTTER_COLOR_Green);
  clutter_actor_set_name (actor, "Green Box");
  clutter_actor_set_size (actor, 100, 100);
  clutter_actor_set_position (actor, 250, 100);
  clutter_actor_set_reactive (actor, TRUE);
  clutter_actor_add_child (stage, actor);
  g_signal_connect (actor, "event", G_CALLBACK (input_cb), (char *) "green box");
  g_signal_connect (actor, "key-focus-in", G_CALLBACK (key_focus_in_cb),
                    focus_box);
  g_signal_connect (actor, "captured-event", G_CALLBACK (capture_cb), NULL);

  clutter_stage_set_key_focus (CLUTTER_STAGE (stage), actor);

  /* non reactive */
  actor = clutter_actor_new ();
  clutter_actor_set_background_color (actor, CLUTTER_COLOR_Black);
  clutter_actor_set_name (actor, "Black Box");
  clutter_actor_set_size (actor, 400, 50);
  clutter_actor_set_position (actor, 100, 250);
  clutter_actor_add_child (stage, actor);
  g_signal_connect (actor, "event", G_CALLBACK (input_cb), (char *) "blue box");
  g_signal_connect (actor, "key-focus-in", G_CALLBACK (key_focus_in_cb),
                    focus_box);
  g_signal_connect (stage, "key-focus-in", G_CALLBACK (key_focus_in_cb),
                    focus_box);

  /* non reactive group, with reactive child */
  actor = clutter_actor_new ();
  clutter_actor_set_background_color (actor, CLUTTER_COLOR_Yellow);
  clutter_actor_set_name (actor, "Yellow Box");
  clutter_actor_set_size (actor, 100, 100);
  clutter_actor_set_reactive (actor, TRUE);

  g_signal_connect (actor, "event", G_CALLBACK (input_cb), (char *) "yellow box");

  /* note group not reactive */
  group = clutter_actor_new ();
  clutter_actor_add_child (group, actor);
  clutter_actor_add_child (stage, group);
  clutter_actor_set_position (group, 100, 350);

  /* border actor */
  actor = clutter_actor_new ();
  clutter_actor_set_background_color (actor, CLUTTER_COLOR_Magenta);
  clutter_actor_set_name (actor, "Border Box");
  clutter_actor_set_size (actor, 100, 100);
  clutter_actor_set_position (actor,
                              (clutter_actor_get_width (stage) - 100) / 2,
                              clutter_actor_get_height (stage) - 100);
  clutter_actor_set_reactive (actor, TRUE);
  clutter_actor_add_child (stage, actor);
  g_signal_connect (actor, "event", G_CALLBACK (input_cb), NULL);

  clutter_actor_show (CLUTTER_ACTOR (stage));

  clutter_test_main ();

  return 0;
}

G_MODULE_EXPORT const char *
test_events_describe (void)
{
  return "Event handling and propagation.";
}
