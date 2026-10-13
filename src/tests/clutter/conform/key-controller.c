#include <clutter/clutter.h>
#include <clutter/clutter-mutter.h>
#include <clutter/clutter-pango.h>

#include "clutter/clutter-event-private.h"
#include "tests/clutter-test-utils.h"

typedef struct
{
  ClutterActor parent_instance;
  unsigned int activation_count;
  uint32_t last_keyval;
} BindingActor;

typedef struct
{
  ClutterActorClass parent_class;
} BindingActorClass;

GType binding_actor_get_type (void);

G_DEFINE_TYPE (BindingActor, binding_actor, CLUTTER_TYPE_ACTOR)

static gboolean
on_binding_activated (BindingActor        *actor,
                      const char          *action_name,
                      uint32_t             keyval,
                      ClutterModifierType  modifiers,
                      gpointer             user_data)
{
  actor->activation_count++;
  actor->last_keyval = keyval;
  return TRUE;
}

static gboolean
on_binding_declined (BindingActor        *actor,
                     const char          *action_name,
                     uint32_t             keyval,
                     ClutterModifierType  modifiers,
                     gpointer             user_data)
{
  on_binding_activated (actor, action_name, keyval, modifiers, user_data);
  return FALSE;
}

static void
binding_actor_class_init (BindingActorClass *klass)
{
  ClutterBindingPool *pool;

  pool = clutter_binding_pool_get_for_class (klass);
  clutter_binding_pool_install_action (pool, "latin-a",
                                       CLUTTER_KEY_a, CLUTTER_CONTROL_MASK,
                                       G_CALLBACK (on_binding_activated),
                                       NULL, NULL);
  clutter_binding_pool_install_action (pool, "latin-a-release",
                                       CLUTTER_KEY_a,
                                       CLUTTER_CONTROL_MASK |
                                       CLUTTER_RELEASE_MASK,
                                       G_CALLBACK (on_binding_activated),
                                       NULL, NULL);
  clutter_binding_pool_install_action (pool, "latin-v-shift",
                                       CLUTTER_KEY_v,
                                       CLUTTER_CONTROL_MASK |
                                       CLUTTER_SHIFT_MASK,
                                       G_CALLBACK (on_binding_activated),
                                       NULL, NULL);
}

static void
binding_actor_init (BindingActor *actor)
{
}

static ClutterAction *
add_controller (ClutterActor *actor)
{
  ClutterAction *controller;

  controller = clutter_key_controller_new (NULL);
  clutter_key_controller_set_trigger_keybindings (
    CLUTTER_KEY_CONTROLLER (controller), TRUE);
  clutter_actor_add_action (actor, controller);
  return controller;
}

static gboolean
send_key (ClutterAction       *controller,
          ClutterEventType     type,
          uint32_t             keyval,
          uint32_t             keycode,
          ClutterModifierType  modifiers)
{
  g_autoptr (ClutterEvent) event = NULL;
  g_autoptr (ClutterInputDevice) device = NULL;
  ClutterModifierSet raw_modifiers = {
    .pressed = modifiers & ~CLUTTER_LOCK_MASK,
    .locked = modifiers & CLUTTER_LOCK_MASK,
  };
  gboolean handled;

  device = g_object_new (CLUTTER_TYPE_INPUT_DEVICE,
                         "device-type", CLUTTER_KEYBOARD_DEVICE,
                         NULL);
  event = clutter_event_key_new (type, CLUTTER_EVENT_FLAG_SYNTHETIC,
                                 g_get_monotonic_time (), device,
                                 raw_modifiers, modifiers, keyval,
                                 keycode >= 8 ? keycode - 8 : 0, keycode, 0);
  handled = CLUTTER_ACTION_GET_CLASS (controller)->handle_event (controller,
                                                                 event);
  g_assert_cmpuint (clutter_event_get_key_symbol (event), ==, keyval);
  g_assert_cmpuint (clutter_event_get_key_code (event), ==, keycode);
  g_assert_cmpuint (clutter_event_get_state (event), ==, modifiers);
  return handled;
}

static void
key_controller_non_latin (void)
{
  const struct
  {
    ClutterEventType type;
    uint32_t keyval;
    uint32_t keycode;
    ClutterModifierType modifiers;
    uint32_t expected_keyval;
  } cases[] = {
    { CLUTTER_KEY_PRESS, CLUTTER_KEY_Cyrillic_ef, 38,
      CLUTTER_CONTROL_MASK, CLUTTER_KEY_a },
    { CLUTTER_KEY_PRESS, CLUTTER_KEY_Cyrillic_EF, 38,
      CLUTTER_CONTROL_MASK | CLUTTER_LOCK_MASK, CLUTTER_KEY_a },
    { CLUTTER_KEY_RELEASE, CLUTTER_KEY_Cyrillic_ef, 38,
      CLUTTER_CONTROL_MASK, CLUTTER_KEY_a },
    { CLUTTER_KEY_PRESS, CLUTTER_KEY_Cyrillic_EM, 55,
      CLUTTER_CONTROL_MASK | CLUTTER_SHIFT_MASK, CLUTTER_KEY_v },
    { CLUTTER_KEY_PRESS, CLUTTER_KEY_hebrew_shin, 38,
      CLUTTER_CONTROL_MASK, CLUTTER_KEY_a },
    { CLUTTER_KEY_PRESS, CLUTTER_KEY_a, 38,
      CLUTTER_CONTROL_MASK, CLUTTER_KEY_a },
    { CLUTTER_KEY_PRESS, CLUTTER_KEY_Cyrillic_ef, 38, 0, 0 },
    { CLUTTER_KEY_PRESS, CLUTTER_KEY_Cyrillic_ef, 38,
      CLUTTER_CONTROL_MASK | CLUTTER_MOD1_MASK, 0 },
    { CLUTTER_KEY_PRESS, CLUTTER_KEY_Cyrillic_ef, 38,
      CLUTTER_CONTROL_MASK | CLUTTER_SUPER_MASK, 0 },
    { CLUTTER_KEY_PRESS, CLUTTER_KEY_Cyrillic_ef, 38,
      CLUTTER_CONTROL_MASK | CLUTTER_MOD5_MASK, 0 },
    { CLUTTER_KEY_PRESS, CLUTTER_KEY_eacute, 38,
      CLUTTER_CONTROL_MASK, 0 },
    { CLUTTER_KEY_PRESS, CLUTTER_KEY_Cyrillic_ef, 0,
      CLUTTER_CONTROL_MASK, 0 },
  };
  unsigned int i;

  for (i = 0; i < G_N_ELEMENTS (cases); i++)
    {
      BindingActor *actor;
      ClutterAction *controller;
      gboolean handled;

      actor = g_object_new (binding_actor_get_type (), NULL);
      g_object_ref_sink (actor);
      controller = add_controller (CLUTTER_ACTOR (actor));
      handled = send_key (controller, cases[i].type, cases[i].keyval,
                          cases[i].keycode, cases[i].modifiers);
      g_assert_cmpint (handled, ==, cases[i].expected_keyval != 0);
      g_assert_cmpuint (actor->activation_count, ==,
                        cases[i].expected_keyval != 0 ? 1 : 0);
      g_assert_cmpuint (actor->last_keyval, ==, cases[i].expected_keyval);
      clutter_actor_destroy (CLUTTER_ACTOR (actor));
      g_object_unref (actor);
    }
}

static gboolean
on_native_key_pressed (ClutterKeyController *controller,
                       gpointer              user_data)
{
  uint32_t keyval = 0;

  g_assert_true (clutter_key_controller_get_key (controller, &keyval,
                                                 NULL, NULL));
  g_assert_cmpuint (keyval, ==, CLUTTER_KEY_Cyrillic_ef);
  return TRUE;
}

static gboolean
on_key_pressed_remove_controller (ClutterKeyController *controller,
                                  ClutterActor         *actor)
{
  clutter_actor_remove_action (actor, CLUTTER_ACTION (controller));
  return FALSE;
}

static void
key_controller_native_priority (void)
{
  BindingActor *actor;
  ClutterAction *controller;
  ClutterBindingPool *pool;
  guint signal_id;

  actor = g_object_new (binding_actor_get_type (), NULL);
  g_object_ref_sink (actor);
  controller = add_controller (CLUTTER_ACTOR (actor));
  pool = clutter_binding_pool_find (G_OBJECT_TYPE_NAME (actor));

  clutter_binding_pool_install_action (pool, "native-a",
                                       CLUTTER_KEY_Cyrillic_ef,
                                       CLUTTER_CONTROL_MASK,
                                       G_CALLBACK (on_binding_activated),
                                       NULL, NULL);
  g_assert_true (send_key (controller, CLUTTER_KEY_PRESS,
                           CLUTTER_KEY_Cyrillic_ef, 38, CLUTTER_CONTROL_MASK));
  g_assert_cmpuint (actor->last_keyval, ==, CLUTTER_KEY_Cyrillic_ef);

  actor->activation_count = 0;
  clutter_binding_pool_block_action (pool, "native-a");
  g_assert_false (send_key (controller, CLUTTER_KEY_PRESS,
                            CLUTTER_KEY_Cyrillic_ef, 38, CLUTTER_CONTROL_MASK));
  g_assert_cmpuint (actor->activation_count, ==, 0);
  clutter_binding_pool_unblock_action (pool, "native-a");

  clutter_binding_pool_override_action (pool, CLUTTER_KEY_Cyrillic_ef,
                                        CLUTTER_CONTROL_MASK,
                                        G_CALLBACK (on_binding_declined),
                                        NULL, NULL);
  g_assert_false (send_key (controller, CLUTTER_KEY_PRESS,
                            CLUTTER_KEY_Cyrillic_ef, 38, CLUTTER_CONTROL_MASK));
  g_assert_cmpuint (actor->activation_count, ==, 1);
  g_assert_cmpuint (actor->last_keyval, ==, CLUTTER_KEY_Cyrillic_ef);
  clutter_binding_pool_remove_action (pool, CLUTTER_KEY_Cyrillic_ef,
                                      CLUTTER_CONTROL_MASK);

  actor->activation_count = 0;
  signal_id = g_signal_connect (controller, "key-press",
                                G_CALLBACK (on_native_key_pressed), NULL);
  g_assert_true (send_key (controller, CLUTTER_KEY_PRESS,
                           CLUTTER_KEY_Cyrillic_ef, 38, CLUTTER_CONTROL_MASK));
  g_assert_cmpuint (actor->activation_count, ==, 0);
  g_signal_handler_disconnect (controller, signal_id);

  clutter_key_controller_set_trigger_keybindings (
    CLUTTER_KEY_CONTROLLER (controller), FALSE);
  g_assert_false (send_key (controller, CLUTTER_KEY_PRESS,
                            CLUTTER_KEY_Cyrillic_ef, 38, CLUTTER_CONTROL_MASK));
  g_assert_cmpuint (actor->activation_count, ==, 0);

  clutter_key_controller_set_trigger_keybindings (
    CLUTTER_KEY_CONTROLLER (controller), TRUE);
  g_object_ref (controller);
  g_signal_connect (controller, "key-press",
                    G_CALLBACK (on_key_pressed_remove_controller), actor);
  g_assert_false (send_key (controller, CLUTTER_KEY_PRESS,
                            CLUTTER_KEY_Cyrillic_ef, 38, CLUTTER_CONTROL_MASK));
  g_assert_cmpuint (actor->activation_count, ==, 0);
  g_object_unref (controller);
  clutter_actor_destroy (CLUTTER_ACTOR (actor));
  g_object_unref (actor);
}

static void
key_controller_text_select_all (void)
{
  ClutterActor *actor;
  ClutterAction *controller;
  g_autofree char *selection = NULL;

  actor = clutter_text_new_with_text (NULL, "Проверка текста");
  g_object_ref_sink (actor);
  clutter_text_set_editable (CLUTTER_TEXT (actor), TRUE);
  clutter_text_set_selection (CLUTTER_TEXT (actor), 0, 0);
  controller = add_controller (actor);

  g_assert_true (send_key (controller, CLUTTER_KEY_PRESS,
                           CLUTTER_KEY_Cyrillic_ef, 38, CLUTTER_CONTROL_MASK));
  selection = clutter_text_get_selection (CLUTTER_TEXT (actor));
  g_assert_cmpstr (selection, ==, "Проверка текста");
  clutter_actor_destroy (actor);
  g_object_unref (actor);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/key-controller/non-latin", key_controller_non_latin)
  CLUTTER_TEST_UNIT ("/key-controller/native-priority", key_controller_native_priority)
  CLUTTER_TEST_UNIT ("/key-controller/text-select-all", key_controller_text_select_all))
