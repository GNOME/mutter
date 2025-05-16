/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2024 Red Hat Inc.
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

#include "clutter/clutter-key-focus.h"

#include "clutter/clutter-action-private.h"
#include "clutter/clutter-actor-private.h"
#include "clutter/clutter-focus-private.h"
#include "clutter/clutter-stage.h"

typedef struct _EventReceiver
{
  ClutterActor *actor;
  ClutterEventPhase phase;
  gboolean emit_to_actor;

  ClutterAction *action;
} EventReceiver;

typedef struct _ClutterKeyFocusPrivate ClutterKeyFocusPrivate;

struct _ClutterKeyFocusPrivate
{
  ClutterActor *key_focused_actor;
  ClutterActor *effective_focused_actor;
  GPtrArray *cur_event_actors;
  GArray *cur_event_emission_chain;
  GArray *event_emission_chain;
};

G_DEFINE_TYPE_WITH_PRIVATE (ClutterKeyFocus,
                            clutter_key_focus,
                            CLUTTER_TYPE_FOCUS)

typedef enum
{
  EVENT_NOT_HANDLED,
  EVENT_HANDLED_BY_ACTOR,
  EVENT_HANDLED_BY_ACTION
} EventHandledState;

static EventHandledState
emit_event (const ClutterEvent *event,
            GArray             *event_emission_chain)
{
  unsigned int i;

  for (i = 0; i < event_emission_chain->len; i++)
    {
      EventReceiver *receiver =
        &g_array_index (event_emission_chain, EventReceiver, i);

      if (receiver->actor)
        {
          if (clutter_actor_event (receiver->actor, event,
                                   receiver->phase == CLUTTER_PHASE_CAPTURE))
            return EVENT_HANDLED_BY_ACTOR;
        }
      else if (receiver->action)
        {
          if (clutter_action_handle_event (receiver->action, event))
            return EVENT_HANDLED_BY_ACTION;
        }
    }

  return EVENT_NOT_HANDLED;
}

static void
free_event_receiver (EventReceiver *receiver)
{
  g_clear_object (&receiver->actor);
  g_clear_object (&receiver->action);
}

static inline void
add_actor_to_event_emission_chain (GArray            *chain,
                                   ClutterActor      *actor,
                                   ClutterEventPhase  phase)
{
  EventReceiver *receiver;

  g_array_set_size (chain, chain->len + 1);
  receiver = &g_array_index (chain, EventReceiver, chain->len - 1);

  receiver->actor = g_object_ref (actor);
  receiver->phase = phase;
}

static inline void
add_action_to_event_emission_chain (GArray        *chain,
                                    ClutterAction *action)
{
  EventReceiver *receiver;

  g_array_set_size (chain, chain->len + 1);
  receiver = &g_array_index (chain, EventReceiver, chain->len - 1);

  receiver->action = g_object_ref (action);
}

static void
create_event_emission_chain (ClutterKeyFocus *key_focus,
                             GArray          *chain,
                             ClutterActor    *topmost,
                             ClutterActor    *deepmost)
{
  ClutterKeyFocusPrivate *priv =
    clutter_key_focus_get_instance_private (key_focus);
  int i;

  g_assert (priv->cur_event_actors->len == 0);
  clutter_actor_collect_event_actors (topmost, deepmost, priv->cur_event_actors);

  for (i = priv->cur_event_actors->len - 1; i >= 0; i--)
    {
      ClutterActor *actor = g_ptr_array_index (priv->cur_event_actors, i);
      const GList *l;

      for (l = clutter_actor_peek_actions (actor); l; l = l->next)
        {
          ClutterAction *action = l->data;

          if (clutter_actor_meta_get_enabled (CLUTTER_ACTOR_META (action)) &&
              clutter_action_get_phase (action) == CLUTTER_PHASE_CAPTURE)
            add_action_to_event_emission_chain (chain, action);
        }

      add_actor_to_event_emission_chain (chain, actor, CLUTTER_PHASE_CAPTURE);
    }

  for (i = 0; i < priv->cur_event_actors->len; i++)
    {
      ClutterActor *actor = g_ptr_array_index (priv->cur_event_actors, i);
      const GList *l;

      for (l = clutter_actor_peek_actions (actor); l; l = l->next)
        {
          ClutterAction *action = l->data;

          if (clutter_actor_meta_get_enabled (CLUTTER_ACTOR_META (action)) &&
              clutter_action_get_phase (action) == CLUTTER_PHASE_BUBBLE)
            add_action_to_event_emission_chain (chain, action);
        }

      add_actor_to_event_emission_chain (chain, actor, CLUTTER_PHASE_BUBBLE);
    }

  priv->cur_event_actors->len = 0;
}

static void
clutter_key_focus_init (ClutterKeyFocus *key_focus)
{
  ClutterKeyFocusPrivate *priv =
    clutter_key_focus_get_instance_private (key_focus);

  priv->event_emission_chain =
    g_array_sized_new (FALSE, TRUE, sizeof (EventReceiver), 32);
  g_array_set_clear_func (priv->event_emission_chain,
                          (GDestroyNotify) free_event_receiver);
  priv->cur_event_emission_chain =
    g_array_sized_new (FALSE, TRUE, sizeof (EventReceiver), 32);
  g_array_set_clear_func (priv->cur_event_emission_chain,
                          (GDestroyNotify) free_event_receiver);

  priv->cur_event_actors = g_ptr_array_sized_new (32);
}

static void
clutter_key_focus_finalize (GObject *object)
{
  ClutterKeyFocus *key_focus = CLUTTER_KEY_FOCUS (object);
  ClutterKeyFocusPrivate *priv =
    clutter_key_focus_get_instance_private (key_focus);

  if (priv->key_focused_actor)
    _clutter_actor_set_has_key_focus (priv->key_focused_actor, FALSE);

  g_clear_pointer (&priv->event_emission_chain, g_array_unref);

  g_assert (priv->cur_event_actors->len == 0);
  g_clear_pointer (&priv->cur_event_actors, g_ptr_array_unref);

  g_assert (priv->cur_event_emission_chain->len == 0);
  g_clear_pointer (&priv->cur_event_emission_chain, g_array_unref);

  G_OBJECT_CLASS (clutter_key_focus_parent_class)->finalize (object);
}

static gboolean
clutter_key_focus_set_current_actor (ClutterFocus       *focus,
                                     ClutterActor       *actor,
                                     ClutterInputDevice *source_device,
                                     uint32_t            time_ms)
{
  ClutterKeyFocus *key_focus = CLUTTER_KEY_FOCUS (focus);
  ClutterKeyFocusPrivate *priv =
    clutter_key_focus_get_instance_private (key_focus);
  ClutterStage *stage = clutter_focus_get_stage (focus);
  ClutterActor *grab_actor, *effective_focus;

  if (!actor || !clutter_stage_is_active (stage))
    effective_focus = CLUTTER_ACTOR (stage);
  else
    effective_focus = actor;

  /* avoid emitting signals and notifications if we're setting the same
   * actor as the key focus
   */
  if (priv->key_focused_actor == actor &&
      priv->effective_focused_actor == effective_focus)
    return FALSE;

  if (priv->effective_focused_actor != NULL)
    {
      ClutterActor *old_focused_actor;

      old_focused_actor = priv->key_focused_actor;
      if (old_focused_actor)
        {
          /* set key_focused_actor to NULL before emitting the signal or someone
           * might hide the previously focused actor in the signal handler
           */
          priv->effective_focused_actor = NULL;

          _clutter_actor_set_has_key_focus (old_focused_actor, FALSE);
        }
    }
  else
    {
      _clutter_actor_set_has_key_focus (CLUTTER_ACTOR (stage), FALSE);
    }

  /* Note, if someone changes key focus in focus-out signal handler we'd be
   * overriding the latter call below moving the focus where it was originally
   * intended. The order of events would be:
   *   1st focus-out, 2nd focus-out (on stage), 2nd focus-in, 1st focus-in
   */
  priv->key_focused_actor = actor;
  priv->effective_focused_actor = effective_focus;

  grab_actor = clutter_stage_get_grab_actor (stage);

  /* If the key focused actor is allowed to receive key events according
   * to the given grab (or there is none) set key focus on it, otherwise
   * key focus is delayed until there are grabbing conditions that allow
   * it to get key focus.
   */
  if (!grab_actor ||
      grab_actor == CLUTTER_ACTOR (stage) ||
      grab_actor == effective_focus ||
      (effective_focus && clutter_actor_contains (grab_actor, effective_focus)))
    {
      if (effective_focus != NULL)
        _clutter_actor_set_has_key_focus (effective_focus, TRUE);
      else
        _clutter_actor_set_has_key_focus (CLUTTER_ACTOR (stage), TRUE);
    }

  return TRUE;
}

static ClutterActor *
clutter_key_focus_get_current_actor (ClutterFocus *focus)
{
  ClutterKeyFocus *key_focus = CLUTTER_KEY_FOCUS (focus);
  ClutterKeyFocusPrivate *priv =
    clutter_key_focus_get_instance_private (key_focus);

  return priv->key_focused_actor;
}

static void
clutter_key_focus_notify_grab (ClutterFocus *focus,
                               ClutterGrab  *grab,
                               ClutterActor *grab_actor,
                               ClutterActor *old_grab_actor)
{
  ClutterKeyFocus *key_focus = CLUTTER_KEY_FOCUS (focus);
  ClutterKeyFocusPrivate *priv =
    clutter_key_focus_get_instance_private (key_focus);
  ClutterActor *focus_actor;
  gboolean focus_in_grab, focus_in_old_grab;

  focus_actor = priv->effective_focused_actor;

  focus_in_grab =
    !grab_actor ||
    grab_actor == focus_actor ||
    clutter_actor_contains (grab_actor, focus_actor);
  focus_in_old_grab =
    !old_grab_actor ||
    old_grab_actor == focus_actor ||
    clutter_actor_contains (old_grab_actor, focus_actor);

  if (focus_in_grab && !focus_in_old_grab)
    _clutter_actor_set_has_key_focus (focus_actor, TRUE);
  else if (!focus_in_grab && focus_in_old_grab)
    _clutter_actor_set_has_key_focus (focus_actor, FALSE);
}

static void
clutter_key_focus_propagate_event (ClutterFocus       *focus,
                                   const ClutterEvent *event)
{
  ClutterKeyFocus *key_focus = CLUTTER_KEY_FOCUS (focus);
  ClutterKeyFocusPrivate *priv =
    clutter_key_focus_get_instance_private (key_focus);
  ClutterStage *stage = clutter_focus_get_stage (focus);
  ClutterActor *target_actor = NULL, *seat_grab_actor = NULL;

  target_actor = priv->effective_focused_actor;

  if (!target_actor)
    return;

  seat_grab_actor = clutter_stage_get_grab_actor (stage);
  if (!seat_grab_actor)
    seat_grab_actor = CLUTTER_ACTOR (stage);

  create_event_emission_chain (key_focus, priv->cur_event_emission_chain,
                               seat_grab_actor, target_actor);

  emit_event (event, priv->cur_event_emission_chain);

  g_array_remove_range (priv->cur_event_emission_chain, 0,
                        priv->cur_event_emission_chain->len);
}

static void
clutter_key_focus_class_init (ClutterKeyFocusClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterFocusClass *focus_class = CLUTTER_FOCUS_CLASS (klass);

  object_class->finalize = clutter_key_focus_finalize;

  focus_class->set_current_actor = clutter_key_focus_set_current_actor;
  focus_class->get_current_actor = clutter_key_focus_get_current_actor;
  focus_class->propagate_event = clutter_key_focus_propagate_event;
  focus_class->notify_grab = clutter_key_focus_notify_grab;
}
