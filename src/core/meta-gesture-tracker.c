/*
 * Copyright (C) 2014 Red Hat
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

/**
 * MetaGestureTracker:
 *
 * Manages gestures on windows/desktop
 *
 * Forwards touch events to clutter actors, and accepts/rejects touch sequences
 * based on the outcome of those.
 */

#include "config.h"

#include "core/meta-gesture-tracker-private.h"

#include "compositor/meta-surface-actor.h"

#define DISTANCE_THRESHOLD 30

typedef struct _MetaGestureTrackerPrivate MetaGestureTrackerPrivate;
typedef struct _GestureActionData GestureActionData;
typedef struct _MetaSequenceInfo MetaSequenceInfo;

struct _MetaSequenceInfo
{
  MetaGestureTracker *tracker;
  ClutterEventSequence *sequence;
  MetaSequenceState state;
  guint autodeny_timeout_id;
  gfloat start_x;
  gfloat start_y;
};

struct _GestureActionData
{
  ClutterGesture *gesture;
  MetaSequenceState state;
  unsigned long gesture_notify_state_id;
};

struct _MetaGestureTrackerPrivate
{
  GHashTable *sequences; /* Hashtable of ClutterEventSequence->MetaSequenceInfo */

  MetaSequenceState stage_state;
  GArray *stage_gestures; /* Array of GestureActionData */
  GList *listeners; /* List of ClutterGestureAction */
  guint autodeny_timeout;
};

enum
{
  PROP_0,
  PROP_AUTODENY_TIMEOUT,
  PROP_LAST,
};

static GParamSpec *obj_props[PROP_LAST];

enum
{
  STATE_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

#define DEFAULT_AUTODENY_TIMEOUT 150

static void meta_gesture_tracker_untrack_stage (MetaGestureTracker *tracker);

G_DEFINE_TYPE_WITH_PRIVATE (MetaGestureTracker, meta_gesture_tracker, G_TYPE_OBJECT)

static void
meta_gesture_tracker_finalize (GObject *object)
{
  MetaGestureTrackerPrivate *priv;

  priv = meta_gesture_tracker_get_instance_private (META_GESTURE_TRACKER (object));

  g_hash_table_destroy (priv->sequences);
  g_array_free (priv->stage_gestures, TRUE);
  g_list_free (priv->listeners);

  G_OBJECT_CLASS (meta_gesture_tracker_parent_class)->finalize (object);
}

static void
meta_gesture_tracker_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  MetaGestureTrackerPrivate *priv;

  priv = meta_gesture_tracker_get_instance_private (META_GESTURE_TRACKER (object));

  switch (prop_id)
    {
    case PROP_AUTODENY_TIMEOUT:
      priv->autodeny_timeout = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_gesture_tracker_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  MetaGestureTrackerPrivate *priv;

  priv = meta_gesture_tracker_get_instance_private (META_GESTURE_TRACKER (object));

  switch (prop_id)
    {
    case PROP_AUTODENY_TIMEOUT:
      g_value_set_uint (value, priv->autodeny_timeout);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_gesture_tracker_class_init (MetaGestureTrackerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_gesture_tracker_finalize;
  object_class->set_property = meta_gesture_tracker_set_property;
  object_class->get_property = meta_gesture_tracker_get_property;

  obj_props[PROP_AUTODENY_TIMEOUT] = g_param_spec_uint ("autodeny-timeout", NULL, NULL,
                                                        0, G_MAXUINT, DEFAULT_AUTODENY_TIMEOUT,
                                                        G_PARAM_STATIC_STRINGS |
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, PROP_LAST, obj_props);

  signals[STATE_CHANGED] =
    g_signal_new ("state-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (MetaGestureTrackerClass, state_changed),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_UINT);
}

static void
autodeny_sequence (gpointer user_data)
{
  MetaSequenceInfo *info = user_data;

  /* Deny the sequence automatically after the given timeout */
  if (info->state == META_SEQUENCE_NONE)
    meta_gesture_tracker_set_sequence_state (info->tracker, info->sequence,
                                             META_SEQUENCE_REJECTED);

  info->autodeny_timeout_id = 0;
}

static MetaSequenceInfo *
meta_sequence_info_new (MetaGestureTracker *tracker,
                        const ClutterEvent *event)
{
  MetaGestureTrackerPrivate *priv;
  MetaSequenceInfo *info;
  guint ms;

  priv = meta_gesture_tracker_get_instance_private (tracker);
  ms = priv->autodeny_timeout;

  info = g_new0 (MetaSequenceInfo, 1);
  info->tracker = tracker;
  info->sequence = clutter_event_get_event_sequence (event);
  info->state = META_SEQUENCE_NONE;
  info->autodeny_timeout_id = g_timeout_add_once (ms, autodeny_sequence, info);

  clutter_event_get_coords (event, &info->start_x, &info->start_y);

  return info;
}

static void
meta_sequence_info_free (MetaSequenceInfo *info)
{
  g_clear_handle_id (&info->autodeny_timeout_id, g_source_remove);

  if (info->state == META_SEQUENCE_NONE)
    meta_gesture_tracker_set_sequence_state (info->tracker, info->sequence,
                                             META_SEQUENCE_REJECTED);
  g_free (info);
}

static gboolean
state_is_applicable (MetaSequenceState prev_state,
                     MetaSequenceState state)
{
  /* Never reject sequences on Wayland, on Wayland we deliver touch events
    * to clients right away and can cancel them later when accepting a
    * sequence.
    */
  if (state == META_SEQUENCE_REJECTED)
    return FALSE;

  /* PENDING_END state is final */
  if (prev_state == META_SEQUENCE_PENDING_END)
    return FALSE;

  /* Don't allow reverting to none */
  if (state == META_SEQUENCE_NONE)
    return FALSE;

  /* Make sequences stick to their accepted/denied state */
  if (state != META_SEQUENCE_PENDING_END &&
      prev_state != META_SEQUENCE_NONE)
    return FALSE;

  return TRUE;
}

static gboolean
meta_gesture_tracker_set_state (MetaGestureTracker *tracker,
                                MetaSequenceState   state)
{
  MetaGestureTrackerPrivate *priv;
  ClutterEventSequence *sequence;
  GHashTableIter iter;

  priv = meta_gesture_tracker_get_instance_private (tracker);

  if (priv->stage_state != state &&
      !state_is_applicable (priv->stage_state, state))
    return FALSE;

  g_hash_table_iter_init (&iter, priv->sequences);
  priv->stage_state = state;

  while (g_hash_table_iter_next (&iter, (gpointer*) &sequence, NULL))
    meta_gesture_tracker_set_sequence_state (tracker, sequence, state);

  return TRUE;
}

static void
gesture_state_changed (ClutterGesture     *gesture,
                       GParamSpec         *pspec,
                       MetaGestureTracker *self)
{
  MetaGestureTrackerPrivate *priv =
    meta_gesture_tracker_get_instance_private (self);
  ClutterGestureState new_state = clutter_gesture_get_state (gesture);

  if (new_state == CLUTTER_GESTURE_STATE_RECOGNIZING)
    {
      if (!g_list_find (priv->listeners, gesture))
        {
          if (meta_gesture_tracker_set_state (self, META_SEQUENCE_ACCEPTED))
            priv->listeners = g_list_prepend (priv->listeners, gesture);
        }
    }
  else if (new_state == CLUTTER_GESTURE_STATE_COMPLETED)
    {
      if (g_list_find (priv->listeners, gesture))
        {
          priv->listeners = g_list_remove (priv->listeners, gesture);

          if (!priv->listeners)
            meta_gesture_tracker_untrack_stage (self);
        }
    }
  else if (new_state == CLUTTER_GESTURE_STATE_CANCELLED)
    {
      if (g_list_find (priv->listeners, gesture))
        {
          priv->listeners = g_list_remove (priv->listeners, gesture);

          if (!priv->listeners)
            meta_gesture_tracker_set_state (self, META_SEQUENCE_PENDING_END);
        }

    }
}

static void
clear_gesture_data (GestureActionData *data)
{
  g_clear_signal_handler (&data->gesture_notify_state_id, data->gesture);
  g_object_unref (data->gesture);
}

static void
meta_gesture_tracker_init (MetaGestureTracker *tracker)
{
  MetaGestureTrackerPrivate *priv;

  priv = meta_gesture_tracker_get_instance_private (tracker);
  priv->sequences = g_hash_table_new_full (NULL, NULL, NULL,
                                           (GDestroyNotify) meta_sequence_info_free);
  priv->stage_gestures = g_array_new (FALSE, FALSE, sizeof (GestureActionData));
  g_array_set_clear_func (priv->stage_gestures, (GDestroyNotify) clear_gesture_data);
}

MetaGestureTracker *
meta_gesture_tracker_new (void)
{
  return g_object_new (META_TYPE_GESTURE_TRACKER, NULL);
}

static void
meta_gesture_tracker_track_stage (MetaGestureTracker *tracker,
                                  ClutterActor       *stage)
{
  MetaGestureTrackerPrivate *priv;
  GList *actions, *l;

  priv = meta_gesture_tracker_get_instance_private (tracker);
  actions = clutter_actor_get_actions (stage);

  for (l = actions; l; l = l->next)
    {
      GestureActionData data;

      if (!clutter_actor_meta_get_enabled (l->data) ||
          !CLUTTER_IS_GESTURE (l->data))
        continue;

      data.gesture = g_object_ref (l->data);
      data.state = META_SEQUENCE_NONE;
      data.gesture_notify_state_id =
        g_signal_connect (data.gesture, "notify::state",
                          G_CALLBACK (gesture_state_changed), tracker);
      g_array_append_val (priv->stage_gestures, data);
    }

  g_list_free (actions);
}

static void
meta_gesture_tracker_untrack_stage (MetaGestureTracker *tracker)
{
  MetaGestureTrackerPrivate *priv;

  priv = meta_gesture_tracker_get_instance_private (tracker);
  priv->stage_state = META_SEQUENCE_NONE;

  g_hash_table_remove_all (priv->sequences);

  if (priv->stage_gestures->len > 0)
    g_array_remove_range (priv->stage_gestures, 0, priv->stage_gestures->len);

  g_list_free (priv->listeners);
  priv->listeners = NULL;
}

gboolean
meta_gesture_tracker_handle_event (MetaGestureTracker *tracker,
                                   ClutterStage       *stage,
                                   const ClutterEvent *event)
{
  MetaGestureTrackerPrivate *priv;
  ClutterEventSequence *sequence;
  MetaSequenceState state;
  MetaSequenceInfo *info;
  gfloat x, y;

  sequence = clutter_event_get_event_sequence (event);

  if (!sequence)
    return FALSE;

  priv = meta_gesture_tracker_get_instance_private (tracker);

  switch (clutter_event_type (event))
    {
    case CLUTTER_TOUCH_BEGIN:
      if (g_hash_table_size (priv->sequences) == 0)
        meta_gesture_tracker_track_stage (tracker, CLUTTER_ACTOR (stage));

      info = meta_sequence_info_new (tracker, event);
      g_hash_table_insert (priv->sequences, sequence, info);

      if (priv->stage_gestures->len == 0)
        {
          /* If no gestures are attached, reject the sequence right away */
          meta_gesture_tracker_set_sequence_state (tracker, sequence,
                                                   META_SEQUENCE_REJECTED);
        }
      else if (priv->stage_state != META_SEQUENCE_NONE)
        {
          /* Make the sequence state match the general state */
          meta_gesture_tracker_set_sequence_state (tracker, sequence,
                                                   priv->stage_state);
        }
      state = info->state;
      break;
    case CLUTTER_TOUCH_END:
      info = g_hash_table_lookup (priv->sequences, sequence);

      if (!info)
        return FALSE;

      /* If nothing was done yet about the sequence, reject it so X11
       * clients may see it
       */
      if (info->state == META_SEQUENCE_NONE)
        meta_gesture_tracker_set_sequence_state (tracker, sequence,
                                                 META_SEQUENCE_REJECTED);

      state = info->state;
      g_hash_table_remove (priv->sequences, sequence);

      if (g_hash_table_size (priv->sequences) == 0)
        meta_gesture_tracker_untrack_stage (tracker);
      break;
    case CLUTTER_TOUCH_UPDATE:
      info = g_hash_table_lookup (priv->sequences, sequence);

      if (!info)
        return FALSE;

      clutter_event_get_coords (event, &x, &y);

      if (info->state == META_SEQUENCE_NONE &&
          (ABS (info->start_x - x) > DISTANCE_THRESHOLD ||
           ABS (info->start_y - y) > DISTANCE_THRESHOLD))
        meta_gesture_tracker_set_sequence_state (tracker, sequence,
                                                 META_SEQUENCE_REJECTED);
      state = info->state;
      break;
    default:
      return FALSE;
      break;
    }

  if (state == META_SEQUENCE_ACCEPTED)
    return TRUE;

  return FALSE;
}

gboolean
meta_gesture_tracker_set_sequence_state (MetaGestureTracker   *tracker,
                                         ClutterEventSequence *sequence,
                                         MetaSequenceState     state)
{
  MetaGestureTrackerPrivate *priv;
  MetaSequenceInfo *info;

  g_return_val_if_fail (META_IS_GESTURE_TRACKER (tracker), FALSE);

  priv = meta_gesture_tracker_get_instance_private (tracker);
  info = g_hash_table_lookup (priv->sequences, sequence);

  if (!info)
    return FALSE;
  else if (state == info->state)
    return TRUE;

  if (!state_is_applicable (info->state, state))
    return FALSE;

  /* Unset autodeny timeout */
  g_clear_handle_id (&info->autodeny_timeout_id, g_source_remove);

  info->state = state;
  g_signal_emit (tracker, signals[STATE_CHANGED], 0, sequence, info->state);

  /* If the sequence was denied, set immediately to PENDING_END after emission */
  if (state == META_SEQUENCE_REJECTED)
    {
      info->state = META_SEQUENCE_PENDING_END;
      g_signal_emit (tracker, signals[STATE_CHANGED], 0, sequence, info->state);
    }

  return TRUE;
}

gint
meta_gesture_tracker_get_n_current_touches (MetaGestureTracker *tracker)
{
  MetaGestureTrackerPrivate *priv;

  g_return_val_if_fail (META_IS_GESTURE_TRACKER (tracker), 0);

  priv = meta_gesture_tracker_get_instance_private (tracker);
  return g_hash_table_size (priv->sequences);
}
