/*
 * Copyright (C) 2026 Red Hat Inc.
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
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "clutter/clutter-key-controller.h"

#include "clutter/clutter-event.h"

enum
{
  KEY_PRESS,
  KEY_RELEASE,
  MODIFIER_CHANGE,
  N_SIGNALS,
};

static guint signals[N_SIGNALS] = { 0, };

enum
{
  PROP_0,
  PROP_IM_FOCUS,
  PROP_TRIGGER_KEYBINDINGS,
  N_PROPS,
};

static GParamSpec *props[N_PROPS] = { 0, };

struct _ClutterKeyController
{
  ClutterAction parent_instance;
  ClutterInputFocus *im_focus;
  const ClutterEvent *event;

  unsigned int trigger_keybindings : 1;
};

G_DEFINE_FINAL_TYPE (ClutterKeyController,
                     clutter_key_controller,
                     CLUTTER_TYPE_ACTION)

static void
clutter_key_controller_finalize (GObject *object)
{
  ClutterKeyController *key_controller = CLUTTER_KEY_CONTROLLER (object);

  g_clear_object (&key_controller->im_focus);

  G_OBJECT_CLASS (clutter_key_controller_parent_class)->finalize (object);
}

static void
clutter_key_controller_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  ClutterKeyController *key_controller = CLUTTER_KEY_CONTROLLER (object);

  switch (prop_id)
    {
    case PROP_TRIGGER_KEYBINDINGS:
      g_value_set_boolean (value, key_controller->trigger_keybindings);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_key_controller_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  ClutterKeyController *key_controller = CLUTTER_KEY_CONTROLLER (object);

  switch (prop_id)
    {
    case PROP_IM_FOCUS:
      key_controller->im_focus = g_value_dup_object (value);
      break;
    case PROP_TRIGGER_KEYBINDINGS:
      key_controller->trigger_keybindings = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static gboolean
clutter_key_controller_handle_event (ClutterAction      *action,
                                     const ClutterEvent *event)
{
  ClutterKeyController *key_controller = CLUTTER_KEY_CONTROLLER (action);
  ClutterEventType evtype = clutter_event_type (event);
  ClutterEventFlags flags;
  gboolean retval = CLUTTER_EVENT_PROPAGATE;

  flags = clutter_event_get_flags (event);

  if (!(flags & CLUTTER_EVENT_FLAG_INPUT_METHOD) &&
      key_controller->im_focus &&
      clutter_input_focus_is_focused (key_controller->im_focus) &&
      clutter_input_focus_filter_event (key_controller->im_focus, event))
    return CLUTTER_EVENT_STOP;

  if (key_controller->trigger_keybindings &&
      (evtype == CLUTTER_KEY_PRESS || evtype == CLUTTER_KEY_RELEASE))
    {
      ClutterActor *actor;
      GType gtype;
      uint32_t keyval;
      ClutterModifierType modifiers;

      modifiers = clutter_event_get_state (event);
      keyval = clutter_event_get_key_symbol (event);
      actor = clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (action));
      gtype = G_OBJECT_TYPE (actor);

      while (gtype != G_TYPE_INVALID)
        {
          ClutterBindingPool *binding_pool;

          binding_pool = clutter_binding_pool_find (g_type_name (gtype));

          if (binding_pool &&
              clutter_binding_pool_activate (binding_pool,
                                             keyval, modifiers,
                                             G_OBJECT (actor)))
            return CLUTTER_EVENT_STOP;

          gtype = g_type_parent (gtype);
          if (gtype == CLUTTER_TYPE_ACTOR)
            break;
        }
    }

  key_controller->event = event;

  switch (evtype)
    {
    case CLUTTER_KEY_PRESS:
      g_signal_emit (key_controller, signals[KEY_PRESS], 0, &retval);
      break;
    case CLUTTER_KEY_RELEASE:
      g_signal_emit (key_controller, signals[KEY_RELEASE], 0, &retval);
      break;
    case CLUTTER_KEY_STATE:
      g_signal_emit (key_controller, signals[MODIFIER_CHANGE], 0);
      break;
    case CLUTTER_IM_COMMIT:
    case CLUTTER_IM_DELETE:
    case CLUTTER_IM_PREEDIT:
      if (key_controller->im_focus &&
          clutter_input_focus_is_focused (key_controller->im_focus))
        return clutter_input_focus_process_event (key_controller->im_focus, event);
    default:
      break;
    }

  key_controller->event = NULL;

  return retval;
}

static void
clutter_key_controller_class_init (ClutterKeyControllerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActionClass *action_class = CLUTTER_ACTION_CLASS (klass);

  object_class->get_property = clutter_key_controller_get_property;
  object_class->set_property = clutter_key_controller_set_property;
  object_class->finalize = clutter_key_controller_finalize;

  action_class->handle_event = clutter_key_controller_handle_event;

  /**
   * ClutterKeyController:im-focus:
   *
   * The delegate for input method handling of the actor
   */
  props[PROP_IM_FOCUS] =
    g_param_spec_object ("im-focus", NULL, NULL,
                         CLUTTER_TYPE_INPUT_FOCUS,
                         G_PARAM_WRITABLE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  /**
   * ClutterKeyController:trigger-keybindings:
   *
   * Whether the controller handles keybindings set on the actor through
   * [type@Clutter.BindingPool]
   */
  props[PROP_TRIGGER_KEYBINDINGS] =
    g_param_spec_boolean ("trigger-keybindings", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, props);

  /**
   * ClutterKeyController::key-press:
   *
   * Emitted when the controller handles a key press.
   */
  signals[KEY_PRESS] =
    g_signal_new ("key-press",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_FIRST,
                  0, g_signal_accumulator_true_handled, NULL, NULL,
                  G_TYPE_BOOLEAN, 0);
  /**
   * ClutterKeyController::key-release:
   *
   * Emitted when the controller handles a key release.
   */
  signals[KEY_RELEASE] =
    g_signal_new ("key-release",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_FIRST,
                  0, g_signal_accumulator_true_handled, NULL, NULL,
                  G_TYPE_BOOLEAN, 0);
  /**
   * ClutterKeyController::modifier-change:
   *
   * Emitted when the effective set of keyboard modifiers changes.
   */
  signals[MODIFIER_CHANGE] =
    g_signal_new ("modifier-change",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
clutter_key_controller_init (ClutterKeyController *controller)
{
}

/**
 * clutter_key_controller_new:
 * @im_focus: (nullable): a [type@Clutter.InputFocus], %NULL
 *   if the actor does not handle IM input
 *
 * Returns a newly created key controller. The @input_focus argument
 * should be set to the actor's [type@Clutter.InputFocus] if the actor
 * is meant to handle textual input.
 *
 * Returns: a new key controller
 **/
ClutterAction *
clutter_key_controller_new (ClutterInputFocus *im_focus)
{
  return g_object_new (CLUTTER_TYPE_KEY_CONTROLLER,
                       "im-focus", im_focus,
                       NULL);
}

/**
 * clutter_key_controller_get_key:
 * @key_controller: a key controller
 * @key_symbol: (out): return location for the key symbol
 * @key_code: (out): return location for the key XKB code
 * @unicode: (out): return location for the unicode represenation, if any
 *
 * Should be called within a [signal@Clutter.KeyController::key-press] or
 * [signal@Clutter.KeyController::key-release] signals to retrieve
 * a meaningful state. %FALSE will be returned otherwise.
 *
 * Returns details about the key being pressed or released.
 *
 * Returns: %TRUE if called while handling keyboard input
 **/
gboolean
clutter_key_controller_get_key (ClutterKeyController *key_controller,
                                uint32_t             *key_symbol,
                                uint32_t             *key_code,
                                gunichar             *unicode)
{
  if (!key_controller->event)
    return FALSE;

  if (key_symbol)
    *key_symbol = clutter_event_get_key_symbol (key_controller->event);
  if (key_code)
    *key_code = clutter_event_get_key_code (key_controller->event);
  if (unicode)
    *unicode = clutter_event_get_key_unicode (key_controller->event);

  return TRUE;
}

/**
 * clutter_key_controller_get_state:
 * @key_controller: a key controller
 * @pressed: (out): return location for the pressed modifiers
 * @latched: (out): return location for the latched modifiers
 * @locked: (out): return location for the locked modifiers
 *
 * Should be called within a [signal@Clutter.KeyController::key-press],
 * [signal@Clutter.KeyController::key-release] or
 * [signal@Clutter.KeyController::modifier-change] signals to retrieve
 * a meaningful state. %FALSE will be returned otherwise.
 *
 * Retrieves the current keyboard state modifiers, @pressed contains the
 * actively pressed keys, @latched the modifiers that have a temporal
 * effect (e.g. on "slow keys" accessibility feature), and @locked the
 *  modifiers that are currently locked in place (e.g. caps-lock key).
 *
 * The retrieved modifiers are those previously in effect before the
 * signal received. E.g. a shift key press will emit a
 * [signal@Clutter.KeyController::key-press] event where
 * [flags@Clutter.ModifierType.SHIFT_MASK] is not yet enabled.
 *
 * Returns: %TRUE if keyboard modifiers could be retrieved
 **/
gboolean
clutter_key_controller_get_state (ClutterKeyController *key_controller,
                                  ClutterModifierType  *pressed,
                                  ClutterModifierType  *latched,
                                  ClutterModifierType  *locked)
{
  if (!key_controller->event)
    return FALSE;

  clutter_event_get_key_state (key_controller->event, pressed, latched, locked);
  return TRUE;
}

/**
 * clutter_key_controller_set_trigger_keybindings:
 * @key_controller: a key controller
 * @trigger_keybindings: %TRUE to let the key controller handle keybindings,
 *   %FALSE otherwise
 *
 * Sets whether the key controller will handle the triggering of keybinding
 * signals, as set on the actor through [type@Clutter.BindingPool].
 **/
void
clutter_key_controller_set_trigger_keybindings (ClutterKeyController *key_controller,
                                                gboolean              trigger_keybindings)
{
  g_return_if_fail (CLUTTER_IS_KEY_CONTROLLER (key_controller));

  if (key_controller->trigger_keybindings == !!trigger_keybindings)
    return;

  key_controller->trigger_keybindings = !!trigger_keybindings;
  g_object_notify (G_OBJECT (key_controller), "trigger-keybindings");
}

/**
 * clutter_key_controller_get_trigger_keybindings:
 * @key_controller: a key controller
 *
 * Gets whether the key controller handles the triggering of keybinding
 * signals, as set on the actor through [type@Clutter.BindingPool].
 *
 * Returns: %TRUE if the key controller handles actor keybindings
 **/
gboolean
clutter_key_controller_get_trigger_keybindings (ClutterKeyController *key_controller)
{
  g_return_val_if_fail (CLUTTER_IS_KEY_CONTROLLER (key_controller), FALSE);

  return key_controller->trigger_keybindings;
}
