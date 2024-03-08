/*
 * Copyright (C) 2023 Jonas Dreßler <verdre@v0yd.nl>
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
 */

/**
 * ClutterGesture:
 *
 * A #ClutterAction for recognizing gestures
 *
 * #ClutterGesture is a sub-class of #ClutterAction and an abstract base class
 * for implementing the logic to recognize various input gestures.
 *
 * Implementing a #ClutterGesture is done by subclassing #ClutterGesture,
 * connecting to the should_handle_sequence(), point_began()/moved()/ended()
 * and sequences_cancelled() vfuncs, and then moving the gesture through the
 * #ClutterGestureState state machine using clutter_gesture_set_state().
 *
 * ## Recognizing new gestures
 *
 * #ClutterGesture uses five separate states to differentiate between the
 * phases of gesture recognition. Those states also define whether to block or
 * allow event delivery:
 *
 * - WAITING: The gesture will be starting out in this state if no points
 *   are available. When points are added, the state automatically moves
 *   to POSSIBLE before the point_began() vfunc gets called.
 *
 * - POSSIBLE: This is the state the gesture will be in when point_began()
 *   gets called the first time. As soon as the implementation is reasonably
 *   sure that the sequence of events is the gesture, it should set the state
 *   to RECOGNIZING.
 *
 * - RECOGNIZING: A continuous gesture is being recognized. In this state
 *   the implementation usually triggers UI changes as feedback to the user.
 *
 * - COMPLETED: The gesture was sucessfully recognized and has been completed.
 *   The gesture will automatically move to state WAITING after all the
 *   remaining points have ended.
 *
 * - CANCELLED: The gesture was either not started at all because preconditions
 *   were not fulfilled or it was cancelled by the implementation.
 *   The gesture will automatically move to state WAITING after all the
 *   remaining points have ended.
 *
 * Each #ClutterGesture starts out in the WAITING state and automatically
 * moves to POSSIBLE when #ClutterGestureClass.should_handle_sequence() returns
 * true for the first event of an event sequence. Events of this sequence must
 * then be handled using the point_began(), point_moved(), point_ended() and
 * sequences_cancelled() vfuncs. From these events, the implementation moves
 * the gesture through the #ClutterGestureState state-machine.
 *
 * Note that point_ended() and sequences_cancelled() both have a default
 * implementation which automatically moves the state of the gesture to
 * CANCELLED.
 *
 * Note that it's not guaranteed that clutter_gesture_set_state() will always
 * (and immediately) enter the requested state. To deal with this, never
 * assume the state has changed after calling clutter_gesture_set_state(),
 * and react to state changes by implementing the state_changed() vfunc.
 *
 * ## Relationships of gestures
 *
 * By default, when multiple gestures try to recognize while sharing one or
 * more points, the first gesture to move to RECOGNIZING wins, and implicitly
 * moves all conflicting gestures to state CANCELLED. This behavior can be
 * prohibited by using the clutter_gesture_can_not_cancel() API or by
 * implementing the should_influence() or should_be_influenced_by() vfuncs
 * in your #ClutterGesture subclass.
 *
 * The relationship between two gestures that are on different actors and
 * don't conflict over any points can also be controlled. By default, globally
 * only a single gesture is allowed to be in the RECOGNIZING state. This
 * default is mostly to avoid UI bugs and complexity that will appear when
 * recognizing multiple gestures at the same time. It's possible to allow
 * starting/recognizing one gesture while another is already in state
 * RECOGNIZING by using the clutter_gesture_recognize_independently_from() API
 * or by implementing the should_start_while() or the other_gesture_may_start()
 * vfuncs in the #ClutterGesture subclass.
 */

#include "config.h"

#include "clutter-gesture.h"

#include "clutter-debug.h"
#include "clutter-enum-types.h"
#include "clutter-marshal.h"
#include "clutter-private.h"
#include "clutter-sprite-private.h"
#include "clutter-stage-private.h"

#include <graphene.h>

static const char * state_to_string[] = {
  "WAITING",
  "POSSIBLE",
  "RECOGNIZE_PENDING",
  "RECOGNIZING",
  "COMPLETED",
  "CANCELLED",
};
G_STATIC_ASSERT (sizeof (state_to_string) / sizeof (state_to_string[0]) == CLUTTER_N_GESTURE_STATES);

typedef struct
{
  ClutterSprite *sprite;

  ClutterEvent *begin_event;
  ClutterEvent *previous_event;
  ClutterEvent *latest_event;

  unsigned int n_buttons_pressed;
  gboolean seen;
  gboolean ended;
} GestureSequenceData;

typedef struct _ClutterGesturePrivate ClutterGesturePrivate;

struct _ClutterGesturePrivate
{
  GArray *sequences;
  GPtrArray *stage_all_active_gestures;

  unsigned int latest_index;

  ClutterGestureState last_state;
  ClutterGestureState state;
  ClutterGestureState pending_state;

  unsigned int inhibited_count;

  GHashTable *in_relationship_with;

  GPtrArray *cancel_on_recognizing;
  GPtrArray *inhibit_until_cancelled;
  GPtrArray *inhibit_until_recognize;

  GHashTable *can_not_cancel;
  GHashTable *require_failure_of;
  GHashTable *recognize_independently_from;
};

enum
{
  PROP_0,

  PROP_STATE,

  PROP_LAST
};

enum
{
  SHOULD_HANDLE_SEQUENCE,
  MAY_RECOGNIZE,
  RECOGNIZE,
  END,
  CANCEL,

  LAST_SIGNAL
};

static GParamSpec *obj_props[PROP_LAST];
static guint obj_signals[LAST_SIGNAL] = { 0, };

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (ClutterGesture,
                                     clutter_gesture,
                                     CLUTTER_TYPE_ACTION)

static inline void
debug_message (ClutterGesture *self,
               const char     *format,
               ...) G_GNUC_PRINTF (2, 3);

static inline void
debug_message_recursion (ClutterGesture *self,
                         unsigned int    recursion_depth,
                         const char     *format,
                         ...) G_GNUC_PRINTF (3, 4);

static void
maybe_move_to_waiting (ClutterGesture *self,
                       unsigned int    recursion_depth);

static void
set_state (ClutterGesture      *self,
           ClutterGestureState  new_state,
           gboolean             influence_others,
           unsigned int         recursion_depth);

static void
set_state_authoritative (ClutterGesture      *self,
                         ClutterGestureState  new_state);

static void
set_state_after (ClutterGesture *self,
                 unsigned int    recursion_depth);

inline void
debug_message (ClutterGesture *self,
               const char     *format,
               ...)
{
  if (G_UNLIKELY (clutter_debug_flags & CLUTTER_DEBUG_GESTURES))
    {
      va_list args;
      char *str;
      const char *name;

      va_start (args, format);

      str = g_strdup_vprintf (format, args);
      name = clutter_actor_meta_get_name (CLUTTER_ACTOR_META (self));

      CLUTTER_NOTE (GESTURES,
                    "<%s> [%p] %s",
                    name ? name : G_OBJECT_TYPE_NAME (self),
                    self, str);

      g_free (str);
      va_end (args);
    }
}

inline void
debug_message_recursion (ClutterGesture *self,
                         unsigned int    recursion_depth,
                         const char     *format,
                         ...)
{
  if (G_UNLIKELY (clutter_debug_flags & CLUTTER_DEBUG_GESTURES))
    {
      va_list args;
      char *str;
      const char *name;

      va_start (args, format);

      str = g_strdup_vprintf (format, args);
      name = clutter_actor_meta_get_name (CLUTTER_ACTOR_META (self));

      CLUTTER_NOTE (GESTURES,
                    "%*s<%s> [%p] %s",
                    recursion_depth * 2, "",
                    name ? name : G_OBJECT_TYPE_NAME (self),
                    self, str);

      g_free (str);
      va_end (args);
    }
}

static GestureSequenceData *
get_sequence_data (ClutterGesture *self,
                   ClutterSprite  *sprite,
                   unsigned int   *index)
{
  ClutterGesturePrivate *priv = clutter_gesture_get_instance_private (self);
  unsigned int i;

  for (i = 0; i < priv->sequences->len; i++)
    {
      GestureSequenceData *iter = &g_array_index (priv->sequences, GestureSequenceData, i);

      if (!iter->ended && iter->sprite == sprite)
        {
          if (index != NULL)
            *index = i;
          return iter;
        }
    }

  return NULL;
}

static void
register_sequence (ClutterGesture     *self,
                   const ClutterEvent *event)
{
  ClutterGesturePrivate *priv = clutter_gesture_get_instance_private (self);
  ClutterActor *actor =
    clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (self));
  ClutterStage *stage = CLUTTER_STAGE (clutter_actor_get_stage (actor));
  ClutterContext *context = clutter_actor_get_context (actor);
  ClutterBackend *backend = clutter_context_get_backend (context);
  ClutterSprite *sprite = clutter_backend_get_sprite (backend, stage, event);
  GestureSequenceData *seq_data;

  g_array_set_size (priv->sequences, priv->sequences->len + 1);
  seq_data = &g_array_index (priv->sequences, GestureSequenceData, priv->sequences->len - 1);

  seq_data->sprite = sprite;
  seq_data->n_buttons_pressed = 0;
  seq_data->seen = FALSE;
  seq_data->ended = FALSE;
  seq_data->begin_event = clutter_event_copy (event);

  debug_message (self,
                 "[s=%p] Registered new sequence, n total sequences now: %u",
                 sprite, priv->sequences->len);
}

static void
free_sequence_data (GestureSequenceData *seq_data)
{
  if (seq_data->latest_event)
    clutter_event_free (seq_data->latest_event);

  if (seq_data->previous_event)
    clutter_event_free (seq_data->previous_event);

  if (seq_data->begin_event)
    clutter_event_free (seq_data->begin_event);
}

static void
cancel_sequence (ClutterGesture *self,
                 unsigned int    seq_index)
{
  ClutterGesturePrivate *priv = clutter_gesture_get_instance_private (self);
  ClutterGestureClass *gesture_class = CLUTTER_GESTURE_GET_CLASS (self);
  GestureSequenceData *seq_data =
    &g_array_index (priv->sequences, GestureSequenceData, seq_index);

  if (priv->state == CLUTTER_GESTURE_STATE_CANCELLED ||
      priv->state == CLUTTER_GESTURE_STATE_COMPLETED)
    goto out;

  g_assert (priv->state == CLUTTER_GESTURE_STATE_POSSIBLE ||
            priv->state == CLUTTER_GESTURE_STATE_RECOGNIZE_PENDING ||
            priv->state == CLUTTER_GESTURE_STATE_RECOGNIZING);

  /* If all sequences are cancelled, it's as if this sequence had never existed
   * and therefore the gesture should never have moved into POSSIBLE. This
   * means there's no reason to emit a sequences_cancelled() to the gesture
   * implementation, and we can just cancel the gesture right away and move
   * back into WAITING state.
   *
   * Note that this check is a bit sloppy and doesn't handle any sequences
   * that ended or got cancelled before. In the case where sequences ended
   * (as in: didn't get cancelled) before, we can not apply this shortcut and
   * must leave the decision to the implementation. In the case where all
   * sequences before were also cancelled, we should theoretically always
   * cancel here too, but we're skipping that for simplicity reasons.
   */
  if (priv->sequences->len == 1)
    {
      set_state_authoritative (self, CLUTTER_GESTURE_STATE_CANCELLED);
      goto out;
    }

  if (!seq_data->seen)
    goto out;

  g_assert (!seq_data->ended);

  if (gesture_class->sequences_cancelled)
    gesture_class->sequences_cancelled (self, &seq_index, 1);

out:
  seq_data->ended = TRUE;

  maybe_move_to_waiting (self, 0);
}

static void
cancel_point (ClutterGesture *self,
              ClutterSprite  *sprite)
{
  unsigned int seq_index;

  if (!get_sequence_data (self, sprite, &seq_index))
    return;

  debug_message (self, "[s=%p] Cancelling point", sprite);

  cancel_sequence (self, seq_index);
}

static void
cancel_all_points (ClutterGesture *self)
{
  ClutterGesturePrivate *priv = clutter_gesture_get_instance_private (self);
  g_autoptr (GArray) emission_points = NULL;
  unsigned int i;
  unsigned int n_ended;
  ClutterGestureClass *gesture_class = CLUTTER_GESTURE_GET_CLASS (self);

  if (priv->state == CLUTTER_GESTURE_STATE_CANCELLED ||
      priv->state == CLUTTER_GESTURE_STATE_COMPLETED)
    goto out;

  g_assert (priv->state == CLUTTER_GESTURE_STATE_POSSIBLE ||
            priv->state == CLUTTER_GESTURE_STATE_RECOGNIZE_PENDING ||
            priv->state == CLUTTER_GESTURE_STATE_RECOGNIZING);

  emission_points =
    g_array_sized_new (FALSE, TRUE, sizeof (unsigned int), priv->sequences->len);

  n_ended = 0;

  for (i = 0; i < priv->sequences->len; i++)
    {
      GestureSequenceData *seq_data =
        &g_array_index (priv->sequences, GestureSequenceData, i);

      if (seq_data->ended)
        n_ended++;

      if (seq_data->seen && !seq_data->ended)
        g_array_append_val (emission_points, i);
    }

  /* Just like in cancel_sequence(), force-cancel the gesture in case all sequences
   * got cancelled, and none of them ended before. Also similar to cancel_sequence(),
   * cheap out on the check a bit and ignore the case where sequences have
   * already been cancelled before.
   */
  if (n_ended == 0)
    {
      set_state_authoritative (self, CLUTTER_GESTURE_STATE_CANCELLED);
      goto out;
    }

  if (emission_points->len == 0)
    goto out;

  if (gesture_class->sequences_cancelled)
    {
      gesture_class->sequences_cancelled (self,
                                          (unsigned int *) emission_points->data,
                                          emission_points->len);
    }

out:
  for (i = 0; i < priv->sequences->len; i++)
    {
      GestureSequenceData *seq_data =
        &g_array_index (priv->sequences, GestureSequenceData, i);

      seq_data->ended = TRUE;
    }

  maybe_move_to_waiting (self, 0);
}

static void
inhibit_gesture (ClutterGesture *self)
{
  ClutterGesturePrivate *priv =
    clutter_gesture_get_instance_private (self);

  priv->inhibited_count++;

  if (priv->inhibited_count == 1)
    debug_message (self, "Inihibiting gesture on behalf of other gesture");
}

static gboolean
uninhibit_gesture (ClutterGesture *self)
{
  ClutterGesturePrivate *priv =
    clutter_gesture_get_instance_private (self);

  g_assert (priv->inhibited_count > 0);

  priv->inhibited_count--;

  if (priv->inhibited_count == 0)
    return TRUE;

  return FALSE;
}

static gboolean
other_gesture_allowed_to_start (ClutterGesture *self,
                                ClutterGesture *other_gesture)
{
  ClutterGesturePrivate *other_priv = clutter_gesture_get_instance_private (other_gesture);
  ClutterGestureClass *gesture_class = CLUTTER_GESTURE_GET_CLASS (self);
  ClutterGestureClass *other_gesture_class = CLUTTER_GESTURE_GET_CLASS (other_gesture);

  if (other_priv->recognize_independently_from &&
      g_hash_table_contains (other_priv->recognize_independently_from, self))
    return TRUE;

  /* Default: Only a single gesture can be recognizing globally at a time */
  gboolean should_start = FALSE;

  if (other_gesture_class->should_start_while)
    other_gesture_class->should_start_while (other_gesture, self, &should_start);

  if (gesture_class->other_gesture_may_start)
    gesture_class->other_gesture_may_start (self, other_gesture, &should_start);

  return should_start;
}

static gboolean
new_gesture_allowed_to_start (ClutterGesture *self)
{
  ClutterGesturePrivate *priv = clutter_gesture_get_instance_private (self);
  unsigned int i;

  for (i = 0; i < priv->stage_all_active_gestures->len; i++)
    {
      ClutterGesture *existing_gesture =
        g_ptr_array_index (priv->stage_all_active_gestures, i);
      ClutterGesturePrivate *other_priv =
        clutter_gesture_get_instance_private (existing_gesture);

      if (existing_gesture == self)
        continue;

      /* For gestures in relationship we have different APIs */
      if (g_hash_table_contains (other_priv->in_relationship_with, self))
        continue;

      if (other_priv->state == CLUTTER_GESTURE_STATE_RECOGNIZING)
        {
          if (!other_gesture_allowed_to_start (existing_gesture, self))
            return FALSE;
        }
    }

  return TRUE;
}

static gboolean
gesture_may_start (ClutterGesture *self)
{
  gboolean may_recognize;

  if (!new_gesture_allowed_to_start (self))
    {
      debug_message (self,
                    "gesture may not recognize, another gesture is already running");
      return FALSE;
    }

  g_signal_emit (self, obj_signals[MAY_RECOGNIZE], 0, &may_recognize);
  if (!may_recognize)
    {
      debug_message (self,
                     "::may-recognize prevented gesture from recognizing");
      return FALSE;
    }

  return TRUE;
}

static void
maybe_cancel_independent_gestures (ClutterGesture *self)
{
  ClutterGesturePrivate *priv = clutter_gesture_get_instance_private (self);
  int i;

  g_assert (priv->stage_all_active_gestures != NULL);

  for (i = priv->stage_all_active_gestures->len - 1; i >= 0; i--)
    {
      if (i >= priv->stage_all_active_gestures->len)
        continue;

      ClutterGesture *other_gesture =
        g_ptr_array_index (priv->stage_all_active_gestures, i);
      ClutterGesturePrivate *other_priv =
        clutter_gesture_get_instance_private (other_gesture);

      if (other_gesture == self)
        continue;

      /* For gestures in relationship we have different APIs */
      if (g_hash_table_contains (priv->in_relationship_with, other_gesture))
        continue;

      if (other_priv->state == CLUTTER_GESTURE_STATE_POSSIBLE &&
          !other_gesture_allowed_to_start (self, other_gesture))
        {
          debug_message (self, "Cancelling independent gesture in POSSIBLE on recognize");
          set_state_authoritative (other_gesture, CLUTTER_GESTURE_STATE_CANCELLED);
        }
    }
}

static void
maybe_influence_other_gestures (ClutterGesture      *self,
                                unsigned int         recursion_depth)
{
  ClutterGesturePrivate *priv = clutter_gesture_get_instance_private (self);

  if (priv->state == CLUTTER_GESTURE_STATE_RECOGNIZING ||
      priv->state == CLUTTER_GESTURE_STATE_COMPLETED)
    {
      unsigned int len, i;

      /* Clear the cancel_on_recognizing array now already so that other
       * gestures cancelling us won't clear the array right underneath
       * our feet.
       */
      len = priv->cancel_on_recognizing->len;
      priv->cancel_on_recognizing->len = 0;

      for (i = 0; i < len; i++)
        {
          ClutterGesture *other_gesture = priv->cancel_on_recognizing->pdata[i];
          ClutterGesturePrivate *other_priv =
            clutter_gesture_get_instance_private (other_gesture);

          if (!g_hash_table_contains (priv->in_relationship_with, other_gesture))
            {
              debug_message_recursion (other_gesture, recursion_depth,
                                       "Was already CANCELLED before");
              priv->cancel_on_recognizing->pdata[i] = NULL;
              continue;
            }

          g_assert (other_priv->state != CLUTTER_GESTURE_STATE_WAITING);

          if (other_priv->state == CLUTTER_GESTURE_STATE_CANCELLED ||
              other_priv->state == CLUTTER_GESTURE_STATE_COMPLETED)
            {
              debug_message_recursion (other_gesture, recursion_depth,
                                       "Was already CANCELLED or COMPLETED by an influencing recursed by us");
              priv->cancel_on_recognizing->pdata[i] = NULL;
              continue;
            }

          set_state (other_gesture, CLUTTER_GESTURE_STATE_CANCELLED, TRUE, recursion_depth);
        }

      /* The CANCELLED influencing is a two step process: We start with the
       * state change to CANCELLED, and then let the cancelled gestures influence
       * other gestures in a second step. We can't do this in a single go because
       * the recursive influencing might move a third gesture into RECOGNIZING
       * (that one was inhibited by the one we just cancelled). If that third
       * gesture is actually part of our primary to-cancel list, we now end
       * up cancelling it right afterwards and it unnecessarily moved into
       * RECOGNIZING.

For cancelling-on-recognize we wanna do all state changes in one step, then all influencing
in another step (the recognizing gesture wins, it should be able to cancel everyone it
wants to cancel). The same doesn't hold for recognize-on-cancel, here for the
recognizing gesture to win, we need to do influencing in the same step.

       */
      for (i = 0; i < len; i++)
        {
          ClutterGesture *other_gesture = priv->cancel_on_recognizing->pdata[i];
          if (!other_gesture)
            continue;

          set_state_after (other_gesture, recursion_depth);
          maybe_move_to_waiting (other_gesture, recursion_depth);
        }



      len = priv->inhibit_until_recognize->len;
      priv->inhibit_until_recognize->len = 0;

      for (i = 0; i < len; i++)
        {
          ClutterGesture *other_gesture = priv->inhibit_until_recognize->pdata[i];
          ClutterGesturePrivate *other_priv =
            clutter_gesture_get_instance_private (other_gesture);

          if (!g_hash_table_contains (priv->in_relationship_with, other_gesture))
            {
              debug_message_recursion (other_gesture, recursion_depth,
                                       "Was already CANCELLED by an influencing before us");
              continue;
            }

          g_assert (other_priv->state != CLUTTER_GESTURE_STATE_WAITING);

          if (other_priv->state == CLUTTER_GESTURE_STATE_CANCELLED ||
              other_priv->state == CLUTTER_GESTURE_STATE_COMPLETED)
            {
              debug_message_recursion (other_gesture, recursion_depth,
                                       "Was already CANCELLED or COMPLETED by an influencing recursed by us");
              continue;
            }

          if (uninhibit_gesture (other_gesture))
            {
              if (other_priv->pending_state)
                {
                  ClutterGestureState pending_state = other_priv->pending_state;

                  set_state (other_gesture, pending_state, TRUE, recursion_depth);
                }
            }
          else
            {
              debug_message_recursion (other_gesture, recursion_depth,
                                       "Still inhibited");
            }
        }

/* For recognize-on-recognize, it's not really a question we can answer,
so just let the "parent" gesture win */
      for (i = 0; i < len; i++)
        {
          ClutterGesture *other_gesture = priv->inhibit_until_recognize->pdata[i];
          if (!other_gesture)
            continue;

          set_state_after (other_gesture, recursion_depth);
        }
    }
  else if (priv->state == CLUTTER_GESTURE_STATE_CANCELLED)
    {
      unsigned int len, i;

      len = priv->inhibit_until_cancelled->len;
      priv->inhibit_until_cancelled->len = 0;

      for (i = 0; i < len; i++)
        {
          ClutterGesture *other_gesture = priv->inhibit_until_cancelled->pdata[i];
          ClutterGesturePrivate *other_priv =
            clutter_gesture_get_instance_private (other_gesture);

          if (!g_hash_table_contains (priv->in_relationship_with, other_gesture))
            {
              debug_message_recursion (other_gesture, recursion_depth,
                                       "Was already CANCELLED by an influencing before us");
              continue;
            }

          g_assert (other_priv->state != CLUTTER_GESTURE_STATE_WAITING);

          if (other_priv->state == CLUTTER_GESTURE_STATE_CANCELLED ||
              other_priv->state == CLUTTER_GESTURE_STATE_COMPLETED)
            {
              debug_message_recursion (other_gesture, recursion_depth,
                                       "Was already CANCELLED or COMPLETED by an influencing recursed by us");
              continue;
            }

          if (uninhibit_gesture (other_gesture))
            {
              if (other_priv->pending_state)
                {
                  ClutterGestureState pending_state = other_priv->pending_state;

                  set_state (other_gesture, pending_state, TRUE, recursion_depth);
                  set_state_after (other_gesture, recursion_depth);
                  maybe_move_to_waiting (other_gesture, recursion_depth);
                }
            }
          else
            {
              debug_message_recursion (other_gesture, recursion_depth,
                                       "Still inhibited");
            }
        }
    }
}

static void
set_state (ClutterGesture      *self,
           ClutterGestureState  new_state,
           gboolean             influence_others,
           unsigned int         recursion_depth)
{
  ClutterGesturePrivate *priv = clutter_gesture_get_instance_private (self);
  ClutterGestureState old_state;

  if (priv->state == new_state)
    {
      debug_message_recursion (self, recursion_depth,
                               "Skipping state change %s -> %s",
                               state_to_string[priv->state],
                               state_to_string[new_state]);
      return;
    }

  switch (priv->state)
    {
    case CLUTTER_GESTURE_STATE_WAITING:
      g_assert (new_state == CLUTTER_GESTURE_STATE_POSSIBLE);
      break;
    case CLUTTER_GESTURE_STATE_POSSIBLE:
      g_assert (new_state == CLUTTER_GESTURE_STATE_RECOGNIZING ||
                new_state == CLUTTER_GESTURE_STATE_COMPLETED ||
                new_state == CLUTTER_GESTURE_STATE_CANCELLED);
      break;
    case CLUTTER_GESTURE_STATE_RECOGNIZE_PENDING:
      g_assert ((priv->inhibited_count == 0 &&
                 (new_state == CLUTTER_GESTURE_STATE_RECOGNIZING ||
                  new_state == CLUTTER_GESTURE_STATE_COMPLETED)) ||
                new_state == CLUTTER_GESTURE_STATE_CANCELLED);
      break;
    case CLUTTER_GESTURE_STATE_RECOGNIZING:
      g_assert (new_state == CLUTTER_GESTURE_STATE_COMPLETED ||
                new_state == CLUTTER_GESTURE_STATE_CANCELLED);
      break;
    case CLUTTER_GESTURE_STATE_COMPLETED:
      g_assert (new_state == CLUTTER_GESTURE_STATE_WAITING);
      break;
    case CLUTTER_GESTURE_STATE_CANCELLED:
      g_assert (new_state == CLUTTER_GESTURE_STATE_WAITING);
      break;
    case CLUTTER_N_GESTURE_STATES:
      g_assert_not_reached ();
      break;
    }

  if (priv->state == CLUTTER_GESTURE_STATE_WAITING)
    {
      if (new_state == CLUTTER_GESTURE_STATE_POSSIBLE)
        {
          if (!priv->stage_all_active_gestures)
            {
              ClutterActor *actor;
              ClutterStage *stage;

              actor = clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (self));
              g_assert (actor);

              stage = CLUTTER_STAGE (clutter_actor_get_stage (actor));
              g_assert (stage);

              priv->stage_all_active_gestures =
                clutter_stage_get_active_gestures_array (stage);
            }

          g_ptr_array_add (priv->stage_all_active_gestures, self);
        }
    }

  if (priv->state == CLUTTER_GESTURE_STATE_POSSIBLE ||
      priv->state == CLUTTER_GESTURE_STATE_RECOGNIZE_PENDING)
    {
      if (new_state == CLUTTER_GESTURE_STATE_RECOGNIZING ||
          new_state == CLUTTER_GESTURE_STATE_COMPLETED)
        {
          /* Enforce the inhibition machinery */
          if (priv->inhibited_count > 0)
            {
              priv->pending_state = new_state;
              new_state = CLUTTER_GESTURE_STATE_RECOGNIZE_PENDING;
            }
          else
            {
              priv->pending_state = 0;

              if (!gesture_may_start (self))
                new_state = CLUTTER_GESTURE_STATE_CANCELLED;
            }
        }
    }

  g_assert (priv->last_state == priv->state);
  old_state = priv->state;

  if (new_state == CLUTTER_GESTURE_STATE_RECOGNIZING ||
      (old_state != CLUTTER_GESTURE_STATE_RECOGNIZING &&
       new_state == CLUTTER_GESTURE_STATE_COMPLETED))
    {
      ClutterActor *actor;
      ClutterStage *stage;
      unsigned int i;

      actor = clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (self));
      g_assert (actor);

      stage = CLUTTER_STAGE (clutter_actor_get_stage (actor));
      g_assert (stage);

      for (i = 0; i < priv->sequences->len; i++)
        {
          GestureSequenceData *seq_data =
            &g_array_index (priv->sequences, GestureSequenceData, i);

          if (seq_data->ended)
            continue;

          clutter_stage_notify_action_implicit_grab (stage, seq_data->sprite);
        }

      /* Cancel gestures that are independent of ours and still in POSSIBLE:
       * That's to prevent subtle UI bugs like a click gesture preemptively
       * applying "pressed" style to a widget even though it most likely won't
       * recognize anyway.
       */
      maybe_cancel_independent_gestures (self);

      /*if (influence_others)
        maybe_influence_other_gestures (self, 1); // TODO: should check for new_state
       */
    }

  if (new_state == CLUTTER_GESTURE_STATE_WAITING)
    {
      G_GNUC_UNUSED gboolean removed;
      GHashTableIter iter;
      ClutterGesture *other_gesture;

      removed = g_ptr_array_remove (priv->stage_all_active_gestures, self);
      g_assert (removed);

      g_array_set_size (priv->sequences, 0);

      g_hash_table_iter_init (&iter, priv->in_relationship_with);
      while (g_hash_table_iter_next (&iter, (gpointer *) &other_gesture, NULL))
        {
          ClutterGesturePrivate *other_priv =
            clutter_gesture_get_instance_private (other_gesture);

          removed = g_hash_table_remove (other_priv->in_relationship_with, self);
          g_assert (removed);

          g_hash_table_iter_remove (&iter);
        }

      g_ptr_array_set_size (priv->cancel_on_recognizing, 0);
      g_ptr_array_set_size (priv->inhibit_until_cancelled, 0);
      g_ptr_array_set_size (priv->inhibit_until_recognize, 0);

      priv->inhibited_count = 0;
    }

  g_assert (priv->state == old_state);
  priv->last_state = old_state;
  priv->state = new_state;
}

static void
maybe_influence_other_gestures (ClutterGesture *self,
                                unsigned int    recursion_depth);

static void
set_state_after (ClutterGesture *self,
                 unsigned int    recursion_depth)
{
  ClutterGesturePrivate *priv = clutter_gesture_get_instance_private (self);
  ClutterGestureClass *gesture_class = CLUTTER_GESTURE_GET_CLASS (self);
  ClutterGestureState old_state, new_state;

  if (priv->last_state == priv->state)
    return;

  old_state = priv->last_state;
  new_state = priv->state;
  priv->last_state = priv->state;

  debug_message_recursion (self, recursion_depth,
                           "State change (%s -> %s)",
                           state_to_string[old_state],
                           state_to_string[new_state]);

  if (gesture_class->state_changed)
    gesture_class->state_changed (self, old_state, new_state);

  if (old_state == CLUTTER_GESTURE_STATE_RECOGNIZING &&
      new_state == CLUTTER_GESTURE_STATE_COMPLETED)
    {
      g_signal_emit (self, obj_signals[END], 0);
    }
  else if (old_state == CLUTTER_GESTURE_STATE_RECOGNIZING &&
           new_state == CLUTTER_GESTURE_STATE_CANCELLED)
    {
      g_signal_emit (self, obj_signals[CANCEL], 0);
    }
  else if (new_state == CLUTTER_GESTURE_STATE_RECOGNIZING ||
           new_state == CLUTTER_GESTURE_STATE_COMPLETED)
    {
      g_signal_emit (self, obj_signals[RECOGNIZE], 0);
    }

  /* If state has been set recursively by implementation or signal handler, bail out */
  if (priv->state != new_state)
    return;

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_STATE]);

  /* If state has been set recursively by notify signal handler, bail out */
  if (priv->state != new_state)
    return;

  if (new_state == CLUTTER_GESTURE_STATE_RECOGNIZING ||
      (old_state != CLUTTER_GESTURE_STATE_RECOGNIZING &&
       new_state == CLUTTER_GESTURE_STATE_COMPLETED) ||
      new_state == CLUTTER_GESTURE_STATE_CANCELLED)
    maybe_influence_other_gestures (self, recursion_depth + 1);
}

void
maybe_move_to_waiting (ClutterGesture *self,
                       unsigned int    recursion_depth)
{
  ClutterGesturePrivate *priv = clutter_gesture_get_instance_private (self);
  unsigned int i;

  if (priv->state != CLUTTER_GESTURE_STATE_COMPLETED &&
      priv->state != CLUTTER_GESTURE_STATE_CANCELLED)
    return;

  for (i = 0; i < priv->sequences->len; i++)
    {
      GestureSequenceData *seq_data = &g_array_index (priv->sequences, GestureSequenceData, i);

      if (!seq_data->ended)
        return;
    }

  set_state (self, CLUTTER_GESTURE_STATE_WAITING, FALSE, recursion_depth);
  set_state_after (self, recursion_depth);
}

void
set_state_authoritative (ClutterGesture      *self,
                         ClutterGestureState  new_state)
{
  set_state (self, new_state, TRUE, recursion_depth);
  set_state_after (self, 0);
  maybe_move_to_waiting (self, recursion_depth);
}

static gboolean
clutter_gesture_real_should_handle_sequence (ClutterGesture     *self,
                                             const ClutterEvent *sequence_begin_event)
{
  /* We expect the actual gesture implementation to implement
   * should_handle_sequence() vfunc and to tell us whether it's able to
   * handle this kind of event.
   */
  g_warning ("gesture <%s> [<%s>:%p]: should_handle_sequence() not implemented",
             clutter_actor_meta_get_name (CLUTTER_ACTOR_META (self)),
             G_OBJECT_TYPE_NAME (self), self);

  return FALSE;
}

static gboolean
clutter_gesture_real_may_recognize (ClutterGesture *self)
{
  return TRUE;
}

static void
clutter_gesture_real_point_ended (ClutterGesture *self,
                                  unsigned int    point_index)
{
  /* As convenience for implementations, if this is the last point, move
   * to CANCELLED.
   */
  if (clutter_gesture_get_n_points (self) == 1)
    set_state_authoritative (self, CLUTTER_GESTURE_STATE_CANCELLED);
}

static void
clutter_gesture_real_sequences_cancelled (ClutterGesture *self,
                                          unsigned int   *sequences,
                                          unsigned int    n_sequences)
{
  set_state_authoritative (self, CLUTTER_GESTURE_STATE_CANCELLED);
}

static void
handle_pointer_event (ClutterGesture     *self,
                      unsigned int        seq_index,
                      const ClutterEvent *event)
{
  ClutterGestureClass *gesture_class = CLUTTER_GESTURE_GET_CLASS (self);

  switch (clutter_event_type (event))
    {
    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_TOUCH_BEGIN:
      if (gesture_class->point_began)
        gesture_class->point_began (self, seq_index);
      break;

    case CLUTTER_MOTION:
    case CLUTTER_TOUCH_UPDATE:
      if (gesture_class->point_moved)
        gesture_class->point_moved (self, seq_index);
      break;

    case CLUTTER_BUTTON_RELEASE:
    case CLUTTER_TOUCH_END:
      if (gesture_class->point_ended)
        gesture_class->point_ended (self, seq_index);
      break;

    case CLUTTER_TOUCH_CANCEL:
      cancel_sequence (self, seq_index);
      break;

    default:
      g_assert_not_reached ();
      break;
    }
}

static gboolean
is_sequence_end_event (const ClutterEvent *event)
{
  switch (clutter_event_type (event))
    {
    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_MOTION:
    case CLUTTER_TOUCH_BEGIN:
    case CLUTTER_TOUCH_UPDATE:
      return FALSE;

    case CLUTTER_BUTTON_RELEASE:
    case CLUTTER_TOUCH_END:
    case CLUTTER_TOUCH_CANCEL:
      return TRUE;

    case CLUTTER_ENTER:
    case CLUTTER_LEAVE:
      return FALSE;

    default:
      g_assert_not_reached ();
      break;
    }
}

static gboolean
supported_gesture_event (ClutterEventType event_type)
{
  switch (event_type)
    {
    case CLUTTER_ENTER:
    case CLUTTER_LEAVE:
    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_MOTION:
    case CLUTTER_BUTTON_RELEASE:
    case CLUTTER_TOUCH_BEGIN:
    case CLUTTER_TOUCH_UPDATE:
    case CLUTTER_TOUCH_END:
    case CLUTTER_TOUCH_CANCEL:
      return TRUE;

    default:
      return FALSE;
    }
}

static gboolean
clutter_gesture_handle_event (ClutterAction      *action,
                              const ClutterEvent *event)
{
  ClutterGesture *self = CLUTTER_GESTURE (action);
  ClutterGesturePrivate *priv = clutter_gesture_get_instance_private (self);
  ClutterGestureClass *gesture_class = CLUTTER_GESTURE_GET_CLASS (self);
  ClutterActor *actor =
    clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (self));
  ClutterStage *stage = CLUTTER_STAGE (clutter_actor_get_stage (actor));
  ClutterContext *context = clutter_actor_get_context (actor);
  ClutterBackend *backend = clutter_context_get_backend (context);
  ClutterEventType event_type = clutter_event_type (event);
  ClutterSprite *sprite;
  GestureSequenceData *seq_data;
  unsigned int seq_index;
  gboolean is_first_event;
  gboolean should_emit;
  gboolean may_remove_point = TRUE;
  ClutterGestureState old_state = priv->state;

  if (clutter_event_get_flags (event) & CLUTTER_EVENT_FLAG_SYNTHETIC)
    return CLUTTER_EVENT_PROPAGATE;

  if (!supported_gesture_event (event_type))
    return CLUTTER_EVENT_PROPAGATE;

  sprite = clutter_backend_get_sprite (backend, stage, event);
  if (!sprite)
    return CLUTTER_EVENT_PROPAGATE;

  if ((seq_data = get_sequence_data (self, sprite, &seq_index)) == NULL)
    return CLUTTER_EVENT_PROPAGATE;

  if (event_type == CLUTTER_ENTER || event_type == CLUTTER_LEAVE)
    {
      if (gesture_class->crossing_event)
        {
          gesture_class->crossing_event (self,
                                         seq_index,
                                         event_type,
                                         clutter_event_get_time (event),
                                         clutter_event_get_flags (event),
                                         clutter_event_get_source (event),
                                         clutter_event_get_related (event));
        }

      return CLUTTER_EVENT_PROPAGATE;
    }

  g_assert (priv->state != CLUTTER_GESTURE_STATE_WAITING);

  is_first_event = !seq_data->seen;

  should_emit =
    priv->state == CLUTTER_GESTURE_STATE_POSSIBLE ||
    priv->state == CLUTTER_GESTURE_STATE_RECOGNIZE_PENDING ||
    priv->state == CLUTTER_GESTURE_STATE_RECOGNIZING;

  if (event_type == CLUTTER_BUTTON_PRESS)
    {
      seq_data->n_buttons_pressed++;
      if (seq_data->n_buttons_pressed >= 2)
        should_emit = FALSE;
    }
  else if (event_type == CLUTTER_BUTTON_RELEASE)
    {
      seq_data->n_buttons_pressed--;
      if (seq_data->n_buttons_pressed >= 1)
        may_remove_point = should_emit = FALSE;
    }

  if (priv->state == CLUTTER_GESTURE_STATE_POSSIBLE &&
      priv->sequences->len == 1 && is_first_event)
    {
      /* We cancel independent gestures that are in POSSIBLE when a gesture is
       * moving to RECOGNIZING, see maybe_cancel_independent_gestures().
       *
       * The other half of this behavior is implemented here: Bailing out
       * on the first event and moving to CANCELLED when an independent one
       * is already RECOGNIZING.
       *
       * Note that we could also return FALSE in register_sequence(), but this
       * would mean we could't track the sequence and remain in CANCELLED until
       * the sequence ends. We could also move to CANCELLED in register_sequence()
       * while still returning TRUE, but then we'd be moving to CANCELLED before
       * the influencing is fully set-up. So we do in the handle_event() stage
       * instead.
       */
      if (!new_gesture_allowed_to_start (self))
        {
          debug_message (self,
                         "Cancelling gesture on first event, another gesture is "
                         "already running");

          set_state_authoritative (self, CLUTTER_GESTURE_STATE_CANCELLED);
          return CLUTTER_EVENT_PROPAGATE;
        }
    }

  if (should_emit)
    {
      if (seq_data->previous_event)
        clutter_event_free (seq_data->previous_event);
      seq_data->previous_event = seq_data->latest_event;
      seq_data->latest_event = clutter_event_copy (event);

      priv->latest_index = seq_index;
      seq_data->seen = TRUE;

      handle_pointer_event (self, seq_index, event);
    }

  if (may_remove_point && is_sequence_end_event (event))
    {
      seq_data->ended = TRUE;

      maybe_move_to_waiting (self, 0);
    }

  /* If we were already RECOGNIZING, a new point was added and the gesture
   * wasn't cancelled, we'll interpret this as a hint to claim the new
   * point, too.
   *
   * Note that we check for !seq_data->ended here because the sequence might
   * have been cancelled as an effect of the points_began() implementation, eg.
   * in case the gesture implementation unmapped our actor.
   */
  if (is_first_event && !seq_data->ended &&
      old_state == CLUTTER_GESTURE_STATE_RECOGNIZING &&
      priv->state == CLUTTER_GESTURE_STATE_RECOGNIZING)
    {
      if (actor && stage)
        clutter_stage_notify_action_implicit_grab (stage, sprite);

      debug_message (self,
                     "Cancelling other gestures on newly added point automatically");

      maybe_influence_other_gestures (self, 1);
    }

  return CLUTTER_EVENT_PROPAGATE;
}

static void
clutter_gesture_sequence_cancelled (ClutterAction *action,
                                    ClutterSprite *sprite)
{
  cancel_point (CLUTTER_GESTURE (action), sprite);
}

static gboolean
clutter_gesture_register_sequence (ClutterAction      *action,
                                   const ClutterEvent *sequence_begin_event)
{
  ClutterGesture *self = CLUTTER_GESTURE (action);
  ClutterGesturePrivate *priv = clutter_gesture_get_instance_private (self);
  ClutterInputDevice *source_device = clutter_event_get_source_device (sequence_begin_event);
  gboolean retval;

  if (priv->state == CLUTTER_GESTURE_STATE_CANCELLED ||
      priv->state == CLUTTER_GESTURE_STATE_COMPLETED)
    return FALSE;

  if (priv->sequences->len > 0)
    {
      unsigned int i;

      for (i = 0; i < priv->sequences->len; i++)
        {
          GestureSequenceData *iter = &g_array_index (priv->sequences, GestureSequenceData, i);
          ClutterInputDevice *iter_source_device;

          if (iter->ended)
            continue;

          iter_source_device = clutter_event_get_source_device (iter->begin_event);

          if (iter_source_device != source_device)
            return FALSE;

          break;
        }
    }

  g_signal_emit (self, obj_signals[SHOULD_HANDLE_SEQUENCE], 0,
                 sequence_begin_event, &retval);
  if (!retval)
    return FALSE;

  if (priv->state == CLUTTER_GESTURE_STATE_WAITING)
    {
      set_state_authoritative (self, CLUTTER_GESTURE_STATE_POSSIBLE);
      g_assert (priv->state == CLUTTER_GESTURE_STATE_POSSIBLE);
    }

  register_sequence (self, sequence_begin_event);

  return TRUE;
}

static void
setup_influence_on_other_gesture (ClutterGesture *self,
                                  ClutterGesture *other_gesture,
                                  gboolean       *cancel_other_gesture_on_recognizing,
                                  gboolean       *inhibit_other_gesture_until_cancelled,
                                  gboolean       *inhibit_other_gesture_until_recognize)
{
  ClutterGesturePrivate *priv = clutter_gesture_get_instance_private (self);
  ClutterGesturePrivate *other_priv = clutter_gesture_get_instance_private (other_gesture);
  ClutterGestureClass *gesture_class = CLUTTER_GESTURE_GET_CLASS (self);
  ClutterGestureClass *other_gesture_class = CLUTTER_GESTURE_GET_CLASS (other_gesture);

  /* The default: We cancel other gestures when we recognize, and don't
   * inhibit anybody.
   */
  gboolean cancel = TRUE;
  gboolean inhibit_until_cancelled = FALSE, inhibit_until_recognize = FALSE;

  /* First check with the implementation specific APIs */
  if (gesture_class->should_influence)
    gesture_class->should_influence (self, other_gesture, &cancel, &inhibit_until_cancelled, &inhibit_until_recognize);

  if (other_gesture_class->should_be_influenced_by)
    other_gesture_class->should_be_influenced_by (other_gesture, self, &cancel, &inhibit_until_cancelled, &inhibit_until_recognize);

  /* Then apply overrides made using the public methods */
  if (priv->can_not_cancel &&
      g_hash_table_contains (priv->can_not_cancel, other_gesture))
    cancel = FALSE;

  if (other_priv->require_failure_of &&
      g_hash_table_contains (other_priv->require_failure_of, self))
    inhibit_until_cancelled = TRUE;

  *cancel_other_gesture_on_recognizing = cancel;
  *inhibit_other_gesture_until_cancelled = inhibit_until_cancelled;
  *inhibit_other_gesture_until_recognize = inhibit_until_recognize;
}

static int
clutter_gesture_setup_sequence_relationship (ClutterAction *action_1,
                                             ClutterAction *action_2,
                                             ClutterSprite *sprite)
{
  if (!CLUTTER_IS_GESTURE (action_1) || !CLUTTER_IS_GESTURE (action_2))
    return 0;

  ClutterGesture *gesture_1 = CLUTTER_GESTURE (action_1);
  ClutterGesture *gesture_2 = CLUTTER_GESTURE (action_2);
  ClutterGesturePrivate *priv_1 = clutter_gesture_get_instance_private (gesture_1);
  ClutterGesturePrivate *priv_2 = clutter_gesture_get_instance_private (gesture_2);
  gboolean cancel_1_on_recognizing, inhibit_1_until_cancelled, inhibit_1_until_recognize;
  gboolean cancel_2_on_recognizing, inhibit_2_until_cancelled, inhibit_2_until_recognize;

  /* redo relationship() might make us end here */
  if (!get_sequence_data (gesture_1, sprite, NULL) ||
      !get_sequence_data (gesture_2, sprite, NULL))
    return 0;

  /* When CANCELLED or COMPLETED, we refuse to accept new points in
   * register_sequence(). Also when WAITING it's impossible to have points,
   * that leaves only two states, POSSIBLE and RECOGNIZING.
   */
  g_assert (priv_1->state != CLUTTER_GESTURE_STATE_WAITING &&
            priv_2->state != CLUTTER_GESTURE_STATE_WAITING);

  /* When CANCELLED or COMPLETED, we can still enter from redo_sequence_relationships()
   * if that happens, return.
   */
  if (priv_1->state == CLUTTER_GESTURE_STATE_CANCELLED ||
      priv_1->state == CLUTTER_GESTURE_STATE_COMPLETED ||
      priv_2->state == CLUTTER_GESTURE_STATE_CANCELLED ||
      priv_2->state == CLUTTER_GESTURE_STATE_COMPLETED)
    return 0;

  /* If gesture 1 knows gesture 2 (this implies vice-versa), everything's
   * figured out already, we won't negotiate again for any new shared sequences!
   */
  if (g_hash_table_contains (priv_1->in_relationship_with, gesture_2))
    {
      cancel_1_on_recognizing = g_ptr_array_find (priv_2->cancel_on_recognizing, gesture_1, NULL);
      inhibit_1_until_cancelled = g_ptr_array_find (priv_2->inhibit_until_cancelled, gesture_1, NULL);
      inhibit_1_until_recognize = g_ptr_array_find (priv_2->inhibit_until_recognize, gesture_1, NULL);
      cancel_2_on_recognizing = g_ptr_array_find (priv_1->cancel_on_recognizing, gesture_2, NULL);
      inhibit_2_until_cancelled = g_ptr_array_find (priv_1->inhibit_until_cancelled, gesture_2, NULL);
      inhibit_2_until_recognize = g_ptr_array_find (priv_1->inhibit_until_recognize, gesture_2, NULL);
    }
  else
    {
      setup_influence_on_other_gesture (gesture_1, gesture_2,
                                        &cancel_2_on_recognizing,
                                        &inhibit_2_until_cancelled,
                                        &inhibit_2_until_recognize);

      setup_influence_on_other_gesture (gesture_2, gesture_1,
                                        &cancel_1_on_recognizing,
                                        &inhibit_1_until_cancelled,
                                        &inhibit_1_until_recognize);

      if (priv_1->state == CLUTTER_GESTURE_STATE_RECOGNIZING)
        {
          // requiring failures is no longer possible for gestures getting added late
          inhibit_2_until_cancelled = FALSE; // no need to inhibit anymore, in theory this failure requirement would have failed
          inhibit_1_until_cancelled = FALSE; // 1 is already recognizing, impossible to inhibit it
          inhibit_1_until_recognize = FALSE; // 1 is already recognizing, impossible to inhibit it
        }
      else if (priv_2->state == CLUTTER_GESTURE_STATE_RECOGNIZING)
        {
          // requiring failures is no longer possible for gestures getting added late
          inhibit_1_until_cancelled = FALSE; // no need to inhibit anymore
          inhibit_2_until_cancelled = FALSE; // 2 is already recognizing, impossible to inhibit it
          inhibit_2_until_recognize = FALSE; // 2 is already recognizing, impossible to inhibit it
        }

      CLUTTER_NOTE (GESTURES,
                    "Setting up relation between \"<%s> [%p]\" (cancel: %d, "
                    "inhibit_until_cancelled=%d, inhibit_until_recognize=%d) and \"<%s> [%p]\" (cancel: %d, inhibit_until_cancelled=%d, inhibit_until_recognize=%d)",
                    clutter_actor_meta_get_name (CLUTTER_ACTOR_META (gesture_1)) ? clutter_actor_meta_get_name (CLUTTER_ACTOR_META (gesture_1)) : G_OBJECT_TYPE_NAME (gesture_1), gesture_1,
                    cancel_1_on_recognizing, inhibit_1_until_cancelled, inhibit_1_until_recognize,
                    clutter_actor_meta_get_name (CLUTTER_ACTOR_META (gesture_2)) ? clutter_actor_meta_get_name (CLUTTER_ACTOR_META (gesture_2)) : G_OBJECT_TYPE_NAME (gesture_2), gesture_2,
                    cancel_2_on_recognizing, inhibit_2_until_cancelled, inhibit_2_until_recognize);

      g_hash_table_add (priv_1->in_relationship_with, g_object_ref (gesture_2));
      g_hash_table_add (priv_2->in_relationship_with, g_object_ref (gesture_1));

      if (cancel_2_on_recognizing)
        g_ptr_array_add (priv_1->cancel_on_recognizing, gesture_2);

      if (inhibit_2_until_cancelled)
        {
          g_ptr_array_add (priv_1->inhibit_until_cancelled, gesture_2);
          inhibit_gesture (gesture_2);
        }

      if (inhibit_1_until_cancelled)
        {
          g_ptr_array_add (priv_2->inhibit_until_cancelled, gesture_1);
          inhibit_gesture (gesture_1);
        }

      if (cancel_1_on_recognizing)
        g_ptr_array_add (priv_2->cancel_on_recognizing, gesture_1);

      if (inhibit_2_until_recognize)
        {
          g_ptr_array_add (priv_1->inhibit_until_recognize, gesture_2);
          inhibit_gesture (gesture_2);
        }

      if (inhibit_1_until_recognize)
        {
          g_ptr_array_add (priv_2->inhibit_until_recognize, gesture_1);
          inhibit_gesture (gesture_1);
        }
    }

  if (inhibit_2_until_cancelled && !inhibit_1_until_cancelled)
    return -1;

  if (!inhibit_2_until_cancelled && inhibit_1_until_cancelled)
    return 1;

  if (inhibit_2_until_recognize && !inhibit_1_until_recognize)
    return -1;

  if (!inhibit_2_until_recognize && inhibit_1_until_recognize)
    return 1;

  if (cancel_2_on_recognizing && !cancel_1_on_recognizing)
    return -1;

  if (!cancel_2_on_recognizing && cancel_1_on_recognizing)
    return 1;

  return 0;
}

static void
clutter_gesture_set_actor (ClutterActorMeta *meta,
                           ClutterActor     *actor)
{
  ClutterGesture *self = CLUTTER_GESTURE (meta);
  ClutterGesturePrivate *priv = clutter_gesture_get_instance_private (self);

  if (priv->sequences->len)
    {
      debug_message (self,
                     "Detaching from actor while gesture has points, cancelling "
                     "all points");

      cancel_all_points (self);
    }

  if (!actor)
    priv->stage_all_active_gestures = NULL;

  CLUTTER_ACTOR_META_CLASS (clutter_gesture_parent_class)->set_actor (meta, actor);
}

static void
clutter_gesture_set_enabled (ClutterActorMeta *meta,
                             gboolean          is_enabled)
{
  ClutterGesture *self = CLUTTER_GESTURE (meta);
  ClutterGesturePrivate *priv = clutter_gesture_get_instance_private (self);

  if (!is_enabled && priv->sequences->len)
    {
      debug_message (self,
                     "Disabling gesture while it has points, cancelling all points");

      cancel_all_points (self);
    }

  CLUTTER_ACTOR_META_CLASS (clutter_gesture_parent_class)->set_enabled (meta, is_enabled);
}

static void
clutter_gesture_get_property (GObject      *gobject,
                              unsigned int  prop_id,
                              GValue       *value,
                              GParamSpec   *pspec)
{
  ClutterGesture *self = CLUTTER_GESTURE (gobject);

  switch (prop_id)
    {
    case PROP_STATE:
      g_value_set_enum (value, clutter_gesture_get_state (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
other_gesture_disposed (gpointer user_data,
                        GObject    *finalized_gesture)
{
  GHashTable *hashtable = user_data;

  g_hash_table_remove (hashtable, finalized_gesture);
}

static void
destroy_weak_ref_hashtable (GHashTable *hashtable)
{
  GHashTableIter iter;
  GObject *key;

  g_hash_table_iter_init (&iter, hashtable);
  while (g_hash_table_iter_next (&iter, (gpointer *) &key, NULL))
    g_object_weak_unref (key, other_gesture_disposed, hashtable);

  g_hash_table_destroy (hashtable);
}

static void
clutter_gesture_finalize (GObject *gobject)
{
  ClutterGesture *self = CLUTTER_GESTURE (gobject);
  ClutterGesturePrivate *priv = clutter_gesture_get_instance_private (self);

  g_assert (priv->state != CLUTTER_GESTURE_STATE_COMPLETED &&
            priv->state != CLUTTER_GESTURE_STATE_CANCELLED);

  if (priv->state != CLUTTER_GESTURE_STATE_WAITING)
    {
      G_GNUC_UNUSED gboolean removed;

      g_warning ("gesture <%s> [<%s>:%p]: Finalizing while in active state (%s), "
                 "implementation didn't move the gesture to an end state.",
                 clutter_actor_meta_get_name (CLUTTER_ACTOR_META (self)),
                 G_OBJECT_TYPE_NAME (self), self,
                 state_to_string[priv->state]);

      removed = g_ptr_array_remove (priv->stage_all_active_gestures, self);
      g_assert (removed);
    }

  g_array_unref (priv->sequences);

  g_assert (g_hash_table_size (priv->in_relationship_with) == 0);
  g_hash_table_destroy (priv->in_relationship_with);

  g_assert (priv->cancel_on_recognizing->len == 0);
  g_ptr_array_free (priv->cancel_on_recognizing, TRUE);
  g_assert (priv->inhibit_until_cancelled->len == 0);
  g_ptr_array_free (priv->inhibit_until_cancelled, TRUE);
  g_assert (priv->inhibit_until_recognize->len == 0);
  g_ptr_array_free (priv->inhibit_until_recognize, TRUE);

  if (priv->can_not_cancel)
    destroy_weak_ref_hashtable (priv->can_not_cancel);
  if (priv->require_failure_of)
    destroy_weak_ref_hashtable (priv->require_failure_of);
  if (priv->recognize_independently_from)
    destroy_weak_ref_hashtable (priv->recognize_independently_from);

  G_OBJECT_CLASS (clutter_gesture_parent_class)->finalize (gobject);
}

static void
clutter_gesture_class_init (ClutterGestureClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterActorMetaClass *meta_class = CLUTTER_ACTOR_META_CLASS (klass);
  ClutterActionClass *action_class = CLUTTER_ACTION_CLASS (klass);

  klass->should_handle_sequence = clutter_gesture_real_should_handle_sequence;
  klass->may_recognize = clutter_gesture_real_may_recognize;
  klass->point_ended = clutter_gesture_real_point_ended;
  klass->sequences_cancelled = clutter_gesture_real_sequences_cancelled;

  action_class->handle_event = clutter_gesture_handle_event;
  action_class->sequence_cancelled = clutter_gesture_sequence_cancelled;
  action_class->register_sequence = clutter_gesture_register_sequence;
  action_class->setup_sequence_relationship = clutter_gesture_setup_sequence_relationship;

  meta_class->set_actor = clutter_gesture_set_actor;
  meta_class->set_enabled = clutter_gesture_set_enabled;

  gobject_class->get_property = clutter_gesture_get_property;
  gobject_class->finalize = clutter_gesture_finalize;

  /**
   * ClutterGesture:state:
   *
   * The current state of the gesture.
   */
  obj_props[PROP_STATE] =
    g_param_spec_enum ("state", NULL, NULL,
                       CLUTTER_TYPE_GESTURE_STATE,
                       CLUTTER_GESTURE_STATE_WAITING,
                       G_PARAM_READABLE |
                       G_PARAM_STATIC_STRINGS |
                       G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     obj_props);

  /**
   * ClutterGesture::should-handle-sequence:
   * @gesture: the #ClutterGesture that emitted the signal
   * @sequence_begin_event: the #ClutterEvent beginning the sequence
   *
   * The ::should-handle-sequence signal is emitted when a sequence gets added
   * to the gesture. Return %FALSE to make the gesture ignore the sequence of
   * events.
   *
   * Returns: %TRUE if the gesture may handle the sequence, %FALSE if it may not.
   */
  obj_signals[SHOULD_HANDLE_SEQUENCE] =
    g_signal_new (I_("should-handle-sequence"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_FIRST,
                  G_STRUCT_OFFSET (ClutterGestureClass, should_handle_sequence),
                  _clutter_boolean_continue_accumulator,
                  NULL, _clutter_marshal_BOOLEAN__BOXED,
                  G_TYPE_BOOLEAN, 1,
                  CLUTTER_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);

  g_signal_set_va_marshaller (obj_signals[SHOULD_HANDLE_SEQUENCE],
                              G_TYPE_FROM_CLASS (gobject_class),
                              _clutter_marshal_BOOLEAN__BOXEDv);

  /**
   * ClutterGesture::may-recognize:
   * @gesture: the #ClutterGesture that emitted the signal
   *
   * The ::may-recognize signal is emitted if the gesture might become
   * active and move to RECOGNIZING. Its purpose is to allow the
   * implementation or a user of a gesture to prohibit the gesture
   * from starting when needed.
   *
   * Note that to limit a gesture to certain input devices, this signal
   * should *not* be used. Instead listen to the ::should-handle-sequence
   * signal and look at the input device of the @sequence_begin_event.
   *
   * Returns: %TRUE if the gesture may recognize, %FALSE if it may not.
   */
  obj_signals[MAY_RECOGNIZE] =
    g_signal_new (I_("may-recognize"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterGestureClass, may_recognize),
                  _clutter_boolean_continue_accumulator,
                  NULL, NULL,
                  G_TYPE_BOOLEAN, 0,
                  G_TYPE_NONE);

  /**
   * ClutterGesture::recognize:
   * @gesture: the #ClutterGesture that emitted the signal
   *
   * The ::recognize signal is emitted when the gesture recognizes.
   *
   * This is the signal gesture users are supposed to use for implementing
   * actions on gesture recognize.
   */
  obj_signals[RECOGNIZE] =
    g_signal_new (I_("recognize"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0,
                  G_TYPE_NONE);

  g_signal_set_va_marshaller (obj_signals[RECOGNIZE],
                              G_TYPE_FROM_CLASS (gobject_class),
                              _clutter_marshal_VOID__VOIDv);

  /**
   * ClutterGesture::end:
   * @gesture: the #ClutterGesture that emitted the signal
   *
   * The ::end signal is emitted when a continuous gesture ends.
   */
  obj_signals[END] =
    g_signal_new (I_("end"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0,
                  G_TYPE_NONE);

  g_signal_set_va_marshaller (obj_signals[END],
                              G_TYPE_FROM_CLASS (gobject_class),
                              _clutter_marshal_VOID__VOIDv);

  /**
   * ClutterGesture::cancel:
   * @gesture: the #ClutterGesture that emitted the signal
   *
   * The ::cancel signal is emitted when a continuous gesture got cancelled.
   */
  obj_signals[CANCEL] =
    g_signal_new (I_("cancel"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0,
                  G_TYPE_NONE);

  g_signal_set_va_marshaller (obj_signals[CANCEL],
                              G_TYPE_FROM_CLASS (gobject_class),
                              _clutter_marshal_VOID__VOIDv);
}

static void
clutter_gesture_init (ClutterGesture *self)
{
  ClutterGesturePrivate *priv = clutter_gesture_get_instance_private (self);

  priv->sequences = g_array_sized_new (FALSE, TRUE, sizeof (GestureSequenceData), 3);
  g_array_set_clear_func (priv->sequences, (GDestroyNotify) free_sequence_data);

  priv->latest_index = 0;

  priv->last_state = CLUTTER_GESTURE_STATE_WAITING;
  priv->state = CLUTTER_GESTURE_STATE_WAITING;
  priv->pending_state = 0;

  priv->inhibited_count = 0;

  priv->in_relationship_with = g_hash_table_new_full (NULL, NULL, (GDestroyNotify) g_object_unref, NULL);

  priv->cancel_on_recognizing = g_ptr_array_new ();
  priv->inhibit_until_cancelled = g_ptr_array_new ();
  priv->inhibit_until_recognize = g_ptr_array_new ();

  priv->can_not_cancel = NULL;
  priv->require_failure_of = NULL;
  priv->recognize_independently_from = NULL;
}

/**
 * clutter_gesture_set_state:
 *
 * Sets the state of the gesture. This method is private to gesture
 * implementations.
 *
 * Allowed state transitions are:
 *
 * - From POSSIBLE into RECOGNIZING, COMPLETED or CANCELLED.
 * - From RECOGNIZING into COMPLETED or CANCELLED.
 */
void
clutter_gesture_set_state (ClutterGesture      *self,
                           ClutterGestureState  state)
{
  ClutterGesturePrivate *priv;

  g_return_if_fail (CLUTTER_IS_GESTURE (self));

  priv = clutter_gesture_get_instance_private (self);

  debug_message (self, "State change requested: %s -> %s",
                 state_to_string[priv->state], state_to_string[state]);

  if ((priv->state == CLUTTER_GESTURE_STATE_POSSIBLE &&
       (state == CLUTTER_GESTURE_STATE_RECOGNIZING ||
        state == CLUTTER_GESTURE_STATE_COMPLETED ||
        state == CLUTTER_GESTURE_STATE_CANCELLED)) ||
      (priv->state == CLUTTER_GESTURE_STATE_RECOGNIZE_PENDING &&
       (state == CLUTTER_GESTURE_STATE_CANCELLED)) ||
      (priv->state == CLUTTER_GESTURE_STATE_RECOGNIZING &&
       (state == CLUTTER_GESTURE_STATE_COMPLETED ||
        state == CLUTTER_GESTURE_STATE_CANCELLED)))
    {
      set_state_authoritative (self, state);
    }
  else
    {
      /* For sake of simplicity, never complain about unnecessary tries to cancel */
      if (state == CLUTTER_GESTURE_STATE_CANCELLED)
        return;

      g_warning ("gesture <%s> [<%s>:%p]: Requested invalid state change: %s -> %s",
                 clutter_actor_meta_get_name (CLUTTER_ACTOR_META (self)),
                 G_OBJECT_TYPE_NAME (self), self,
                 state_to_string[priv->state], state_to_string[state]);
    }
}

/**
 * clutter_gesture_cancel:
 * @self: a #ClutterGesture
 *
 * Cancels the gesture by setting its state to CANCELLED.
 */
void
clutter_gesture_cancel (ClutterGesture *self)
{
  clutter_gesture_set_state (self, CLUTTER_GESTURE_STATE_CANCELLED);
}

/**
 * clutter_gesture_reset_state_machine:
 */
void
clutter_gesture_reset_state_machine (ClutterGesture *self)
{
  ClutterGesturePrivate *priv;

  g_return_if_fail (CLUTTER_IS_GESTURE (self));

  priv = clutter_gesture_get_instance_private (self);

  if (priv->state == CLUTTER_GESTURE_STATE_CANCELLED ||
      priv->state == CLUTTER_GESTURE_STATE_COMPLETED)
    set_state_authoritative (self, CLUTTER_GESTURE_STATE_WAITING);
}

/**
 * clutter_gesture_get_state:
 * @self: a #ClutterGesture
 *
 * Gets the current state of the gesture.
 *
 * Returns: the #ClutterGestureState
 */
ClutterGestureState
clutter_gesture_get_state (ClutterGesture *self)
{
  ClutterGesturePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_GESTURE (self), CLUTTER_GESTURE_STATE_WAITING);

  priv = clutter_gesture_get_instance_private (self);

  return priv->state;
}

/**
 * clutter_gesture_get_n_points:
 * @self: a #ClutterGesture
 *
 * Retrieves the number of active points the gesture currently has.
 *
 * Returns: the number of active points
 */
unsigned int
clutter_gesture_get_n_points (ClutterGesture *self)
{
  ClutterGesturePrivate *priv;
  unsigned int i, n_points = 0;

  g_return_val_if_fail (CLUTTER_IS_GESTURE (self), 0);

  priv = clutter_gesture_get_instance_private (self);

  for (i = 0; i < priv->sequences->len; i++)
    {
      GestureSequenceData *seq_data = &g_array_index (priv->sequences, GestureSequenceData, i);

      if (seq_data->seen && !seq_data->ended)
        n_points++;
    }

  return n_points;
}

/**
 * clutter_gesture_get_points:
 * @self: a #ClutterGesture
 * @n_points: (out) (optional): number of points
 *
 * Retrieves an array of the currently active points of the gesture, the array is
 * ordered in the order the points were added in (newest to oldest).
 *
 * Returns: (array length=n_points): array with active points of the gesture
 */
unsigned int *
clutter_gesture_get_points (ClutterGesture *self,
                            size_t         *n_points)
{
  ClutterGesturePrivate *priv;
  g_autoptr (GArray) points = NULL;
  unsigned int i;

  g_return_val_if_fail (CLUTTER_IS_GESTURE (self), 0);

  priv = clutter_gesture_get_instance_private (self);

  points = g_array_sized_new (TRUE, TRUE, sizeof (unsigned int), 1);

  for (i = 0; i < priv->sequences->len; i++)
    {
      GestureSequenceData *seq_data =
        &g_array_index (priv->sequences, GestureSequenceData, i);

      if (seq_data->seen && !seq_data->ended)
        g_array_append_val (points, i);
    }

  return (unsigned int *) g_array_steal (points, n_points);
}

/**
 * clutter_gesture_get_point_coords:
 * @self: a #ClutterGesture
 * @point_index: index of the point
 * @coords_out: (out): a #graphene_point_t
 *
 * Retrieves the latest coordinates of the point with index @point_index.
 */
void
clutter_gesture_get_point_coords (ClutterGesture   *self,
                                  int               point_index,
                                  graphene_point_t *coords_out)
{
  ClutterGesturePrivate *priv;
  GestureSequenceData *seq_data;
  ClutterActor *action_actor;

  g_return_if_fail (CLUTTER_IS_GESTURE (self));
  g_return_if_fail (coords_out != NULL);

  priv = clutter_gesture_get_instance_private (self);
  seq_data =
    &g_array_index (priv->sequences, GestureSequenceData,
                    point_index == -1 ? priv->latest_index : point_index);

  clutter_event_get_position (seq_data->latest_event, coords_out);

  action_actor = clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (self));
  if (action_actor)
    {
      clutter_actor_transform_stage_point (action_actor,
                                           coords_out->x, coords_out->y,
                                           &coords_out->x, &coords_out->y);
    }
}

/**
 * clutter_gesture_get_point_coords_abs:
 * @self: a #ClutterGesture
 * @point_index: index of the point
 * @coords_out: (out): a #graphene_point_t
 */
void
clutter_gesture_get_point_coords_abs (ClutterGesture   *self,
                                      int               point_index,
                                      graphene_point_t *coords_out)
{
  ClutterGesturePrivate *priv;
  GestureSequenceData *seq_data;

  g_return_if_fail (CLUTTER_IS_GESTURE (self));
  g_return_if_fail (coords_out != NULL);

  priv = clutter_gesture_get_instance_private (self);
  seq_data =
    &g_array_index (priv->sequences, GestureSequenceData,
                    point_index == -1 ? priv->latest_index : point_index);

  clutter_event_get_position (seq_data->latest_event, coords_out);
}

/**
 * clutter_gesture_get_point_begin_coords:
 * @self: a #ClutterGesture
 * @point_index: index of the point
 * @coords_out: (out): a #graphene_point_t
 *
 * Retrieves the begin coordinates of the point with index @point_index.
 */
void
clutter_gesture_get_point_begin_coords (ClutterGesture   *self,
                                        int               point_index,
                                        graphene_point_t *coords_out)
{
  ClutterGesturePrivate *priv;
  GestureSequenceData *seq_data;
  ClutterActor *action_actor;

  g_return_if_fail (CLUTTER_IS_GESTURE (self));
  g_return_if_fail (coords_out != NULL);

  priv = clutter_gesture_get_instance_private (self);
  seq_data =
    &g_array_index (priv->sequences, GestureSequenceData,
                    point_index == -1 ? priv->latest_index : point_index);

  clutter_event_get_position (seq_data->begin_event, coords_out);

  action_actor = clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (self));
  if (action_actor)
    {
      clutter_actor_transform_stage_point (action_actor,
                                           coords_out->x, coords_out->y,
                                           &coords_out->x, &coords_out->y);
    }
}

/**
 * clutter_gesture_get_point_begin_coords_abs:
 * @self: a #ClutterGesture
 * @point_index: index of the point
 * @coords_out: (out): a #graphene_point_t
 */
void
clutter_gesture_get_point_begin_coords_abs (ClutterGesture   *self,
                                            int               point_index,
                                            graphene_point_t *coords_out)
{
  ClutterGesturePrivate *priv;
  GestureSequenceData *seq_data;

  g_return_if_fail (CLUTTER_IS_GESTURE (self));
  g_return_if_fail (coords_out != NULL);

  priv = clutter_gesture_get_instance_private (self);
  seq_data =
    &g_array_index (priv->sequences, GestureSequenceData,
                    point_index == -1 ? priv->latest_index : point_index);

  clutter_event_get_position (seq_data->begin_event, coords_out);
}

/**
 * clutter_gesture_get_point_previous_coords:
 * @self: a #ClutterGesture
 * @point_index: index of the point
 * @coords_out: (out): a #graphene_point_t
 *
 * Retrieves the previous coordinates of the point with index @point_index.
 */
void
clutter_gesture_get_point_previous_coords (ClutterGesture   *self,
                                           int               point_index,
                                           graphene_point_t *coords_out)
{
  ClutterGesturePrivate *priv;
  GestureSequenceData *seq_data;
  ClutterActor *action_actor;

  g_return_if_fail (CLUTTER_IS_GESTURE (self));
  g_return_if_fail (coords_out != NULL);

  priv = clutter_gesture_get_instance_private (self);
  seq_data =
    &g_array_index (priv->sequences, GestureSequenceData,
                    point_index == -1 ? priv->latest_index : point_index);

  clutter_event_get_position (seq_data->previous_event, coords_out);

  action_actor = clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (self));
  if (action_actor)
    {
      clutter_actor_transform_stage_point (action_actor,
                                           coords_out->x, coords_out->y,
                                           &coords_out->x, &coords_out->y);
    }
}

/**
 * clutter_gesture_get_point_previous_coords_abs:
 * @self: a #ClutterGesture
 * @point_index: index of the point
 * @coords_out: (out): a #graphene_point_t
 */
void
clutter_gesture_get_point_previous_coords_abs (ClutterGesture   *self,
                                               int               point_index,
                                               graphene_point_t *coords_out)
{
  ClutterGesturePrivate *priv;
  GestureSequenceData *seq_data;

  g_return_if_fail (CLUTTER_IS_GESTURE (self));
  g_return_if_fail (coords_out != NULL);

  priv = clutter_gesture_get_instance_private (self);
  seq_data =
    &g_array_index (priv->sequences, GestureSequenceData,
                    point_index == -1 ? priv->latest_index : point_index);

  clutter_event_get_position (seq_data->previous_event, coords_out);
}

/**
 * clutter_gesture_get_point_event:
 * @self: a #ClutterGesture
 * @point_index: index of the point
 *
 * Retrieves the the latest event of the point with index @point_index.
 *
 * Returns: The #ClutterEvent
 */
const ClutterEvent *
clutter_gesture_get_point_event (ClutterGesture *self,
                                 int             point_index)
{
  ClutterGesturePrivate *priv;
  GestureSequenceData *seq_data;

  g_return_val_if_fail (CLUTTER_IS_GESTURE (self), NULL);

  priv = clutter_gesture_get_instance_private (self);

  g_return_val_if_fail (point_index < (int) priv->sequences->len, NULL);
  g_return_val_if_fail (priv->latest_index < priv->sequences->len, NULL);

  seq_data =
    &g_array_index (priv->sequences, GestureSequenceData,
                    point_index < 0 ? priv->latest_index : point_index);

  return seq_data->latest_event;
}

/**
 * clutter_gesture_can_not_cancel:
 * @self: a #ClutterGesture
 * @other_gesture: the other #ClutterGesture
 *
 * In case @self and @other_gesture are operating on the same points, calling
 * this function will make sure that @self does not cancel @other_gesture
 * when @self moves to state RECOGNIZING.
 *
 * To allow two gestures to recognize simultaneously using the same set of
 * points (for example a zoom and a rotate gesture on the same actor), call
 * clutter_gesture_can_not_cancel() twice, so that both gestures can not
 * cancel each other.
 */
void
clutter_gesture_can_not_cancel (ClutterGesture *self,
                                ClutterGesture *other_gesture)
{
  ClutterGesturePrivate *priv;

  g_return_if_fail (CLUTTER_IS_GESTURE (self));
  g_return_if_fail (CLUTTER_IS_GESTURE (other_gesture));

  priv = clutter_gesture_get_instance_private (self);

  if (!priv->can_not_cancel)
    priv->can_not_cancel = g_hash_table_new (NULL, NULL);

  if (!g_hash_table_add (priv->can_not_cancel, other_gesture))
    return;

  g_object_weak_ref (G_OBJECT (other_gesture),
                     (GWeakNotify) other_gesture_disposed,
                     priv->can_not_cancel);
}

/**
 * clutter_gesture_require_failure_of:
 * @self: a #ClutterGesture
 * @other_gesture: the other #ClutterGesture
 *
 * Calling this function will make the recognition of @self depend on
 * @other_gesture moving to state CANCELLED. Until that happens @self will
 * not move to RECOGNIZING, but instead enter and remain in RECOGNIZE_PENDING.
 * Once @other_gesture enters CANCELLED and @self is in RECOGNIZE_PENDING,
 * @self will automatically move to the pending state (RECOGNIZING or COMPLETED).
 */
void
clutter_gesture_require_failure_of (ClutterGesture *self,
                                    ClutterGesture *other_gesture)
{
  ClutterGesturePrivate *priv;

  priv = clutter_gesture_get_instance_private (self);

  if (!priv->require_failure_of)
    priv->require_failure_of = g_hash_table_new (NULL, NULL);

  if (!g_hash_table_add (priv->require_failure_of, other_gesture))
    return;

  g_object_weak_ref (G_OBJECT (other_gesture),
                     (GWeakNotify) other_gesture_disposed,
                     priv->require_failure_of);
}

void
clutter_gesture_relationships_changed (ClutterGesture *self)
{
  ClutterGesturePrivate *priv;
  GHashTableIter iter;
  ClutterGesture *other_gesture;
  ClutterActor *actor;
  ClutterStage *stage;

  g_return_if_fail (CLUTTER_IS_GESTURE (self));

  priv = clutter_gesture_get_instance_private (self);


  g_hash_table_iter_init (&iter, priv->in_relationship_with);
  while (g_hash_table_iter_next (&iter, (gpointer *) &other_gesture, NULL))
    {
      ClutterGesturePrivate *other_priv =
        clutter_gesture_get_instance_private (other_gesture);
      gboolean removed;

      removed = g_hash_table_remove (other_priv->in_relationship_with, self);
      g_assert (removed);

      g_ptr_array_remove (other_priv->cancel_on_recognizing, self);
      g_ptr_array_remove (other_priv->inhibit_until_cancelled, self);
      g_ptr_array_remove (other_priv->inhibit_until_recognize, self);

      g_hash_table_iter_remove (&iter);
    }

  g_ptr_array_set_size (priv->cancel_on_recognizing, 0);
  g_ptr_array_set_size (priv->inhibit_until_cancelled, 0);
  g_ptr_array_set_size (priv->inhibit_until_recognize, 0);

  priv->inhibited_count = 0;

  actor = clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (self));
  stage = CLUTTER_STAGE (clutter_actor_get_stage (actor));

  if (stage)
    {
      unsigned int i;

      for (i = 0; i < priv->sequences->len; i++)
        {
          GestureSequenceData *seq_data = &g_array_index (priv->sequences, GestureSequenceData, i);

          clutter_sprite_setup_sequence_actions_special (seq_data->sprite);
        }
    }
}

/**
 * clutter_gesture_recognize_independently_from:
 * @self: a #ClutterGesture
 * @other_gesture: the other #ClutterGesture
 *
 * In case @self and @other_gesture are operating on a different set of points,
 * calling this function will allow @self to start while @other_gesture is
 * already in state RECOGNIZING.
 */
void
clutter_gesture_recognize_independently_from (ClutterGesture *self,
                                              ClutterGesture *other_gesture)
{
  ClutterGesturePrivate *priv;

  g_return_if_fail (CLUTTER_IS_GESTURE (self));
  g_return_if_fail (CLUTTER_IS_GESTURE (other_gesture));

  priv = clutter_gesture_get_instance_private (self);

  if (!priv->recognize_independently_from)
    priv->recognize_independently_from = g_hash_table_new (NULL, NULL);

  if (!g_hash_table_add (priv->recognize_independently_from, other_gesture))
    return;

  g_object_weak_ref (G_OBJECT (other_gesture),
                     (GWeakNotify) other_gesture_disposed,
                     priv->recognize_independently_from);
}
