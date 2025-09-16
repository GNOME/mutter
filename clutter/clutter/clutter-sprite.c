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

#include "clutter/clutter-sprite-private.h"

#include "clutter/clutter-action-private.h"
#include "clutter/clutter-actor-private.h"
#include "clutter/clutter-debug.h"
#include "clutter/clutter-event-private.h"
#include "clutter/clutter-focus-private.h"
#include "clutter/clutter-grab.h"
#include "clutter/clutter-private.h"
#include "clutter/clutter-stage.h"

typedef struct _EventReceiver
{
  ClutterActor *actor;
  ClutterEventPhase phase;
  gboolean emit_to_actor;

  ClutterAction *action;
} EventReceiver;

enum
{
  PROP_0,
  PROP_DEVICE,
  PROP_SEQUENCE,
  N_PROPS,
};

static GParamSpec *props[N_PROPS] = { 0, };

typedef struct _ClutterSpritePrivate ClutterSpritePrivate;

struct _ClutterSpritePrivate
{
  ClutterInputDevice *device;
  ClutterEventSequence *sequence;
  graphene_point_t coords;
  ClutterActor *current_actor;
  MtkRegion *clear_area;

  GPtrArray *cur_event_actors;
  GArray *cur_event_emission_chain;

  unsigned int press_count;
  ClutterActor *implicit_grab_actor;
  GArray *event_emission_chain;
};

G_DEFINE_TYPE_WITH_PRIVATE (ClutterSprite, clutter_sprite, CLUTTER_TYPE_FOCUS)

static void clutter_sprite_emit_crossing_event (ClutterSprite      *sprite,
                                                const ClutterEvent *event,
                                                ClutterActor       *deepmost,
                                                ClutterActor       *topmost);

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
          ClutterEventType type = clutter_event_type (event);
          gboolean may_emit = receiver->emit_to_actor ||
                              type == CLUTTER_ENTER ||
                              type == CLUTTER_LEAVE;

          if (may_emit &&
              clutter_actor_event (receiver->actor, event, receiver->phase == CLUTTER_PHASE_CAPTURE))
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

static void
cleanup_implicit_grab (ClutterSprite *sprite)
{
  ClutterSpritePrivate *priv = clutter_sprite_get_instance_private (sprite);

  clutter_actor_set_implicitly_grabbed (priv->implicit_grab_actor, FALSE);
  priv->implicit_grab_actor = NULL;

  g_array_remove_range (priv->event_emission_chain, 0,
                        priv->event_emission_chain->len);

  priv->press_count = 0;
}

static gboolean
setup_implicit_grab (ClutterSprite *sprite)
{
  ClutterSpritePrivate *priv = clutter_sprite_get_instance_private (sprite);

  /* With a mouse, it's possible to press two buttons at the same time,
   * We ignore the second BUTTON_PRESS event here, and we'll release the
   * implicit grab on the BUTTON_RELEASE of the second press.
   */
  if (priv->sequence == NULL && priv->press_count)
    {
      priv->press_count++;
      return FALSE;
    }

  CLUTTER_NOTE (GRABS,
                "[device=%p sequence=%p] Acquiring implicit grab",
                priv->device, priv->sequence);

  g_assert (priv->press_count == 0);
  g_assert (priv->event_emission_chain->len == 0);

  priv->press_count = 1;
  return TRUE;
}

static gboolean
release_implicit_grab (ClutterSprite *sprite)
{
  ClutterSpritePrivate *priv = clutter_sprite_get_instance_private (sprite);

  if (!priv->press_count)
    return FALSE;

  /* See comment in setup_implicit_grab() */
  if (priv->sequence == NULL && priv->press_count > 1)
    {
      priv->press_count--;
      return FALSE;
    }

  CLUTTER_NOTE (GRABS,
                "[device=%p sequence=%p] Releasing implicit grab",
                priv->device, priv->sequence);

  g_assert (priv->press_count == 1);

  priv->press_count = 0;
  return TRUE;
}

static void
clutter_sprite_remove_all_actions_from_chain (ClutterSprite *sprite)
{
  ClutterSpritePrivate *priv = clutter_sprite_get_instance_private (sprite);
  unsigned int i;

  for (i = 0; i < priv->event_emission_chain->len; i++)
    {
      EventReceiver *receiver =
        &g_array_index (priv->event_emission_chain, EventReceiver, i);

      if (receiver->action)
        {
          clutter_action_sequence_cancelled (receiver->action, sprite);
          g_clear_object (&receiver->action);
        }
    }
}

static void
sync_crossings_on_implicit_grab_end (ClutterSprite *sprite)
{
  ClutterSpritePrivate *priv = clutter_sprite_get_instance_private (sprite);
  ClutterActor *deepmost, *topmost;
  ClutterActor *parent;
  ClutterEvent *crossing;

  if (!priv->current_actor)
    return;
  if (clutter_actor_contains (priv->current_actor, priv->implicit_grab_actor))
    return;

  deepmost = priv->current_actor;
  topmost = priv->current_actor;

  while ((parent = clutter_actor_get_parent (topmost)))
    {
      if (clutter_actor_contains (parent, priv->implicit_grab_actor))
        break;

      topmost = parent;
    }

  crossing = clutter_event_crossing_new (CLUTTER_ENTER,
                                         CLUTTER_EVENT_FLAG_GRAB_NOTIFY,
                                         CLUTTER_CURRENT_TIME,
                                         priv->device,
                                         priv->sequence,
                                         priv->coords,
                                         priv->current_actor,
                                         NULL);

  if (!_clutter_event_process_filters (crossing, deepmost))
    {
      clutter_sprite_emit_crossing_event (sprite,
                                          crossing,
                                          deepmost,
                                          topmost);
    }

  clutter_event_free (crossing);
}

static ClutterActor *
find_common_root_actor (ClutterStage *stage,
                        ClutterActor *a,
                        ClutterActor *b)
{
  if (a && b)
    {
      while (a)
        {
          if (a == b || clutter_actor_contains (a, b))
            return a;

          a = clutter_actor_get_parent (a);
        }
    }

  return CLUTTER_ACTOR (stage);
}

static void
setup_sequence_actions (ClutterSprite      *sprite,
                        GArray             *emission_chain,
                        const ClutterEvent *sequence_begin_event)
{
  unsigned int i, j;

  for (i = 0; i < emission_chain->len; i++)
    {
      EventReceiver *receiver = &g_array_index (emission_chain, EventReceiver, i);

      if (!receiver->action)
        continue;

      if (!clutter_action_register_sequence (receiver->action, sequence_begin_event))
        g_clear_object (&receiver->action);
    }

  for (i = 0; i < emission_chain->len; i++)
    {
      EventReceiver *receiver_1 = &g_array_index (emission_chain, EventReceiver, i);

      if (!receiver_1->action)
        continue;

      for (j = i + 1; j < emission_chain->len; j++)
        {
          EventReceiver *receiver_2 = &g_array_index (emission_chain, EventReceiver, j);

          if (!receiver_2->action)
            continue;

          clutter_action_setup_sequence_relationship (receiver_1->action,
                                                      receiver_2->action,
                                                      sprite);
        }
    }
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
  receiver->emit_to_actor = TRUE;
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
create_event_emission_chain (ClutterSprite *sprite,
                             GArray        *chain,
                             ClutterActor  *topmost,
                             ClutterActor  *deepmost)
{
  ClutterSpritePrivate *priv = clutter_sprite_get_instance_private (sprite);
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
clutter_sprite_init (ClutterSprite *sprite)
{
  ClutterSpritePrivate *priv = clutter_sprite_get_instance_private (sprite);

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
clutter_sprite_finalize (GObject *object)
{
  ClutterSprite *sprite = CLUTTER_SPRITE (object);
  ClutterSpritePrivate *priv = clutter_sprite_get_instance_private (sprite);

  if (priv->current_actor)
    {
      _clutter_actor_set_has_pointer (priv->current_actor, FALSE);
      priv->current_actor = NULL;
    }

  g_clear_pointer (&priv->clear_area, mtk_region_unref);

  g_assert (!priv->press_count);
  g_assert (priv->event_emission_chain->len == 0);
  g_clear_pointer (&priv->event_emission_chain, g_array_unref);

  g_assert (priv->cur_event_actors->len == 0);
  g_clear_pointer (&priv->cur_event_actors, g_ptr_array_unref);

  g_assert (priv->cur_event_emission_chain->len == 0);
  g_clear_pointer (&priv->cur_event_emission_chain, g_array_unref);

  G_OBJECT_CLASS (clutter_sprite_parent_class)->finalize (object);
}

static void
clutter_sprite_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  ClutterSprite *sprite = CLUTTER_SPRITE (object);
  ClutterSpritePrivate *priv = clutter_sprite_get_instance_private (sprite);

  switch (prop_id)
    {
    case PROP_DEVICE:
      priv->device = g_value_get_object (value);
      break;
    case PROP_SEQUENCE:
      priv->sequence = g_value_get_boxed (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_sprite_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  ClutterSprite *sprite = CLUTTER_SPRITE (object);
  ClutterSpritePrivate *priv = clutter_sprite_get_instance_private (sprite);

  switch (prop_id)
    {
    case PROP_DEVICE:
      g_value_set_object (value, priv->device);
      break;
    case PROP_SEQUENCE:
      g_value_set_boxed (value, priv->sequence);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static gboolean
clutter_sprite_set_current_actor (ClutterFocus       *focus,
                                  ClutterActor       *actor,
                                  ClutterInputDevice *source_device,
                                  uint32_t            time_ms)
{
  ClutterSprite *sprite = CLUTTER_SPRITE (focus);
  ClutterSpritePrivate *priv = clutter_sprite_get_instance_private (sprite);
  ClutterStage *stage;
  ClutterEvent *event;
  ClutterActor *grab_actor, *root, *old_actor;

  if (priv->current_actor == actor)
    return FALSE;

  if (priv->current_actor)
    _clutter_actor_set_has_pointer (priv->current_actor, FALSE);

  old_actor = priv->current_actor;
  priv->current_actor = actor;

  if (actor)
    _clutter_actor_set_has_pointer (actor, TRUE);

  stage = clutter_focus_get_stage (focus);
  root = find_common_root_actor (stage, actor, old_actor);

  if (!source_device)
    source_device = priv->device;

  grab_actor = clutter_stage_get_grab_actor (stage);

  /* If the common root is outside the currently effective grab,
   * it involves actors outside the grabbed actor hierarchy, the
   * events should be propagated from/inside the grab actor.
   */
  if (grab_actor &&
      root != grab_actor &&
      !clutter_actor_contains (grab_actor, root))
    root = grab_actor;

  /* we need to make sure that this event is processed
   * before any other event we might have queued up until
   * now, so we go on, and synthesize the event emission
   * ourselves
   */
  if (old_actor)
    {
      event = clutter_event_crossing_new (CLUTTER_LEAVE,
                                          CLUTTER_EVENT_NONE,
                                          ms2us (time_ms),
                                          source_device,
                                          priv->sequence,
                                          priv->coords,
                                          old_actor,
                                          actor);
      if (!_clutter_event_process_filters (event, old_actor))
        {
          clutter_sprite_emit_crossing_event (sprite,
                                              event,
                                              old_actor,
                                              root);
        }

      clutter_event_free (event);
    }

  if (actor)
    {
      event = clutter_event_crossing_new (CLUTTER_ENTER,
                                          CLUTTER_EVENT_NONE,
                                          ms2us (time_ms),
                                          source_device,
                                          priv->sequence,
                                          priv->coords,
                                          actor,
                                          old_actor);
      if (!_clutter_event_process_filters (event, actor))
        {
          clutter_sprite_emit_crossing_event (sprite,
                                              event,
                                              actor,
                                              root);
        }

      clutter_event_free (event);
    }

  return TRUE;
}

static ClutterActor *
clutter_sprite_get_current_actor (ClutterFocus *focus)
{
  ClutterSprite *sprite = CLUTTER_SPRITE (focus);
  ClutterSpritePrivate *priv = clutter_sprite_get_instance_private (sprite);

  return priv->current_actor;
}

static void
clutter_sprite_notify_grab (ClutterFocus *focus,
                            ClutterGrab  *grab,
                            ClutterActor *grab_actor,
                            ClutterActor *old_grab_actor)
{
  ClutterSprite *sprite = CLUTTER_SPRITE (focus);
  ClutterSpritePrivate *priv = clutter_sprite_get_instance_private (sprite);
  gboolean pointer_in_grab, pointer_in_old_grab;
  gboolean implicit_grab_cancelled = FALSE;
  unsigned int implicit_grab_n_removed = 0, implicit_grab_n_remaining = 0;
  ClutterEventType event_type = CLUTTER_NOTHING;
  ClutterActor *topmost, *deepmost;

  if (!priv->current_actor)
    return;

  pointer_in_grab =
    !grab_actor ||
    grab_actor == priv->current_actor ||
    clutter_actor_contains (grab_actor, priv->current_actor);
  pointer_in_old_grab =
    !old_grab_actor ||
    old_grab_actor == priv->current_actor ||
    clutter_actor_contains (old_grab_actor, priv->current_actor);

  if (grab_actor && priv->press_count > 0)
    {
      ClutterInputDevice *device = priv->device;
      ClutterEventSequence *sequence = priv->sequence;
      unsigned int i;

      for (i = 0; i < priv->event_emission_chain->len; i++)
        {
          EventReceiver *receiver =
            &g_array_index (priv->event_emission_chain, EventReceiver, i);

          if (receiver->actor && receiver->emit_to_actor)
            {
              if (!clutter_actor_contains (grab_actor, receiver->actor))
                {
                  receiver->emit_to_actor = FALSE;
                  implicit_grab_n_removed++;
                }
              else
                {
                  implicit_grab_n_remaining++;
                }
            }
          else if (receiver->action)
            {
              ClutterActor *action_actor =
                clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (receiver->action));

              if (!action_actor || !clutter_actor_contains (grab_actor, action_actor))
                {
                  clutter_action_sequence_cancelled (receiver->action, sprite);
                  g_clear_object (&receiver->action);
                  implicit_grab_n_removed++;
                }
              else
                {
                  implicit_grab_n_remaining++;
                }
            }
        }

      /* Seat grabs win over implicit grabs, so we default to cancel the ongoing
       * implicit grab. If the seat grab contains one or more actors from
       * the implicit grab though, the implicit grab remains in effect.
       */
      implicit_grab_cancelled = implicit_grab_n_remaining == 0;

      CLUTTER_NOTE (GRABS,
                    "[grab=%p device=%p sequence=%p implicit_grab_cancelled=%d] "
                    "Cancelled %u actors and actions (%u remaining) on implicit "
                    "grab due to new seat grab",
                    grab, device, sequence, implicit_grab_cancelled,
                    implicit_grab_n_removed, implicit_grab_n_remaining);
    }

  /* Equate NULL actors to the stage here, to ease calculations further down. */
  if (!grab_actor)
    grab_actor = CLUTTER_ACTOR (clutter_focus_get_stage (focus));
  if (!old_grab_actor)
    old_grab_actor = CLUTTER_ACTOR (clutter_focus_get_stage (focus));

  if (grab_actor == old_grab_actor)
    {
      g_assert ((implicit_grab_n_removed == 0 && implicit_grab_n_remaining == 0) ||
                !implicit_grab_cancelled);
      return;
    }

  if (pointer_in_grab && pointer_in_old_grab)
    {
      /* Both grabs happen to contain the pointer actor, we have to figure out
       * which is topmost, and emit ENTER/LEAVE events accordingly on the actors
       * between old/new grabs.
       */
      if (clutter_actor_contains (grab_actor, old_grab_actor))
        {
          /* grab_actor is above old_grab_actor, emit ENTER events in the
           * line between those two actors.
           */
          event_type = CLUTTER_ENTER;
          deepmost = clutter_actor_get_parent (old_grab_actor);
          topmost = grab_actor;
        }
      else if (clutter_actor_contains (old_grab_actor, grab_actor))
        {
          /* old_grab_actor is above grab_actor, emit LEAVE events in the
           * line between those two actors.
           */
          event_type = CLUTTER_LEAVE;
          deepmost = clutter_actor_get_parent (grab_actor);
          topmost = old_grab_actor;
        }
    }
  else if (pointer_in_grab)
    {
      /* Pointer is somewhere inside the grab_actor hierarchy. Emit ENTER events
       * from the current grab actor to the pointer actor.
       */
      event_type = CLUTTER_ENTER;
      deepmost = priv->current_actor;
      topmost = grab_actor;
    }
  else if (pointer_in_old_grab)
    {
      /* Pointer is somewhere inside the old_grab_actor hierarchy. Emit LEAVE
       * events from the common root of old/cur grab actors to the pointer
       * actor.
       */
      event_type = CLUTTER_LEAVE;
      deepmost = priv->current_actor;
      topmost = find_common_root_actor (clutter_focus_get_stage (CLUTTER_FOCUS (sprite)),
                                        grab_actor, old_grab_actor);
    }

  if (event_type == CLUTTER_ENTER && implicit_grab_cancelled)
    cleanup_implicit_grab (sprite);

  if (event_type != CLUTTER_NOTHING)
    {
      ClutterEvent *event;

      if (priv->implicit_grab_actor)
        deepmost = find_common_root_actor (clutter_focus_get_stage (CLUTTER_FOCUS (sprite)),
                                           priv->implicit_grab_actor, deepmost);

      event = clutter_event_crossing_new (event_type,
                                          CLUTTER_EVENT_FLAG_GRAB_NOTIFY,
                                          CLUTTER_CURRENT_TIME,
                                          priv->device,
                                          priv->sequence,
                                          priv->coords,
                                          priv->current_actor,
                                          event_type == CLUTTER_LEAVE ?
                                          grab_actor : old_grab_actor);
      if (!_clutter_event_process_filters (event, priv->current_actor))
        {
          clutter_sprite_emit_crossing_event (sprite,
                                              event,
                                              deepmost,
                                              topmost);
        }

      clutter_event_free (event);
    }

  if ((event_type == CLUTTER_NOTHING || event_type == CLUTTER_LEAVE) &&
      implicit_grab_cancelled)
    cleanup_implicit_grab (sprite);
}

static void
clutter_sprite_propagate_event (ClutterFocus       *focus,
                                const ClutterEvent *event)
{
  ClutterSprite *sprite = CLUTTER_SPRITE (focus);
  ClutterSpritePrivate *priv = clutter_sprite_get_instance_private (sprite);
  ClutterStage *stage = clutter_focus_get_stage (focus);
  ClutterActor *target_actor = NULL, *seat_grab_actor = NULL;
  gboolean is_sequence_begin, is_sequence_end;
  ClutterEventType event_type;

  event_type = clutter_event_type (event);

  switch (event_type)
    {
    case CLUTTER_NOTHING:
    case CLUTTER_DEVICE_REMOVED:
    case CLUTTER_DEVICE_ADDED:
    case CLUTTER_EVENT_LAST:
      return;

    case CLUTTER_KEY_PRESS:
    case CLUTTER_KEY_RELEASE:
    case CLUTTER_KEY_STATE:
    case CLUTTER_PAD_BUTTON_PRESS:
    case CLUTTER_PAD_BUTTON_RELEASE:
    case CLUTTER_PAD_STRIP:
    case CLUTTER_PAD_RING:
    case CLUTTER_PAD_DIAL:
    case CLUTTER_IM_COMMIT:
    case CLUTTER_IM_DELETE:
    case CLUTTER_IM_PREEDIT:
      {
        target_actor = clutter_stage_get_key_focus (stage);
        break;
      }

    /* x11 stage enter/leave events */
    case CLUTTER_ENTER:
    case CLUTTER_LEAVE:
      {
        target_actor = priv->current_actor;
        break;
      }

    case CLUTTER_MOTION:
    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_BUTTON_RELEASE:
    case CLUTTER_SCROLL:
    case CLUTTER_TOUCHPAD_PINCH:
    case CLUTTER_TOUCHPAD_SWIPE:
    case CLUTTER_TOUCHPAD_HOLD:
    case CLUTTER_TOUCH_UPDATE:
    case CLUTTER_TOUCH_BEGIN:
    case CLUTTER_TOUCH_CANCEL:
    case CLUTTER_TOUCH_END:
    case CLUTTER_PROXIMITY_IN:
    case CLUTTER_PROXIMITY_OUT:
      {
        float x, y;

        clutter_event_get_coords (event, &x, &y);

        CLUTTER_NOTE (EVENT,
                      "Reactive event received at %.2f, %.2f - actor: %p",
                      x, y, priv->current_actor);

        target_actor = priv->current_actor;
        break;
      }
    }

  if (!target_actor)
    return;

  seat_grab_actor = clutter_stage_get_grab_actor (stage);
  if (!seat_grab_actor)
    seat_grab_actor = CLUTTER_ACTOR (stage);

  is_sequence_begin =
    event_type == CLUTTER_BUTTON_PRESS || event_type == CLUTTER_TOUCH_BEGIN;
  is_sequence_end =
    event_type == CLUTTER_BUTTON_RELEASE || event_type == CLUTTER_TOUCH_END ||
    event_type == CLUTTER_TOUCH_CANCEL;

  if (is_sequence_begin && setup_implicit_grab (sprite))
    {
      g_assert (priv->implicit_grab_actor == NULL);
      priv->implicit_grab_actor = target_actor;
      clutter_actor_set_implicitly_grabbed (priv->implicit_grab_actor, TRUE);

      create_event_emission_chain (sprite, priv->event_emission_chain, seat_grab_actor, target_actor);
      setup_sequence_actions (sprite, priv->event_emission_chain, event);
    }

  if (priv->press_count)
    {
      EventHandledState state;

      state = emit_event (event, priv->event_emission_chain);

      if (state == EVENT_HANDLED_BY_ACTOR)
        clutter_sprite_remove_all_actions_from_chain (sprite);
    }
  else
    {
      create_event_emission_chain (sprite, priv->cur_event_emission_chain, seat_grab_actor, target_actor);

      emit_event (event, priv->cur_event_emission_chain);

      g_array_remove_range (priv->cur_event_emission_chain, 0, priv->cur_event_emission_chain->len);
    }

  if (is_sequence_end && release_implicit_grab (sprite))
    {
      /* Sync crossings after the implicit grab for mice */
      if (event_type == CLUTTER_BUTTON_RELEASE)
        sync_crossings_on_implicit_grab_end (sprite);

      cleanup_implicit_grab (sprite);
    }
}

static void
clutter_sprite_class_init (ClutterSpriteClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterFocusClass *focus_class = CLUTTER_FOCUS_CLASS (klass);

  object_class->finalize = clutter_sprite_finalize;
  object_class->set_property = clutter_sprite_set_property;
  object_class->get_property = clutter_sprite_get_property;

  focus_class->set_current_actor = clutter_sprite_set_current_actor;
  focus_class->get_current_actor = clutter_sprite_get_current_actor;
  focus_class->propagate_event = clutter_sprite_propagate_event;
  focus_class->notify_grab = clutter_sprite_notify_grab;

  props[PROP_DEVICE] =
    g_param_spec_object ("device", NULL, NULL,
                         CLUTTER_TYPE_INPUT_DEVICE,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS |
                         G_PARAM_CONSTRUCT_ONLY);
  props[PROP_SEQUENCE] =
    g_param_spec_boxed ("sequence", NULL, NULL,
                        CLUTTER_TYPE_EVENT_SEQUENCE,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, N_PROPS, props);
}

ClutterInputDevice *
clutter_sprite_get_device (ClutterSprite *sprite)
{
  ClutterSpritePrivate *priv = clutter_sprite_get_instance_private (sprite);

  return priv->device;
}

ClutterEventSequence *
clutter_sprite_get_sequence (ClutterSprite *sprite)
{
  ClutterSpritePrivate *priv = clutter_sprite_get_instance_private (sprite);

  return priv->sequence;
}

void
clutter_sprite_update (ClutterSprite    *sprite,
                       graphene_point_t  coords,
                       MtkRegion        *clear_area)
{
  ClutterSpritePrivate *priv = clutter_sprite_get_instance_private (sprite);

  priv->coords = coords;

  g_clear_pointer (&priv->clear_area, mtk_region_unref);
  if (clear_area)
    priv->clear_area = mtk_region_ref (clear_area);
}

/**
 * clutter_sprite_get_coords:
 * @sprite: a #ClutterSprite
 * @coords: (out): return location for the sprite coordinates
 *
 * Returns the current position that @sprite points to, in stage-global
 * coordinate system.
 **/
void
clutter_sprite_get_coords (ClutterSprite    *sprite,
                           graphene_point_t *coords)
{
  ClutterSpritePrivate *priv = clutter_sprite_get_instance_private (sprite);

  *coords = priv->coords;
}

void
clutter_sprite_update_coords (ClutterSprite    *sprite,
                              graphene_point_t  coords)
{
  ClutterSpritePrivate *priv = clutter_sprite_get_instance_private (sprite);

  priv->coords = coords;
}

void
clutter_sprite_remove_all_actors_from_chain (ClutterSprite *sprite)
{
  ClutterSpritePrivate *priv = clutter_sprite_get_instance_private (sprite);
  unsigned int i;

  g_assert (priv->press_count > 0);

  for (i = 0; i < priv->event_emission_chain->len; i++)
    {
      EventReceiver *receiver =
        &g_array_index (priv->event_emission_chain, EventReceiver, i);

      if (receiver->actor)
        receiver->emit_to_actor = FALSE;
    }
}


void
clutter_sprite_maybe_lost_implicit_grab (ClutterSprite *sprite)
{
  ClutterSpritePrivate *priv = clutter_sprite_get_instance_private (sprite);
  unsigned int i;

  if (!priv->press_count)
    return;

  CLUTTER_NOTE (GRABS,
                "[sprite=%p] Lost implicit grab",
                sprite);

  for (i = 0; i < priv->event_emission_chain->len; i++)
    {
      EventReceiver *receiver =
        &g_array_index (priv->event_emission_chain, EventReceiver, i);

      if (receiver->action)
        clutter_action_sequence_cancelled (receiver->action, sprite);
    }

  sync_crossings_on_implicit_grab_end (sprite);

  cleanup_implicit_grab (sprite);
}

void
clutter_sprite_maybe_break_implicit_grab (ClutterSprite *sprite,
                                          ClutterActor  *actor)
{
  ClutterSpritePrivate *priv = clutter_sprite_get_instance_private (sprite);
  unsigned int i;
  ClutterActor *parent = clutter_actor_get_parent (actor);

  if (priv->implicit_grab_actor != actor)
    return;

  CLUTTER_NOTE (GRABS,
                "[device=%p sequence=%p] Cancelling implicit grab on actor (%s) "
                "due to unmap",
                priv->device, priv->sequence,
                _clutter_actor_get_debug_name (actor));

  for (i = 0; i < priv->event_emission_chain->len; i++)
    {
      EventReceiver *receiver =
        &g_array_index (priv->event_emission_chain, EventReceiver, i);

      if (receiver->actor)
        {
          if (receiver->actor == actor)
            g_clear_object (&receiver->actor);
        }
      else if (receiver->action)
        {
          ClutterActor *action_actor =
            clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (receiver->action));

          if (!action_actor || action_actor == actor)
            {
              clutter_action_sequence_cancelled (receiver->action, sprite);
              g_clear_object (&receiver->action);
            }
        }
    }

  clutter_actor_set_implicitly_grabbed (priv->implicit_grab_actor, FALSE);
  priv->implicit_grab_actor = NULL;

  if (parent)
    {
      g_assert (clutter_actor_is_mapped (parent));

      priv->implicit_grab_actor = parent;
      clutter_actor_set_implicitly_grabbed (priv->implicit_grab_actor, TRUE);
    }
}

void
clutter_sprite_emit_crossing_event (ClutterSprite      *sprite,
                                    const ClutterEvent *event,
                                    ClutterActor       *deepmost,
                                    ClutterActor       *topmost)
{
  ClutterSpritePrivate *priv = clutter_sprite_get_instance_private (sprite);

  if (priv->press_count &&
      !(clutter_event_get_flags (event) & CLUTTER_EVENT_FLAG_GRAB_NOTIFY))
    {
      emit_event (event, priv->event_emission_chain);
    }
  else
    {
      gboolean in_event_emission;
      GArray *event_emission_chain;

      /* Crossings can happen while we're in the middle of event emission
       * (for example when an actor goes unmapped or gets grabbed), so we
       * can't reuse priv->cur_event_emission_chain here, it might already be in use.
       */
      in_event_emission = priv->cur_event_emission_chain->len != 0;

      if (in_event_emission)
        {
          event_emission_chain =
            g_array_sized_new (FALSE, TRUE, sizeof (EventReceiver), 32);
          g_array_set_clear_func (event_emission_chain,
                                  (GDestroyNotify) free_event_receiver);
        }
      else
        {
          event_emission_chain = g_array_ref (priv->cur_event_emission_chain);
        }

      create_event_emission_chain (sprite, event_emission_chain, topmost, deepmost);

      emit_event (event, event_emission_chain);

      g_array_remove_range (event_emission_chain, 0, event_emission_chain->len);
      g_array_unref (event_emission_chain);
    }
}

gboolean
clutter_sprite_point_in_clear_area (ClutterSprite    *sprite,
                                    graphene_point_t  point)
{
  ClutterSpritePrivate *priv = clutter_sprite_get_instance_private (sprite);

  if (!priv->clear_area)
    return FALSE;

  return mtk_region_contains_point (priv->clear_area,
                                    (int) point.x, (int) point.y);
}
